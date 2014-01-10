/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2012 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "Analyser.h"

#include "transform/TransformFactory.h"
#include "transform/ModelTransformer.h"
#include "transform/FeatureExtractionModelTransformer.h"
#include "framework/Document.h"
#include "data/model/WaveFileModel.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "layer/Layer.h"
#include "layer/TimeValueLayer.h"
#include "layer/NoteLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/WaveformLayer.h"
#include "layer/ColourDatabase.h"
#include "layer/LayerFactory.h"

#include <QSettings>

Analyser::Analyser() :
    m_document(0),
    m_fileModel(0),
    m_paneStack(0),
    m_pane(0)
{
    QSettings settings;
    settings.beginGroup("LayerDefaults");
    settings.setValue
        ("timevalues",
         QString("<layer verticalScale=\"%1\" plotStyle=\"%2\" "
                 "scaleMinimum=\"%3\" scaleMaximum=\"%4\"/>")
         .arg(int(TimeValueLayer::LogScale))
         .arg(int(TimeValueLayer::PlotDiscreteCurves))
         .arg(27.5f).arg(880.f)); // temporary values: better get the real extents of the data from the model
    settings.setValue
        ("flexinotes",
         QString("<layer verticalScale=\"%1\"/>")
         .arg(int(FlexiNoteLayer::AutoAlignScale)));
    settings.endGroup();
}

Analyser::~Analyser()
{
}

void
Analyser::newFileLoaded(Document *doc, WaveFileModel *model,
			PaneStack *paneStack, Pane *pane)
{
    m_document = doc;
    m_fileModel = model;
    m_paneStack = paneStack;
    m_pane = pane;

    QString base = "vamp:pyin:pyin:";
    QString f0out = "smoothedpitchtrack";
    QString noteout = "notes";

    // We need at least one main-model layer (time ruler, waveform or
    // what have you). It could be hidden if we don't want to see it
    // but it must exist.

    // Our waveform layer is just a shadow, light grey and taking up
    // little space at the bottom

    WaveformLayer *waveform = qobject_cast<WaveformLayer *>
        (m_document->createMainModelLayer(LayerFactory::Waveform));

    waveform->setMiddleLineHeight(0.9);
    waveform->setShowMeans(false); // too small & pale for this
    waveform->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Grey")));
    PlayParameters *params = waveform->getPlayParameters();
    if (params) params->setPlayPan(1);
    
    m_document->addLayerToView(m_pane, waveform);

    m_layers[Audio] = waveform;

    Transforms transforms;
    
    TransformFactory *tf = TransformFactory::getInstance();

/*!!! we could have more than one pitch track...
    QString cx = "vamp:cepstral-pitchtracker:cepstral-pitchtracker:f0";
    if (tf->haveTransform(cx)) {
        Transform tx = tf->getDefaultTransformFor(cx);
        TimeValueLayer *lx = qobject_cast<TimeValueLayer *>
            (m_document->createDerivedLayer(tx, m_fileModel));
        lx->setVerticalScale(TimeValueLayer::AutoAlignScale);
        lx->setBaseColour(ColourDatabase::getInstance()->getColourIndex(tr("Bright Red")));
        m_document->addLayerToView(m_pane, lx);
    }
*/

    if (!tf->haveTransform(base + f0out) || !tf->haveTransform(base + noteout)) {
        std::cerr << "ERROR: Analyser::newFileLoaded: Transform unknown" << std::endl;
	return;
    }

    Transform t = tf->getDefaultTransformFor
        (base + f0out, m_fileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    transforms.push_back(t);

    t.setOutput(noteout);
    
    transforms.push_back(t);

    std::vector<Layer *> layers =
        m_document->createDerivedLayers(transforms, m_fileModel);

    if (!layers.empty()) {

        for (int i = 0; i < (int)layers.size(); ++i) {

            FlexiNoteLayer *f = qobject_cast<FlexiNoteLayer *>(layers[i]);
            TimeValueLayer *t = qobject_cast<TimeValueLayer *>(layers[i]);

            if (f) m_layers[Notes] = f;
            if (t) m_layers[PitchTrack] = t;

            m_document->addLayerToView(m_pane, layers[i]);
        }
    
        ColourDatabase *cdb = ColourDatabase::getInstance();

        TimeValueLayer *pitchLayer = 
            qobject_cast<TimeValueLayer *>(m_layers[PitchTrack]);
        if (pitchLayer) {
            pitchLayer->setBaseColour(cdb->getColourIndex(tr("Black")));
            PlayParameters *params = pitchLayer->getPlayParameters();
            if (params) params->setPlayPan(-1);
        }

        FlexiNoteLayer *flexiNoteLayer = 
            qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);
        if (flexiNoteLayer) {
            flexiNoteLayer->setBaseColour(cdb->getColourIndex(tr("Bright Blue")));
            PlayParameters *params = flexiNoteLayer->getPlayParameters();
            if (params) params->setPlayPan(0);
        }
    }

    loadState(Audio);
    loadState(PitchTrack);
    loadState(Notes);

    emit layersChanged();
}

void
Analyser::getEnclosingSelectionScope(size_t f, size_t &f0, size_t &f1)
{
    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);

    int f0i = f, f1i = f;
    size_t res = 1;

    if (!flexiNoteLayer) {
        f0 = f1 = f;
        return;
    }
    
    flexiNoteLayer->snapToFeatureFrame(m_pane, f0i, res, Layer::SnapLeft);
    flexiNoteLayer->snapToFeatureFrame(m_pane, f1i, res, Layer::SnapRight);

    f0 = (f0i < 0 ? 0 : f0i);
    f1 = (f1i < 0 ? 0 : f1i);
}

void
Analyser::saveState(Component c) const
{
    bool v = isVisible(c);
    bool a = isAudible(c);
    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue(QString("Visibility %1").arg(int(c)), v);
    settings.setValue(QString("Audibility %1").arg(int(c)), a);
    settings.endGroup();
}

void
Analyser::loadState(Component c)
{
    QSettings settings;
    settings.beginGroup("Analyser");
    bool v = settings.value(QString("Visibility %1").arg(int(c)), true).toBool();
    bool a = settings.value(QString("Audibility %1").arg(int(c)), true).toBool();
    settings.endGroup();
    setVisible(c, v);
    setAudible(c, a);
}

void
Analyser::setIntelligentActions(bool on) 
{
    std::cerr << "toggle setIntelligentActions " << on << std::endl;

    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);
    if (flexiNoteLayer) {
        flexiNoteLayer->setIntelligentActions(on);
    }
}

bool
Analyser::isVisible(Component c) const
{
    if (m_layers[c]) {
        return !m_layers[c]->isLayerDormant(m_pane);
    } else {
        return false;
    }
}

void
Analyser::setVisible(Component c, bool v)
{
    if (m_layers[c]) {
        m_layers[c]->setLayerDormant(m_pane, !v);

        if (v && (c == Notes)) {
            m_paneStack->setCurrentLayer(m_pane, m_layers[c]);
        }

        m_pane->layerParametersChanged();
        saveState(c);
    }
}

bool
Analyser::isAudible(Component c) const
{
    if (m_layers[c]) {
        PlayParameters *params = m_layers[c]->getPlayParameters();
        if (!params) return false;
        return params->isPlayAudible();
    } else {
        return false;
    }
}

void
Analyser::setAudible(Component c, bool a)
{
    if (m_layers[c]) {
        PlayParameters *params = m_layers[c]->getPlayParameters();
        if (!params) return;
        params->setPlayAudible(a);
        saveState(c);
    }
}

