/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    Realtime analysis helper (extracted from Analyser)
*/

#include "RealtimeAnalyser.h"

#include "OverlapProcessor.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <memory>
#include <iostream>

#include <QSettings>
#include <QMutexLocker>

#include <vamp-hostsdk/RealTime.h>

#include "transform/TransformFactory.h"
#include "framework/Document.h"
#include "data/model/WaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"

#include "view/Pane.h"
#include "layer/Layer.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/ColourDatabase.h"

using std::cerr;
using std::endl;

using namespace sv;

// Global overlap processor instance for efficient processing (same as previously in Analyser.cpp)
static OverlapProcessor s_overlapProcessor;

static void logPatchFrameRange(const char *label,
                               const EventVector &events)
{
    if (events.empty()) {
        cerr << label << ": empty" << endl;
        return;
    }

    auto minFrame = events.front().getFrame();
    auto maxFrame = events.front().getFrame() + events.front().getDuration();
    for (const auto &event : events) {
        minFrame = std::min(minFrame, event.getFrame());
        maxFrame = std::max(maxFrame, event.getFrame() + event.getDuration());
    }

    cerr << label << ": count=" << events.size()
         << " start=" << minFrame
         << " end=" << maxFrame << endl;
}

// Wrapper functions
static OverlapProcessor::EventPatch processPitchEvents(sv_frame_t contextStart,
                                                       const std::shared_ptr<SparseTimeValueModel> &fromModel,
                                                       const std::shared_ptr<SparseTimeValueModel> &toModel)
{
    return s_overlapProcessor.processPitchEvents(contextStart,
                                                 fromModel->getAllEvents(),
                                                 toModel->getAllEvents());
}

static OverlapProcessor::EventPatch processNoteEvents(sv_frame_t contextStart,
                                                      const std::shared_ptr<NoteModel> &fromModel,
                                                      const std::shared_ptr<NoteModel> &toModel)
{
    return s_overlapProcessor.processNoteEvents(contextStart,
                                                fromModel->getAllEvents(),
                                                toModel->getAllEvents());
}

static std::map<QString, bool> getAnalysisSettingsFromSettings()
{
    std::map<QString, bool> analysisSettings;

    QSettings settings;
    settings.beginGroup("Analyser");

    analysisSettings["precision-analysis"] = settings.value("precision-analysis", false).toBool();
    analysisSettings["lowamp-analysis"] = settings.value("lowamp-analysis", true).toBool();
    analysisSettings["onset-analysis"] = settings.value("onset-analysis", true).toBool();
    analysisSettings["prune-analysis"] = settings.value("prune-analysis", true).toBool();

    settings.endGroup();

    return analysisSettings;
}

static void setAnalysisSettings(Transform &transform)
{
    const auto analysisSettings = getAnalysisSettingsFromSettings();

    if (analysisSettings.count("precision-analysis") > 0) {
        bool precise = analysisSettings.at("precision-analysis");
        if (precise) {
            cerr << "setting parameters for precise mode" << endl;
            transform.setParameter("precisetime", 1);
        } else {
            cerr << "setting parameters for vague mode" << endl;
            transform.setParameter("precisetime", 0);
        }
    }

    if (analysisSettings.count("lowamp-analysis") > 0) {
        bool lowamp = analysisSettings.at("lowamp-analysis");
        if (lowamp) {
            cerr << "setting parameters for lowamp suppression" << endl;
            transform.setParameter("lowampsuppression", 0.2f);
        } else {
            cerr << "setting parameters for no lowamp suppression" << endl;
            transform.setParameter("lowampsuppression", 0.0f);
        }
    }

    if (analysisSettings.count("onset-analysis") > 0) {
        bool onset = analysisSettings.at("onset-analysis");
        if (onset) {
            cerr << "setting parameters for increased onset sensitivity" << endl;
            transform.setParameter("onsetsensitivity", 0.7f);
        } else {
            cerr << "setting parameters for non-increased onset sensitivity" << endl;
            transform.setParameter("onsetsensitivity", 0.0f);
        }
    }

    if (analysisSettings.count("prune-analysis") > 0) {
        bool prune = analysisSettings.at("prune-analysis");
        if (prune) {
            cerr << "setting parameters for duration pruning" << endl;
            transform.setParameter("prunethresh", 0.1f);
        } else {
            cerr << "setting parameters for no duration pruning" << endl;
            transform.setParameter("prunethresh", 0.0f);
        }
    }
}

RealtimeAnalyser::RealtimeAnalyser(QObject *parent) :
    QObject(parent)
{
}

RealtimeAnalyser::~RealtimeAnalyser()
{
    cleanup();
}

void
RealtimeAnalyser::setContext(Document *document,
                             ModelId fileModel,
                             Pane *pane,
                             TimeValueLayer *targetPitchLayer,
                             FlexiNoteLayer *targetNoteLayer)
{
    QMutexLocker locker(&m_mutex);

    m_ctx.document = document;
    m_ctx.fileModel = fileModel;
    m_ctx.pane = pane;
    m_ctx.targetPitchLayer = targetPitchLayer;
    m_ctx.targetNoteLayer = targetNoteLayer;
}

void
RealtimeAnalyser::clearContext()
{
    QMutexLocker locker(&m_mutex);

    m_ctx.document = nullptr;
    m_ctx.fileModel = ModelId();
    m_ctx.pane = nullptr;
    m_ctx.targetPitchLayer = nullptr;
    m_ctx.targetNoteLayer = nullptr;
}

void
RealtimeAnalyser::cleanup()
{
    std::vector<QPointer<Layer>> layersToClean;
    QPointer<Document> doc;
    quint64 newGeneration = 0;
    bool wasInFlight = false;
    bool hadPending = false;

    {
        QMutexLocker locker(&m_mutex);

        layersToClean.swap(m_tempLayers);

        wasInFlight = m_inFlight;
        hadPending = m_pendingSelection.has_value();

        // Every caller of cleanup() is reacting to the ground moving --
        // the document closing, a new file loading, or the target layers
        // being replaced. Restarting the queued chunk here would schedule
        // work against state that is being torn down (and, from the
        // destructor, would connect signals to a half-destroyed object),
        // so all outstanding work is simply abandoned. Bumping the
        // generation makes any callback already queued for delivery a
        // no-op.
        m_inFlight = false;
        m_pendingSelection = std::nullopt;

        newGeneration = ++m_generation;

        doc = m_ctx.document;

        cerr << "RealtimeAnalyser::cleanup: generation=" << newGeneration
             << " wasInFlight=" << wasInFlight
             << " hadPending=" << hadPending
             << " tempLayers=" << layersToClean.size() << endl;
    }

    deleteTempLayers(std::move(layersToClean), doc);
}

void
RealtimeAnalyser::invalidateGeneration()
{
    QMutexLocker locker(&m_mutex);

    // If we invalidate generation, any existing callbacks become stale.
    // Do not clear m_inFlight here: the owning run must retire itself via
    // completion so we don't break the in-flight/pending state machine.
    ++m_generation;
    m_pendingSelection = std::nullopt;

    cerr << "RealtimeAnalyser::invalidateGeneration: generation=" << m_generation
         << " inFlight=" << m_inFlight << endl;
}

QString
RealtimeAnalyser::analyseChunk(Selection sel)
{
    bool startedChunk = false;

    if (sel.isEmpty()) return "";

    quint64 generation = 0;
    QPointer<Document> safeDocument;
    QPointer<Pane> safePane;
    QPointer<TimeValueLayer> safeTargetPitchLayer;
    QPointer<FlexiNoteLayer> safeTargetNoteLayer;
    ModelId fileModel;
    std::shared_ptr<WaveFileModel> waveFileModel;

    {
        QMutexLocker locker(&m_mutex);

        if (m_inFlight) {

            // Merge rather than replace: recordDurationChanged fires
            // several times during a single chunk, and simply keeping
            // the newest selection would leave the frames between the
            // running chunk's end and the newest chunk's start
            // permanently unanalysed.
            if (m_pendingSelection) {
                m_pendingSelection = Selection
                    (std::min(m_pendingSelection->getStartFrame(),
                              sel.getStartFrame()),
                     std::max(m_pendingSelection->getEndFrame(),
                              sel.getEndFrame()));
            } else {
                m_pendingSelection = sel;
            }

            cerr << "RealtimeAnalyser::analyseChunk: already in flight, merged into pending selection"
                 << " generation=" << m_generation
                 << " pendingStart=" << m_pendingSelection->getStartFrame()
                 << " pendingEnd=" << m_pendingSelection->getEndFrame() << endl;
            return "";
        }

        if (!m_ctx.document || !m_ctx.pane) {
            return "Internal error: RealtimeAnalyser::analyseChunk() called with no document or pane present";
        }

        if (m_ctx.fileModel.isNone()) {
            return "Internal error: RealtimeAnalyser::analyseChunk() called with no model present";
        }

        if (!m_ctx.targetPitchLayer || !m_ctx.targetNoteLayer) {
            return "Internal error: RealtimeAnalyser::analyseChunk() called with no target pitch/note layers present";
        }

        m_inFlight = true;
        startedChunk = true;
        generation = m_generation;

        safeDocument = m_ctx.document;
        safePane = m_ctx.pane;
        safeTargetPitchLayer = m_ctx.targetPitchLayer;
        safeTargetNoteLayer = m_ctx.targetNoteLayer;
        fileModel = m_ctx.fileModel;

        cerr << "RealtimeAnalyser::analyseChunk: acquired in-flight slot"
             << " generation=" << generation
             << " start=" << sel.getStartFrame()
             << " end=" << sel.getEndFrame() << endl;
    }

    auto finishIfStarted = [this, startedChunk, generation]() {
        if (startedChunk) {
            finishChunk(generation);
        }
    };

    waveFileModel = ModelById::getAs<WaveFileModel>(fileModel);
    if (!waveFileModel) {
        finishIfStarted();
        return "Internal error: RealtimeAnalyser::analyseChunk() called with no WaveFileModel";
    }

    auto cleanupTempLayer = [this](QPointer<Layer> safeTempLayer,
                                   QPointer<Document> doc) {
        Layer *layerToDelete = safeTempLayer.data();
        if (!layerToDelete) return;

        {
            QMutexLocker locker(&m_mutex);
            untrackTempLayerLocked(layerToDelete);
        }

        // Temp layers are never added to a view, so removeLayerFromView()
        // must not be used here: it would push an undoable
        // RemoveLayerCommand holding a raw pointer to a layer we are
        // about to delete, leaving the undo stack full of commands that
        // dereference freed memory on undo. Delete directly instead.
        if (doc) {
            doc->deleteLayer(layerToDelete, true);
        }
    };

    auto state = std::make_shared<RealtimeChunkState>();

    auto completePart = [this, state, generation](bool canFinish) {
        --state->remainingParts;
        cerr << "RealtimeAnalyser::completePart: generation=" << generation
             << " canFinish=" << canFinish
             << " remainingParts=" << state->remainingParts << endl;
        if (state->remainingParts == 0) {
            cerr << "RealtimeAnalyser::completePart: retiring chunk for generation=" << generation
                 << " finishAllowed=" << canFinish << endl;
            finishChunk(generation);
        }
    };

    // Registers a completion handler for one temp layer. The handler
    // runs at most once, whether it is reached through the layer's
    // signal or through the immediate check below -- if the transform
    // has already finished by the time we get here the signal has been
    // and gone, and without the check nothing would ever retire the
    // chunk, leaving m_inFlight stuck true and realtime analysis
    // silently dead for the rest of the session.
    auto registerPart = [this, state](Layer *layer,
                                      std::function<void(ModelId)> handler) {

        ++state->remainingParts;

        auto done = std::make_shared<bool>(false);
        auto once = [done, handler](ModelId modelId) {
            if (*done) return;
            *done = true;
            handler(modelId);
        };

        QObject::connect(layer, &Layer::modelCompletionChanged, this,
                         [once](ModelId modelId) {
                             auto model = ModelById::get(modelId);
                             if (!model || model->getCompletion() != 100) return;
                             once(modelId);
                         },
                         Qt::QueuedConnection);

        const ModelId modelId = layer->getModel();
        auto model = ModelById::get(modelId);
        if (model && model->getCompletion() == 100) {
            QMetaObject::invokeMethod(this,
                                      [once, modelId]() { once(modelId); },
                                      Qt::QueuedConnection);
        }
    };

    TransformFactory *tf = TransformFactory::getInstance();

    const auto f0_transform = QString(PYIN_TRANSFORM_BASE) + QString(PYIN_F0_OUT);
    const auto note_transform = QString(PYIN_TRANSFORM_BASE) + QString(PYIN_NOTE_OUT);

    QString notFound =
        tr("Transform \"%1\" not found. Unable to perform interactive analysis."
           "<br><br>Are the %2 and %3 Vamp plugins correctly installed?");

    if (!tf->haveTransform(f0_transform)) {
        finishIfStarted();
        return notFound.arg(f0_transform).arg(PYIN_PLUGIN_NAME);
    }

    if (!tf->haveTransform(note_transform)) {
        finishIfStarted();
        return notFound.arg(note_transform).arg(PYIN_PLUGIN_NAME);
    }

    Transform t = tf->getDefaultTransformFor(f0_transform, waveFileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    setAnalysisSettings(t);

    const RealTime start =
        RealTime::frame2RealTime(sel.getStartFrame(), waveFileModel->getSampleRate());
    const RealTime end =
        RealTime::frame2RealTime(sel.getEndFrame(), waveFileModel->getSampleRate());

    RealTime duration;
    if (sel.getEndFrame() > sel.getStartFrame()) {
        duration = end - start;
    }

    cerr << "RealtimeAnalyser::analyseChunk: start " << start
         << " end " << end
         << " original selection start " << sel.getStartFrame()
         << " end " << sel.getEndFrame()
         << " duration " << duration << endl;

    if (duration <= RealTime::zeroTime) {
        cerr << "RealtimeAnalyser::analyseChunk: duration <= 0, not analysing" << endl;
        finishIfStarted();
        return "";
    }

    t.setStartTime(start);
    t.setDuration(duration);

    Transforms transforms;
    transforms.push_back(t);

    t.setOutput(PYIN_NOTE_OUT);
    transforms.push_back(t);

    if (!safeDocument) {
        finishIfStarted();
        return "Internal error: RealtimeAnalyser::analyseChunk() document deleted during scheduling";
    }

    const std::vector<Layer *> layers = safeDocument->createDerivedLayers(transforms, fileModel);

    if (layers.empty()) {
        cerr << "WARNING: RealtimeAnalyser::analyseChunk: no layers returned from createDerivedLayers" << endl;
        finishIfStarted();
        return "";
    }

    {
        QMutexLocker locker(&m_mutex);
        for (auto *layer : layers) {
            m_tempLayers.push_back(QPointer<Layer>(layer));
        }
    }

    ColourDatabase *cdb = ColourDatabase::getInstance();

    std::vector<Layer *> unrecognised;

    for (auto *layer : layers) {

        if (auto *tempPitchLayer = qobject_cast<TimeValueLayer *>(layer)) {

            tempPitchLayer->setBaseColour(cdb->getColourIndex(tr("Black")));

            QPointer<Layer> safeTempLayer(tempPitchLayer);

            registerPart
                (tempPitchLayer,
                 [this, safeTempLayer, safeTargetPitchLayer, safeDocument, safePane,
                  sel, generation, cleanupTempLayer, completePart](ModelId modelId) {

                    const auto fromModel = ModelById::getAs<SparseTimeValueModel>(modelId);
                    if (!fromModel) {
                        cleanupTempLayer(safeTempLayer, safeDocument);
                        completePart(false);
                        return;
                    }

                    cerr << "RealtimeAnalyser::analyseChunk: Processing pitch track completion" << endl;

                    bool stale = false;
                    {
                        QMutexLocker locker(&m_mutex);
                        stale = (generation != m_generation);
                    }

                    if (stale) {
                        cerr << "RealtimeAnalyser::analyseChunk: Ignoring stale pitch callback from old generation"
                             << " callbackGeneration=" << generation << endl;
                        cleanupTempLayer(safeTempLayer, safeDocument);
                        completePart(false);
                        return;
                    }

                    if (safeTargetPitchLayer) {
                        const auto toModel =
                            ModelById::getAs<SparseTimeValueModel>(safeTargetPitchLayer->getModel());

                        if (toModel) {
                            const auto patch =
                                processPitchEvents(sel.getStartFrame(), fromModel, toModel);

                            cerr << "RealtimeAnalyser::pitchPatch: selectionStart=" << sel.getStartFrame()
                                 << " selectionEnd=" << sel.getEndFrame() << endl;
                            logPatchFrameRange("RealtimeAnalyser::pitchPatch.remove", patch.remove);
                            logPatchFrameRange("RealtimeAnalyser::pitchPatch.add", patch.add);

                            for (const Event &p : patch.remove) {
                                toModel->remove(p);
                            }
                            for (const Event &p : patch.add) {
                                toModel->add(p);
                            }
                        } else {
                            cerr << "ERROR: RealtimeAnalyser pitch callback - target model is null" << endl;
                        }
                    } else {
                        cerr << "WARNING: RealtimeAnalyser pitch callback - target layer deleted" << endl;
                    }

                    cleanupTempLayer(safeTempLayer, safeDocument);

                    if (safeTargetPitchLayer && safePane) {
                        safeTargetPitchLayer->layerParametersChanged();
                        safePane->layerParametersChanged();
                    }
                    emit layersChanged();

                    completePart(true);
                });

        } else if (auto *tempNoteLayer = qobject_cast<FlexiNoteLayer *>(layer)) {

            tempNoteLayer->setBaseColour(cdb->getColourIndex(tr("Bright Blue")));

            QPointer<Layer> safeTempLayer(tempNoteLayer);

            registerPart
                (tempNoteLayer,
                 [this, safeTempLayer, safeTargetNoteLayer, safeDocument,
                  sel, generation, cleanupTempLayer, completePart](ModelId modelId) {

                    const auto fromModel = ModelById::getAs<NoteModel>(modelId);
                    if (!fromModel) {
                        cleanupTempLayer(safeTempLayer, safeDocument);
                        completePart(false);
                        return;
                    }

                    cerr << "RealtimeAnalyser::analyseChunk: Processing note layer completion" << endl;

                    bool stale = false;
                    {
                        QMutexLocker locker(&m_mutex);
                        stale = (generation != m_generation);
                    }

                    if (stale) {
                        cerr << "RealtimeAnalyser::analyseChunk: Ignoring stale note callback from old generation"
                             << " callbackGeneration=" << generation << endl;
                        cleanupTempLayer(safeTempLayer, safeDocument);
                        completePart(false);
                        return;
                    }

                    if (safeTargetNoteLayer) {
                        const auto toModel =
                            ModelById::getAs<NoteModel>(safeTargetNoteLayer->getModel());

                        if (toModel) {
                            const auto patch =
                                processNoteEvents(sel.getStartFrame(), fromModel, toModel);

                            logPatchFrameRange("RealtimeAnalyser::notePatch.remove", patch.remove);
                            logPatchFrameRange("RealtimeAnalyser::notePatch.add", patch.add);

                            for (const Event &p : patch.remove) {
                                toModel->remove(p);
                            }
                            for (const Event &p : patch.add) {
                                toModel->add(p);
                            }
                        } else {
                            cerr << "ERROR: RealtimeAnalyser note callback - target model is null" << endl;
                        }
                    } else {
                        cerr << "WARNING: RealtimeAnalyser note callback - target layer deleted" << endl;
                    }

                    cleanupTempLayer(safeTempLayer, safeDocument);

                    emit layersChanged();

                    completePart(true);
                });

        } else {
            unrecognised.push_back(layer);
        }
    }

    // Anything we can't drive to completion would otherwise sit in
    // m_tempLayers until the next cleanup()
    for (auto *layer : unrecognised) {
        cerr << "WARNING: RealtimeAnalyser::analyseChunk: discarding unrecognised temp layer" << endl;
        cleanupTempLayer(QPointer<Layer>(layer), safeDocument);
    }

    if (state->remainingParts == 0) {
        cerr << "WARNING: RealtimeAnalyser::analyseChunk: no recognised temp layers created" << endl;
        finishIfStarted();
    }

    return "";
}

void
RealtimeAnalyser::untrackTempLayerLocked(Layer *layer)
{
    m_tempLayers.erase(
        std::remove_if(m_tempLayers.begin(),
                       m_tempLayers.end(),
                       [layer](const QPointer<Layer> &p) {
                           return p.isNull() || p.data() == layer;
                       }),
        m_tempLayers.end());
}

void
RealtimeAnalyser::deleteTempLayers(std::vector<QPointer<Layer>> layersToClean,
                                   const QPointer<Document> &doc)
{
    // If the document has already gone it owns and destroys its layers,
    // so there is nothing left for us to release.
    if (!doc) return;

    for (const auto &layerPtr : layersToClean) {
        Layer *layer = layerPtr.data();
        if (!layer) continue;

        // See cleanupTempLayer(): these layers were never added to a
        // view, so they must not go through removeLayerFromView().
        doc->deleteLayer(layer, true);
    }
}

void
RealtimeAnalyser::finishChunk(quint64 generation)
{
    std::optional<Selection> pending;

    {
        QMutexLocker locker(&m_mutex);

        if (generation != m_generation) {
            // A cleanup() or invalidateGeneration() has superseded this
            // chunk, so it no longer owns the in-flight slot. Clearing
            // m_inFlight here would release a slot that a newer chunk
            // may already have taken, letting two chunks write to the
            // same models at once.
            cerr << "RealtimeAnalyser::finishChunk: ignoring retirement of stale generation="
                 << generation << " current=" << m_generation << endl;
            return;
        }

        m_inFlight = false;
        pending = std::exchange(m_pendingSelection, std::nullopt);

        cerr << "RealtimeAnalyser::finishChunk: generation=" << generation
             << " pending=" << (pending.has_value() ? "yes" : "no") << endl;
    }

    if (pending) {
        cerr << "RealtimeAnalyser::finishChunk: starting pending realtime selection"
             << " start=" << pending->getStartFrame()
             << " end=" << pending->getEndFrame() << endl;
        (void)analyseChunk(*pending);
    }
}
