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

#include <map>
#include <vector>

#include "framework/Document.h"
#include "base/Selection.h"

class WaveFileModel;
class Pane;
class PaneStack;
class Layer;
class TimeValueLayer;
class Layer;

class Analyser : public QObject,
                 public Document::LayerCreationHandler
{
    Q_OBJECT

public:
    Analyser();
    virtual ~Analyser();

    // Process new main model, add derived layers; return "" on success or error string on failure
    QString newFileLoaded(Document *newDocument, WaveFileModel *model,
                          PaneStack *paneStack, Pane *pane);
		       
    void setIntelligentActions(bool);

    enum Component {
        Audio,
        PitchTrack,
        Notes,
        Spectrogram,
    };

    bool isVisible(Component c) const;
    void setVisible(Component c, bool v);
    void toggleVisible(Component c) { setVisible(c, !isVisible(c)); }

    bool isAudible(Component c) const;
    void setAudible(Component c, bool v);
    void toggleAudible(Component c) { setAudible(c, !isAudible(c)); }

    void cycleStatus(Component c) {
        if (isVisible(c)) {
            if (isAudible(c)) {
                setVisible(c, false);
                setAudible(c, false);
            } else {
                setAudible(c, true);
            }
        } else {
            setVisible(c, true);
            setAudible(c, false);
        }
    }

    float getGain(Component c) const;
    void setGain(Component c, float gain);

    float getPan(Component c) const;
    void setPan(Component c, float pan);

    void getEnclosingSelectionScope(size_t f, size_t &f0, size_t &f1);

    /**
     * Analyse the selection and schedule asynchronous adds of
     * candidate layers for the region it contains. Returns "" on
     * success or a user-readable error string on failure.
     */
    QString reAnalyseSelection(Selection sel);

    /**
     * If a re-analysis has been activated, switch the selected area
     * of the main pitch track to a different candidate from the
     * analysis results.
     */
    void switchPitchCandidate(Selection sel, bool up);

    /**
     * Remove the pitch estimates from the selected area of the main
     * pitch track.
     */
    void clearPitches(Selection sel);

    /**
     * Move the main pitch track and any active analysis candidate
     * tracks up or down an octave in the selected area.
     */
    void shiftOctave(Selection sel, bool up);

    /**
     * Remove any re-analysis layers.
     */
    void clearReAnalysis();

    /**
     * Import the pitch track from the given layer into our
     * pitch-track layer.
     */
    void takePitchTrackFrom(Layer *layer);

    Pane *getPane() {
        return m_pane;
    }

    Layer *getLayer(Component type) {
        return m_layers[type];
    }

signals:
    void layersChanged();

protected:
    Document *m_document;
    WaveFileModel *m_fileModel;
    PaneStack *m_paneStack;
    Pane *m_pane;

    mutable std::map<Component, Layer *> m_layers;

    Selection m_reAnalysingSelection;
    std::vector<Layer *> m_reAnalysisCandidates;
    int m_currentCandidate;

    QString addVisualisations();
    QString addWaveform();
    QString addAnalyses();

    // Document::LayerCreationHandler method
    void layersCreated(std::vector<Layer *>, std::vector<Layer *>);

    void saveState(Component c) const;
    void loadState(Component c);
};

#endif
