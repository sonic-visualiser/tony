/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

 /*
     Tony
     Realtime analysis helper (extracted from Analyser)
 */

#ifndef REALTIMEANALYSER_H
#define REALTIMEANALYSER_H

#include <QObject>
#include <QPointer>
#include <QMutex>
#include <QtGlobal>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

#include "framework/Document.h"
#include "base/Selection.h"

namespace sv {
class Pane;
class Layer;
class TimeValueLayer;
class FlexiNoteLayer;
}

class RealtimeAnalyser : public QObject
{
    Q_OBJECT

public:
    explicit RealtimeAnalyser(QObject *parent = nullptr);
    ~RealtimeAnalyser() override;

    void setContext(sv::Document *document,
                    sv::ModelId fileModel,
                    sv::Pane *pane,
                    sv::TimeValueLayer *targetPitchLayer,
                    sv::FlexiNoteLayer *targetNoteLayer);

    void clearContext();

    /**
     * Abandon all in-flight and pending work, delete any temporary
     * layers, and bump the generation so that callbacks still queued
     * for delivery are ignored. Never starts new work, so it is safe to
     * call from a destructor or while tearing down a document.
     */
    void cleanup();

    /**
     * Ignore stale callbacks after external state changes, and drop any
     * pending follow-up chunk. Does not delete layers, and does not
     * retire the in-flight chunk: that chunk still owns the in-flight
     * slot and must retire itself through completion.
     */
    void invalidateGeneration();

    /**
     * One-in-flight realtime analysis. If a chunk is already running,
     * the selection is merged into the pending one -- rather than
     * replacing it, which would drop the region between them -- and is
     * analysed when the running chunk retires.
     */
    QString analyseChunk(sv::Selection sel);

signals:
    void layersChanged();

private:
    struct Context {
        QPointer<sv::Document> document;
        sv::ModelId fileModel;
        QPointer<sv::Pane> pane;
        QPointer<sv::TimeValueLayer> targetPitchLayer;
        QPointer<sv::FlexiNoteLayer> targetNoteLayer;
    };

    struct RealtimeChunkState {
        int remainingParts = 0;
    };

    static constexpr const char* PYIN_PLUGIN_NAME = "pYIN";
    static constexpr const char* PYIN_TRANSFORM_BASE = "vamp:pyin:pyin:";
    static constexpr const char* PYIN_F0_OUT = "smoothedpitchtrack";
    static constexpr const char* PYIN_NOTE_OUT = "notes";

    void untrackTempLayerLocked(sv::Layer *layer);
    void deleteTempLayers(std::vector<QPointer<sv::Layer>> layersToClean,
                          const QPointer<sv::Document> &doc);

    void finishChunk(quint64 generation);

private:
    mutable QMutex m_mutex;
    Context m_ctx;
    std::vector<QPointer<sv::Layer>> m_tempLayers;

    bool m_inFlight = false;
    std::optional<sv::Selection> m_pendingSelection;
    quint64 m_generation = 0;
};

#endif
