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
    m_pane(0),
    m_flexiNoteLayer(0)
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
    m_pane = pane;

    QString base = "vamp:pyin:pyin:";
    QString f0out = "smoothedpitchtrack";
    QString noteout = "notes";

    // We need at least one main-model layer (time ruler, waveform or
    // what have you). It could be hidden if we don't want to see it
    // but it must exist.

    m_document->addLayerToView
	(m_pane, m_document->createMainModelLayer(LayerFactory::TimeRuler));

    // This waveform layer is just a shadow, light grey and taking up
    // little space at the bottom

    WaveformLayer *waveform = qobject_cast<WaveformLayer *>
        (m_document->createMainModelLayer(LayerFactory::Waveform));

    waveform->setMiddleLineHeight(0.9);
    waveform->setShowMeans(false); // too small & pale for this
    waveform->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Grey")));

    m_document->addLayerToView(m_pane, waveform);

    Transforms transforms;
    
    TransformFactory *tf = TransformFactory::getInstance();
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

        ColourDatabase *cdb = ColourDatabase::getInstance();

        for (int i = 0; i < (int)layers.size(); ++i) {

            SingleColourLayer *scl = dynamic_cast<SingleColourLayer *>
                (layers[i]);

            if (scl) {
                if (i == 0) {
                    scl->setBaseColour(cdb->getColourIndex(tr("Black")));
                } else {
                    scl->setBaseColour(cdb->getColourIndex(tr("Bright Blue")));
                }
            }

            m_document->addLayerToView(m_pane, layers[i]);
        }

        m_flexiNoteLayer = dynamic_cast<FlexiNoteLayer *>
            (layers[layers.size()-1]);
        paneStack->setCurrentLayer(m_pane, m_flexiNoteLayer);
    }
}

void
Analyser::setIntelligentActions(bool on) 
{
    std::cerr << "toggle setIntelligentActions " << on << std::endl;
    if (m_flexiNoteLayer) {
        m_flexiNoteLayer->setIntelligentActions(on);
    }
}
