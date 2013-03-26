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
#include "framework/Document.h"
#include "data/model/WaveFileModel.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "layer/Layer.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/ColourDatabase.h"

Analyser::Analyser() :
    m_document(0),
    m_fileModel(0),
    m_pane(0)
{
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

    TransformId f0 = "vamp:yintony:yintony:f0";
    TransformId notes = "vamp:cepstral-pitchtracker:cepstral-pitchtracker:notes";

    // We don't want a waveform in the main pane. We must have a
    // main-model layer of some sort, but the layers created by
    // transforms are derived layers, so we'll create a time ruler for
    // the main-model layer. It could subsequently be hidden if we
    // didn't want it

    m_document->addLayerToView
	(m_pane, m_document->createMainModelLayer(LayerFactory::TimeRuler));

    Layer *layer = 0;

    layer = addLayerFor(f0);

    if (layer) {
	TimeValueLayer *tvl = qobject_cast<TimeValueLayer *>(layer);
	if (tvl) {
	    tvl->setPlotStyle(TimeValueLayer::PlotDiscreteCurves);
	    tvl->setBaseColour(ColourDatabase::getInstance()->
			       getColourIndex(QString("Black")));
	}
    }

    layer = addLayerFor(notes);

    if (layer) {
	FlexiNoteLayer *nl = qobject_cast<FlexiNoteLayer *>(layer);
	if (nl) {
	    nl->setBaseColour(ColourDatabase::getInstance()->
			      getColourIndex(QString("Bright Blue")));
	}
    }

    paneStack->setCurrentLayer(m_pane, layer);
}

Layer *
Analyser::addLayerFor(TransformId id)
{
    TransformFactory *tf = TransformFactory::getInstance();

    if (!tf->haveTransform(id)) {
	std::cerr << "ERROR: Analyser::addLayerFor(" << id << "): Transform unknown" << std::endl;
	return 0;
    }
    
    Transform transform = tf->getDefaultTransformFor
	(id, m_fileModel->getSampleRate());
	
    transform.setStepSize(512);
    transform.setBlockSize(2048);
	
    ModelTransformer::Input input(m_fileModel, -1);
    
    Layer *layer;
    layer = m_document->createDerivedLayer(transform, m_fileModel);
    if (layer) {
	m_document->addLayerToView(m_pane, layer);
    }

    return layer;
}
