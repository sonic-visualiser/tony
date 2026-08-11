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

#include "RecordingPreview.h"

#include "base/Debug.h"
#include "base/RealTime.h"
#include "transform/Transform.h"
#include "transform/TransformFactory.h"
#include "transform/ModelTransformerFactory.h"
#include "data/model/WaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"

#include <algorithm>

using namespace sv;

RecordingPreview::RecordingPreview(QObject *parent) :
    QObject(parent),
    m_active(false),
    m_sampleRate(0),
    m_currentRange({ 0, 0 }),
    m_analysedTo(0),
    m_recordedTo(0)
{
}

RecordingPreview::~RecordingPreview()
{
    releaseTransforms();
}

sv_frame_t
RecordingPreview::toFrames(double seconds) const
{
    return sv_frame_t(seconds * m_sampleRate);
}

QString
RecordingPreview::begin(ModelId sourceModel,
                        TimeValueLayer *targetPitchLayer,
                        FlexiNoteLayer *targetNoteLayer)
{
    abandon();

    if (!targetPitchLayer && !targetNoteLayer) {
        return "Internal error: RecordingPreview::begin() called with no target layers";
    }

    auto source = ModelById::getAs<WaveFileModel>(sourceModel);
    if (!source) {
        return "Internal error: RecordingPreview::begin() called with no source model";
    }

    TransformFactory *tf = TransformFactory::getInstance();

    QString f0Id = QString("%1%2").arg(PYIN_TRANSFORM_BASE).arg(PYIN_F0_OUTPUT);
    QString noteId = QString("%1%2").arg(PYIN_TRANSFORM_BASE).arg(PYIN_NOTE_OUTPUT);

    if (!tf->haveTransform(f0Id) || !tf->haveTransform(noteId)) {
        return tr("Transform \"%1\" not found. Unable to preview analysis while "
                  "recording.<br><br>Is the pYIN Vamp plugin correctly installed?")
            .arg(f0Id);
    }

    m_sourceModel = sourceModel;
    m_sampleRate = source->getSampleRate();

    m_targetPitchLayer = targetPitchLayer;
    m_targetPitchModel = targetPitchLayer ? targetPitchLayer->getModel() : ModelId();

    m_targetNoteLayer = targetNoteLayer;
    m_targetNoteModel = targetNoteLayer ? targetNoteLayer->getModel() : ModelId();

    m_analysedTo = 0;
    m_recordedTo = 0;
    m_addedPitch.clear();
    m_addedNotes.clear();
    m_active = true;

    SVDEBUG << "RecordingPreview::begin: previewing into pitch model "
            << m_targetPitchModel << " and note model " << m_targetNoteModel
            << endl;

    return "";
}

void
RecordingPreview::recordedTo(sv_frame_t frame)
{
    if (!m_active) return;

    if (frame > m_recordedTo) {
        m_recordedTo = frame;
    }

    if (!m_pitchOutput.isNone() || !m_noteOutput.isNone()) {
        // A pass is already running; it will pick up the new mark when
        // it finishes
        return;
    }

    auto range = PreviewChunk::nextRange(m_analysedTo, m_recordedTo,
                                         toFrames(MIN_CHUNK_SECONDS),
                                         toFrames(MAX_CHUNK_SECONDS),
                                         toFrames(REVISIT_SECONDS));
    if (range) {
        startPass(*range);
    }
}

void
RecordingPreview::startPass(const PreviewChunk::Range &range)
{
    auto source = ModelById::getAs<WaveFileModel>(m_sourceModel);
    if (!source) {
        abandon();
        return;
    }

    TransformFactory *tf = TransformFactory::getInstance();

    QString f0Id = QString("%1%2").arg(PYIN_TRANSFORM_BASE).arg(PYIN_F0_OUTPUT);

    Transform transform = tf->getDefaultTransformFor(f0Id, m_sampleRate);
    transform.setStepSize(PYIN_STEP_SIZE);
    transform.setBlockSize(PYIN_BLOCK_SIZE);
    transform.setStartTime(RealTime::frame2RealTime(range.from, m_sampleRate));
    transform.setDuration(RealTime::frame2RealTime(range.length(), m_sampleRate));

    // Both outputs from a single run of the plugin: transformMultiple
    // requires transforms differing only in output identifier
    Transforms transforms;
    transforms.push_back(transform);
    transform.setOutput(PYIN_NOTE_OUTPUT);
    transforms.push_back(transform);

    QString message;

    // Not Document::createDerivedLayers: we want the output models only,
    // with no layers, no registration with the document and nothing
    // added to the undo history. The models returned here belong to us.
    std::vector<ModelId> outputs = ModelTransformerFactory::getInstance()->
        transformMultiple(transforms, ModelTransformer::Input(m_sourceModel),
                          message);

    if (outputs.size() < 2) {
        SVDEBUG << "RecordingPreview::startPass: transform failed: "
                << message << endl;
        for (ModelId id: outputs) {
            ModelTransformerFactory::getInstance()->cancel(id);
            ModelById::release(id);
        }
        // Move past this region rather than retrying it forever
        m_analysedTo = range.to;
        return;
    }

    m_pitchOutput = outputs[0];
    m_noteOutput = outputs[1];
    m_currentRange = range;

    bool complete = true;

    for (ModelId id: { m_pitchOutput, m_noteOutput }) {
        auto model = ModelById::get(id);
        if (!model) {
            complete = false;
            continue;
        }
        // Queued: completionChanged is emitted from the transform's own
        // thread, and the handler releases the model that emitted it.
        // Going through the event loop keeps us from doing that while
        // the signal is still being delivered.
        connect(model.get(), SIGNAL(completionChanged(ModelId)),
                this, SLOT(transformCompletionChanged(ModelId)),
                Qt::QueuedConnection);
        if (model->getCompletion() != 100) complete = false;
    }

    // The transforms may have finished already, in which case the
    // signals have been and gone and nothing further would arrive
    if (complete) {
        collectPass();
    }
}

void
RecordingPreview::transformCompletionChanged(ModelId)
{
    if (m_pitchOutput.isNone() || m_noteOutput.isNone()) return;

    for (ModelId id: { m_pitchOutput, m_noteOutput }) {
        auto model = ModelById::get(id);
        if (!model || model->getCompletion() != 100) return;
    }

    collectPass();
}

void
RecordingPreview::collectPass()
{
    const PreviewChunk::Range range = m_currentRange;

    auto pitchOut = ModelById::getAs<SparseTimeValueModel>(m_pitchOutput);
    auto noteOut = ModelById::getAs<NoteModel>(m_noteOutput);

    EventVector pitchEvents, noteEvents;

    if (pitchOut) {
        // pYIN's smoothedpitchtrack is a fixed-sample-rate output whose
        // features carry the host's block timestamps, so these frames
        // are already absolute.
        pitchEvents = PreviewChunk::withinRange(pitchOut->getAllEvents(), range);
    }

    if (noteOut) {
        // The notes output is different: it is variable-sample-rate and
        // derives its timestamps from a frame index counting from zero,
        // ignoring the host's block timestamps, so these frames are
        // relative to the start of the analysed region.
        EventVector relative = noteOut->getAllEvents();
        noteEvents.reserve(relative.size());
        for (const Event &e: relative) {
            noteEvents.push_back(e.withFrame(e.getFrame() + range.from));
        }
        noteEvents = PreviewChunk::withinRange(noteEvents, range);
    }

    releaseTransforms();

    // Replace, rather than merge: drop whatever we put in this region
    // last time before adding what we have now
    removeAddedFrom(range.from);

    auto pitchTarget = ModelById::getAs<SparseTimeValueModel>(m_targetPitchModel);
    if (pitchTarget && m_targetPitchLayer &&
        m_targetPitchLayer->getModel() == m_targetPitchModel) {
        for (const Event &e: pitchEvents) {
            pitchTarget->add(e);
            m_addedPitch.push_back(e);
        }
    }

    auto noteTarget = ModelById::getAs<NoteModel>(m_targetNoteModel);
    if (noteTarget && m_targetNoteLayer &&
        m_targetNoteLayer->getModel() == m_targetNoteModel) {
        for (const Event &e: noteEvents) {
            noteTarget->add(e);
            m_addedNotes.push_back(e);
        }
    }

    m_analysedTo = range.to;

    if (!pitchEvents.empty() || !noteEvents.empty()) {
        emit previewUpdated();
    }

    // Pick up anything that arrived while this pass was running
    auto next = PreviewChunk::nextRange(m_analysedTo, m_recordedTo,
                                        toFrames(MIN_CHUNK_SECONDS),
                                        toFrames(MAX_CHUNK_SECONDS),
                                        toFrames(REVISIT_SECONDS));
    if (next) {
        startPass(*next);
    }
}

void
RecordingPreview::releaseTransforms()
{
    for (ModelId *id: { &m_pitchOutput, &m_noteOutput }) {

        if (id->isNone()) continue;

        ModelId output = *id;
        *id = {};

        auto model = ModelById::get(output);
        if (model) {
            disconnect(model.get(), SIGNAL(completionChanged(ModelId)),
                       this, SLOT(transformCompletionChanged(ModelId)));
        }

        // Unconditionally, including when the transform has already
        // reported completion: reaching 100 happens inside run(), so the
        // thread may still be tearing down. cancel() waits for it to
        // exit, which is what makes it safe to release the model here.
        ModelTransformerFactory::getInstance()->cancel(output);
        ModelById::release(output);
    }
}

void
RecordingPreview::removeAddedFrom(sv_frame_t frame)
{
    auto pitchTarget = ModelById::getAs<SparseTimeValueModel>(m_targetPitchModel);
    bool pitchUsable = (pitchTarget && m_targetPitchLayer &&
                        m_targetPitchLayer->getModel() == m_targetPitchModel);

    auto noteTarget = ModelById::getAs<NoteModel>(m_targetNoteModel);
    bool noteUsable = (noteTarget && m_targetNoteLayer &&
                       m_targetNoteLayer->getModel() == m_targetNoteModel);

    // Each pass prunes from its region start and then appends that
    // region's events in order, and the region start only ever moves
    // forward, so these stay sorted by frame. That lets us find the
    // cut point rather than scanning: this runs on every pass, and the
    // vectors reach six figures over a long take.
    auto prune = [frame](EventVector &added, bool usable, auto target) {
        auto split = std::lower_bound
            (added.begin(), added.end(), frame,
             [](const Event &e, sv_frame_t f) { return e.getFrame() < f; });
        if (usable) {
            for (auto i = split; i != added.end(); ++i) {
                target->remove(*i);
            }
        }
        added.erase(split, added.end());
    };

    prune(m_addedPitch, pitchUsable, pitchTarget);
    prune(m_addedNotes, noteUsable, noteTarget);
}

void
RecordingPreview::end()
{
    if (!m_active) return;

    SVDEBUG << "RecordingPreview::end: removing " << m_addedPitch.size()
            << " preview pitch point(s) and " << m_addedNotes.size()
            << " preview note(s)" << endl;

    releaseTransforms();

    // Everything, from frame zero
    removeAddedFrom(0);

    m_active = false;
    m_targetPitchLayer = nullptr;
    m_targetNoteLayer = nullptr;
    m_targetPitchModel = {};
    m_targetNoteModel = {};
    m_sourceModel = {};
}

void
RecordingPreview::abandon()
{
    releaseTransforms();

    m_addedPitch.clear();
    m_addedNotes.clear();
    m_active = false;
    m_targetPitchLayer = nullptr;
    m_targetNoteLayer = nullptr;
    m_targetPitchModel = {};
    m_targetNoteModel = {};
    m_sourceModel = {};
    m_analysedTo = 0;
    m_recordedTo = 0;
}
