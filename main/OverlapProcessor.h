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

#ifndef OVERLAP_PROCESSOR_H
#define OVERLAP_PROCESSOR_H

#include <vector>
#include <cstddef>
#include <optional>

#include "base/BaseTypes.h"
#include "base/Event.h"

/**
 * Configuration parameters for overlap processing algorithms
 */
struct OverlapConfig {
    sv::sv_frame_t interpolationThreshold = 512;  // ~11ms at 44.1kHz
    double pitchSimilarityThreshold = 0.1;        // 10% pitch difference threshold

    OverlapConfig() = default;
    OverlapConfig(sv::sv_frame_t interpThresh, double pitchThresh)
        : interpolationThreshold(interpThresh)
        , pitchSimilarityThreshold(pitchThresh) {}
};

/**
 * Structure to represent a group of overlapping events. The indices
 * refer to positions within the event vector the group was derived
 * from; startFrame and endFrame span the whole group.
 */
struct OverlapGroup {
    std::vector<size_t> indices;
    sv::sv_frame_t startFrame;
    sv::sv_frame_t endFrame;

    OverlapGroup();
    explicit OverlapGroup(size_t index, const sv::Event &event);
    bool isEmpty() const { return indices.empty(); }
    size_t size() const { return indices.size(); }
    void addEvent(size_t index, const sv::Event &event);
};

/**
 * Main overlap processing class that handles detection and merging of
 * overlapping events.
 *
 * The process*Events methods are pure functions of their inputs: they
 * return the set of events to remove from, and add to, the target
 * model, without touching any model themselves.
 */
class OverlapProcessor {
public:
    explicit OverlapProcessor(const OverlapConfig& config = OverlapConfig());

    struct EventPatch {
        sv::EventVector remove;
        sv::EventVector add;
    };

    /**
     * Return the connected components of the overlap graph over the
     * given events, omitting any component with only one member. Two
     * events overlap if their half-open frame ranges intersect;
     * grouping is transitive, so A-B and B-C yields one group {A,B,C}.
     */
    std::vector<OverlapGroup> findOverlapGroups(const sv::EventVector& events) const;

    /**
     * Collapse a group into a single event spanning the whole group,
     * taking its label and level from the longest member and its value
     * from the duration-weighted geometric mean of the members.
     */
    std::optional<sv::Event> mergeOverlapGroup(const OverlapGroup& group,
                                               const sv::EventVector& events) const;

    float calculateWeightedFrequency(const sv::EventVector& overlappingEvents,
                                     sv::sv_frame_t overlapStart,
                                     sv::sv_frame_t overlapDuration) const;

    /**
     * Produce the patch that replaces the pitch track from
     * contextStart onwards with incomingEvents.
     *
     * incomingEvents are expected to carry ABSOLUTE frames, as pYIN's
     * smoothedpitchtrack output does: it is a FixedSampleRate output
     * whose features carry the host's block timestamp verbatim, and the
     * transformer reconstructs the frame from that timestamp. Here
     * contextStart is only the boundary of the region being replaced,
     * not an offset to add.
     */
    EventPatch processPitchEvents(sv::sv_frame_t contextStart,
                                  const sv::EventVector& incomingEvents,
                                  const sv::EventVector& existingEvents) const;

    /**
     * Produce the patch that merges incomingEvents into existingEvents.
     * Existing notes absorbed into a merge are included in the remove
     * list, and existing notes untouched by the incoming ones are left
     * alone.
     *
     * Unlike processPitchEvents(), incomingEvents are expected to carry
     * frames RELATIVE to contextStart, as pYIN's notes output does: it
     * derives its timestamps from a frame index counting from zero and
     * ignores the host's block timestamps. contextStart is added to them.
     */
    EventPatch processNoteEvents(sv::sv_frame_t contextStart,
                                 const sv::EventVector& incomingEvents,
                                 const sv::EventVector& existingEvents) const;

    /**
     * The pairwise overlap predicate that grouping is the transitive
     * closure of: true if the half-open ranges [frame, frame+duration)
     * of a and b intersect. Events that merely touch end-to-end do not
     * overlap.
     */
    bool eventsOverlap(const sv::Event& a, const sv::Event& b) const;

    const OverlapConfig& getConfig() const { return m_config; }
    void setConfig(const OverlapConfig& config) { m_config = config; }

private:
    OverlapConfig m_config;

    const sv::Event* findLongestEvent(const OverlapGroup& group,
                                      const sv::EventVector& events) const;
};

#endif // OVERLAP_PROCESSOR_H
