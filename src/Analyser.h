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

#ifndef ANALYSER_H
#define ANALYSER_H

#include <QObject>

#include "transform/Transform.h"
#include "layer/LayerFactory.h" // GF: added so we can access the FlexiNotes enum value.


class WaveFileModel;
class Pane;
class PaneStack;
class Document;
class Layer;
class LayerFactory;

class Analyser : public QObject
{
    Q_OBJECT

public:
    Analyser();
    virtual ~Analyser();

    void newFileLoaded(Document *newDocument, WaveFileModel *model,
		       PaneStack *paneStack, Pane *pane);

protected:
    Document *m_document;
    WaveFileModel *m_fileModel;
    Pane *m_pane;

    Layer *addLayerFor(TransformId);
	LayerFactory::LayerType preferredLayer;

};

#endif
