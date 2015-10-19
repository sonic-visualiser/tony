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
#include "layer/ShowLayerCommand.h"

#include <QSettings>
#include <QMutexLocker>

using std::vector;

Analyser::Analyser() :
    m_document(0),
    m_fileModel(0),
    m_paneStack(0),
    m_pane(0),
    m_currentCandidate(-1),
    m_candidatesVisible(false),
    m_currentAsyncHandle(0)
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

    if (!m_fileModel) return "Internal error: Analyser::newFileLoaded() called with no model present";
    
    connect(doc, SIGNAL(layerAboutToBeDeleted(Layer *)),
            this, SLOT(layerAboutToBeDeleted(Layer *)));

    QSettings settings;
    settings.beginGroup("Analyser");
    bool autoAnalyse = settings.value("auto-analysis", true).toBool();
    settings.endGroup();

    return doAllAnalyses(autoAnalyse);
}

QString
Analyser::analyseExistingFile()
{
    if (!m_document) return "Internal error: Analyser::analyseExistingFile() called with no document present";

    if (!m_pane) return "Internal error: Analyser::analyseExistingFile() called with no pane present";

    if (!m_fileModel) return "Internal error: Analyser::analyseExistingFile() called with no model present";
    
    if (m_layers[PitchTrack]) {
        m_document->removeLayerFromView(m_pane, m_layers[PitchTrack]);
        m_layers[PitchTrack] = 0;
    }
    if (m_layers[Notes]) {
        m_document->removeLayerFromView(m_pane, m_layers[Notes]);
        m_layers[Notes] = 0;
    }

    return doAllAnalyses(true);
}

QString
Analyser::doAllAnalyses(bool withPitchTrack)
{
    m_reAnalysingSelection = Selection();
    m_reAnalysisCandidates.clear();
    m_currentCandidate = -1;
    m_candidatesVisible = false;

    // Note that we need at least one main-model layer (time ruler,
    // waveform or what have you). It could be hidden if we don't want
    // to see it but it must exist.

    QString warning, error;

    cerr << "Analyser::newFileLoaded: about to check visualisations etc" << endl;

    // This isn't fatal -- we can proceed without
    // visualisations. Other failures are fatal though.
    warning = addVisualisations();

    error = addWaveform();
    if (error != "") return error;

    if (withPitchTrack) {
        error = addAnalyses();
        if (error != "") return error;
    }

    loadState(Audio);
    loadState(PitchTrack);
    loadState(Notes);
    loadState(Spectrogram);

    stackLayers();

    emit layersChanged();

    return warning;
}

void
Analyser::fileClosed()
{
    cerr << "Analyser::fileClosed" << endl;
    m_layers.clear();
    m_reAnalysisCandidates.clear();
    m_currentCandidate = -1;
    m_reAnalysingSelection = Selection();
}

bool
Analyser::getDisplayFrequencyExtents(double &min, double &max)
{
    if (!m_layers[Spectrogram]) return false;
    return m_layers[Spectrogram]->getDisplayExtents(min, max);
}

bool
Analyser::setDisplayFrequencyExtents(double min, double max)
{
    if (!m_layers[Spectrogram]) return false;
    m_layers[Spectrogram]->setDisplayExtents(min, max);
    return true;
}

int
Analyser::getInitialAnalysisCompletion()
{
    int completion = 0;

    if (m_layers[PitchTrack]) {
        completion = m_layers[PitchTrack]->getCompletion(m_pane);
    }

    if (m_layers[Notes]) {
        int c = m_layers[Notes]->getCompletion(m_pane);
        if (c < completion) completion = c;
    }
    
    return completion;
}

void
Analyser::layerCompletionChanged()
{
    if (getInitialAnalysisCompletion() == 100) {

        emit initialAnalysisCompleted();

        if (m_layers[Audio]) {

            // Extend pitch-track and note layers so as to nominally
            // end at the same time as the audio. This affects any
            // time-filling done on export etc.
            
            sv_frame_t endFrame = m_layers[Audio]->getModel()->getEndFrame();
        
            if (m_layers[PitchTrack]) {
                SparseTimeValueModel *model = qobject_cast<SparseTimeValueModel *>
                    (m_layers[PitchTrack]->getModel());
                if (model) {
                    model->extendEndFrame(endFrame);
                }
            }
            if (m_layers[Notes]) {
                NoteModel *model = qobject_cast<NoteModel *>
                    (m_layers[Notes]->getModel());
                if (model) {
                    model->extendEndFrame(endFrame);
                }
            }
        }
    }
}

QString
Analyser::addVisualisations()
{
    if (!m_fileModel) return "Internal error: Analyser::addVisualisations() called with no model present";

    // A spectrogram, off by default. Must go at the back because it's
    // opaque

/* This is roughly what we'd do for a constant-Q spectrogram, but it
   currently has issues with y-axis alignment
  
    TransformFactory *tf = TransformFactory::getInstance();

    QString name = "Constant-Q";
    QString base = "vamp:cqvamp:cqvamp:";
    QString out = "constantq";

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

    // As with all the visualisation layers, if we already have one in
    // the pane we do not create another, just record its
    // existence. (We create a new one when loading a new audio file,
    // but just note the existing one when loading a complete session.)

    for (int i = 0; i < m_pane->getLayerCount(); ++i) {
        SpectrogramLayer *existing = qobject_cast<SpectrogramLayer *>
            (m_pane->getLayer(i));
        if (existing) {
            cerr << "recording existing spectrogram layer" << endl;
            m_layers[Spectrogram] = existing;
            return "";
        }
    }

    SpectrogramLayer *spectrogram = qobject_cast<SpectrogramLayer *>
        (m_document->createMainModelLayer(LayerFactory::MelodicRangeSpectrogram));

    spectrogram->setColourMap((int)ColourMapper::BlackOnWhite);
    spectrogram->setNormalization(SpectrogramLayer::NormalizeHybrid);
//    spectrogram->setGain(100);
    spectrogram->setGain(0.02f);
    m_document->addLayerToView(m_pane, spectrogram);
    spectrogram->setLayerDormant(m_pane, true);

    m_layers[Spectrogram] = spectrogram;

    return "";
}

QString
Analyser::addWaveform()
{
    // Our waveform layer is just a shadow, light grey and taking up
    // little space at the bottom.

    // As with the spectrogram above, if one exists already we just
    // use it
    for (int i = 0; i < m_pane->getLayerCount(); ++i) {
        WaveformLayer *existing = qobject_cast<WaveformLayer *>
            (m_pane->getLayer(i));
        if (existing) {
            cerr << "recording existing waveform layer" << endl;
            m_layers[Audio] = existing;
            return "";
        }
    }

    WaveformLayer *waveform = qobject_cast<WaveformLayer *>
        (m_document->createMainModelLayer(LayerFactory::Waveform));

    waveform->setMiddleLineHeight(0.9);
    waveform->setShowMeans(false); // too small & pale for this
    waveform->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Grey")));
    PlayParameters *params = waveform->getPlayParameters();
    if (params) {
        params->setPlayPan(-1);
        params->setPlayGain(1);
    }
    
    m_document->addLayerToView(m_pane, waveform);

    m_layers[Audio] = waveform;
    return "";
}

QString
Analyser::addAnalyses()
{
    // As with the spectrogram above, if these layers exist we use
    // them
    TimeValueLayer *existingPitch = 0;
    FlexiNoteLayer *existingNotes = 0;
    for (int i = 0; i < m_pane->getLayerCount(); ++i) {
        if (!existingPitch) {
            existingPitch = qobject_cast<TimeValueLayer *>(m_pane->getLayer(i));
        }
        if (!existingNotes) {
            existingNotes = qobject_cast<FlexiNoteLayer *>(m_pane->getLayer(i));
        }
    }
    if (existingPitch && existingNotes) {
        cerr << "recording existing pitch and notes layers" << endl;
        m_layers[PitchTrack] = existingPitch;
        m_layers[Notes] = existingNotes;
        return "";
    } else {
        if (existingPitch) {
            m_document->removeLayerFromView(m_pane, existingPitch);
            m_layers[PitchTrack] = 0;
        }
        if (existingNotes) {
            m_document->removeLayerFromView(m_pane, existingNotes);
            m_layers[Notes] = 0;
        }
    }

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

    QSettings settings;
    settings.beginGroup("Analyser");
    bool precise = settings.value("precision-analysis", false).toBool();
    bool lowamp = settings.value("lowamp-analysis", false).toBool();
    bool onset = settings.value("onset-analysis", true).toBool(); // should these be the same as in MainWindow.cpp?
    bool prune = settings.value("prune-analysis", true).toBool();
    settings.endGroup();

    Transform t = tf->getDefaultTransformFor
        (base + f0out, m_fileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    if (precise) {
        cerr << "setting parameters for precise mode" << endl;
        t.setParameter("precisetime", 1);
    } else {
        cerr << "setting parameters for vague mode" << endl;
        t.setParameter("precisetime", 0);
    }

    if (lowamp) {
        cerr << "setting parameters for lowamp suppression" << endl;
        t.setParameter("lowampsuppression", 0.2f);
    } else {
        cerr << "setting parameters for no lowamp suppression" << endl;
        t.setParameter("lowampsuppression", 0.0f);
    }

    if (onset) {
        cerr << "setting parameters for increased onset sensitivity" << endl;
        t.setParameter("onsetsensitivity", 0.7f);
    } else {
        cerr << "setting parameters for non-increased onset sensitivity" << endl;
        t.setParameter("onsetsensitivity", 0.0f);
    }

    if (prune) {
        cerr << "setting parameters for duration pruning" << endl;
        t.setParameter("prunethresh", 0.1f);
    } else {
        cerr << "setting parameters for no duration pruning" << endl;
        t.setParameter("prunethresh", 0.0f);
    }

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
        if (params) {
            params->setPlayPan(1);
            params->setPlayGain(0.5);
        }
        connect(pitchLayer, SIGNAL(modelCompletionChanged()),
                this, SLOT(layerCompletionChanged()));
    }
    
    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);
    if (flexiNoteLayer) {
        flexiNoteLayer->setBaseColour(cdb->getColourIndex(tr("Bright Blue")));
        PlayParameters *params = flexiNoteLayer->getPlayParameters();
        if (params) {
            params->setPlayPan(1);
            params->setPlayGain(0.5);
        }
        connect(flexiNoteLayer, SIGNAL(modelCompletionChanged()),
                this, SLOT(layerCompletionChanged()));
        connect(flexiNoteLayer, SIGNAL(reAnalyseRegion(sv_frame_t, sv_frame_t, float, float)),
                this, SLOT(reAnalyseRegion(sv_frame_t, sv_frame_t, float, float)));
        connect(flexiNoteLayer, SIGNAL(materialiseReAnalysis()),
                this, SLOT(materialiseReAnalysis()));
    }
    
    return "";
}

void
Analyser::reAnalyseRegion(sv_frame_t frame0, sv_frame_t frame1, float freq0, float freq1)
{
    cerr << "Analyser::reAnalyseRegion(" << frame0 << ", " << frame1
         << ", " << freq0 << ", " << freq1 << ")" << endl;
    showPitchCandidates(true);
    (void)reAnalyseSelection(Selection(frame0, frame1),
                             FrequencyRange(freq0, freq1));
}

void
Analyser::materialiseReAnalysis()
{
    if (m_reAnalysingSelection.isEmpty()) return;
    switchPitchCandidate(m_reAnalysingSelection, true); // or false, doesn't matter
}

QString
Analyser::reAnalyseSelection(Selection sel, FrequencyRange range)
{
    QMutexLocker locker(&m_asyncMutex);

    if (!m_reAnalysingSelection.isEmpty()) {
        if (sel == m_reAnalysingSelection && range == m_reAnalysingRange) {
            cerr << "selection & range are same as current analysis, ignoring" << endl;
            return "";
        }
    }

    if (sel.isEmpty()) return "";

    if (m_currentAsyncHandle) {
        m_document->cancelAsyncLayerCreation(m_currentAsyncHandle);
    }

    if (!m_reAnalysisCandidates.empty()) {
        CommandHistory::getInstance()->startCompoundOperation
            (tr("Discard Previous Candidates"), true);
        discardPitchCandidates();
        CommandHistory::getInstance()->endCompoundOperation();
    }

    m_reAnalysingSelection = sel;
    m_reAnalysingRange = range;

    m_preAnalysis = Clipboard();
    Layer *myLayer = m_layers[PitchTrack];
    if (myLayer) {
        myLayer->copy(m_pane, sel, m_preAnalysis);
    }

    TransformFactory *tf = TransformFactory::getInstance();
    
    QString plugname1 = "pYIN";
    QString plugname2 = "CHP";

    QString base = "vamp:pyin:localcandidatepyin:";
    QString out = "pitchtrackcandidates";

    if (range.isConstrained()) {
        base = "vamp:chp:constrainedharmonicpeak:";
        out = "peak";
    }

    Transforms transforms;

    QString notFound = tr("Transform \"%1\" not found. Unable to perform interactive analysis.<br><br>Are the %2 and %3 Vamp plugins correctly installed?");
    if (!tf->haveTransform(base + out)) {
	return notFound.arg(base + out).arg(plugname1).arg(plugname2);
    }

    Transform t = tf->getDefaultTransformFor
        (base + out, m_fileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    if (range.isConstrained()) {
        t.setParameter("minfreq", float(range.min));
        t.setParameter("maxfreq", float(range.max));
        t.setBlockSize(4096);
    }

    // get time stamps that align with the 256-sample grid of the original extraction
    const sv_frame_t grid = 256;
    sv_frame_t startSample = (sel.getStartFrame() / grid) * grid;
    if (startSample < sel.getStartFrame()) startSample += grid;
    sv_frame_t endSample = (sel.getEndFrame() / grid) * grid;
    if (endSample < sel.getEndFrame()) endSample += grid;
    if (!range.isConstrained()) {
        startSample -= 4*grid; // 4*256 is for 4 frames offset due to timestamp shift
        endSample   -= 4*grid;
    } else {
        endSample   -= 9*grid; // MM says: not sure what the CHP plugin does there
    }
    RealTime start = RealTime::frame2RealTime(startSample, m_fileModel->getSampleRate()); 
    RealTime end = RealTime::frame2RealTime(endSample, m_fileModel->getSampleRate());

    RealTime duration;

    if (sel.getEndFrame() > sel.getStartFrame()) {
        duration = end - start;
    }

    cerr << "Analyser::reAnalyseSelection: start " << start << " end " << end << " original selection start " << sel.getStartFrame() << " end " << sel.getEndFrame() << " duration " << duration << endl;

    if (duration <= RealTime::zeroTime) {
        cerr << "Analyser::reAnalyseSelection: duration <= 0, not analysing" << endl;
        return "";
    }
    
    t.setStartTime(start);
    t.setDuration(duration);

    transforms.push_back(t);
    
    m_currentAsyncHandle =
        m_document->createDerivedLayersAsync(transforms, m_fileModel, this);

    return "";
}

bool
Analyser::arePitchCandidatesShown() const
{
    return m_candidatesVisible;
}

void
Analyser::showPitchCandidates(bool shown) 
{
    if (m_candidatesVisible == shown) return;

    foreach (Layer *layer, m_reAnalysisCandidates) {
        if (shown) {
            CommandHistory::getInstance()->addCommand
                (new ShowLayerCommand(m_pane, layer, true,
                                      tr("Show Pitch Candidates")));
        } else {
            CommandHistory::getInstance()->addCommand
                (new ShowLayerCommand(m_pane, layer, false,
                                      tr("Hide Pitch Candidates")));
        }
    }

    m_candidatesVisible = shown;
}

void
Analyser::layersCreated(Document::LayerCreationAsyncHandle handle,
                        vector<Layer *> primary,
                        vector<Layer *> additional)
{
    {
        QMutexLocker locker(&m_asyncMutex);

        if (handle != m_currentAsyncHandle || 
            m_reAnalysingSelection == Selection()) {
            // We don't want these!
            for (int i = 0; i < (int)primary.size(); ++i) {
                m_document->deleteLayer(primary[i]);
            }
            for (int i = 0; i < (int)additional.size(); ++i) {
                m_document->deleteLayer(additional[i]);
            }
            return;
        }
        m_currentAsyncHandle = 0;

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Re-Analyse Selection"), true);

        m_reAnalysisCandidates.clear();

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
                t->setPresentationName("candidate");
                m_document->addLayerToView(m_pane, t);
                m_reAnalysisCandidates.push_back(t);
                cerr << "New re-analysis candidate model has "
                     << ((SparseTimeValueModel *)t->getModel())->getPoints().size() << " point(s)" << endl;
            }
        }

        if (!all.empty()) {
            bool show = m_candidatesVisible;
            m_candidatesVisible = !show; // to ensure the following takes effect
            showPitchCandidates(show);
        }

        CommandHistory::getInstance()->endCompoundOperation();
    }

    emit layersChanged();
}

bool
Analyser::haveHigherPitchCandidate() const
{
    if (m_reAnalysisCandidates.empty()) return false;
    return (m_currentCandidate < 0 ||
            (m_currentCandidate + 1 < (int)m_reAnalysisCandidates.size()));
}    

bool
Analyser::haveLowerPitchCandidate() const
{
    if (m_reAnalysisCandidates.empty()) return false;
    return (m_currentCandidate < 0 || m_currentCandidate >= 1);
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

    stackLayers();
}

void
Analyser::stackLayers()
{
    // raise the pitch track, then notes on top (if present)
    if (m_layers[PitchTrack]) {
        m_paneStack->setCurrentLayer(m_pane, m_layers[PitchTrack]);
    }
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
Analyser::deletePitches(Selection sel)
{
    Layer *pitchTrack = m_layers[PitchTrack];
    if (!pitchTrack) return;

    pitchTrack->deleteSelection(sel);
}

void
Analyser::abandonReAnalysis(Selection sel)
{
    // A compound command is already in progress

    discardPitchCandidates();

    Layer *myLayer = m_layers[PitchTrack];
    if (!myLayer) return;
    myLayer->deleteSelection(sel);
    myLayer->paste(m_pane, m_preAnalysis, 0, false);
}    

void
Analyser::clearReAnalysis()
{
    discardPitchCandidates();
}

void
Analyser::discardPitchCandidates()
{
    if (!m_reAnalysisCandidates.empty()) {
        // We don't use a compound command here, because we may be
        // already in one. Caller bears responsibility for doing that
        foreach (Layer *layer, m_reAnalysisCandidates) {
            // This will cause the layer to be deleted later (ownership is
            // transferred to the remove command)
            m_document->removeLayerFromView(m_pane, layer);
        }
        m_reAnalysisCandidates.clear();
    }

    m_currentCandidate = -1;
    m_reAnalysingSelection = Selection();
    m_candidatesVisible = false;
}

void
Analyser::layerAboutToBeDeleted(Layer *doomed)
{
    cerr << "Analyser::layerAboutToBeDeleted(" << doomed << ")" << endl;
    
    vector<Layer *> notDoomed;

    foreach (Layer *layer, m_reAnalysisCandidates) {
        if (layer != doomed) {
            notDoomed.push_back(layer);
        }
    }

    m_reAnalysisCandidates = notDoomed;
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

    // Remove all pitches <= 0Hz -- we now save absent pitches as 0Hz
    // values when exporting a pitch track, so we need to exclude them
    // here when importing again
    Clipboard::PointList after;
    int excl = 0;
    for (auto &p: clip.getPoints()) {
        if (p.haveValue() && p.getValue() > 0.f) {
            after.push_back(p);
        } else {
            ++excl;
        }
    }
    clip.setPoints(after);

    myLayer->paste(m_pane, clip, 0, false);
}

void
Analyser::getEnclosingSelectionScope(sv_frame_t f, sv_frame_t &f0, sv_frame_t &f1)
{
    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);

    sv_frame_t f0i = f, f1i = f;
    int res = 1;

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


    
