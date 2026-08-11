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

#ifndef TEST_PREVIEW_CHUNK_H
#define TEST_PREVIEW_CHUNK_H

#include "../PreviewChunk.h"

#include <QObject>
#include <QtTest>

using namespace sv;

class TestPreviewChunk : public QObject
{
    Q_OBJECT

private:
    static const sv_frame_t MIN = 11025;
    static const sv_frame_t MAX = 220500;

    static Event pitch(sv_frame_t frame, float value) {
        return Event(frame, value, QString());
    }

private slots:

    // ---- nextRange --------------------------------------------------

    void nothingRecordedYet() {

        QCOMPARE(PreviewChunk::nextRange(0, 0, MIN, MAX, 0).has_value(), false);
    }

    void tooLittleNewAudio() {

        // Duration notifications arrive far more often than it is worth
        // starting a transform
        QCOMPARE(PreviewChunk::nextRange(0, MIN - 1, MIN, MAX, 0).has_value(),
                 false);
    }

    void exactlyEnoughNewAudio() {

        auto r = PreviewChunk::nextRange(0, MIN, MIN, MAX, 0);
        QVERIFY(r.has_value());
        QCOMPARE(r->from, sv_frame_t(0));
        QCOMPARE(r->to, MIN);
    }

    void rangeStartsWhereTheLastOneEnded() {

        // Adjacent and non-overlapping, so results can simply be
        // concatenated with nothing to merge
        auto first = PreviewChunk::nextRange(0, 50000, MIN, MAX, 0);
        QVERIFY(first.has_value());

        auto second = PreviewChunk::nextRange(first->to, 100000, MIN, MAX, 0);
        QVERIFY(second.has_value());

        QCOMPARE(second->from, first->to);
        QVERIFY(second->from >= first->to);
    }

    void neverRunsBackwards() {

        // A duration notification that arrives out of order, or a
        // recording that restarts, must not make the preview rewrite
        // what it has already drawn
        QCOMPARE(PreviewChunk::nextRange(100000, 50000, MIN, MAX, 0).has_value(),
                 false);
    }

    void longStallIsBrokenIntoChunks() {

        // If analysis falls a long way behind we want several ordinary
        // chunks rather than one enormous one
        auto r = PreviewChunk::nextRange(0, MAX * 3, MIN, MAX, 0);
        QVERIFY(r.has_value());
        QCOMPARE(r->from, sv_frame_t(0));
        QCOMPARE(r->to, MAX);
        QCOMPARE(r->length(), MAX);
    }

    void chunksEventuallyCatchUp() {

        // Repeatedly applying nextRange against a fixed end must
        // terminate, having covered the whole span exactly once
        const sv_frame_t recordedTo = MAX * 3 + 5000;

        sv_frame_t at = 0;
        int iterations = 0;

        while (auto r = PreviewChunk::nextRange(at, recordedTo, MIN, MAX, 0)) {
            QCOMPARE(r->from, at);
            QVERIFY(r->to > r->from);
            at = r->to;
            QVERIFY(++iterations < 100);
        }

        // Whatever is left is below the minimum chunk size
        QVERIFY(recordedTo - at < MIN);
    }

    void unboundedWhenMaxIsZero() {

        auto r = PreviewChunk::nextRange(0, 10 * MAX, MIN, 0, 0);
        QVERIFY(r.has_value());
        QCOMPARE(r->to, 10 * MAX);
    }

    void negativeStartIsClamped() {

        auto r = PreviewChunk::nextRange(-500, 50000, MIN, MAX, 0);
        QVERIFY(r.has_value());
        QCOMPARE(r->from, sv_frame_t(0));
    }

    // ---- revisiting -------------------------------------------------

    void revisitReachesBackIntoThepreviousRegion() {

        // Notes have duration and would be cut in two at a region
        // boundary. Reaching back lets the note tracker see the
        // surrounding audio and emit the note whole; the caller replaces
        // its earlier results for the revisited part.
        const sv_frame_t revisit = 66150;   // 1.5s at 44.1kHz

        auto r = PreviewChunk::nextRange(200000, 250000, MIN, MAX, revisit);
        QVERIFY(r.has_value());
        QCOMPARE(r->from, sv_frame_t(200000 - revisit));
        QCOMPARE(r->to, sv_frame_t(250000));
    }

    void revisitDoesNotChangeHowFarWeGet() {

        // Only the start moves back: the region must still end at the
        // recording head, or analysis would never catch up
        auto plain = PreviewChunk::nextRange(200000, 250000, MIN, MAX, 0);
        auto revisited = PreviewChunk::nextRange(200000, 250000, MIN, MAX, 66150);

        QVERIFY(plain.has_value() && revisited.has_value());
        QCOMPARE(revisited->to, plain->to);
        QVERIFY(revisited->from < plain->from);
    }

    void revisitIsClampedAtTheStartOfTheRecording() {

        auto r = PreviewChunk::nextRange(20000, 50000, MIN, MAX, 66150);
        QVERIFY(r.has_value());
        QCOMPARE(r->from, sv_frame_t(0));
    }

    void revisitStillTerminates() {

        // Revisiting must not stop analysis advancing: the end still
        // moves forward every pass even though the start moves back
        const sv_frame_t recordedTo = MAX * 3 + 5000;
        const sv_frame_t revisit = 66150;

        sv_frame_t at = 0;
        int iterations = 0;

        while (auto r = PreviewChunk::nextRange(at, recordedTo, MIN, MAX,
                                                revisit)) {
            QVERIFY(r->to > at);
            at = r->to;
            QVERIFY(++iterations < 100);
        }

        QVERIFY(recordedTo - at < MIN);
    }

    void addedEventsStaySortedAcrossPasses() {

        // RecordingPreview relies on this: it prunes its record of added
        // events from the region start and then appends that region's
        // events, so the record stays sorted by frame and can be pruned
        // by binary search rather than by scanning. That matters because
        // the record reaches six figures over a long recording and is
        // pruned on every pass.
        const sv_frame_t revisit = 66150;
        const sv_frame_t step = 20000;

        sv_frame_t analysedTo = 0;
        sv_frame_t recordedTo = 0;
        sv_frame_t lastFrom = -1;

        for (int pass = 0; pass < 40; ++pass) {

            recordedTo += step;

            auto r = PreviewChunk::nextRange(analysedTo, recordedTo,
                                             MIN, MAX, revisit);
            if (!r) continue;

            // The region start must never go backwards, or a prune
            // would leave later events in front of earlier ones
            QVERIFY(r->from >= lastFrom);
            lastFrom = r->from;

            // and must not exceed where we had reached, or a pass would
            // leave a gap it never fills
            QVERIFY(r->from <= analysedTo);

            analysedTo = r->to;
        }
    }

    // ---- withinRange ------------------------------------------------

    void withinRangeKeepsEventsInside() {

        EventVector events {
            pitch(1000, 440.f),
            pitch(1500, 441.f),
            pitch(1999, 442.f)
        };

        auto kept = PreviewChunk::withinRange(events, { 1000, 2000 });
        QCOMPARE(int(kept.size()), 3);
    }

    void withinRangeIsHalfOpen() {

        EventVector events {
            pitch(999, 440.f),    // before
            pitch(1000, 441.f),   // first frame, kept
            pitch(2000, 442.f)    // one past the end, dropped
        };

        auto kept = PreviewChunk::withinRange(events, { 1000, 2000 });
        QCOMPARE(int(kept.size()), 1);
        QCOMPARE(kept[0].getFrame(), sv_frame_t(1000));
    }

    void withinRangeRejectsRelativeFrames() {

        // A plugin whose output is relative to the start of the analysed
        // region rather than absolute would land near frame zero. Those
        // events must be discarded rather than drawn in the wrong place:
        // an empty preview is a far better failure than a pitch track
        // scattered across the recording.
        EventVector relative {
            pitch(0, 440.f),
            pitch(256, 441.f),
            pitch(512, 442.f)
        };

        auto kept = PreviewChunk::withinRange(relative, { 500000, 550000 });
        QCOMPARE(int(kept.size()), 0);
    }

    void withinRangeRejectsDoubledFrames() {

        // The specific failure this guards against: frames offset by the
        // region start a second time, landing at twice the true position
        const sv_frame_t from = 500000, to = 550000;

        EventVector doubled {
            pitch(from * 2, 440.f),
            pitch(from * 2 + 256, 441.f)
        };

        QCOMPARE(int(PreviewChunk::withinRange(doubled, { from, to }).size()), 0);
    }

    void withinRangeOfEmptyIsEmpty() {

        QCOMPARE(int(PreviewChunk::withinRange(EventVector(),
                                               { 0, 1000 }).size()), 0);
    }
};

#endif
