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

#include "OverlapProcessor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace sv;

namespace {

bool lessEventForDedup(const Event &a, const Event &b)
{
    if (a.getFrame() != b.getFrame()) return a.getFrame() < b.getFrame();
    if (a.getDuration() != b.getDuration()) return a.getDuration() < b.getDuration();

    if (a.hasValue() != b.hasValue()) return a.hasValue() < b.hasValue();
    if (a.hasValue() && a.getValue() != b.getValue()) return a.getValue() < b.getValue();

    if (a.hasLabel() != b.hasLabel()) return a.hasLabel() < b.hasLabel();
    if (a.hasLabel() && a.getLabel() != b.getLabel()) return a.getLabel() < b.getLabel();

    if (a.hasLevel() != b.hasLevel()) return a.hasLevel() < b.hasLevel();
    if (a.hasLevel() && a.getLevel() != b.getLevel()) return a.getLevel() < b.getLevel();

    return false;
}

bool equalEventForDedup(const Event &a, const Event &b)
{
    return a.getFrame() == b.getFrame() &&
           a.getDuration() == b.getDuration() &&
           a.hasValue() == b.hasValue() &&
           (!a.hasValue() || a.getValue() == b.getValue()) &&
           a.hasLabel() == b.hasLabel() &&
           (!a.hasLabel() || a.getLabel() == b.getLabel()) &&
           a.hasLevel() == b.hasLevel() &&
           (!a.hasLevel() || a.getLevel() == b.getLevel());
}

void sortAndDedupeEvents(EventVector &events)
{
    std::sort(events.begin(), events.end(), lessEventForDedup);
    events.erase(std::unique(events.begin(), events.end(), equalEventForDedup),
                 events.end());
}

EventVector shiftedBy(const EventVector &events, sv_frame_t offset)
{
    EventVector shifted;
    shifted.reserve(events.size());
    for (const auto &event : events) {
        shifted.push_back(event.withFrame(event.getFrame() + offset));
    }
    return shifted;
}

}

// OverlapGroup implementation
OverlapGroup::OverlapGroup() : startFrame(0), endFrame(0)
{
}

OverlapGroup::OverlapGroup(size_t index, const Event &event) :
    indices{index},
    startFrame(event.getFrame()),
    endFrame(event.getFrame() + event.getDuration())
{
}

void OverlapGroup::addEvent(size_t index, const Event &event)
{
    if (indices.empty()) {
        startFrame = event.getFrame();
        endFrame = event.getFrame() + event.getDuration();
    } else {
        startFrame = std::min(startFrame, event.getFrame());
        endFrame = std::max(endFrame, event.getFrame() + event.getDuration());
    }

    indices.push_back(index);
}

// OverlapProcessor implementation
OverlapProcessor::OverlapProcessor(const OverlapConfig &config) :
    m_config(config)
{
}

std::vector<OverlapGroup>
OverlapProcessor::findOverlapGroups(const EventVector &events) const
{
    std::vector<OverlapGroup> groups;
    if (events.size() < 2) return groups;

    // The connected components of an interval overlap graph are
    // contiguous once the intervals are sorted by start frame, so a
    // single sweep tracking the running maximum end frame finds them
    // all. (The previous implementation rescanned the whole event set
    // after every match, which is cubic in the worst case and was being
    // run over the entire note track once per realtime chunk.)

    std::vector<size_t> order(events.size());
    std::iota(order.begin(), order.end(), size_t(0));

    std::sort(order.begin(), order.end(),
              [&events](size_t a, size_t b) {
                  if (events[a].getFrame() != events[b].getFrame()) {
                      return events[a].getFrame() < events[b].getFrame();
                  }
                  return a < b;
              });

    OverlapGroup current(order[0], events[order[0]]);

    for (size_t i = 1; i < order.size(); ++i) {

        const size_t index = order[i];
        const Event &event = events[index];

        // Equivalent to eventsOverlap() against some group member,
        // given that no later event can start before this one does.
        if (event.getFrame() < current.endFrame) {
            current.addEvent(index, event);
        } else {
            if (current.size() > 1) groups.push_back(current);
            current = OverlapGroup(index, event);
        }
    }

    if (current.size() > 1) groups.push_back(current);

    return groups;
}

float OverlapProcessor::calculateWeightedFrequency(const EventVector& overlappingEvents,
                                                  sv_frame_t overlapStart,
                                                  sv_frame_t overlapDuration) const {
    if (overlappingEvents.empty()) return 0.0f;
    if (overlappingEvents.size() == 1) {
        return overlappingEvents[0].hasValue() ? overlappingEvents[0].getValue() : 0.0f;
    }

    std::vector<float> frequencies;
    std::vector<double> weights;
    frequencies.reserve(overlappingEvents.size());
    weights.reserve(overlappingEvents.size());

    const auto overlapEnd = overlapStart + overlapDuration;

    for (const auto& event : overlappingEvents) {
        const float freq = event.hasValue() ? event.getValue() : 0.0f;
        if (freq <= 0.0f) continue;

        const auto eventStart = event.getFrame();
        const auto eventEnd = event.getFrame() + event.getDuration();

        const auto eventOverlapStart = std::max(eventStart, overlapStart);
        const auto eventOverlapEnd = std::min(eventEnd, overlapEnd);
        const auto eventOverlapContrib = std::max<sv_frame_t>(0, eventOverlapEnd - eventOverlapStart);

        if (eventOverlapContrib > 0) {
            frequencies.push_back(freq);
            weights.push_back(static_cast<double>(eventOverlapContrib));
        }
    }

    if (frequencies.empty()) return 0.0f;
    if (frequencies.size() == 1) return frequencies[0];

    double totalWeight = 0.0;
    for (double weight : weights) totalWeight += weight;
    if (totalWeight <= 0.0) {
        double logSum = 0.0;
        for (float freq : frequencies) {
            logSum += std::log(freq);
        }
        return float(std::exp(logSum / double(frequencies.size())));
    }

    for (double& weight : weights) weight /= totalWeight;

    double weightedLogSum = 0.0;
    for (size_t i = 0; i < frequencies.size(); ++i) {
        weightedLogSum += std::log(frequencies[i]) * weights[i];
    }

    return float(std::exp(weightedLogSum));
}

std::optional<Event> OverlapProcessor::mergeOverlapGroup(const OverlapGroup& group, const EventVector& events) const {
    if (group.isEmpty()) {
        return std::nullopt;
    }

    if (group.size() == 1) {
        return events[group.indices[0]];
    }

    const auto mergedStart = group.startFrame;
    const auto mergedEnd = group.endFrame;
    const auto mergedDuration = mergedEnd - mergedStart;

    EventVector overlappingEvents;
    overlappingEvents.reserve(group.indices.size());
    for (size_t index : group.indices) {
        overlappingEvents.push_back(events[index]);
    }

    const float weightedFreq =
        calculateWeightedFrequency(overlappingEvents, mergedStart, mergedDuration);

    const Event* longestEvent = findLongestEvent(group, events);
    if (!longestEvent) {
        return std::nullopt;
    }

    Event mergedEvent = longestEvent->withFrame(mergedStart)
                                   .withDuration(mergedDuration);

    if (weightedFreq > 0.0f) {
        mergedEvent = mergedEvent.withValue(weightedFreq);
    }

    if (longestEvent->hasLabel()) {
        mergedEvent = mergedEvent.withLabel(longestEvent->getLabel());
    }

    if (longestEvent->hasLevel()) {
        mergedEvent = mergedEvent.withLevel(longestEvent->getLevel());
    }

    return mergedEvent;
}

OverlapProcessor::EventPatch OverlapProcessor::processPitchEvents(sv_frame_t contextStart,
                                                                  const EventVector& incomingEvents,
                                                                  const EventVector& existingEvents) const {
    EventPatch patch;

    // Incoming pitch frames are NOT shifted, unlike note frames.
    //
    // pYIN's smoothedpitchtrack is a FixedSampleRate output, and its
    // features carry the host-supplied block timestamp verbatim
    // (PYinVamp.cpp: f.timestamp = m_timestamp[iFrame]). The host feeds
    // absolute block timestamps, and FeatureExtractionModelTransformer's
    // FixedSampleRate branch reconstructs the frame from that timestamp,
    // so these frames are already absolute.
    //
    // Its notes output behaves differently -- it counts frames from zero
    // and ignores the host timestamps -- so processNoteEvents() does have
    // to shift. Adding contextStart here as well was what put the live
    // pitch track at roughly twice its correct frame.
    const EventVector &incoming = incomingEvents;

    // Everything from contextStart onwards is superseded by the fresh
    // analysis. We need the latest-ending event before contextStart in
    // order to bridge the seam, but not a copy of the whole preceding
    // track -- this runs once per realtime chunk over the entire pitch
    // track, which grows for the length of the recording.
    const Event *lastBefore = nullptr;

    patch.remove.reserve(existingEvents.size());

    for (const auto& event : existingEvents) {
        const auto eventStart = event.getFrame();
        const auto eventEnd = eventStart + event.getDuration();

        if (eventStart >= contextStart || eventEnd > contextStart) {
            patch.remove.push_back(event);
        } else if (!lastBefore ||
                   eventEnd > lastBefore->getFrame() + lastBefore->getDuration()) {
            lastBefore = &event;
        }
    }

    sortAndDedupeEvents(patch.remove);

    patch.add = incoming;

    if (lastBefore && !incoming.empty()) {

        const auto& firstNewEvent = incoming.front();

        const auto lastExistingEnd =
            lastBefore->getFrame() + lastBefore->getDuration();
        const auto gapFrames = firstNewEvent.getFrame() - lastExistingEnd;

        if (gapFrames > 0 && gapFrames <= m_config.interpolationThreshold) {
            if (lastBefore->hasValue() && firstNewEvent.hasValue()) {
                const auto lastValue = lastBefore->getValue();
                const auto firstValue = firstNewEvent.getValue();

                if (lastValue > 0.0f && firstValue > 0.0f) {
                    const auto valueDiff = std::abs(lastValue - firstValue) / lastValue;

                    if (valueDiff <= m_config.pitchSimilarityThreshold) {
                        const auto midFrame = lastExistingEnd + gapFrames / 2;
                        const auto midValue = (lastValue + firstValue) / 2.0f;
                        patch.add.push_back(Event(midFrame, midValue, "interpolated"));
                    }
                }
            }
        }
    }

    sortAndDedupeEvents(patch.add);

    return patch;
}

OverlapProcessor::EventPatch OverlapProcessor::processNoteEvents(sv_frame_t contextStart,
                                                                 const EventVector& incomingEvents,
                                                                 const EventVector& existingEvents) const {
    EventPatch patch;

    const EventVector shiftedIncoming = shiftedBy(incomingEvents, contextStart);
    if (shiftedIncoming.empty()) return patch;

    // Single index space: [0, existingCount) are existing events, the
    // rest are incoming ones. Overlap grouping is transitive, so an
    // existing note can be pulled into a merge by way of another
    // existing note even when it does not itself overlap anything
    // incoming. It is still superseded by the merged event and must be
    // removed, or it would be left behind as a duplicate underneath it.
    const size_t existingCount = existingEvents.size();

    EventVector combined;
    combined.reserve(existingCount + shiftedIncoming.size());
    combined.insert(combined.end(), existingEvents.begin(), existingEvents.end());
    combined.insert(combined.end(), shiftedIncoming.begin(), shiftedIncoming.end());

    const auto groups = findOverlapGroups(combined);

    std::vector<char> grouped(combined.size(), 0);

    for (const auto& group : groups) {

        bool touchesIncoming = false;
        for (size_t index : group.indices) {
            grouped[index] = 1;
            if (index >= existingCount) touchesIncoming = true;
        }

        if (!touchesIncoming) {
            // Pre-existing notes that overlap only each other, outside
            // the region we just analysed: leave them exactly as they
            // are.
            continue;
        }

        for (size_t index : group.indices) {
            if (index < existingCount) {
                patch.remove.push_back(combined[index]);
            }
        }

        if (auto merged = mergeOverlapGroup(group, combined)) {
            patch.add.push_back(*merged);
        }
    }

    // Incoming notes that overlapped nothing at all
    for (size_t i = existingCount; i < combined.size(); ++i) {
        if (!grouped[i]) patch.add.push_back(combined[i]);
    }

    // Groups are disjoint, so remove holds each existing event at most
    // once already; sort for determinism but don't dedupe, so that a
    // model containing genuine duplicates has both instances removed.
    std::sort(patch.remove.begin(), patch.remove.end(), lessEventForDedup);
    sortAndDedupeEvents(patch.add);

    return patch;
}

// Private helper methods
bool OverlapProcessor::eventsOverlap(const Event& a, const Event& b) const {
    const auto aStart = a.getFrame();
    const auto aEnd = a.getFrame() + a.getDuration();
    const auto bStart = b.getFrame();
    const auto bEnd = b.getFrame() + b.getDuration();

    // Intentionally strict overlap test: both grouping and patch inclusion
    // only consider events that truly overlap in time.
    return aStart < bEnd && aEnd > bStart;
}

const Event* OverlapProcessor::findLongestEvent(const OverlapGroup& group, const EventVector& events) const {
    if (group.indices.empty()) return nullptr;

    const Event* longestEvent = &events[group.indices[0]];
    for (size_t index : group.indices) {
        const Event &event = events[index];
        if (event.getDuration() > longestEvent->getDuration()) {
            longestEvent = &event;
        }
    }
    return longestEvent;
}
