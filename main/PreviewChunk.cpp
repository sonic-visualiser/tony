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

#include "PreviewChunk.h"

using namespace sv;

namespace PreviewChunk {

std::optional<Range>
nextRange(sv_frame_t analysedTo,
          sv_frame_t recordedTo,
          sv_frame_t minFrames,
          sv_frame_t maxFrames,
          sv_frame_t revisitFrames)
{
    if (analysedTo < 0) analysedTo = 0;

    if (recordedTo <= analysedTo) {
        return {};
    }

    if (recordedTo - analysedTo < minFrames) {
        return {};
    }

    sv_frame_t to = recordedTo;

    if (maxFrames > 0 && to - analysedTo > maxFrames) {
        to = analysedTo + maxFrames;
    }

    sv_frame_t from = analysedTo - revisitFrames;
    if (from < 0) from = 0;

    return Range { from, to };
}

EventVector
withinRange(const EventVector &events, const Range &range)
{
    EventVector result;
    result.reserve(events.size());

    for (const auto &e: events) {
        if (e.getFrame() >= range.from && e.getFrame() < range.to) {
            result.push_back(e);
        }
    }

    return result;
}

}
