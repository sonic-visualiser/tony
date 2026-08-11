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

#ifndef TONY_PREVIEW_CHUNK_H
#define TONY_PREVIEW_CHUNK_H

#include "base/BaseTypes.h"
#include "base/Event.h"

#include <optional>

/**
 * Pure helpers for previewing analysis of a recording that is still
 * being made. These have no dependency on the document, models or UI,
 * so they can be tested directly.
 */
namespace PreviewChunk {

struct Range {
    sv::sv_frame_t from;
    sv::sv_frame_t to;

    sv::sv_frame_t length() const { return to - from; }

    bool operator==(const Range &r) const {
        return from == r.from && to == r.to;
    }
};

/**
 * Return the next region of a growing recording to analyse, given how
 * far analysis has already reached and how far the recording has got.
 *
 * The region starts revisitFrames before the end of the previous one,
 * so each pass re-analyses a short tail of what it did last time. The
 * caller is expected to discard its earlier results for that tail and
 * replace them wholesale.
 *
 * That matters for notes, which have duration and would otherwise be
 * chopped in two wherever a region boundary fell in the middle of one.
 * Revisiting gives the plugin's note tracker the surrounding audio and
 * lets it produce the note whole. Because a preview owns everything it
 * draws, replacing a region is just a delete and an add -- there is
 * nothing to merge, and no heuristic deciding which of two overlapping
 * notes to believe.
 *
 * The region never starts later than the previous one ended, so a
 * duration notification arriving out of order cannot make the preview
 * rewrite material it has already settled.
 *
 * Returns nothing if there is less than minFrames of new audio, so we
 * don't spend more time starting transforms than running them. The new
 * audio is clamped to maxFrames (when positive) so that a long stall
 * results in several ordinary passes rather than one huge one.
 */
std::optional<Range> nextRange(sv::sv_frame_t analysedTo,
                               sv::sv_frame_t recordedTo,
                               sv::sv_frame_t minFrames,
                               sv::sv_frame_t maxFrames,
                               sv::sv_frame_t revisitFrames);

/**
 * Return only those events whose frame lies within the given range.
 *
 * A transform output is expected to be expressed in absolute frames of
 * the source audio, but that depends on the plugin: a Vamp plugin that
 * derives its timestamps from a frame counter rather than from the
 * timestamps the host supplies will produce output relative to the
 * start of the region instead. Discarding anything outside the region
 * we asked for means such an output shows up as a preview that stays
 * empty, rather than as points scattered across the recording.
 */
sv::EventVector withinRange(const sv::EventVector &events,
                            const Range &range);

}

#endif
