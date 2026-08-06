/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef TEST_OVERLAP_PROCESSOR_H
#define TEST_OVERLAP_PROCESSOR_H

#include "../OverlapProcessor.h"

#include <QObject>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace sv;

class TestOverlapProcessor : public QObject
{
    Q_OBJECT

private:
    static Event note(sv_frame_t frame, sv_frame_t duration, float value) {
        return Event(frame, value, duration, QString());
    }

    static Event pitch(sv_frame_t frame, float value) {
        return Event(frame, value, QString());
    }

    /** Apply a patch the way RealtimeAnalyser does: remove first, then
     *  add. Removing an event drops a single matching instance, as
     *  EventSeries::remove does. */
    static EventVector applyPatch(EventVector model,
                                  const OverlapProcessor::EventPatch &patch) {
        for (const auto &e: patch.remove) {
            auto itr = std::find(model.begin(), model.end(), e);
            if (itr != model.end()) model.erase(itr);
        }
        for (const auto &e: patch.add) {
            model.push_back(e);
        }
        std::sort(model.begin(), model.end());
        return model;
    }

    /** Brute-force reference implementation of grouping: the transitive
     *  closure of the pairwise overlap predicate. */
    static std::vector<std::set<size_t>>
    referenceGroups(const OverlapProcessor &p, const EventVector &events) {

        const size_t n = events.size();
        std::vector<size_t> component(n);
        for (size_t i = 0; i < n; ++i) component[i] = i;

        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = i + 1; j < n; ++j) {
                    if (!p.eventsOverlap(events[i], events[j])) continue;
                    const size_t a = std::min(component[i], component[j]);
                    const size_t b = std::max(component[i], component[j]);
                    if (a == b) continue;
                    for (size_t k = 0; k < n; ++k) {
                        if (component[k] == b) component[k] = a;
                    }
                    changed = true;
                }
            }
        }

        std::vector<std::set<size_t>> groups;
        for (size_t root = 0; root < n; ++root) {
            std::set<size_t> members;
            for (size_t k = 0; k < n; ++k) {
                if (component[k] == root) members.insert(k);
            }
            if (members.size() > 1) groups.push_back(members);
        }
        return groups;
    }

    static std::vector<std::set<size_t>>
    actualGroups(const OverlapProcessor &p, const EventVector &events) {
        std::vector<std::set<size_t>> groups;
        for (const auto &g: p.findOverlapGroups(events)) {
            groups.push_back(std::set<size_t>(g.indices.begin(),
                                              g.indices.end()));
        }
        std::sort(groups.begin(), groups.end());
        return groups;
    }

    /** True if any two distinct events in the vector overlap. */
    static bool hasAnyOverlap(const OverlapProcessor &p,
                              const EventVector &events) {
        for (size_t i = 0; i < events.size(); ++i) {
            for (size_t j = i + 1; j < events.size(); ++j) {
                if (p.eventsOverlap(events[i], events[j])) return true;
            }
        }
        return false;
    }

private slots:

    // ---- findOverlapGroups ------------------------------------------

    void groupsEmptyAndSingle() {

        OverlapProcessor p;

        QCOMPARE(int(p.findOverlapGroups(EventVector()).size()), 0);

        EventVector one { note(0, 100, 440.f) };
        QCOMPARE(int(p.findOverlapGroups(one).size()), 0);
    }

    void groupsIgnoreNonOverlapping() {

        OverlapProcessor p;

        EventVector events {
            note(0, 100, 440.f),
            note(200, 100, 450.f),
            note(400, 100, 460.f)
        };

        QCOMPARE(int(p.findOverlapGroups(events).size()), 0);
    }

    void groupsIgnoreTouchingEndToEnd() {

        // [0,100) and [100,200) share a boundary but do not overlap
        OverlapProcessor p;

        EventVector events {
            note(0, 100, 440.f),
            note(100, 100, 450.f)
        };

        QCOMPARE(int(p.findOverlapGroups(events).size()), 0);
        QCOMPARE(p.eventsOverlap(events[0], events[1]), false);
    }

    void groupsAreTransitive() {

        // A and C do not overlap each other, but both overlap B
        OverlapProcessor p;

        EventVector events {
            note(0, 100, 440.f),     // A
            note(90, 100, 450.f),    // B
            note(180, 100, 460.f)    // C
        };

        auto groups = p.findOverlapGroups(events);
        QCOMPARE(int(groups.size()), 1);
        QCOMPARE(int(groups[0].size()), 3);
        QCOMPARE(groups[0].startFrame, sv_frame_t(0));
        QCOMPARE(groups[0].endFrame, sv_frame_t(280));
        QCOMPARE(p.eventsOverlap(events[0], events[2]), false);
    }

    void groupsSpanIsNotBrokenByAShortMember() {

        // The short event in the middle must not end the run: the
        // running span is the maximum end frame of the group so far,
        // not the end frame of the previous event.
        OverlapProcessor p;

        EventVector events {
            note(0, 1000, 440.f),   // long
            note(10, 5, 450.f),     // short, entirely inside the long one
            note(900, 200, 460.f)   // overlaps the long one only
        };

        auto groups = p.findOverlapGroups(events);
        QCOMPARE(int(groups.size()), 1);
        QCOMPARE(int(groups[0].size()), 3);
        QCOMPARE(groups[0].endFrame, sv_frame_t(1100));
    }

    void groupsSeparateRuns() {

        OverlapProcessor p;

        EventVector events {
            note(0, 100, 440.f),
            note(50, 100, 450.f),
            note(1000, 100, 460.f),
            note(1050, 100, 470.f)
        };

        auto groups = p.findOverlapGroups(events);
        QCOMPARE(int(groups.size()), 2);
        QCOMPARE(int(groups[0].size()), 2);
        QCOMPARE(int(groups[1].size()), 2);
    }

    void groupsHandleZeroDurationEvents() {

        // Pitch-style events have no duration and so never overlap each
        // other, not even at the same frame
        OverlapProcessor p;

        EventVector events {
            pitch(100, 440.f),
            pitch(100, 441.f),
            pitch(200, 442.f)
        };

        QCOMPARE(int(p.findOverlapGroups(events).size()), 0);

        // ...but a durationed event does overlap the point events it
        // spans, and only those: [50,150) covers the two at frame 100
        // but not the one at frame 200.
        events.push_back(note(50, 100, 443.f));
        auto groups = p.findOverlapGroups(events);
        QCOMPARE(int(groups.size()), 1);
        QCOMPARE(groups[0].indices, std::vector<size_t>({ 3, 0, 1 }));
    }

    void groupsMatchTransitiveClosureReference() {

        // The sweep implementation must agree with a brute-force
        // transitive closure of eventsOverlap() on unsorted, nested,
        // duplicated and zero-duration input.
        OverlapProcessor p;

        EventVector events {
            note(500, 100, 440.f),
            note(0, 50, 441.f),
            note(40, 5, 442.f),
            note(490, 400, 443.f),
            pitch(45, 444.f),
            note(1000, 0, 445.f),
            note(880, 130, 446.f),
            note(0, 50, 441.f),
            note(2000, 10, 447.f),
            note(300, 100, 448.f)
        };

        auto expected = referenceGroups(p, events);
        std::sort(expected.begin(), expected.end());

        QCOMPARE(actualGroups(p, events), expected);
    }

    void groupsScaleToLongTracks() {

        // Regression guard for the old rescan-on-every-match loop, which
        // was cubic in the worst case and ran over the whole note track
        // once per realtime chunk. 20000 chained notes would not finish
        // in any reasonable time under that implementation.
        OverlapProcessor p;

        const int count = 20000;
        EventVector events;
        events.reserve(count);
        for (int i = 0; i < count; ++i) {
            events.push_back(note(i * 100, 150, 440.f));  // each overlaps the next
        }

        QElapsedTimer timer;
        timer.start();
        auto groups = p.findOverlapGroups(events);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(int(groups.size()), 1);
        QCOMPARE(int(groups[0].size()), count);
        QVERIFY2(elapsed < 5000,
                 QString("findOverlapGroups took %1 ms for %2 events")
                 .arg(elapsed).arg(count).toLocal8Bit().data());
    }

    // ---- mergeOverlapGroup / weighted frequency ---------------------

    void weightedFrequencyIsDurationWeightedGeometricMean() {

        OverlapProcessor p;

        EventVector events {
            note(0, 300, 400.f),
            note(0, 100, 800.f)
        };

        // weights 3/4 and 1/4 => exp(0.75*ln400 + 0.25*ln800)
        const double expected =
            std::exp(0.75 * std::log(400.0) + 0.25 * std::log(800.0));

        const float actual = p.calculateWeightedFrequency(events, 0, 300);
        QVERIFY(std::fabs(double(actual) - expected) < 0.01);
    }

    void weightedFrequencyIgnoresUnvoicedEvents() {

        OverlapProcessor p;

        EventVector events {
            note(0, 100, 440.f),
            note(0, 100, 0.f)      // unvoiced, must not drag the mean to zero
        };

        QCOMPARE(p.calculateWeightedFrequency(events, 0, 100), 440.f);
    }

    void mergeSpansWholeGroupAndTakesLabelFromLongest() {

        OverlapProcessor p;

        EventVector events {
            Event(0, 440.f, 100, "short"),
            Event(50, 450.f, 400, "long")
        };

        OverlapGroup group(0, events[0]);
        group.addEvent(1, events[1]);

        auto merged = p.mergeOverlapGroup(group, events);
        QVERIFY(merged.has_value());
        QCOMPARE(merged->getFrame(), sv_frame_t(0));
        QCOMPARE(merged->getDuration(), sv_frame_t(450));
        QCOMPARE(merged->getLabel(), QString("long"));
    }

    // ---- processNoteEvents ------------------------------------------

    void notesEmptyIncomingIsANoOp() {

        OverlapProcessor p;

        EventVector existing { note(0, 100, 440.f) };

        auto patch = p.processNoteEvents(1000, EventVector(), existing);
        QCOMPARE(int(patch.remove.size()), 0);
        QCOMPARE(int(patch.add.size()), 0);
    }

    void notesIncomingIsShiftedByContextStart() {

        OverlapProcessor p;

        EventVector incoming { note(0, 100, 440.f), note(200, 100, 450.f) };

        auto patch = p.processNoteEvents(5000, incoming, EventVector());

        QCOMPARE(int(patch.remove.size()), 0);
        QCOMPARE(int(patch.add.size()), 2);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(5000));
        QCOMPARE(patch.add[1].getFrame(), sv_frame_t(5200));
    }

    void notesLeaveUntouchedExistingAlone() {

        // An existing group far from the analysed region must be left
        // exactly as it is -- neither removed nor re-added.
        OverlapProcessor p;

        EventVector existing {
            note(0, 100, 440.f),
            note(50, 100, 441.f)      // overlaps its neighbour, but not the incoming
        };

        EventVector incoming { note(0, 100, 460.f) };

        auto patch = p.processNoteEvents(5000, incoming, existing);

        QCOMPARE(int(patch.remove.size()), 0);
        QCOMPARE(int(patch.add.size()), 1);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(5000));

        auto result = applyPatch(existing, patch);
        QCOMPARE(int(result.size()), 3);
    }

    void notesOverlappingExistingIsRemovedAndMerged() {

        OverlapProcessor p;

        EventVector existing { note(100, 150, 440.f) };   // [100,250)
        EventVector incoming { note(0, 200, 445.f) };     // shifted to [200,400)

        auto patch = p.processNoteEvents(200, incoming, existing);

        QCOMPARE(int(patch.remove.size()), 1);
        QCOMPARE(patch.remove[0].getFrame(), sv_frame_t(100));

        QCOMPARE(int(patch.add.size()), 1);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(100));
        QCOMPARE(patch.add[0].getFrame() + patch.add[0].getDuration(),
                 sv_frame_t(400));

        auto result = applyPatch(existing, patch);
        QCOMPARE(int(result.size()), 1);
    }

    void notesChainedExistingIsAlsoRemoved() {

        // Regression test.
        //
        // R overlaps D, and D overlaps the incoming note N, but R does
        // not overlap N. Grouping is transitive, so {N,D,R} merges into
        // a single event -- which means R is superseded too. Removing
        // only D (the one that directly overlapped N) left R behind
        // underneath the merged note, producing two overlapping notes
        // covering the same span.
        OverlapProcessor p;

        EventVector existing {
            note(150, 100, 440.f),   // D: [150,250)
            note(240, 60, 441.f)     // R: [240,300)
        };
        EventVector incoming { note(0, 100, 450.f) };  // N: [100,200)

        QCOMPARE(p.eventsOverlap(existing[1],
                                 incoming[0].withFrame(100)), false);

        auto patch = p.processNoteEvents(100, incoming, existing);

        QCOMPARE(int(patch.remove.size()), 2);
        QCOMPARE(int(patch.add.size()), 1);

        auto result = applyPatch(existing, patch);
        QCOMPARE(int(result.size()), 1);
        QCOMPARE(result[0].getFrame(), sv_frame_t(100));
        QCOMPARE(result[0].getFrame() + result[0].getDuration(),
                 sv_frame_t(300));
        QCOMPARE(hasAnyOverlap(p, result), false);
    }

    void notesLeaveNoOverlapsBehindInAChain() {

        // A longer version of the same shape: a chain of existing notes
        // reached only through their neighbours. Whatever the merge
        // decides to produce, the resulting model must not contain two
        // notes overlapping each other.
        OverlapProcessor p;

        EventVector existing;
        for (int i = 0; i < 8; ++i) {
            existing.push_back(note(300 + i * 90, 100, 440.f + i));
        }

        EventVector incoming { note(0, 100, 500.f) };  // -> [350,450)

        auto patch = p.processNoteEvents(350, incoming, existing);
        auto result = applyPatch(existing, patch);

        QCOMPARE(hasAnyOverlap(p, result), false);
    }

    void notesRepeatedChunksDoNotAccumulate() {

        // Realtime chunks overlap each other by design, so the same
        // audio is analysed more than once. Re-applying an identical
        // chunk must not grow the model.
        OverlapProcessor p;

        EventVector incoming {
            note(0, 100, 440.f),
            note(200, 100, 450.f)
        };

        EventVector model;
        model = applyPatch(model, p.processNoteEvents(1000, incoming, model));
        const int afterFirst = int(model.size());

        for (int i = 0; i < 5; ++i) {
            model = applyPatch(model,
                               p.processNoteEvents(1000, incoming, model));
        }

        QCOMPARE(int(model.size()), afterFirst);
        QCOMPARE(hasAnyOverlap(p, model), false);
    }

    // ---- processPitchEvents -----------------------------------------
    //
    // Note the asymmetry with processNoteEvents throughout this section:
    // incoming PITCH frames are absolute and are not shifted, because
    // pYIN's smoothedpitchtrack echoes the host's absolute block
    // timestamp, whereas its notes output counts frames from zero. See
    // pitchAndNoteFrameOriginsDiffer() below.

    void pitchIncomingIsNotShiftedByContextStart() {

        // Regression test. Adding contextStart to the incoming pitch
        // frames -- correct for notes, wrong for pitch -- put the live
        // pitch track at roughly twice its true frame, so during
        // recording it drifted off past the record head.
        OverlapProcessor p;

        EventVector incoming { pitch(5000, 440.f), pitch(5100, 441.f) };

        auto patch = p.processPitchEvents(5000, incoming, EventVector());

        QCOMPARE(int(patch.add.size()), 2);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(5000));
        QCOMPARE(patch.add[1].getFrame(), sv_frame_t(5100));
    }

    void pitchAndNoteFrameOriginsDiffer() {

        // Pins the contract that the two entry points disagree on
        // purpose. Same contextStart, same nominal event position, two
        // different frame conventions.
        OverlapProcessor p;

        const sv_frame_t contextStart = 5000;

        auto pitchPatch = p.processPitchEvents
            (contextStart, EventVector { pitch(5000, 440.f) }, EventVector());

        auto notePatch = p.processNoteEvents
            (contextStart, EventVector { note(0, 100, 440.f) }, EventVector());

        QCOMPARE(int(pitchPatch.add.size()), 1);
        QCOMPARE(int(notePatch.add.size()), 1);

        // Absolute in, absolute out -- unchanged
        QCOMPARE(pitchPatch.add[0].getFrame(), sv_frame_t(5000));
        // Relative in, shifted to absolute out
        QCOMPARE(notePatch.add[0].getFrame(), sv_frame_t(5000));
    }

    void pitchReplacesFromContextStartOnly() {

        OverlapProcessor p;

        EventVector existing {
            pitch(0, 400.f),
            pitch(100, 401.f),
            pitch(1000, 402.f),
            pitch(1100, 403.f)
        };

        EventVector incoming { pitch(1000, 500.f), pitch(1100, 501.f) };

        auto patch = p.processPitchEvents(1000, incoming, existing);

        QCOMPARE(int(patch.remove.size()), 2);
        QCOMPARE(patch.remove[0].getFrame(), sv_frame_t(1000));
        QCOMPARE(patch.remove[1].getFrame(), sv_frame_t(1100));

        auto result = applyPatch(existing, patch);
        QCOMPARE(int(result.size()), 4);
        QCOMPARE(result[0].getValue(), 400.f);
        QCOMPARE(result[1].getValue(), 401.f);
        QCOMPARE(result[2].getFrame(), sv_frame_t(1000));
        QCOMPARE(result[2].getValue(), 500.f);
        QCOMPARE(result[3].getFrame(), sv_frame_t(1100));
        QCOMPARE(result[3].getValue(), 501.f);
    }

    void pitchInterpolatesAcrossASmallSimilarGap() {

        OverlapProcessor p;   // threshold 512 frames, 10% pitch difference

        EventVector existing { pitch(900, 440.f) };
        EventVector incoming { pitch(1100, 445.f) };   // 200-frame gap

        auto patch = p.processPitchEvents(1000, incoming, existing);

        QCOMPARE(int(patch.add.size()), 2);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(1000));   // midpoint
        QCOMPARE(patch.add[0].getLabel(), QString("interpolated"));
        QCOMPARE(patch.add[0].getValue(), (440.f + 445.f) / 2.f);
        QCOMPARE(patch.add[1].getFrame(), sv_frame_t(1100));
    }

    void pitchDoesNotInterpolateAcrossALargeGap() {

        OverlapProcessor p;

        EventVector existing { pitch(0, 440.f) };
        EventVector incoming { pitch(1100, 445.f) };   // gap of 1100 frames

        auto patch = p.processPitchEvents(1000, incoming, existing);

        QCOMPARE(int(patch.add.size()), 1);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(1100));
    }

    void pitchDoesNotInterpolateAcrossADissimilarPitch() {

        OverlapProcessor p;

        EventVector existing { pitch(900, 440.f) };
        EventVector incoming { pitch(1100, 880.f) };   // an octave up

        auto patch = p.processPitchEvents(1000, incoming, existing);

        QCOMPARE(int(patch.add.size()), 1);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(1100));
    }

    void pitchSeamUsesLatestEventBeforeContextStart() {

        // The seam must be measured from the last event before the
        // analysed region, whichever order the existing events arrive in
        EventVector existing {
            pitch(100, 300.f),
            pitch(900, 440.f),
            pitch(500, 350.f)
        };

        OverlapProcessor p;
        EventVector incoming { pitch(1100, 445.f) };

        auto patch = p.processPitchEvents(1000, incoming, existing);

        QCOMPARE(int(patch.add.size()), 2);
        QCOMPARE(patch.add[0].getFrame(), sv_frame_t(1000));
        QCOMPARE(patch.add[0].getValue(), (440.f + 445.f) / 2.f);
    }

    void pitchEmptyIncomingStillClearsTheRegion() {

        // An analysis that found no pitch at all should clear whatever
        // the previous chunk left behind in that region
        OverlapProcessor p;

        EventVector existing { pitch(0, 400.f), pitch(1000, 401.f) };

        auto patch = p.processPitchEvents(1000, EventVector(), existing);

        QCOMPARE(int(patch.remove.size()), 1);
        QCOMPARE(patch.remove[0].getFrame(), sv_frame_t(1000));
        QCOMPARE(int(patch.add.size()), 0);
    }

    void pitchRepeatedChunksDoNotAccumulate() {

        OverlapProcessor p;

        EventVector incoming { pitch(1000, 440.f), pitch(1100, 441.f) };

        EventVector model;
        model = applyPatch(model, p.processPitchEvents(1000, incoming, model));
        const int afterFirst = int(model.size());

        for (int i = 0; i < 5; ++i) {
            model = applyPatch(model,
                               p.processPitchEvents(1000, incoming, model));
        }

        QCOMPARE(int(model.size()), afterFirst);
    }
};

#endif
