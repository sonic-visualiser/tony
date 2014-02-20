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
#include "layer/ColourMapper.h"
#include "layer/LayerFactory.h"
#include "layer/SpectrogramLayer.h"
#include "layer/Colour3DPlotLayer.h"

#include <QSettings>

using std::vector;

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
         .arg(int(TimeValueLayer::AutoAlignScale))
         .arg(int(TimeValueLayer::PlotPoints))
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

QString
Analyser::newFileLoaded(Document *doc, WaveFileModel *model,
			PaneStack *paneStack, Pane *pane)
{
    m_document = doc;
    m_fileModel = model;
    m_paneStack = paneStack;
    m_pane = pane;

    m_reAnalysingSelection = Selection();
    m_reAnalysisCandidates.clear();
    m_currentCandidate = -1;

    // Note that we need at least one main-model layer (time ruler,
    // waveform or what have you). It could be hidden if we don't want
    // to see it but it must exist.

    QString warning, error;

    // This isn't fatal -- we can proceed without
    // visualisations. Other failures are fatal though.
    warning = addVisualisations();

    error = addWaveform();
    if (error != "") return error;

    error = addAnalyses();
    if (error != "") return error;

    loadState(Audio);
    loadState(PitchTrack);
    loadState(Notes);
    loadState(Spectrogram);

    emit layersChanged();

    return warning;
}

QString
Analyser::addVisualisations()
{
/*
    TransformFactory *tf = TransformFactory::getInstance();

    QString name = "Constant-Q";
    QString base = "vamp:cqvamp:cqvamp:";
    QString out = "constantq";

    // A spectrogram, off by default. Must go at the back because it's
    // opaque

    QString notFound = tr("Transform \"%1\" not found, spectrogram will not be enabled.<br><br>Is the %2 Vamp plugin correctly installed?");
    if (!tf->haveTransform(base + out)) {
	return notFound.arg(base + out).arg(name);
    }

    Transform transform = tf->getDefaultTransformFor
        (base + out, m_fileModel->getSampleRate());
    transform.setParameter("bpo", 36);

    Colour3DPlotLayer *spectrogram = qobject_cast<Colour3DPlotLayer *>
        (m_document->createDerivedLayer(transform, m_fileModel));

    if (!spectrogram) return tr("Transform \"%1\" did not run correctly (no layer or wrong layer type returned)").arg(base + out);
*/    

    SpectrogramLayer *spectrogram = qobject_cast<SpectrogramLayer *>
        (m_document->createMainModelLayer(LayerFactory::MelodicRangeSpectrogram));

    spectrogram->setColourMap((int)ColourMapper::BlackOnWhite);
    spectrogram->setNormalizeHybrid(true);
//    spectrogram->setSmooth(true);
//    spectrogram->setGain(0.5); //!!! arbitrary at this point
    spectrogram->setMinFrequency(15);
    spectrogram->setGain(100);
    m_document->addLayerToView(m_pane, spectrogram);
    spectrogram->setLayerDormant(m_pane, true);

    m_layers[Spectrogram] = spectrogram;

    return "";
}

QString
Analyser::addWaveform()
{
    // Our waveform layer is just a shadow, light grey and taking up
    // little space at the bottom

    WaveformLayer *waveform = qobject_cast<WaveformLayer *>
        (m_document->createMainModelLayer(LayerFactory::Waveform));

    waveform->setMiddleLineHeight(0.9);
    waveform->setShowMeans(false); // too small & pale for this
    waveform->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Grey")));
    PlayParameters *params = waveform->getPlayParameters();
    if (params) params->setPlayPan(-1);
    
    m_document->addLayerToView(m_pane, waveform);

    m_layers[Audio] = waveform;
    return "";
}

QString
Analyser::addAnalyses()
{
    TransformFactory *tf = TransformFactory::getInstance();
    
    QString plugname = "pYIN";
    QString base = "vamp:pyin:pyin:";
    QString f0out = "smoothedpitchtrack";
    QString noteout = "notes";

    Transforms transforms;

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

    QString notFound = tr("Transform \"%1\" not found. Unable to analyse audio file.<br><br>Is the %2 Vamp plugin correctly installed?");
    if (!tf->haveTransform(base + f0out)) {
	return notFound.arg(base + f0out).arg(plugname);
    }
    if (!tf->haveTransform(base + noteout)) {
	return notFound.arg(base + noteout).arg(plugname);
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
        if (params) params->setPlayPan(1);
    }
    
    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);
    if (flexiNoteLayer) {
        flexiNoteLayer->setBaseColour(cdb->getColourIndex(tr("Bright Blue")));
        PlayParameters *params = flexiNoteLayer->getPlayParameters();
        if (params) params->setPlayPan(1);
    }
    
    return "";
}

QString
Analyser::reAnalyseSelection(Selection sel)
{
    if (sel == m_reAnalysingSelection) return "";

    clearReAnalysis();

    m_reAnalysingSelection = sel;

    TransformFactory *tf = TransformFactory::getInstance();
    
    QString plugname = "pYIN";
    QString base = "vamp:pyin:localcandidatepyin:";
    QString out = "pitchtrackcandidates";

    Transforms transforms;

    QString notFound = tr("Transform \"%1\" not found. Unable to perform interactive analysis.<br><br>Is the %2 Vamp plugin correctly installed?");
    if (!tf->haveTransform(base + out)) {
	return notFound.arg(base + out).arg(plugname);
    }

    Transform t = tf->getDefaultTransformFor
        (base + out, m_fileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    RealTime start = RealTime::frame2RealTime
        ((sel.getStartFrame()/256) * 256 - 2*256, m_fileModel->getSampleRate());

    RealTime end = RealTime::frame2RealTime
        ((sel.getEndFrame()/256) * 256 + 11*256, m_fileModel->getSampleRate());

    RealTime duration;

    if (sel.getEndFrame() > sel.getStartFrame()) {
        duration = end - start;
    }

    t.setStartTime(start);
    t.setDuration(duration);

    transforms.push_back(t);

    m_document->createDerivedLayersAsync(transforms, m_fileModel, this);

    return "";
}

void
Analyser::layersCreated(vector<Layer *> primary,
                        vector<Layer *> additional)
{
    //!!! how do we know these came from the right selection? user
    //!!! might have made another one since this request was issued

    vector<Layer *> all;
    for (int i = 0; i < (int)primary.size(); ++i) {
        all.push_back(primary[i]);
    }
    for (int i = 0; i < (int)additional.size(); ++i) {
        all.push_back(additional[i]);
    }

    for (int i = 0; i < (int)all.size(); ++i) {
        TimeValueLayer *t = qobject_cast<TimeValueLayer *>(all[i]);
        if (t) {
            PlayParameters *params = t->getPlayParameters();
            if (params) {
                params->setPlayAudible(false);
            }
            t->setBaseColour
                (ColourDatabase::getInstance()->getColourIndex(tr("Bright Orange")));
            m_document->addLayerToView(m_pane, t);
            m_reAnalysisCandidates.push_back(t);
        }
    }
}

void
Analyser::switchPitchCandidate(Selection sel, bool up)
{
    if (m_reAnalysisCandidates.empty()) return;

    if (up) {
        m_currentCandidate = m_currentCandidate + 1;
        if (m_currentCandidate >= (int)m_reAnalysisCandidates.size()) {
            m_currentCandidate = 0;
        }
    } else {
        m_currentCandidate = m_currentCandidate - 1;
        if (m_currentCandidate < 0) {
            m_currentCandidate = (int)m_reAnalysisCandidates.size() - 1;
        }
    }

    Layer *pitchTrack = m_layers[PitchTrack];
    if (!pitchTrack) return;

    Clipboard clip;
    pitchTrack->deleteSelection(sel);
    m_reAnalysisCandidates[m_currentCandidate]->copy(m_pane, sel, clip);
    pitchTrack->paste(m_pane, clip, 0, false);

    // raise the pitch track, then notes on top (if present)
    m_paneStack->setCurrentLayer(m_pane, m_layers[PitchTrack]);
    if (m_layers[Notes] && !m_layers[Notes]->isLayerDormant(m_pane)) {
        m_paneStack->setCurrentLayer(m_pane, m_layers[Notes]);
    }
}

void
Analyser::shiftOctave(Selection sel, bool up)
{
    float factor = (up ? 2.f : 0.5f);
    
    vector<Layer *> actOn;

    Layer *pitchTrack = m_layers[PitchTrack];
    if (pitchTrack) actOn.push_back(pitchTrack);

    foreach (Layer *c, m_reAnalysisCandidates) {
        actOn.push_back(c);
    }

    foreach (Layer *layer, actOn) {
        
        Clipboard clip;
        layer->copy(m_pane, sel, clip);
        layer->deleteSelection(sel);

        Clipboard shifted;
        foreach (Clipboard::Point p, clip.getPoints()) {
            if (p.haveValue()) {
                Clipboard::Point sp = p.withValue(p.getValue() * factor);
                shifted.addPoint(sp);
            } else {
                shifted.addPoint(p);
            }
        }
        
        layer->paste(m_pane, shifted, 0, false);
    }
}

void
Analyser::clearPitches(Selection sel)
{
    Layer *pitchTrack = m_layers[PitchTrack];
    if (!pitchTrack) return;

    pitchTrack->deleteSelection(sel);
}

void
Analyser::clearReAnalysis()
{
    foreach (Layer *layer, m_reAnalysisCandidates) {
        m_document->removeLayerFromView(m_pane, layer);
        m_document->deleteLayer(layer); // also releases its model
    }
    m_reAnalysisCandidates.clear();
    m_reAnalysingSelection = Selection();
    m_currentCandidate = -1;
}    

void
Analyser::takePitchTrackFrom(Layer *otherLayer)
{
    Layer *myLayer = m_layers[PitchTrack];
    if (!myLayer) return;

    Clipboard clip;

    Selection sel = Selection(myLayer->getModel()->getStartFrame(),
                              myLayer->getModel()->getEndFrame());
    myLayer->deleteSelection(sel);

    sel = Selection(otherLayer->getModel()->getStartFrame(),
                    otherLayer->getModel()->getEndFrame());
    otherLayer->copy(m_pane, sel, clip);

    myLayer->paste(m_pane, clip, 0, false);
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
    settings.setValue(QString("visible-%1").arg(int(c)), v);
    settings.setValue(QString("audible-%1").arg(int(c)), a);
    settings.endGroup();
}

void
Analyser::loadState(Component c)
{
    QSettings settings;
    settings.beginGroup("Analyser");
    bool deflt = (c == Spectrogram ? false : true);
    bool v = settings.value(QString("visible-%1").arg(int(c)), deflt).toBool();
    bool a = settings.value(QString("audible-%1").arg(int(c)), true).toBool();
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

        if (v) {
            if (c == Notes) {
                m_paneStack->setCurrentLayer(m_pane, m_layers[c]);
            } else if (c == PitchTrack) {
                // raise the pitch track, then notes on top (if present)
                m_paneStack->setCurrentLayer(m_pane, m_layers[c]);
                if (m_layers[Notes] &&
                    !m_layers[Notes]->isLayerDormant(m_pane)) {
                    m_paneStack->setCurrentLayer(m_pane, m_layers[Notes]);
                }
            }
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

float
Analyser::getGain(Component c) const
{
    if (m_layers[c]) {
        PlayParameters *params = m_layers[c]->getPlayParameters();
        if (!params) return 1.f;
        return params->getPlayGain();
    } else {
        return 1.f;
    }
}
    
void
Analyser::setGain(Component c, float gain)
{
    if (m_layers[c]) {
        PlayParameters *params = m_layers[c]->getPlayParameters();
        if (!params) return;
        params->setPlayGain(gain);
        saveState(c);
    }
}

float
Analyser::getPan(Component c) const
{
    if (m_layers[c]) {
        PlayParameters *params = m_layers[c]->getPlayParameters();
        if (!params) return 1.f;
        return params->getPlayPan();
    } else {
        return 1.f;
    }
}
    
void
Analyser::setPan(Component c, float pan)
{
    if (m_layers[c]) {
        PlayParameters *params = m_layers[c]->getPlayParameters();
        if (!params) return;
        params->setPlayPan(pan);
        saveState(c);
    }
}


    
