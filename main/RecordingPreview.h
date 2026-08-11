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

#ifndef TONY_RECORDING_PREVIEW_H
#define TONY_RECORDING_PREVIEW_H

#include "PreviewChunk.h"

#include "base/BaseTypes.h"
#include "data/model/Model.h"

#include <QObject>
#include <QPointer>

namespace sv {
class TimeValueLayer;
class FlexiNoteLayer;
}

/**
 * Fills in the pitch track and notes as a recording is made, so that the
 * performer can see something while singing or playing.
 *
 * This is only ever a preview. When the recording stops, the pitch and
 * note layers are regenerated in full from the completed audio (by
 * Document::replaceModel, via MainWindowBase's refreshModel call), so
 * nothing produced here survives into the saved session. Accordingly
 * this class removes everything it added when the recording finishes,
 * leaving the layers exactly as they would have been without it.
 *
 * Each pass runs a pYIN transform over one region of the recording so
 * far, and the regions overlap: each pass revisits a short tail of the
 * previous one and replaces the preview's contents there. Notes have
 * duration and would otherwise be cut in two wherever a region boundary
 * fell inside one; revisiting lets the note tracker see the surrounding
 * audio and emit the note whole. Because everything drawn here belongs
 * to the preview, replacing a region is a delete followed by an add,
 * with nothing to merge and no heuristic choosing between overlapping
 * notes.
 *
 * Only one transform runs at a time. Duration updates arriving while one
 * is running raise the mark for the next pass rather than starting
 * another.
 *
 * The output models come from ModelTransformerFactory rather than from
 * Document, so they belong to us: no layer is created for them, they are
 * not registered with the document, and nothing is added to the undo
 * history.
 */
class RecordingPreview : public QObject
{
    Q_OBJECT

public:
    explicit RecordingPreview(QObject *parent = nullptr);
    virtual ~RecordingPreview();

    /**
     * Begin previewing into the given layers, analysing the given source
     * (recording) model. Either target layer may be null, in which case
     * that part of the preview is skipped. Returns "" on success or an
     * error string on failure.
     */
    QString begin(sv::ModelId sourceModel,
                  sv::TimeValueLayer *targetPitchLayer,
                  sv::FlexiNoteLayer *targetNoteLayer);

    /**
     * Note that the recording has reached the given frame. Starts a pass
     * if one is not already running and there is enough new audio.
     */
    void recordedTo(sv::sv_frame_t frame);

    /**
     * The recording has finished: cancel any running analysis and remove
     * everything this preview added.
     */
    void end();

    /**
     * Abandon the preview without touching the targets, for use when
     * they are being replaced anyway.
     */
    void abandon();

    bool isActive() const { return m_active; }

signals:
    void previewUpdated();

protected slots:
    void transformCompletionChanged(sv::ModelId);

protected:
    void startPass(const PreviewChunk::Range &range);
    void collectPass();
    void releaseTransforms();
    void removeAddedFrom(sv::sv_frame_t frame);

    // Enough new audio to be worth starting a transform for, but short
    // enough to feel responsive
    static constexpr double MIN_CHUNK_SECONDS = 0.25;

    // Bound the work in a single pass, so a stall produces several
    // ordinary passes rather than one very long one
    static constexpr double MAX_CHUNK_SECONDS = 5.0;

    // How much of the previous pass to re-analyse and replace. Needs to
    // comfortably exceed the length of a note for notes to come out
    // whole across a boundary.
    static constexpr double REVISIT_SECONDS = 1.5;

    static const int PYIN_STEP_SIZE = 256;
    static const int PYIN_BLOCK_SIZE = 2048;

    static constexpr const char *PYIN_TRANSFORM_BASE = "vamp:pyin:pyin:";
    static constexpr const char *PYIN_F0_OUTPUT = "smoothedpitchtrack";
    static constexpr const char *PYIN_NOTE_OUTPUT = "notes";

    sv::sv_frame_t toFrames(double seconds) const;

    bool m_active;

    sv::ModelId m_sourceModel;
    sv::sv_samplerate_t m_sampleRate;

    QPointer<sv::TimeValueLayer> m_targetPitchLayer;
    sv::ModelId m_targetPitchModel;
    sv::EventVector m_addedPitch;

    QPointer<sv::FlexiNoteLayer> m_targetNoteLayer;
    sv::ModelId m_targetNoteModel;
    sv::EventVector m_addedNotes;

    sv::ModelId m_pitchOutput;
    sv::ModelId m_noteOutput;

    PreviewChunk::Range m_currentRange;

    sv::sv_frame_t m_analysedTo;
    sv::sv_frame_t m_recordedTo;
};

#endif
