#include "terrain_reactor_item.hpp"
#include "terrain_reactor_gpu_data.hpp"
#include "terrain_column_mesh.hpp"
#include "terrain_shadow_map.hpp"
#include "terrain_spatial_lyrics.hpp"

#include "audio_visual_feature_controller.hpp"
#include "player_experience_controller.hpp"
#include "immersive_theme_catalog.hpp"
#include "visual_terrain_response.hpp"

#include <QColor>
#include <QEvent>
#include <QFile>
#include <QMatrix4x4>
#include <QMetaMethod>
#include <QMetaObject>
#include <QQuickWindow>
#include <QStringList>
#include <QSGRendererInterface>
#include <QVector3D>
#include <QWindow>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

using namespace agplayer::terrain;

namespace {

using namespace agplayer::terrain::gpu;

QShader loadShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(file.readAll());
}

float zoneValue(ColorZone zone) noexcept
{
    return static_cast<float>(zone);
}

float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float finiteUnit(float value, float fallback = 0.0F) noexcept
{
    return std::clamp(finiteOr(value, fallback), 0.0F, 1.0F);
}

AudioFeatures smoothReactorFeatures(const AudioFeatures& current,
                                    const AudioFeatures& target,
                                    float elapsedSeconds) noexcept
{
    AudioFeatures result;
    for (std::size_t index = 0; index < result.bands.size(); ++index) {
        result.bands[index] = smoothReactorFeature(
            current.bands[index], target.bands[index], elapsedSeconds);
    }
    result.energy = smoothReactorFeature(current.energy, target.energy,
                                         elapsedSeconds);
    result.spectralFlux = smoothReactorFeature(current.spectralFlux,
                                               target.spectralFlux,
                                               elapsedSeconds);
    // Keep onset events prompt; only the continuous terrain field is eased.
    result.kick = finiteUnit(target.kick);
    result.snare = finiteUnit(target.snare);
    return result;
}

GpuInstance toGpuInstance(const SceneInstance& source, float type)
{
    GpuInstance result{};
    result.position[0] = source.position.x();
    result.position[1] = source.position.y();
    result.position[2] = source.position.z();
    result.scale[0] = source.scale.x();
    result.scale[1] = source.scale.y();
    result.scale[2] = source.scale.z();
    result.data[0] = type;
    result.data[1] = zoneValue(source.zone);
    result.data[2] = source.random;
    result.data[3] = source.aux;
    return result;
}

} // namespace

class TerrainReactorRenderer final : public QQuickRhiItemRenderer {
public:
    TerrainReactorRenderer(
        std::shared_ptr<TerrainReactorItem::Telemetry> telemetry,
        std::shared_ptr<RendererResourceState> resourceState,
        bool softwareBackend)
        : telemetry_(std::move(telemetry)),
          resourceState_(std::move(resourceState)),
          rendererId_(++nextRendererId_),
          punchEvents_(*resourceState_),
          beatEvents_(*resourceState_),
          impactEvents_(*resourceState_),
          softwareBackend_(softwareBackend)
    {
        claimed_ = resourceState_->acquireRenderer(rendererId_);
        frameTimer_.start();
        counterTimer_.start();
    }

    ~TerrainReactorRenderer() override
    {
        if (claimed_) {
            releaseResources();
            resourceState_->releaseRenderer(rendererId_);
            if (performanceProbe_)
                qInfo("Reactor released: renderer=%llu",
                    static_cast<unsigned long long>(rendererId_));
        }
    }

    bool claimed() const noexcept { return claimed_; }

protected:
    void initialize(QRhiCommandBuffer*) override
    {
        if (!claimed_) return;
        if (softwareBackend_ || rhi() == nullptr || rhi()->backend() == QRhi::Null) {
            fail(TerrainReactorItem::RenderStatus::SoftwareBackend,
                 QStringLiteral("Terrain Reactor requires an accelerated QRhi backend"));
            return;
        }
        if (!rhi()->isFeatureSupported(QRhi::Instancing)) {
            fail(TerrainReactorItem::RenderStatus::ResourceError,
                 QStringLiteral("QRhi backend does not support instancing"));
            return;
        }

        const bool targetChanged = renderTarget() != lastRenderTarget_
            || (pipeline_ && pipeline_->sampleCount() != renderTarget()->sampleCount());
        if (targetChanged && pipeline_) {
            pipeline_.reset();
            lastRenderTarget_ = nullptr;
            resourceState_->invalidateResources();
        }
        if (pipeline_) return;
        if (!createResources()) {
            fail(TerrainReactorItem::RenderStatus::ResourceError,
                 QStringLiteral("Failed to create Terrain Reactor QRhi resources"));
            return;
        }
        resourceState_->initializeResources();
        notifyCounters(true);
        fail(TerrainReactorItem::RenderStatus::Ready, QString());
    }

    void synchronize(QQuickRhiItem* item) override
    {
        auto* terrainItem = static_cast<TerrainReactorItem*>(item);
        item_ = terrainItem;
        const auto next = terrainItem->snapshotForRenderer();
        const bool resuming = next.running && (!snapshot_.running
            || next.activityRevision != snapshot_.activityRevision);
        if (resuming) {
            if (performanceProbe_) qInfo("Reactor resume: activity=%llu",
                static_cast<unsigned long long>(next.activityRevision));
            frameTimer_.restart();
            renderTimeEpochSeconds_ = next.timeSeconds;
            renderTimeTimer_.restart();
            renderTimeAnchored_ = true;
            // Events raised while rendering was gated (including a source's
            // pre-existing revisions on first attach) are historical state,
            // not a new foreground cue.
            punchEvents_.discard(next.punchEvent, camera_);
            beatEvents_.discard(next.beatEvent);
            impactEvents_.discard(next.impactEvent);
            meteorFlight_.cancel();
            smoothedFeatures_ = AudioFeatures{};
            referenceResponse_.reset();
            lastPcmFrameSequence_ = next.pcm.sequence;
        }
        const bool pcmDiscontinuity =
            TerrainReactorItem::hasVisualPcmDiscontinuity(snapshot_.pcm, next.pcm);
        const bool visualReset = next.referenceAudio != snapshot_.referenceAudio
            || next.visualResetRevision != snapshot_.visualResetRevision
            // A pause deliberately makes the retained window non-current.
            // Preserve the analyzer/terrain history so the original 1.6 s
            // release can run; seek, track and device epochs still reset.
            || (pcmDiscontinuity && !next.pcm.paused);
        if (!snapshot_.referenceAudio && next.referenceAudio)
            lastPcmFrameSequence_ = next.pcm.sequence;
        if (resuming || visualReset) {
            frameAnalyzer_.reset();
            floatingParameters_ = {};
            meteorParticles_.reset();
            snareTrigger_.reset();
            consumedRippleRevision_ = next.rippleRevision;
            TerrainReactorItem::restoreRenderAudioEvents(renderBeat_, renderImpact_, next, *resourceState_);
        }
        if (visualReset) {
            if (performanceProbe_) qInfo("Reactor audio reset: revision=%llu",
                static_cast<unsigned long long>(next.visualResetRevision));
            referenceResponse_.reset();
            smoothedFeatures_ = {};
            bassEnvelope_ = {};
            beatEvents_.discard(next.beatEvent);
            impactEvents_.discard(next.impactEvent);
            punchEvents_.discard(next.punchEvent, camera_);
            travelingWaves_.fill(QVector4D());
            meteorFlight_.cancel();
        }
        if (next.running || failed_) publishStatus();
        const bool seedChanged = next.seed != snapshot_.seed;
        const bool layoutChanged = seedChanged
            || next.style.materialMode != snapshot_.style.materialMode
            || next.style.columnDensity != snapshot_.style.columnDensity
            || next.style.topographyDensity != snapshot_.style.topographyDensity
            || next.quality != snapshot_.quality
            || next.style.floatingCubesEnabled != snapshot_.style.floatingCubesEnabled
            || next.style.meteorsEnabled != snapshot_.style.meteorsEnabled
            || next.style.ripplesEnabled != snapshot_.style.ripplesEnabled
            || quality_.stage() != lastStage_;
        TrackPalette nextPalette = next.style.colors;
        if (next.trackPaletteActive && next.style.materialMode != 2) {
            // Keep the preset's art direction when applying a song accent.
            nextPalette = blendTrackPalettes(next.style.colors, next.trackPalette, 0.18F);
            nextPalette[0] = next.style.colors[0];
        }
        if (!paletteInitialized_) {
            currentPalette_ = nextPalette;
            fromPalette_ = nextPalette;
            targetPalette_ = nextPalette;
            paletteProgress_ = 1.0F;
            paletteInitialized_ = true;
        } else if (nextPalette != targetPalette_) {
            fromPalette_ = currentPalette_;
            targetPalette_ = nextPalette;
            paletteProgress_ = 0.0F;
        }
        const bool canonicalPalette = next.style.bodyColor.w() > 0.5F;
        if (!canonicalPalette && themePaletteInitialized_) {
            fromPalette_ = currentPalette_;
            targetPalette_ = nextPalette;
            paletteProgress_ = 0.0F;
        }
        if (canonicalPalette) {
            std::copy(nextPalette.begin(), nextPalette.end(), targetThemePalette_.colors.begin());
            targetThemePalette_.colors[5] = next.style.bodyColor;
            targetThemePalette_.colors[6] = next.style.atmosphereColor;
            targetThemePalette_.colors[7] = next.style.rippleColor;
            targetThemePalette_.glow = next.style.glowIntensity;
            if (!themePaletteInitialized_) currentThemePalette_ = targetThemePalette_;
        }
        themePaletteInitialized_ = canonicalPalette;
        if (!cameraSynchronized_) {
            camera_.synchronize(next.camera, next.cameraManualUntilSeconds);
            previousGuiCamera_ = next.camera;
            cameraSynchronized_ = true;
        } else if (syncedCameraRevision_ != next.cameraRevision) {
            camera_.applyManualDelta(previousGuiCamera_, next.camera,
                                     next.timeSeconds);
            previousGuiCamera_ = next.camera;
        }
        syncedCameraRevision_ = next.cameraRevision;
        snapshot_ = next;
        if (seedChanged || !waveSourcesInitialized_) {
            waveSources_ = multiWaveSources(snapshot_.seed);
            waveSourcesInitialized_ = true;
        }
        if (layoutChanged) instancesDirty_ = true;
    }

    void render(QRhiCommandBuffer* commandBuffer) override
    {
        if (!snapshot_.running || failed_ || !pipeline_ || commandBuffer == nullptr) {
            return;
        }

        const double targetFps = snapshot_.quality == TerrainReactorItem::Quality::Eco
            ? 30.0
            : snapshot_.quality == TerrainReactorItem::Quality::Balanced
                ? 45.0 : 60.0;
        const double renderTimeSeconds = renderTimeAnchored_
            ? renderTimeEpochSeconds_ + std::max(0.0,
                static_cast<double>(renderTimeTimer_.nsecsElapsed())
                    / 1'000'000'000.0)
            : snapshot_.timeSeconds;

        const qint64 elapsedNanoseconds = frameTimer_.nsecsElapsed();
        frameTimer_.restart();
        const double wallElapsedSeconds = std::max(0.0,
            static_cast<double>(elapsedNanoseconds) / 1'000'000'000.0);
        const double animationElapsedSeconds = std::clamp(
            wallElapsedSeconds, 0.001, 0.25);
        if (themePaletteInitialized_) {
            const auto& warm = targetThemePalette_.colors[2];
            meteorMaterialColor_.advance({
                agplayer::immersive::srgbChannelToLinear(warm.x()),
                agplayer::immersive::srgbChannelToLinear(warm.y()),
                agplayer::immersive::srgbChannelToLinear(warm.z())}, float(wallElapsedSeconds));
            currentThemePalette_ = advanceThemePalette(currentThemePalette_,
                targetThemePalette_, float(wallElapsedSeconds));
            std::copy_n(currentThemePalette_.colors.begin(), currentPalette_.size(),
                        currentPalette_.begin());
        } else if (paletteProgress_ < 1.0F) {
            paletteProgress_ = std::min(1.0F, paletteProgress_
                + float(animationElapsedSeconds) / 1.15F);
            const float eased = paletteProgress_ * paletteProgress_
                * (3.0F - 2.0F * paletteProgress_);
            currentPalette_ = blendTrackPalettes(fromPalette_, targetPalette_,
                                                 eased);
        }
        QElapsedTimer workTimer;
        workTimer.start();

        buildInstancesIfNeeded();
        smoothedFeatures_ = smoothReactorFeatures(smoothedFeatures_,
                                                   snapshot_.features,
                                                   float(animationElapsedSeconds));
        VisualParameters visual = mapVisualParameters(smoothedFeatures_,
            renderTimeSeconds, snapshot_.style);
        double snareStrength = 0;
        int pulseWaveCount = 0;
        double pulseWaveStrength = 0;
        if (snapshot_.referenceAudio) {
            // AudioEngine/MapScene read the latest analyser once per rendered
            // frame. Replaying a backlog here completes lifts before display
            // and changes both the frame-count trigger gates and glow response.
            const auto processed = TerrainReactorItem::advanceReferenceAudioFrame(
                frameAnalyzer_, referenceResponse_, snapshot_, wallElapsedSeconds,
                snareTrigger_);
            const auto& frame = processed.audio;
            floatingParameters_ = advanceFloatingBlocks(floatingParameters_.x(), float(frame.kick.envelope),
                float(wallElapsedSeconds), snapshot_.style.floatingBlockMinSize, snapshot_.style.floatingBlockMaxSize,
                snapshot_.style.floatingBlockSpeed, snapshot_.style.floatingBlockIntensity);
            if (performanceProbe_) ++analysisFrames_;
            const auto& response = processed.terrain;
            if (processed.snare.triggered) snareStrength = processed.snare.strength;
            pulseWaveCount = processed.pulseCount;
            pulseWaveStrength = processed.pulseStrength;
            for (std::size_t i = 0; i < visual.bands.size(); ++i)
                visual.bands[i] = float(response.bands[i]);
            visual.energy = float(response.energy);
            visual.spectralFlux = float(frame.kick.flux);
            referenceDescriptors_ = response;
            if (frame.valid && processed.beatCount > 0) {
                renderBeat_.strength = float(processed.beatStrength);
                renderBeat_.revision += quint64(processed.beatCount);
            }
            if (frame.valid && processed.meteor.triggered) {
                renderImpact_.strength = float(processed.meteor.strength);
                ++renderImpact_.revision;
            }
            // Render owns the analysis; GUI properties receive a value copy.
            // Reset/activity versions reject queued reports from an old source,
            // seek, pause, hidden window, or previous renderer activity.
            AudioFeatures exposed;
            for (std::size_t i=0; i<exposed.bands.size(); ++i)
                exposed.bands[i] = float(frame.descriptors.bands[i]);
            exposed.energy = float(frame.descriptors.energy);
            exposed.spectralFlux = float(frame.kick.flux);
            exposed.kick = processed.beatCount > 0 ? 1.0F : float(frame.kick.onset);
            exposed.snare = processed.snare.triggered ? 1.0F : 0.0F;
            TerrainReactorItem* const target = item_;
            // Keep only version stamps; do not queue a second PCM window copy.
            TerrainReactorItem::AudioFrameOrigin origin;
            origin.visualResetRevision = snapshot_.visualResetRevision;
            origin.activityRevision = snapshot_.activityRevision;
            origin.styleRevision = snapshot_.styleRevision;
            const auto beat = renderBeat_;
            const auto impact = renderImpact_;
            QMetaObject::invokeMethod(target, [target, exposed, origin, beat, impact] {
                target->applyRenderAudioFrame(exposed, origin, beat, impact);
            }, Qt::QueuedConnection);
        }
        if (!snapshot_.referenceAudio)
            floatingParameters_ = advanceFloatingBlocks(floatingParameters_.x(), snapshot_.features.kick,
                float(wallElapsedSeconds), snapshot_.style.floatingBlockMinSize, snapshot_.style.floatingBlockMaxSize,
                snapshot_.style.floatingBlockSpeed, snapshot_.style.floatingBlockIntensity);
        bassEnvelope_.advance(visual.bands[0] * 0.72F
                                  + visual.bands[1] * 0.28F,
                              float(animationElapsedSeconds));
        punchEvents_.consume(snapshot_.punchEvent, camera_);
        const auto& frameBeat = snapshot_.referenceAudio ? renderBeat_ : snapshot_.beatEvent;
        const auto& frameImpact = snapshot_.referenceAudio ? renderImpact_ : snapshot_.impactEvent;
        const bool newBeat = beatEvents_.consume(frameBeat,
                                                renderTimeSeconds);
        const bool newImpact = impactEvents_.consume(frameImpact, renderTimeSeconds);
        const float waveStrength = newImpact ? frameImpact.strength : frameBeat.strength;
        if (!snapshot_.style.meteorsEnabled) meteorFlight_.cancel();
        if (newImpact && snapshot_.style.meteorsEnabled && !meteorOrigins_.isEmpty()
            && waveStrength > 0.0F && (snapshot_.referenceAudio
                || waveGate_.consume(renderTimeSeconds, waveStrength))) {
            // Reference meteors use their own high-frequency trigger/cooldown.
            meteorFlight_.launch(renderTimeSeconds, int(meteorOrigins_.size()), waveStrength);
        }
        const bool meteorLanded = meteorFlight_.landed(renderTimeSeconds);
        if (!snapshot_.style.meteorsEnabled) meteorParticles_.reset();
        else meteorParticles_.frame(float(wallElapsedSeconds), meteorFlight_.trajectory(),
            meteorFlight_.age(renderTimeSeconds), meteorFlight_.group() >= 0
                && meteorFlight_.age(renderTimeSeconds) < meteorFlight_.duration(), meteorLanded);
        // Legacy material spacing starts at touchdown. Reference colored and
        // Snare waves are independent of this gate.
        if (meteorLanded && snapshot_.style.ripplesEnabled)
            waveGate_.anchor(renderTimeSeconds, meteorFlight_.strength());
        TravelingWaveGate unsharedSnareGate;
        const auto snareWave = consumeSnareWave(unsharedSnareGate, renderTimeSeconds,
            snareStrength, snapshot_.seed ^ ((waveSequence_ + 1U) * 0x9e3779b9U),
            snapshot_.referenceAudio && snapshot_.style.ripplesEnabled, meteorLanded);
        if (!snapshot_.style.ripplesEnabled) {
            travelingWaves_.fill(QVector4D());
        } else {
            // Confirmed PCM kicks feed the reference ten-slot wave pool;
            // no additional presentation gate may suppress repeated drums.
            for (int wave = 0; wave < pulseWaveCount; ++wave) {
                const int slot = nextWave_ % int(travelingWaves_.size());
                nextWave_ = (slot + 1) % int(travelingWaves_.size());
                const QVector4D origin = waveSources_[std::size_t(slot)];
                travelingWaves_[std::size_t(slot)] = QVector4D(
                    origin.x(), origin.y(), renderTimeSeconds,
                    float(std::min(pulseWaveStrength * 2.0, 3.0)));
            }
        }
        if (snapshot_.style.ripplesEnabled
            && (meteorLanded || snareWave.w() < 0
                || (((!snapshot_.referenceAudio && newBeat)
                     || (newImpact && !snapshot_.style.meteorsEnabled))
                   && waveStrength > 0.0F
                   && waveGate_.consume(renderTimeSeconds, waveStrength)))) {
            const int slot = nextWave_ % std::max(1, currentRippleCount_);
            nextWave_ = (slot + 1) % std::max(1, currentRippleCount_);
            waveTints_[std::size_t(slot)] = waveSequence_++ % 4U;
            const QVector4D origin = meteorLanded
                ? QVector4D(meteorFlight_.trajectory().x(), meteorFlight_.trajectory().y(), 0, 0)
                : waveSources_[std::size_t(slot)];
            travelingWaves_[std::size_t(slot)] = snareWave.w() < 0 ? snareWave : QVector4D(
                origin.x(), origin.y(), renderTimeSeconds,
                meteorLanded ? std::min(meteorFlight_.strength(), 1.2F)
                    * (snapshot_.style.rippleColor.w() > 0.5F ? -1.0F : 1.0F)
                    : finiteUnit(waveStrength));
        }
        const BeatPulseSnapshot beat = beatEvents_.snapshot(
            renderTimeSeconds);
        const ImpactPulseSnapshot impact = impactEvents_.snapshot(
            renderTimeSeconds);
        if (snapshot_.rippleRevision != consumedRippleRevision_) {
            consumedRippleRevision_ = snapshot_.rippleRevision;
            if (snapshot_.style.ripplesEnabled) {
                const auto camera = camera_.snapshot();
                const float radius = camera.distance;
                const QVector3D eye(radius * std::cos(camera.pitch) * std::sin(camera.yaw),
                    radius * std::sin(camera.pitch),
                    radius * std::cos(camera.pitch) * std::cos(camera.yaw));
                QMatrix4x4 projection, view;
                const QSize size = renderTarget()->pixelSize();
                projection.perspective(45.0F, float(size.width()) / std::max(1, size.height()), .1F, 1000.0F);
                view.lookAt(eye, QVector3D(), QVector3D(0, 1, 0));
                const auto inverse = (projection * view).inverted();
                const float x = float(snapshot_.ripplePosition.x() * 2 - 1);
                const float y = float(1 - snapshot_.ripplePosition.y() * 2);
                const QVector3D nearPoint = (inverse * QVector4D(x, y, -1, 1)).toVector3DAffine();
                const QVector3D farPoint = (inverse * QVector4D(x, y, 1, 1)).toVector3DAffine();
                const QVector3D ray = farPoint - nearPoint;
                if (std::abs(ray.y()) > 1e-6F) {
                    const float distance = -nearPoint.y() / ray.y();
                    QVector3D hit = nearPoint + ray * distance;
                    if (snapshot_.style.rippleColor.w() > .5F) {
                        // The pointer ray is in world space; wave origins are
                        // local to the rotating reference platter.
                        const float c = std::cos(platterAngle_), s = std::sin(platterAngle_);
                        hit = QVector3D(c * hit.x() - s * hit.z(), 0,
                                        s * hit.x() + c * hit.z());
                    }
                    if (distance >= 0 && std::abs(hit.x()) <= 84 && std::abs(hit.z()) <= 84) {
                        const auto slot = std::size_t(nextWave_++ % int(travelingWaves_.size()));
                        travelingWaves_[slot] = QVector4D(hit.x(), hit.z(), renderTimeSeconds, 1.0F);
                    }
                }
            }
        }
        visual.beatStrength = beat.strength * snapshot_.style.rhythmSensitivity
            * snapshot_.style.rhythmStrength;
        visual.beatAge = beat.age;
        visual.impactStrength = impact.strength;
        visual.impactAge = impact.age;
        const RenderDynamics dynamics = mapRenderDynamics(snapshot_.style);
        const bool referenceTheme = snapshot_.style.rippleColor.w() > .5F;
        camera_.advance(renderTimeSeconds, float(animationElapsedSeconds),
                        referenceTheme ? 0.0F : dynamics.autoRotateSpeed);
        if (referenceTheme && snapshot_.style.autoRotate > 0)
            platterAngle_ = std::fmod(platterAngle_ + float(animationElapsedSeconds)
                * snapshot_.style.autoRotateSpeed, 6.28318530718F);
        UniformBlock uniforms = buildUniforms(visual, camera_.snapshot());
        shadow_.configure(rhi(), uniforms,
            snapshot_.quality != TerrainReactorItem::Quality::Eco
            && quality_.stage() < DegradationStage::ReducedGrid
            && snapshot_.style.materialMode != 2);
        QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();
        if (!spatialLyrics_.prepare(rhi(), renderTarget(), updates, uniforms.mvp,
                snapshot_.spatialLyrics, !snapshot_.spatialLyrics.isEmpty(),
                float(wallElapsedSeconds), snapshot_.lyricOpacity,
                snapshot_.lyricOrbit, snapshot_.lyricElevation,
                snapshot_.lyricScale, snapshot_.lyricDepth, snapshot_.lyricColor)) {
            updates->release();
            fail(TerrainReactorItem::RenderStatus::ResourceError,
                 QStringLiteral("Unable to create spatial lyrics resources"));
            return;
        }
        updates->updateDynamicBuffer(uniformBuffer_.get(), 0,
                                     sizeof(UniformBlock), &uniforms);
        if (instancesDirtyUpload_) {
            updates->updateDynamicBuffer(instanceBuffer_.get(), 0,
                static_cast<quint32>(instances_.size() * sizeof(GpuInstance)),
                instances_.constData());
            instancesDirtyUpload_ = false;
        }
        // Fixed-capacity tail shares the existing instance buffer; terrain and
        // static extras are not re-uploaded just because particles move.
        int activeMeteorParticles = 0;
        if (meteorParticleOffset_ >= 0) {
            for (std::size_t i=0;i<meteorParticles_.particles().size();++i) {
                const auto& p = meteorParticles_.particles()[i];
                activeMeteorParticles += p.active ? 1 : 0;
                auto& gpu = instances_[meteorParticleOffset_+qsizetype(i)];
                gpu = {{p.position.x(),p.position.y(),p.position.z()},
                    {.8F*p.scale(),.8F*p.scale(),.8F*p.scale()},
                    {7,0,p.active ? .6F : 0,0}};
            }
            updates->updateDynamicBuffer(instanceBuffer_.get(),  quint32(meteorParticleOffset_*sizeof(GpuInstance)),
                quint32(200*sizeof(GpuInstance)), instances_.constData()+meteorParticleOffset_);
        }
        telemetry_->activeMeteorParticles.store(activeMeteorParticles, std::memory_order_release);
        telemetry_->meteorParticleSlots.store(meteorParticleOffset_ >= 0 ? 200 : 0, std::memory_order_release);

        if (pendingStaticUploads_) {
            commandBuffer->resourceUpdate(
                std::exchange(pendingStaticUploads_, nullptr));
        }

        commandBuffer->resourceUpdate(updates);
        shadow_.render(rhi(), commandBuffer, uniforms, columnVertexBuffer_.get(),
            columnIndexBuffer_.get(), instanceBuffer_.get(), quint32(columnIndices.size()),
            quint32(currentTerrainCount_));

        // QQuickRhiItem textures use premultiplied alpha, including the clear
        // color; see Qt's BSD-3-Clause rhitextureitem example.
        commandBuffer->beginPass(renderTarget(), QColor(0, 0, 0, 0),
                                 {1.0F, 0});
        commandBuffer->setGraphicsPipeline(pipeline_.get());
        commandBuffer->setShaderResources(bindings_.get());
        const QSize size = renderTarget()->pixelSize();
        commandBuffer->setViewport(QRhiViewport(0.0F, 0.0F,
                                                float(size.width()),
                                                float(size.height())));
        const QRhiCommandBuffer::VertexInput columnBindings[] = {
            {columnVertexBuffer_.get(), 0}, {instanceBuffer_.get(), 0}
        };
        commandBuffer->setVertexInput(0, 2, columnBindings, columnIndexBuffer_.get(), 0,
                                      QRhiCommandBuffer::IndexUInt16);
        commandBuffer->drawIndexed(quint32(columnIndices.size()), quint32(currentTerrainCount_));
        const QRhiCommandBuffer::VertexInput otherBindings[] = {
            {vertexBuffer_.get(), 0},
            {instanceBuffer_.get(), quint32(currentTerrainCount_ * sizeof(GpuInstance))}
        };
        commandBuffer->setVertexInput(0, 2, otherBindings, indexBuffer_.get(), 0,
                                      QRhiCommandBuffer::IndexUInt16);
        commandBuffer->drawIndexed(cubeIndexCount,
            quint32(instances_.size() - currentTerrainCount_));
        spatialLyrics_.draw(commandBuffer);
        commandBuffer->endPass();
        telemetry_->terrainCount.store(currentTerrainCount_, std::memory_order_release);

        const double workMilliseconds = static_cast<double>(workTimer.nsecsElapsed())
            / 1'000'000.0;
        if (performanceProbe_) {
            frameIntervals_[probeFrames_++] = wallElapsedSeconds * 1000.0;
            probeWorkMs_ += workMilliseconds;
            if (probeFrames_ == frameIntervals_.size()) {
                auto sorted = frameIntervals_;
                std::sort(sorted.begin(), sorted.end());
                const double gpuSeconds = commandBuffer->lastCompletedGpuTime();
                qInfo("Reactor performance: frames=%u medianMs=%.3f p95Ms=%.3f maxMs=%.3f cpuMs=%.3f gpuMs=%.3f width=%d height=%d columns=%d resources=%llu analysisFrames=%llu pcmEpoch=%llu",
                    unsigned(probeFrames_), sorted[probeFrames_ / 2],
                    sorted[(probeFrames_ * 95) / 100], sorted.back(),
                    probeWorkMs_ / probeFrames_, gpuSeconds > 0 ? gpuSeconds * 1000.0 : -1.0,
                    size.width(), size.height(), currentTerrainCount_,
                    static_cast<unsigned long long>(resourceState_->generation()),
                    static_cast<unsigned long long>(analysisFrames_),
                    static_cast<unsigned long long>(snapshot_.pcm.epoch));
                QStringList intervals;
                intervals.reserve(int(probeFrames_));
                for (double interval : frameIntervals_)
                    intervals.append(QString::number(interval, 'f', 3));
                qInfo().noquote() << "Reactor frame intervalsMs:" << intervals.join(',');
                probeFrames_ = 0;
                probeWorkMs_ = 0;
            }
        }
        const double targetFrameMilliseconds = 1000.0 / targetFps;
        quality_.observeFrameSample(workMilliseconds,
                                    wallElapsedSeconds * 1000.0,
                                    targetFrameMilliseconds);
        quality_.advanceWallClock(wallElapsedSeconds);
        if (lastStage_ != quality_.stage()) {
            lastStage_ = quality_.stage();
            instancesDirty_ = true;
        }

        telemetry_->frames.fetch_add(1, std::memory_order_relaxed);
        telemetry_->animations.fetch_add(1, std::memory_order_relaxed);
        telemetry_->uploads.fetch_add(1, std::memory_order_relaxed);
        publishRenderedRevisions();
        notifyCounters();
        if (snapshot_.style.rippleColor.w() > .5F
            && snapshot_.quality == TerrainReactorItem::Quality::High) {
            // Dirty the item on the GUI thread: renderer::update() alone can
            // render repeatedly without synchronizing new PCM or control state.
            TerrainReactorItem* const target = item_;
            QMetaObject::invokeMethod(target, [target] {
                if (target->renderingRequested()) target->update();
            }, Qt::QueuedConnection);
        }
    }

private:
    bool createResources()
    {
        releaseResources();
        const QShader vertexShader = loadShader(
            QStringLiteral(":/terrain/shaders/terrain_reactor.vert.qsb"));
        const QShader fragmentShader = loadShader(
            QStringLiteral(":/terrain/shaders/terrain_reactor.frag.qsb"));
        if (!vertexShader.isValid() || !fragmentShader.isValid()) return false;

        vertexBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Immutable,
            QRhiBuffer::VertexBuffer, sizeof(cubeVertices)));
        indexBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Immutable,
            QRhiBuffer::IndexBuffer, sizeof(cubeIndices)));
        columnVertexBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Immutable,
            QRhiBuffer::VertexBuffer, sizeof(columnVertices)));
        columnIndexBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Immutable,
            QRhiBuffer::IndexBuffer, sizeof(columnIndices)));
        instanceBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
            QRhiBuffer::VertexBuffer, maximumInstances * sizeof(GpuInstance)));
        uniformBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
            QRhiBuffer::UniformBuffer, sizeof(UniformBlock)));
        if (!vertexBuffer_->create() || !indexBuffer_->create()
            || !instanceBuffer_->create() || !uniformBuffer_->create()
            || !columnVertexBuffer_->create() || !columnIndexBuffer_->create()) {
            return false;
        }

        const auto layout = terrainVertexLayout();
        if (!shadow_.create(rhi(),
                loadShader(QStringLiteral(":/terrain-shadow/shaders/terrain_shadow.vert.qsb")),
                loadShader(QStringLiteral(":/terrain/shaders/terrain_shadow.frag.qsb")), layout))
            return false;
        const auto createMaterial = [&]() {
            bindings_.reset(rhi()->newShaderResourceBindings());
            bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                uniformBuffer_.get()), QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage,
                    shadow_.texture(), shadow_.sampler())});
            if (!bindings_->create()) return false;

            pipeline_.reset(rhi()->newGraphicsPipeline());
            pipeline_->setShaderStages({
                {QRhiShaderStage::Vertex, vertexShader},
                {QRhiShaderStage::Fragment, fragmentShader},
            });
            pipeline_->setVertexInputLayout(layout);
            pipeline_->setShaderResourceBindings(bindings_.get());
            pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            pipeline_->setSampleCount(renderTarget()->sampleCount());
            pipeline_->setCullMode(QRhiGraphicsPipeline::Back);
            // Clip-Y inversion below reverses winding as well as sample coverage.
            pipeline_->setFrontFace(QRhiGraphicsPipeline::CW);
            pipeline_->setDepthTest(true);
            pipeline_->setDepthWrite(true);
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            pipeline_->setTargetBlends({blend});
            return pipeline_->create();
        };
        if (!shadow_.createMaterialOrFallback(rhi(), createMaterial, [&]() {
                pipeline_.reset();
                bindings_.reset();
            })) return false;

        QRhiResourceUpdateBatch* uploads = rhi()->nextResourceUpdateBatch();
        uploads->uploadStaticBuffer(vertexBuffer_.get(), cubeVertices.data());
        uploads->uploadStaticBuffer(indexBuffer_.get(), cubeIndices.data());
        uploads->uploadStaticBuffer(columnVertexBuffer_.get(), columnVertices.data());
        uploads->uploadStaticBuffer(columnIndexBuffer_.get(), columnIndices.data());
        // initialize() receives a command buffer, but the first render pass can
        // safely consume this batch together with the dynamic frame uploads.
        pendingStaticUploads_ = uploads;
        lastRenderTarget_ = renderTarget();
        instancesDirty_ = true;
        layoutBuildTimer_.invalidate();
        instancesDirtyUpload_ = true;
        failed_ = false;
        return true;
    }

    void buildInstancesIfNeeded()
    {
        if (!instancesDirty_) return;
        // Keep drawing the previous complete layout while slider changes merge.
        if (!instances_.isEmpty() && layoutBuildTimer_.isValid()
            && layoutBuildTimer_.elapsed() < 100) return;
        layoutBuildTimer_.restart();
        QualityConfiguration config = quality_.configuration(
            snapshot_.quality == TerrainReactorItem::Quality::Eco);
        int gridCeiling = config.gridSize;
        switch (snapshot_.quality) {
        case TerrainReactorItem::Quality::Eco:
            config.rippleCount = std::min(config.rippleCount, 2);
            break;
        case TerrainReactorItem::Quality::Balanced:
            config.rippleCount = std::min(config.rippleCount, 4);
            config.gridSize = std::min(config.gridSize, 128);
            config.floatingCount = std::min(config.floatingCount, 52);
            if (quality_.stage() < DegradationStage::ReducedGrid) gridCeiling = 160;
            config.particleCount = std::min(config.particleCount, 960);
            break;
        case TerrainReactorItem::Quality::High:
            if (quality_.stage() < DegradationStage::ReducedGrid) {
                config.gridSize = 160;
                gridCeiling = snapshot_.style.topographyDensity >= 0 ? 224 : 192;
                config.internalScale = 1.0F;
            }
            if (quality_.stage() < DegradationStage::ReducedRipples)
                config.rippleCount = 8;
            break;
        }
        if (snapshot_.style.rippleColor.w() > .5F) {
            // Original scene has meteor debris, not the legacy star sphere.
            config.particleCount = 0;
            if (snapshot_.quality == TerrainReactorItem::Quality::High) {
                config.gridSize = referenceTerrainGridSize(snapshot_.style.topographyDensity);
                gridCeiling = 224;
                config.floatingCount = 80;
                config.meteorCount = 10;
                config.rippleCount = 10;
                config.internalScale = 1;
                config.sampleCount = 4;
            }
        }
        if (!snapshot_.style.floatingCubesEnabled) config.floatingCount = 0;
        if (!snapshot_.style.meteorsEnabled) config.meteorCount = 0;
        if (!snapshot_.style.ripplesEnabled) config.rippleCount = 0;
        if (snapshot_.style.materialMode == 2) config.particleCount = 0;
        // Density controls instance count; column size changes only each
        // column's cross-section in the shader. At the reference 125% density,
        // High remains the exact 160 x 160 source grid.
        config.gridSize = snapshot_.style.topographyDensity >= 0
            ? std::min(referenceTerrainGridSize(snapshot_.style.topographyDensity), gridCeiling)
            : terrainGridSizeForDensity(config.gridSize, snapshot_.style.columnDensity, gridCeiling);
        const SceneLayout layout = makeSceneLayout(snapshot_.seed,
            config.gridSize, config.floatingCount, config.meteorCount,
            config.particleCount);
        meteorOrigins_.clear();
        meteorFlight_.cancel();
        for (const auto& meteor : layout.meteors) meteorOrigins_.append(meteor.position);
        instances_.clear();
        currentTerrainCount_ = int(layout.terrain.size());
        instances_.reserve(layout.terrain.size() + layout.floating.size()
                           + layout.meteors.size() + layout.particles.size() + 200);
        for (const auto& instance : layout.terrain) {
            instances_.append(toGpuInstance(instance, 0.0F));
        }
        for (const auto& instance : layout.floating) {
            instances_.append(toGpuInstance(instance, 1.0F));
        }
        for (const auto& instance : layout.meteors) {
            instances_.append(toGpuInstance(instance, 2.0F));
        }
        for (const auto& instance : layout.particles) {
            instances_.append(toGpuInstance(instance, 3.0F));
        }
        meteorParticles_.reset();
        meteorParticleOffset_ = config.meteorCount > 0 ? instances_.size() : -1;
        if (meteorParticleOffset_ >= 0)
            for (int i=0;i<200;++i) instances_.append(GpuInstance{{0,0,0},{0,0,0},{7,0,0,0}});
        if (quint32(instances_.size()) > maximumInstances) {
            qWarning("TerrainReactorItem instance capacity exceeded");
            instances_.resize(qsizetype(maximumInstances));
        }
        currentTerrainCount_ = std::min(currentTerrainCount_, int(instances_.size()));
        instancesDirty_ = false;
        instancesDirtyUpload_ = true;
        currentRippleCount_ = config.rippleCount;
        const bool scaleChanged = !qFuzzyCompare(
            currentInternalScale_, config.internalScale);
        const bool sampleCountChanged = currentSampleCount_ != config.sampleCount;
        if (scaleChanged || sampleCountChanged) {
            currentInternalScale_ = config.internalScale;
            currentSampleCount_ = config.sampleCount;
            TerrainReactorItem* const target = item_;
            QMetaObject::invokeMethod(target, [target, scale = currentInternalScale_,
                                                sampleCount = currentSampleCount_] {
                target->applyInternalScale(scale, sampleCount);
            }, Qt::QueuedConnection);
        }
    }

    UniformBlock buildUniforms(const VisualParameters& visual,
                               const CameraSnapshot& camera)
    {
        // Camera composition belongs exclusively to manual/automatic motion.
        // Audio impulses animate columns and light, never the ground projection.
        QMatrix4x4 projection;
        const QSize size = renderTarget()->pixelSize();
        const float aspect = size.height() > 0
            ? float(size.width()) / float(size.height()) : 1.0F;
        const CameraSnapshot defaults;
        const float yaw = finiteOr(camera.yaw, defaults.yaw);
        const float pitch = std::clamp(
            finiteOr(camera.pitch, defaults.pitch), 0.1F, 1.5707953F);
        const float distance = std::clamp(
            finiteOr(camera.distance, defaults.distance), 5.0F, 120.0F);
        const float punch = finiteUnit(camera.punch * snapshot_.style.cinemaShake);
        projection.perspective(45.0F, aspect, 0.1F, 1000.0F);
        const float radius = distance;
        const RenderDynamics dynamics = mapRenderDynamics(snapshot_.style);
        QVector3D eye(radius * std::cos(pitch) * std::sin(yaw),
                      radius * std::sin(pitch),
                      radius * std::cos(pitch) * std::cos(yaw));
        QMatrix4x4 view;
        view.lookAt(eye, QVector3D(0.0F, 0.0F, 0.0F),
                    QVector3D(0.0F, 1.0F, 0.0F));
        QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * projection * view;
        // Preserve the reference's vertical multisample coverage orientation.
        // QQuickRhiItem mirrors the texture back for normal scene presentation.
        for (int column = 0; column < 4; ++column) mvp(1, column) *= -1;

        UniformBlock result;
        std::memcpy(result.mvp, mvp.constData(), sizeof(result.mvp));
        std::copy_n(visual.bands.begin(), 4, result.bandsLow);
        std::copy_n(visual.bands.begin() + 4, 4, result.bandsHigh);
        for (int index = 0; index < 5; ++index) {
            const QVector4D& color = currentPalette_[std::size_t(index)];
            result.colors[index][0] = color.x();
            result.colors[index][1] = color.y();
            result.colors[index][2] = color.z();
            result.colors[index][3] = color.w();
        }
        // Independent theme roles stay precise through the snapshot.
        for (int channel = 0; channel < 3; ++channel) {
            result.bodyColor[channel] = snapshot_.style.bodyColor.w() > 0.5F
                ? currentThemePalette_.colors[5][channel]
                : result.colors[0][channel] * 0.92F + result.colors[1][channel] * 0.08F;
            result.atmosphereColor[channel] = snapshot_.style.atmosphereColor.w() > 0.5F
                ? (themePaletteInitialized_ ? currentThemePalette_.colors[6][channel]
                                           : snapshot_.style.atmosphereColor[channel])
                : result.colors[0][channel];
        }
        // Material mode is discrete, not an interpolated palette alpha.
        result.bodyColor[3] = snapshot_.style.bodyColor.w() > 1.5F ? 2.0F : 1.0F;
        result.atmosphereColor[3] = 1.0F;
        for (int channel = 0; channel < 4; ++channel)
            result.rippleColor[channel] = themePaletteInitialized_
                ? currentThemePalette_.colors[7][channel] : snapshot_.style.rippleColor[channel];
        std::copy_n(snapshot_.style.visualEqGains.begin(), 4,
                    result.equalizerLow);
        std::copy_n(snapshot_.style.visualEqGains.begin() + 4, 4,
                    result.equalizerHigh);
        if (snapshot_.referenceAudio) {
            // EQ and smoothing have already been applied by the reference response.
            std::fill_n(result.equalizerLow, 4, 1.0F);
            std::fill_n(result.equalizerHigh, 4, 1.0F);
        }
        result.parameters[0] = visual.energy;
        result.parameters[1] = visual.spectralFlux;
        result.parameters[2] = visual.rippleStrength;
        result.parameters[3] = visual.timeSeconds;
        result.effects[0] = visual.particleActivity;
        result.effects[1] = visual.meteorActivity;
        result.effects[2] = punch;
        result.effects[3] = float(currentRippleCount_);
        result.styleParameters[0] = snapshot_.style.terrainAmplitude;
        result.styleParameters[1] = snapshot_.style.motionResponse;
        result.styleParameters[2] = themePaletteInitialized_
            ? currentThemePalette_.glow : snapshot_.style.glowIntensity;
        result.styleParameters[3] = snapshot_.style.cinemaShake;
        result.styleDynamics[0] = snapshot_.style.autoRotate;
        result.styleDynamics[1] = snapshot_.style.peakBoost;
        result.styleDynamics[2] = float(snapshot_.style.colorMode);
        result.styleDynamics[3] = snapshot_.style.gradientLayers;
        result.styleToggles[0] = snapshot_.style.ripplesEnabled ? 1.0F : 0.0F;
        result.styleToggles[1] = snapshot_.style.floatingCubesEnabled ? 1.0F : 0.0F;
        result.styleToggles[2] = snapshot_.style.meteorsEnabled ? 1.0F : 0.0F;
        result.styleToggles[3] = snapshot_.style.idleBreathingEnabled ? 1.0F : 0.0F;
        result.styleExtra[0] = snapshot_.style.themeCycleEnabled ? 1.0F : 0.0F;
        result.styleExtra[1] = snapshot_.style.burstEnabled ? 1.0F : 0.0F;
        result.styleExtra[2] = snapshot_.style.streamHighlightEnabled ? 1.0F : 0.0F;
        result.styleExtra[3] = meteorFlight_.age(visual.timeSeconds);
        result.styleAudio[0] = dynamics.inputCompression;
        result.styleAudio[1] = dynamics.audioResponse;
        result.styleAudio[2] = dynamics.responseRadius;
        result.styleAudio[3] = dynamics.centerHighlight;
        result.stylePresentation[0] = dynamics.rhythmStrength;
        result.stylePresentation[1] = dynamics.depthOfField;
        result.stylePresentation[2] = dynamics.subjectClarity;
        result.stylePresentation[3] = snapshot_.style.rippleColor.w() > .5F
            ? platterAngle_ : dynamics.autoRotateSpeed;
        result.impact[0] = visual.impactStrength;
        result.impact[1] = visual.impactAge;
        result.impact[2] = float(meteorFlight_.group() + 1);
        result.impact[3] = dynamics.rhythmSensitivity;
        for (int channel=0; channel<4; ++channel)
            result.floatingParameters[channel] = floatingParameters_[channel];
        const auto trajectory = meteorFlight_.trajectory();
        const auto meteorColor = meteorMaterialColor_.color();
        for (int channel=0; channel<3; ++channel)
            result.meteorMaterialColor[channel] = meteorColor[channel];
        result.meteorMaterialColor[3] = themePaletteInitialized_ ? 1.0F : 0.0F;
        for (int channel=0; channel<4; ++channel)
            result.meteorTrajectory[channel] = trajectory[channel];
        for (std::size_t index = 0; index < waveSources_.size(); ++index) {
            const QVector4D& source = travelingWaves_[index];
            result.waveSources[index][0] = source.x();
            result.waveSources[index][1] = source.y();
            const float age = std::max(0.0F, visual.timeSeconds - source.z());
            if (snapshot_.style.rippleColor.w() > 0.5F) {
                // Reference events carry plain elapsed seconds and signed
                // strength: meteor touchdown is white (negative); ordinary
                // beat waves remain colored (positive).
                result.waveSources[index][2] = age;
                result.waveSources[index][3] = source.w();
            } else {
                result.waveSources[index][2] = std::min(age, 31.0F)
                    + float(waveTints_[index]) * 32.0F;
                result.waveSources[index][3] = age < 2.8F / snapshot_.style.rippleDecay
                    ? source.w() * dynamics.rhythmStrength : 0.0F;
            }
        }
        const BassEnvelopeSnapshot envelope = bassEnvelope_.snapshot();
        result.audioEnvelope[0] = envelope.fast;
        result.audioEnvelope[1] = envelope.slow;
        result.audioEnvelope[2] = visual.beatStrength;
        result.audioEnvelope[3] = visual.beatAge;
        result.cameraPosition[0] = eye.x();
        result.cameraPosition[1] = eye.y();
        result.cameraPosition[2] = eye.z();
        result.cameraPosition[3] = dynamics.depthOfField;
        result.materialParameters[0] = float(snapshot_.style.materialMode);
        result.sceneControls[0] = snapshot_.style.columnOpacity;
        result.sceneControls[1] = snapshot_.style.reactorBrightness;
        result.sceneLighting[0] = snapshot_.style.columnInnerLight;
        result.sceneLighting[1] = snapshot_.style.columnLightSpill;
        result.sceneLighting[2] = snapshot_.style.columnLightRadius;
        result.sceneControls[2] = kTerrainStageExtent * 0.5F;
        result.sceneControls[3] = snapshot_.style.columnSize;
        result.materialParameters[1] = snapshot_.style.materialSoftness;
        result.materialParameters[2] = snapshot_.style.jellyElasticity;
        result.materialParameters[3] = snapshot_.style.inkDensity;
        result.waveParameters[0] = snapshot_.style.rippleStrength;
        result.waveParameters[1] = snapshot_.style.rippleWidth;
        result.waveParameters[2] = snapshot_.style.rippleDecay;
        if (snapshot_.referenceAudio) {
            result.waveParameters[3] = float(referenceDescriptors_.smoothness);
            result.sceneLighting[3] = float(referenceDescriptors_.density);
            result.timbre[0] = float(referenceDescriptors_.warmth);
            result.timbre[1] = float(referenceDescriptors_.brightness);
            result.timbre[2] = float(referenceDescriptors_.sharpness);
        }
        // 0: replay; 1: canonical theme; 2: user-custom material.
        result.timbre[3] = snapshot_.style.bodyColor.w() > 0.5F ? 1.0F : 2.0F;
        return result;
    }

    void releaseResources()
    {
        spatialLyrics_.reset();
        telemetry_->stableRenderedFrames.store(0, std::memory_order_release);
        telemetry_->terrainCount.store(0, std::memory_order_release);
        telemetry_->renderedFeatureRevision.store(0, std::memory_order_release);
        telemetry_->renderedStyleRevision.store(0, std::memory_order_release);
        if (pendingStaticUploads_ != nullptr) {
            pendingStaticUploads_->release();
            pendingStaticUploads_ = nullptr;
        }
        pipeline_.reset();
        bindings_.reset();
        shadow_.reset();
        uniformBuffer_.reset();
        instanceBuffer_.reset();
        indexBuffer_.reset();
        vertexBuffer_.reset();
        columnIndexBuffer_.reset();
        columnVertexBuffer_.reset();
        lastRenderTarget_ = nullptr;
        resourceState_->invalidateResources();
    }

    void fail(TerrainReactorItem::RenderStatus status, const QString& message)
    {
        failed_ = status == TerrainReactorItem::RenderStatus::SoftwareBackend
            || status == TerrainReactorItem::RenderStatus::ResourceError;
        status_ = status;
        diagnostic_ = message;
        publishStatus();
    }

    void publishStatus()
    {
        if (item_ == nullptr) return;
        TerrainReactorItem* const target = item_;
        const auto status = status_;
        const auto diagnostic = diagnostic_;
        QMetaObject::invokeMethod(target, [target, status, diagnostic] {
            target->reportRenderStatus(status, diagnostic);
        }, Qt::QueuedConnection);
    }

    void notifyCounters(bool force = false)
    {
        if (item_ == nullptr) return;
        if (!force && counterTimer_.isValid()
            && counterTimer_.elapsed() < 125) return;
        counterTimer_.restart();
        QMetaObject::invokeMethod(item_, &TerrainReactorItem::countersChanged,
                                  Qt::QueuedConnection);
    }

    void publishRenderedRevisions()
    {
        if (instancesDirty_) {
            telemetry_->stableRenderedFrames.store(0, std::memory_order_release);
            return;
        }
        const quint64 featureRevision = snapshot_.featureRevision;
        const quint64 styleRevision = snapshot_.styleRevision;
        const bool samePair = telemetry_->renderedFeatureRevision.load(
                                  std::memory_order_acquire) == featureRevision
            && telemetry_->renderedStyleRevision.load(
                   std::memory_order_acquire) == styleRevision;
        if (samePair) {
            telemetry_->stableRenderedFrames.fetch_add(
                1, std::memory_order_release);
            return;
        }
        // Reset before publishing a new pair so readers cannot combine new
        // revisions with the previous pair's settled-frame count.
        telemetry_->stableRenderedFrames.store(0, std::memory_order_release);
        telemetry_->renderedFeatureRevision.store(featureRevision,
                                                   std::memory_order_release);
        telemetry_->renderedStyleRevision.store(styleRevision,
                                                 std::memory_order_release);
        telemetry_->stableRenderedFrames.store(1, std::memory_order_release);
    }

    static std::atomic<quint64> nextRendererId_;
    std::shared_ptr<TerrainReactorItem::Telemetry> telemetry_;
    std::shared_ptr<RendererResourceState> resourceState_;
    quint64 rendererId_ = 0;
    bool claimed_ = false;
    AutomaticQualityController quality_;
    DegradationStage lastStage_ = DegradationStage::Full;
    TerrainReactorItem::RenderSnapshot snapshot_;
    AudioFeatures smoothedFeatures_;
    agplayer::VisualTerrainResponse referenceResponse_;
    agplayer::VisualAudioFrameAnalyzer frameAnalyzer_;
    agplayer::VisualSnareTrigger snareTrigger_;
    BeatEvent renderBeat_;
    ImpactEvent renderImpact_;
    std::uint64_t lastPcmFrameSequence_ = 0;
    quint64 analysisFrames_ = 0;
    agplayer::VisualSpectrumFeatures::Features referenceDescriptors_;
    CameraMotion camera_;
    PunchEventConsumer punchEvents_;
    BeatEventConsumer beatEvents_;
    ImpactEventConsumer impactEvents_;
    BassEnvelopeFollower bassEnvelope_;
    MultiWaveSources waveSources_{};
    MultiWaveSources travelingWaves_{};
    std::array<unsigned, 8> waveTints_{};
    unsigned waveSequence_ = 0;
    MeteorFlight meteorFlight_;
    QVector4D floatingParameters_;
    MeteorParticlePool meteorParticles_;
    // MeshBasicMaterial survives audio resets, layout changes and enable toggles.
    MeteorMaterialColor meteorMaterialColor_;
    qsizetype meteorParticleOffset_ = -1;
    QVector<QVector3D> meteorOrigins_;
    TravelingWaveGate waveGate_;
    int nextWave_ = 0;
    TerrainSpatialLyrics spatialLyrics_;
    quint64 consumedRippleRevision_ = 0;
    float platterAngle_ = 0;
    int currentSampleCount_ = 4;
    bool waveSourcesInitialized_ = false;
    TrackPalette currentPalette_{};
    ThemePalette currentThemePalette_{};
    ThemePalette targetThemePalette_{};
    bool themePaletteInitialized_ = false;
    TrackPalette fromPalette_{};
    TrackPalette targetPalette_{};
    float paletteProgress_ = 1.0F;
    bool paletteInitialized_ = false;
    CameraSnapshot previousGuiCamera_;
    bool cameraSynchronized_ = false;
    quint64 syncedCameraRevision_ = std::numeric_limits<quint64>::max();
    TerrainReactorItem* item_ = nullptr;
    QElapsedTimer frameTimer_;
    QElapsedTimer renderTimeTimer_;
    double renderTimeEpochSeconds_ = 0.0;
    bool renderTimeAnchored_ = false;
    QElapsedTimer counterTimer_;
    QElapsedTimer layoutBuildTimer_;
    const bool performanceProbe_ = qEnvironmentVariableIsSet("AGPLAYER_REACTOR_PERF");
    std::array<double, 120> frameIntervals_{};
    std::size_t probeFrames_ = 0;
    double probeWorkMs_ = 0;
    QVector<GpuInstance> instances_;
    bool instancesDirty_ = true;
    bool instancesDirtyUpload_ = false;
    int currentRippleCount_ = 4;
    int currentTerrainCount_ = 0;
    // Mirrors QQuickRhiItem's initial full-resolution buffer. The first
    // Balanced/Auto layout must therefore schedule the 0.90 scale update.
    float currentInternalScale_ = 1.0F;
    bool failed_ = false;
    const bool softwareBackend_ = false;
    TerrainReactorItem::RenderStatus status_ = TerrainReactorItem::RenderStatus::Inactive;
    QString diagnostic_;
    QRhiRenderTarget* lastRenderTarget_ = nullptr;
    std::unique_ptr<QRhiBuffer> vertexBuffer_;
    std::unique_ptr<QRhiBuffer> columnVertexBuffer_, columnIndexBuffer_;
    TerrainShadowMap shadow_;
    std::unique_ptr<QRhiBuffer> indexBuffer_;
    std::unique_ptr<QRhiBuffer> instanceBuffer_;
    std::unique_ptr<QRhiBuffer> uniformBuffer_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    QRhiResourceUpdateBatch* pendingStaticUploads_ = nullptr;
};

std::atomic<quint64> TerrainReactorRenderer::nextRendererId_{0};

TerrainReactorItem::TerrainReactorItem(QQuickItem* parent)
    : QQuickRhiItem(parent), telemetry_(std::make_shared<Telemetry>()),
      resourceState_(std::make_shared<RendererResourceState>())
{
    setSampleCount(1);
    setAlphaBlending(true);
    setMirrorVertically(true);
    clock_.start();
    // One GUI-thread scheduler owns the cadence. Never drop a requested RHI
    // render: its target may have just been recreated or resolved by Qt.
    renderTick_.setTimerType(Qt::PreciseTimer);
    renderTick_.setInterval(16);
    connect(&renderTick_, &QTimer::timeout, this, [this] {
        if (renderingRequested()) update();
        else scheduleIfRunnable();
    });
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        emit renderingRequestedChanged();
        scheduleIfRunnable();
    });
    connect(this, &QQuickItem::windowChanged,
            this, &TerrainReactorItem::updateWindowState);
    connect(this, &QQuickItem::widthChanged,
            this, &TerrainReactorItem::updateColorBufferSize);
    connect(this, &QQuickItem::heightChanged,
            this, &TerrainReactorItem::updateColorBufferSize);
    updateWindowState(window());
}

bool TerrainReactorItem::hasVisualPcmDiscontinuity(
    const agplayer::VisualAudioFrameAnalyzer::Snapshot& previous,
    const agplayer::VisualAudioFrameAnalyzer::Snapshot& next)
{
    // valid intentionally drops while playback is paused and returns on
    // resume. The retained PCM window remains on the same generation and
    // sample-rate timeline, so only those timeline changes require a reset.
    return next.epoch != previous.epoch
        || next.sampleRate != previous.sampleRate;
}

TerrainReactorItem::~TerrainReactorItem()
{
    if (auto* visual = qobject_cast<AudioVisualFeatureController*>(featureSource_))
        visual->releaseRenderFrameAnalysis();
    if (trackedWindow_ != nullptr) trackedWindow_->removeEventFilter(this);
    disconnect(windowVisibilityConnection_);
    disconnect(featureConnection_);
    disconnect(impactConnection_);
    disconnect(sourceDestroyedConnection_);
    for (const auto& connection : styleConnections_) disconnect(connection);
    disconnect(this, nullptr, this, nullptr);
}

QObject* TerrainReactorItem::styleSource() const noexcept { return styleSource_; }
void TerrainReactorItem::setStyleSource(QObject* source)
{
    auto* typed = qobject_cast<PlayerExperienceController*>(source);
    if (styleSource_ == typed) return;
    for (const auto& connection : styleConnections_) disconnect(connection);
    styleConnections_.clear();
    styleSource_ = typed;
    if (styleSource_ != nullptr) {
        const auto capture = [this] { copyStyleSource(); };
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::themeChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::materialModeChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::materialSoftnessChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::columnSizeChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::columnDensityChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::topographyDensityChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::columnOpacityChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::reactorBrightnessChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::columnInnerLightChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::columnLightSpillChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::columnLightRadiusChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::jellyElasticityChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::inkDensityChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::rippleStrengthChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::rippleWidthChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &PlayerExperienceController::rippleDecayChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::colorModeChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::coolColorChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::warmColorChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::accentColorChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::peakColorChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::baseColorChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::terrainAmplitudeChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::motionResponseChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::gradientLayersChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::glowIntensityChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::cinemaShakeChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::autoRotateChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::peakBoostChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::ripplesEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::burstEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::floatingCubesEnabledChanged, this, capture));
    styleConnections_.append(connect(styleSource_, &PlayerExperienceController::floatingBlockMinSizeChanged, this, capture));
    styleConnections_.append(connect(styleSource_, &PlayerExperienceController::floatingBlockMaxSizeChanged, this, capture));
    styleConnections_.append(connect(styleSource_, &PlayerExperienceController::floatingBlockSpeedChanged, this, capture));
    styleConnections_.append(connect(styleSource_, &PlayerExperienceController::floatingBlockIntensityChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::meteorsEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::idleBreathingEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::themeCycleEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::streamHighlightEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::visualEqGainsChanged, this, capture));
        styleConnections_.push_back(connect(styleSource_,
            &PlayerExperienceController::visualEqEnabledChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::inputCompressionChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::audioResponseChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::responseRangeChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::centerHighlightChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::rhythmStrengthChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::depthOfFieldChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::subjectClarityChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::autoRotateSpeedChanged, this, capture));
        styleConnections_.append(connect(styleSource_,
            &PlayerExperienceController::rhythmSensitivityChanged, this, capture));
        styleConnections_.append(connect(styleSource_, &QObject::destroyed,
            this, [this] {
                styleSource_.clear();
                emit styleSourceChanged();
            }));
        copyStyleSource();
    }
    emit styleSourceChanged();
}

bool TerrainReactorItem::active() const noexcept { return active_; }
void TerrainReactorItem::setActive(bool active)
{
    if (active_ == active) return;
    active_ = active;
    if (!active_) reportRenderStatus(RenderStatus::Inactive, QString());
    emit activeChanged();
    emit renderingRequestedChanged();
    scheduleIfRunnable();
}
bool TerrainReactorItem::hostExposed() const noexcept { return hostExposed_; }
void TerrainReactorItem::setHostExposed(bool exposed)
{
    if (hostExposed_ == exposed) return;
    hostExposed_ = exposed;
    emit hostExposedChanged();
    emit renderingRequestedChanged();
    scheduleIfRunnable();
}
bool TerrainReactorItem::renderingRequested() const noexcept
{
    const bool backendUnavailable = renderStatus_ == RenderStatus::SoftwareBackend
        || renderStatus_ == RenderStatus::ResourceError;
    return active_ && isVisible() && hostExposed_ && windowExposed_
        && !backendUnavailable;
}

QObject* TerrainReactorItem::featureSource() const noexcept { return featureSource_; }
void TerrainReactorItem::setFeatureSource(QObject* source)
{
    QObject* accepted = qobject_cast<AudioVisualFeatureController*>(source);
    if (accepted == nullptr && source != nullptr) {
        constexpr std::array<const char*, 5> requiredProperties{
            "bands", "energy", "spectralFlux", "kickPulse", "snarePulse"
        };
        const QMetaObject* meta = source->metaObject();
        const bool hasProperties = std::all_of(requiredProperties.cbegin(),
            requiredProperties.cend(), [meta](const char* name) {
                return meta->indexOfProperty(name) >= 0;
            });
        if (hasProperties && meta->indexOfSignal("featuresChanged()") >= 0) {
            accepted = source;
        }
    }
    if (featureSource_ == accepted) return;
    if (featureConnection_) disconnect(featureConnection_);
    if (visualConnection_) disconnect(visualConnection_);
    if (visualResetConnection_) disconnect(visualResetConnection_);
    if (impactConnection_) disconnect(impactConnection_);
    if (sourceDestroyedConnection_) disconnect(sourceDestroyedConnection_);
    if (auto* visual = qobject_cast<AudioVisualFeatureController*>(featureSource_))
        visual->releaseRenderFrameAnalysis();
    featureSource_ = accepted;
    ++visualResetRevision_;
    liveFeatures_ = {};
    featureSourceProvidesBeat_ = featureSource_ != nullptr
        && featureSource_->metaObject()->indexOfProperty("beatRevision") >= 0;
    featureSourceProvidesImpact_ = featureSource_ != nullptr
        && featureSource_->metaObject()->indexOfProperty("impactRevision") >= 0;
    if (featureSource_ != nullptr) {
        if (auto* visual = qobject_cast<AudioVisualFeatureController*>(featureSource_)) {
            visual->acquireRenderFrameAnalysis();
            if (styleSource_)
                visual->setVisualKickSensitivity(styleSource_->rhythmSensitivity());
            visualConnection_ = connect(visual, &AudioVisualFeatureController::visualSpectrumReady,
                                       this, &TerrainReactorItem::copyVisualSource);
            visualResetConnection_ = connect(visual, &AudioVisualFeatureController::visualStateReset,
                this, [this] {
                    ++visualResetRevision_;
                    liveFeatures_ = {};
                    ++featureRevision_;
                    emit featureRevisionChanged();
                    copyVisualSource();
                });
        }
        const QMetaObject* sourceMeta = featureSource_->metaObject();
        const int signalIndex = sourceMeta->indexOfSignal("featuresChanged()");
        const int slotIndex = metaObject()->indexOfSlot("copyFeatureSource()");
        featureConnection_ = QObject::connect(featureSource_,
            sourceMeta->method(signalIndex), this,
            metaObject()->method(slotIndex));
        const int impactSignalIndex = sourceMeta->indexOfSignal("impactChanged()");
        if (impactSignalIndex >= 0) {
            impactConnection_ = QObject::connect(featureSource_,
                sourceMeta->method(impactSignalIndex), this,
                metaObject()->method(slotIndex));
        }
        sourceDestroyedConnection_ = connect(featureSource_, &QObject::destroyed,
            this, [this] {
                featureSource_.clear();
                ++visualResetRevision_;
                liveFeatures_ = {};
                applyCurrentFeatures(liveFeatures_);
                emit featureSourceChanged();
            });
        copyFeatureSource();
    } else {
        liveFeatures_ = {};
        applyCurrentFeatures(liveFeatures_);
    }
    emit featureSourceChanged();
}
bool TerrainReactorItem::useSyntheticFeatures() const noexcept
{
    return useSyntheticFeatures_;
}
void TerrainReactorItem::setUseSyntheticFeatures(bool enabled)
{
    if (useSyntheticFeatures_ == enabled) return;
    useSyntheticFeatures_ = enabled;
    if (enabled || styleSource_.isNull() || styleSource_->themeId().isEmpty()
        || qobject_cast<AudioVisualFeatureController*>(featureSource_) == nullptr)
        applyCurrentFeatures(enabled ? syntheticFeatures_ : liveFeatures_);
    else {
        ++visualResetRevision_;
        ++featureRevision_;
        emit featureRevisionChanged();
        scheduleIfRunnable();
    }
    emit useSyntheticFeaturesChanged();
}
quint32 TerrainReactorItem::deterministicSeed() const noexcept
{
    return deterministicSeed_;
}
void TerrainReactorItem::setDeterministicSeed(quint32 seed)
{
    if (deterministicSeed_ == seed) return;
    deterministicSeed_ = seed;
    emit deterministicSeedChanged();
    scheduleIfRunnable();
}
QString TerrainReactorItem::trackIdentity() const { return trackIdentity_; }
void TerrainReactorItem::setTrackIdentity(const QString& identity)
{
    if (trackIdentity_ == identity) return;
    trackIdentity_ = identity;
    trackPaletteSeed_ = stableTrackPaletteSeed(QStringView(trackIdentity_));
    ++paletteRevision_;
    emit trackIdentityChanged();
    scheduleIfRunnable();
}
quint32 TerrainReactorItem::trackPaletteSeed() const noexcept
{
    return trackPaletteSeed_;
}
TerrainReactorItem::Quality TerrainReactorItem::quality() const noexcept
{
    return quality_;
}
void TerrainReactorItem::setQuality(Quality quality)
{
    if (quality_ == quality) return;
    quality_ = quality;
    applyInternalScale(internalScale_, quality_ == Quality::Eco ? 1 : 4);
    emit qualityChanged();
    scheduleIfRunnable();
}

QVariantList TerrainReactorItem::featureBands() const
{
    const auto& current = useSyntheticFeatures_ ? syntheticFeatures_ : liveFeatures_;
    QVariantList result;
    result.reserve(8);
    for (float band : current.bands) result.append(double(band));
    return result;
}
qreal TerrainReactorItem::featureEnergy() const noexcept
{
    return useSyntheticFeatures_ ? syntheticFeatures_.energy : liveFeatures_.energy;
}
qreal TerrainReactorItem::featureSpectralFlux() const noexcept
{
    return useSyntheticFeatures_ ? syntheticFeatures_.spectralFlux
                                 : liveFeatures_.spectralFlux;
}
bool TerrainReactorItem::featureKick() const noexcept
{
    return (useSyntheticFeatures_ ? syntheticFeatures_.kick : liveFeatures_.kick) > 0.5F;
}
bool TerrainReactorItem::featureSnare() const noexcept
{
    return (useSyntheticFeatures_ ? syntheticFeatures_.snare : liveFeatures_.snare) > 0.5F;
}
quint64 TerrainReactorItem::featureRevision() const noexcept { return featureRevision_; }
quint64 TerrainReactorItem::styleRevision() const noexcept { return styleRevision_; }
RenderStyleSnapshot TerrainReactorItem::renderStyleSnapshot() const
{
    return renderStyle_;
}

qreal TerrainReactorItem::cameraYaw() const noexcept { return camera_.snapshot().yaw; }
qreal TerrainReactorItem::cameraPitch() const noexcept { return camera_.snapshot().pitch; }
qreal TerrainReactorItem::cameraDistance() const noexcept { return camera_.snapshot().distance; }
qreal TerrainReactorItem::cameraPunch() const noexcept { return pendingPunch_.strength; }
quint64 TerrainReactorItem::punchRevision() const noexcept
{
    return pendingPunch_.revision;
}
qreal TerrainReactorItem::beatStrength() const noexcept
{
    return pendingBeat_.strength;
}
quint64 TerrainReactorItem::beatRevision() const noexcept
{
    return pendingBeat_.revision;
}
qreal TerrainReactorItem::impactStrength() const noexcept
{
    return pendingImpact_.strength;
}
quint64 TerrainReactorItem::impactRevision() const noexcept
{
    return pendingImpact_.revision;
}

quint64 TerrainReactorItem::frameCount() const noexcept
{
    return telemetry_->frames.load(std::memory_order_relaxed);
}
quint64 TerrainReactorItem::animationCount() const noexcept
{
    return telemetry_->animations.load(std::memory_order_relaxed);
}
quint64 TerrainReactorItem::uploadCount() const noexcept
{
    return telemetry_->uploads.load(std::memory_order_relaxed);
}
int TerrainReactorItem::renderedTerrainCount() const noexcept
{
    return telemetry_->terrainCount.load(std::memory_order_acquire);
}

quint64 TerrainReactorItem::renderedFeatureRevision() const noexcept
{
    return telemetry_->renderedFeatureRevision.load(std::memory_order_acquire);
}
quint64 TerrainReactorItem::renderedStyleRevision() const noexcept
{
    return telemetry_->renderedStyleRevision.load(std::memory_order_acquire);
}
quint64 TerrainReactorItem::stableRenderedFrameCount() const noexcept
{
    return telemetry_->stableRenderedFrames.load(std::memory_order_acquire);
}
quint64 TerrainReactorItem::resourceGeneration() const noexcept
{
    return resourceState_->generation();
}
int TerrainReactorItem::liveRendererCount() const noexcept
{
    return resourceState_->liveRendererCount();
}
TerrainReactorItem::RenderStatus TerrainReactorItem::renderStatus() const noexcept
{
    return renderStatus_;
}
QString TerrainReactorItem::diagnostic() const { return diagnostic_; }

void TerrainReactorItem::setSyntheticFeatures(const QVariantList& bands,
                                               qreal energy, qreal spectralFlux,
                                               bool kick, bool snare)
{
    AudioFeatures next;
    for (int index = 0; index < 8; ++index) {
        next.bands[std::size_t(index)] = index < bands.size()
            ? finiteUnit(float(bands.at(index).toDouble())) : 0.0F;
    }
    next.energy = finiteUnit(float(energy));
    next.spectralFlux = finiteUnit(float(spectralFlux));
    next.kick = kick ? 1.0F : 0.0F;
    next.snare = snare ? 1.0F : 0.0F;
    syntheticFeatures_ = next;
    if (useSyntheticFeatures_) applyCurrentFeatures(syntheticFeatures_);
}
void TerrainReactorItem::orbitBy(qreal yawDelta, qreal pitchDelta,
                                  qreal nowSeconds)
{
    const float safeYawDelta = float(yawDelta);
    const float safePitchDelta = float(pitchDelta);
    if (!std::isfinite(double(nowSeconds))
        || !std::isfinite(double(yawDelta)) || !std::isfinite(safeYawDelta)
        || !std::isfinite(double(pitchDelta))
        || !std::isfinite(safePitchDelta)) return;
    camera_.orbitBy(safeYawDelta, safePitchDelta,
                    double(clock_.elapsed()) / 1000.0);
    ++cameraRevision_;
    emit cameraChanged();
    scheduleIfRunnable();
}
void TerrainReactorItem::zoomBy(qreal wheelDelta, qreal nowSeconds)
{
    const float safeWheelDelta = float(wheelDelta);
    if (!std::isfinite(double(nowSeconds))
        || !std::isfinite(double(wheelDelta))
        || !std::isfinite(safeWheelDelta)) return;
    camera_.zoomBy(safeWheelDelta, double(clock_.elapsed()) / 1000.0);
    ++cameraRevision_;
    emit cameraChanged();
    scheduleIfRunnable();
}
void TerrainReactorItem::triggerCameraPunch(qreal strength)
{
    pendingPunch_.strength = finiteUnit(float(strength));
    ++pendingPunch_.revision;
    emit cameraChanged();
    scheduleIfRunnable();
}

void TerrainReactorItem::triggerRipple(qreal x, qreal y)
{
    if (!active_ || !hostExposed_ || width() <= 0 || height() <= 0
        || !std::isfinite(x) || !std::isfinite(y)) return;
    ripplePosition_ = QPointF(std::clamp(x / width(), 0.0, 1.0),
                             std::clamp(y / height(), 0.0, 1.0));
    ++rippleRevision_;
    scheduleIfRunnable();
}

void TerrainReactorItem::setSpatialLyrics(const QStringList& lines)
{
    if (spatialLyrics_ == lines) return;
    spatialLyrics_ = lines;
    emit spatialLyricsChanged();
    scheduleIfRunnable();
}

QQuickRhiItemRenderer* TerrainReactorItem::createRenderer()
{
    const bool softwareBackend = window() != nullptr
        && window()->rendererInterface()->graphicsApi()
            == QSGRendererInterface::Software;
    auto* renderer = new TerrainReactorRenderer(
        telemetry_, resourceState_, softwareBackend);
    const bool claimed = renderer->claimed();
    QMetaObject::invokeMethod(this, [this, claimed] {
        if (!claimed) {
            reportRenderStatus(RenderStatus::ResourceError,
                QStringLiteral("Terrain Reactor rejected a duplicate renderer"));
        }
        emit countersChanged();
    }, Qt::QueuedConnection);
    return renderer;
}

TerrainReactorItem::RenderSnapshot TerrainReactorItem::snapshotForRenderer() const
{
    RenderSnapshot result;
    result.features = useSyntheticFeatures_ ? syntheticFeatures_ : liveFeatures_;
    result.referenceAudio = !useSyntheticFeatures_ && !styleSource_.isNull()
        && !styleSource_->themeId().isEmpty()
        && qobject_cast<AudioVisualFeatureController*>(featureSource_) != nullptr;
    if (auto* visual = qobject_cast<AudioVisualFeatureController*>(featureSource_)) {
        result.pcm = visual->visualPcmSnapshot();
        result.pcmBatch = visual->visualPcmBatch();
    }
    result.visualResetRevision = visualResetRevision_;
    result.style = renderStyle_;
    result.camera = camera_.snapshot();
    result.ripplePosition = ripplePosition_;
    result.rippleRevision = rippleRevision_;
    result.spatialLyrics = spatialLyrics_;
    if (styleSource_) {
        result.lyricOpacity = float(styleSource_->lyricOpacity()) / 100.0F;
        result.lyricOrbit = -.663225116F + float(styleSource_->lyricPositionX() - 50) * .06283185F
            + float(styleSource_->lyricPosition() - 1) * .5F;
        result.lyricElevation = float(42 - styleSource_->lyricPositionY());
        result.lyricScale = float(styleSource_->lyricSize()) / 100.0F;
        result.lyricDepth = 1.0F + float(styleSource_->lyricDepth() - 62) / 100.0F;
        const QColor background(styleSource_->themeBackground());
        result.lyricColor = background.lightnessF() > .65 ? QColor(30,35,44) : QColor(Qt::white);
    }
    result.punchEvent = pendingPunch_;
    result.beatEvent = pendingBeat_;
    result.impactEvent = pendingImpact_;
    result.cameraManualUntilSeconds = camera_.manualUntilSeconds();
    result.cameraRevision = cameraRevision_;
    result.seed = deterministicSeed_;
    result.trackPaletteActive = trackPaletteSeed_ != 0U;
    result.trackPalette = trackPalette(trackPaletteSeed_);
    result.paletteRevision = paletteRevision_;
    result.quality = quality_;
    result.running = renderingRequested();
    result.activityRevision = activityRevision_;
    result.timeSeconds = float(clock_.elapsed()) / 1000.0F;
    result.featureRevision = featureRevision_;
    result.styleRevision = styleRevision_;
    return result;
}

void TerrainReactorItem::copyStyleSource()
{
    if (styleSource_ == nullptr) return;
    const auto colorVector = [](const QString& text) {
        const QColor color(text);
        return QVector4D(float(color.redF()), float(color.greenF()),
                         float(color.blueF()), float(color.alphaF()));
    };
    RenderStyleSnapshot next;
    next.materialMode = styleSource_->materialMode();
    next.materialSoftness = float(styleSource_->materialSoftness()) / 100.0F;
    next.columnSize = float(styleSource_->columnSize()) / 100.0F;
    next.columnDensity = styleSource_->columnDensity();
    next.columnOpacity = float(styleSource_->columnOpacity()) / 100.0F;
    next.reactorBrightness = float(styleSource_->reactorBrightness()) / 100.0F;
    next.columnInnerLight = float(styleSource_->columnInnerLight()) / 100.0F;
    next.columnLightSpill = float(styleSource_->columnLightSpill()) / 100.0F;
    next.columnLightRadius = float(styleSource_->columnLightRadius()) / 100.0F;
    next.jellyElasticity = float(styleSource_->jellyElasticity()) / 100.0F;
    next.inkDensity = float(styleSource_->inkDensity()) / 100.0F;
    next.rippleStrength = float(styleSource_->rippleStrength()) / 100.0F;
    next.rippleWidth = float(styleSource_->rippleWidth()) / 100.0F;
    next.rippleDecay = float(styleSource_->rippleDecay()) / 100.0F;
    next.colors[0] = colorVector(styleSource_->baseColor());
    next.colors[1] = colorVector(styleSource_->coolColor());
    next.colors[2] = colorVector(styleSource_->warmColor());
    next.colors[3] = colorVector(styleSource_->accentColor());
    next.colors[4] = colorVector(styleSource_->peakColor());
    next.colorMode = static_cast<RenderColorMode>(styleSource_->colorMode());
    const QVariantList gains = styleSource_->visualEqGains();
    const QVariantList enabled = styleSource_->visualEqEnabled();
    for (int index = 0; index < 8; ++index)
        next.visualEqEnabled[std::size_t(index)] = enabled.at(index).toBool();
    for (int index = 0; index < 8; ++index) {
        next.visualEqGains[std::size_t(index)] = index < gains.size()
            ? finiteUnit(float(gains.at(index).toDouble()) / 100.0F, 0.5F)
            : 0.5F;
    }
    next.terrainAmplitude = float(styleSource_->terrainAmplitude()) / 100.0F;
    next.motionResponse = float(styleSource_->motionResponse()) / 100.0F;
    next.gradientLayers = float(styleSource_->gradientLayers()) / 100.0F;
    next.glowIntensity = float(styleSource_->glowIntensity()) / 100.0F;
    next.cinemaShake = finiteOr(float(styleSource_->cinemaShake()),
                                next.cinemaShake);
    next.autoRotate = float(styleSource_->autoRotate()) / 100.0F;
    next.peakBoost = float(styleSource_->peakBoost()) / 100.0F;
    next.inputCompression = float(styleSource_->inputCompression()) / 100.0F;
    next.audioResponse = float(styleSource_->audioResponse()) / 100.0F;
    next.responseRange = float(styleSource_->responseRange()) / 100.0F;
    next.centerHighlight = float(styleSource_->centerHighlight()) / 100.0F;
    next.rhythmStrength = float(styleSource_->rhythmStrength()) / 100.0F;
    next.depthOfField = float(styleSource_->depthOfField()) / 100.0F;
    next.subjectClarity = float(styleSource_->subjectClarity()) / 100.0F;
    next.autoRotateSpeed = float(styleSource_->autoRotateSpeed()) / 100.0F;
    next.rhythmSensitivity = float(styleSource_->rhythmSensitivity()) / 100.0F;
    next.ripplesEnabled = styleSource_->ripplesEnabled();
    next.burstEnabled = styleSource_->burstEnabled();
    next.floatingCubesEnabled = styleSource_->floatingCubesEnabled();
    next.floatingBlockMinSize = styleSource_->floatingBlockMinSize();
    next.floatingBlockMaxSize = styleSource_->floatingBlockMaxSize();
    next.floatingBlockSpeed = styleSource_->floatingBlockSpeed();
    next.floatingBlockIntensity = styleSource_->floatingBlockIntensity();
    next.meteorsEnabled = styleSource_->meteorsEnabled();
    next.idleBreathingEnabled = styleSource_->idleBreathingEnabled();
    // Theme rotation is presentation orchestration in QML. It must not also
    // enable the legacy per-frame shader hue cycle.
    next.themeCycleEnabled = false;
    next.streamHighlightEnabled = styleSource_->streamHighlightEnabled();
    using namespace agplayer::immersive;
    const QByteArray themeId = styleSource_->themeId().toUtf8();
    const auto* theme = findBuiltInTheme(std::string_view(themeId.constData(),
                                                        std::size_t(themeId.size())));
    if (theme || themeId == "custom") {
        // Canonical shader consumes half of the original amplitude multiplier.
        // Preserve the stored UI percentage and the custom-material contract.
        if (next.terrainAmplitude > 0.5F) {
            const float upper = (next.terrainAmplitude - 0.5F) * 2.0F;
            next.terrainAmplitude = (1.0F + upper * upper * 14.0F) * 0.5F;
        }
        const auto encodedRole = [theme](ThemeColorRole role) {
            const auto value = workingLinearToSrgb(toWorkingLinear(theme->colors[themeColorIndex(role)]));
            return QVector4D(value.red, value.green, value.blue, 1.0F);
        };
        if (theme) {
            next.colors = {encodedRole(ThemeColorRole::BasePrimary),
                       encodedRole(ThemeColorRole::CoolCore),
                       encodedRole(ThemeColorRole::WarmCore),
                       encodedRole(ThemeColorRole::CoolEdge),
                       encodedRole(ThemeColorRole::WarmEdge)};
            next.bodyColor = encodedRole(ThemeColorRole::BaseSecondary);
            // Authored Violet Heart keeps purple surfaces; its pink/yellow
            // warm roles illuminate only the raised central interior.
            if (themeId == "violet-heart") next.bodyColor.setW(2.0F);
            next.rippleColor = encodedRole(ThemeColorRole::Ripple);
            next.atmosphereColor = encodedRole(ThemeColorRole::Fog);
        } else {
            // Five independent user roles share the canonical shader/audio
            // path: body, beat core, ripple, hot edge, and environment.
            next.rippleColor = next.colors[3];
            next.atmosphereColor = next.colors[0];
            const auto blendLinear = [](QVector4D a, QVector4D b, float weight) {
                QVector4D result(0, 0, 0, 1);
                for (int channel = 0; channel < 3; ++channel)
                    result[channel] = workingLinearChannelToSrgb(
                        srgbChannelToLinear(a[channel]) * (1.0F - weight)
                        + srgbChannelToLinear(b[channel]) * weight);
                return result;
            };
            next.bodyColor = blendLinear(next.colors[0], next.colors[1], .06F);
            next.colors[3] = blendLinear(next.colors[1], next.colors[0], .35F);
        }
        next.topographyDensity = styleSource_->topographyDensity();
        // Keep the theme's authored glow as the baseline while preserving the
        // user-facing surface-sheen slider as a live multiplier.
        next.glowIntensity = float(styleSource_->themeGlow())
            * float(styleSource_->glowIntensity()) / 100.0F;
        next.colorMode = RenderColorMode::MultiRegion;
        next.themeCycleEnabled = false;
    }
    renderStyle_ = next;
    if (auto* visual = qobject_cast<AudioVisualFeatureController*>(featureSource_))
        visual->setVisualKickSensitivity(styleSource_->rhythmSensitivity());
    copyFeatureSource();
    ++styleRevision_;
    emit styleRevisionChanged();
    scheduleIfRunnable();
}

void TerrainReactorItem::copyFeatureSource()
{
    if (featureSource_ == nullptr) return;
    if (styleSource_ && !styleSource_->themeId().isEmpty()
        && qobject_cast<AudioVisualFeatureController*>(featureSource_)) {
        copyVisualSource();
        return;
    }
    AudioFeatures next;
    const QVariantList bands = featureSource_->property("bands").toList();
    for (int index = 0; index < 8; ++index) {
        next.bands[std::size_t(index)] = index < bands.size()
            ? finiteUnit(float(bands.at(index).toDouble())) : 0.0F;
    }
    next.energy = finiteUnit(float(featureSource_->property("energy").toDouble()));
    next.spectralFlux = finiteUnit(float(featureSource_
        ->property("spectralFlux").toDouble()));
    next.kick = featureSource_->property("kickPulse").toBool() ? 1.0F : 0.0F;
    next.snare = featureSource_->property("snarePulse").toBool() ? 1.0F : 0.0F;
    if (featureSourceProvidesBeat_) {
        const quint64 revision = featureSource_->property("beatRevision")
            .toULongLong();
        if (revision > pendingBeat_.revision) {
            pendingBeat_.revision = revision;
            pendingBeat_.strength = finiteUnit(float(featureSource_
                ->property("beatStrength").toDouble()));
            emit beatChanged();
        }
    }
    if (featureSourceProvidesImpact_) {
        const quint64 revision = featureSource_->property("impactRevision")
            .toULongLong();
        if (revision > pendingImpact_.revision) {
            pendingImpact_.revision = revision;
            pendingImpact_.strength = finiteUnit(float(featureSource_
                ->property("impactStrength").toDouble()));
            emit impactChanged();
        }
    }
    liveFeatures_ = next;
    if (!useSyntheticFeatures_) applyCurrentFeatures(liveFeatures_);
}

TerrainReactorItem::ReferenceAudioFrame TerrainReactorItem::advanceReferenceAudioFrame(
    agplayer::VisualAudioFrameAnalyzer& analyzer, agplayer::VisualTerrainResponse& response,
    const RenderSnapshot& snapshot, double wallDelta, agplayer::VisualSnareTrigger& snare)
{
    // MapScene uses actual frame delta for terrain, including the first frame.
    // The audio analyzer independently owns Kick's first-frame and .25s limits.
    const auto& audio = analyzer.process(snapshot.pcm, wallDelta,
        qRound(snapshot.style.rhythmSensitivity * 100.0F));
    if (!audio.valid) response.reset();
    agplayer::VisualTerrainResponse::EqBands eq;
    for (std::size_t i = 0; i < eq.size(); ++i) eq[i] = snapshot.style.visualEqGains[i];
    const auto& terrain = response.update(audio.descriptors, audio.kick.envelope, eq,
        snapshot.style.visualEqEnabled, wallDelta, snapshot.style.motionResponse * 100.0);
    const auto snareOutput = snapshot.pcm.paused ? snare.suspend()
        : snare.process(audio.spectrum, audio.valid);
    return {audio, terrain, snareOutput, audio.kick.onset > 0 ? 1 : 0,
            audio.kick.onset > 0 ? audio.kick.confidence : 0.0,
            audio.pulse.triggered ? 1 : 0,
            audio.pulse.triggered ? audio.pulse.strength : 0.0, audio.meteor};
}

TerrainReactorItem::ReferenceAudioFrame TerrainReactorItem::advanceReferenceAudioFrames(
    agplayer::VisualAudioFrameAnalyzer& analyzer, agplayer::VisualTerrainResponse& response,
    const RenderSnapshot& snapshot, double wallDelta, agplayer::VisualSnareTrigger& snare,
    std::uint64_t& consumedSequence)
{
    int beatCount = 0;
    double beatStrength = 0.0;
    int pulseCount = 0;
    double pulseStrength = 0.0;
    agplayer::VisualSnareTrigger::Output strongestSnare;
    agplayer::VisualSnareTrigger::Output strongestMeteor;
    const agplayer::VisualAudioFrameAnalyzer::Frame* finalAudio = nullptr;
    const agplayer::VisualSpectrumFeatures::Features* finalTerrain = nullptr;
    const auto analyzePcm = [&](const agplayer::VisualAudioFrameAnalyzer::Snapshot& pcm,
                                double delta) {
        const auto& audio = analyzer.process(pcm, delta,
            qRound(snapshot.style.rhythmSensitivity * 100.0F));
        if (!audio.valid) response.reset();
        agplayer::VisualTerrainResponse::EqBands eq;
        for (std::size_t i = 0; i < eq.size(); ++i)
            eq[i] = snapshot.style.visualEqGains[i];
        const auto& terrain = response.update(audio.descriptors, audio.kick.envelope, eq,
            snapshot.style.visualEqEnabled, delta, snapshot.style.motionResponse * 100.0);
        const auto snareOutput = snare.process(audio.spectrum, audio.valid);
        return ReferenceAudioFrame{audio, terrain, snareOutput,
            audio.kick.onset > 0 ? 1 : 0,
            audio.kick.onset > 0 ? audio.kick.confidence : 0.0,
            audio.pulse.triggered ? 1 : 0,
            audio.pulse.triggered ? audio.pulse.strength : 0.0, audio.meteor};
    };

    for (std::size_t index = 0; index < snapshot.pcmBatch.count; ++index) {
        const auto& pcm = snapshot.pcmBatch.frames[index];
        if (pcm.sequence <= consumedSequence) continue;
        double dt = wallDelta;
        if (index > 0) {
            const auto& previous = snapshot.pcmBatch.frames[index - 1];
            if (previous.epoch == pcm.epoch && previous.sampleRate == pcm.sampleRate
                && pcm.firstSampleIndex > previous.firstSampleIndex) {
                dt = double(pcm.firstSampleIndex - previous.firstSampleIndex)
                    / double(pcm.sampleRate);
            }
        }
        const auto analyzed = analyzePcm(pcm, dt);
        finalAudio = &analyzed.audio;
        finalTerrain = &analyzed.terrain;
        beatCount += analyzed.beatCount;
        beatStrength = std::max(beatStrength, analyzed.beatStrength);
        pulseCount += analyzed.pulseCount;
        pulseStrength = std::max(pulseStrength, analyzed.pulseStrength);
        if (analyzed.meteor.triggered) strongestMeteor = analyzed.meteor;
        if (analyzed.snare.triggered
            && analyzed.snare.strength >= strongestSnare.strength) {
            strongestSnare = analyzed.snare;
        }
        consumedSequence = pcm.sequence;
    }

    if (finalAudio == nullptr) {
        const bool needsAnalysis = !snapshot.pcm.valid || snapshot.pcm.paused
            || snapshot.pcm.sequence == 0
            || snapshot.pcm.sequence > consumedSequence;
        if (needsAnalysis) {
            const auto frame = advanceReferenceAudioFrame(analyzer, response, snapshot,
                                                           wallDelta, snare);
            finalAudio = &frame.audio;
            finalTerrain = &frame.terrain;
            beatCount = frame.beatCount;
            beatStrength = frame.beatStrength;
            strongestSnare = frame.snare;
            pulseCount = frame.pulseCount;
            pulseStrength = frame.pulseStrength;
            strongestMeteor = frame.meteor;
            if (snapshot.pcm.sequence > consumedSequence)
                consumedSequence = snapshot.pcm.sequence;
        } else {
            // A late GUI handoff is not a new audio observation. Hold the last
            // continuous terrain state until the next monotonic PCM window;
            // reprocessing the same samples can synthesize a falling-flux peak.
            finalAudio = &analyzer.frame();
            finalTerrain = &response.features();
        }
    }
    return {*finalAudio, *finalTerrain, strongestSnare, beatCount, beatStrength,
            pulseCount, pulseStrength, strongestMeteor};
}

void TerrainReactorItem::restoreRenderAudioEvents(BeatEvent& beat, ImpactEvent& impact,
    const RenderSnapshot& snapshot, const RendererResourceState& resources)
{
    // A consumed render event may never have reached the GUI before a reset.
    // Retain the persistent floor across both resume and renderer recreation.
    beat.revision = std::max({beat.revision, snapshot.beatEvent.revision,
                             resources.consumedBeatRevision()});
    impact.revision = std::max({impact.revision, snapshot.impactEvent.revision,
                               resources.consumedImpactRevision()});
    beat.strength = 0;
    impact.strength = 0;
}

void TerrainReactorItem::applyRenderAudioFrame(const AudioFeatures& features,
    const AudioFrameOrigin& origin, const BeatEvent& beat, const ImpactEvent& impact)
{
    if (visualResetRevision_ != origin.visualResetRevision
        || activityRevision_ != origin.activityRevision
        || styleRevision_ != origin.styleRevision
        || !styleSource_ || styleSource_->themeId().isEmpty()
        || useSyntheticFeatures_ || !renderingRequested()) return;
    liveFeatures_ = features;
    if (pendingBeat_.revision != beat.revision) {
        pendingBeat_ = beat;
        emit beatChanged();
    }
    if (pendingImpact_.revision != impact.revision) {
        pendingImpact_ = impact;
        emit impactChanged();
    }
    ++featureRevision_;
    emit featureRevisionChanged();
}

void TerrainReactorItem::copyVisualSource()
{
    // PCM collection and GUI feature notifications only request a frame. The
    // blocked-GUI synchronize phase copies PCM; render owns every analysis step.
    if (!useSyntheticFeatures_) scheduleIfRunnable();
}

void TerrainReactorItem::applyCurrentFeatures(const AudioFeatures& features)
{
    const float punch = mapVisualParameters(features,
        float(clock_.elapsed()) / 1000.0F, renderStyle_).cameraPunch;
    if (punch > 0.0F) {
        pendingPunch_.strength = punch;
        ++pendingPunch_.revision;
        emit cameraChanged();
    }
    if ((useSyntheticFeatures_ || !featureSourceProvidesBeat_)
        && punch > 0.0F) {
        pendingBeat_.strength = punch;
        ++pendingBeat_.revision;
        emit beatChanged();
    }
    if ((useSyntheticFeatures_ || !featureSourceProvidesImpact_)
        && punch > 0.0F) {
        pendingImpact_.strength = punch;
        ++pendingImpact_.revision;
        emit impactChanged();
    }
    ++featureRevision_;
    emit featureRevisionChanged();
    scheduleIfRunnable();
}

void TerrainReactorItem::scheduleIfRunnable()
{
    const bool running = renderingRequested();
    const int interval = quality_ == Quality::Eco ? 33
        : quality_ == Quality::Balanced ? 22 : 16;
    if (renderTick_.interval() != interval) renderTick_.setInterval(interval);
    const bool frameDriven = renderStyle_.rippleColor.w() > .5F && quality_ == Quality::High;
    if (running && !frameDriven && !renderTick_.isActive()) renderTick_.start();
    else if (!running || frameDriven) renderTick_.stop();
    if (running != lastScheduledRunning_) {
        lastScheduledRunning_ = running;
        ++activityRevision_;
        update();
    }
}

void TerrainReactorItem::applyInternalScale(float scale, int sampleCount)
{
    const float bounded = std::clamp(scale, 0.5F, 1.0F);
    if (!qFuzzyCompare(internalScale_, bounded)) {
        internalScale_ = bounded;
        updateColorBufferSize();
    }
    const int boundedSamples = sampleCount == 1 ? 1 : 4;
    if (this->sampleCount() != boundedSamples) {
        setSampleCount(boundedSamples);
    }
}

void TerrainReactorItem::updateColorBufferSize()
{
    if (internalScale_ >= 0.999F) {
        setFixedColorBufferWidth(0);
        setFixedColorBufferHeight(0);
        return;
    }
    const qreal dpr = window() != nullptr
        ? window()->effectiveDevicePixelRatio() : 1.0;
    setFixedColorBufferWidth(std::max(1, qRound(width() * dpr * internalScale_)));
    setFixedColorBufferHeight(std::max(1, qRound(height() * dpr * internalScale_)));
}

void TerrainReactorItem::updateWindowState(QQuickWindow* window)
{
    if (trackedWindow_ != nullptr) trackedWindow_->removeEventFilter(this);
    if (windowVisibilityConnection_) disconnect(windowVisibilityConnection_);
    trackedWindow_ = window;
    if (window == nullptr) {
        windowExposed_ = true;
    } else {
        // Only the dedicated immersive host opts out of hidden-window GPU
        // caching. Never change the normal player's scene-graph policy.
        if (window->objectName() == QStringLiteral("immersiveVisualWindow")) {
            window->setPersistentSceneGraph(false);
            window->setPersistentGraphics(false);
        }
        window->installEventFilter(this);
        const auto refresh = [this] { refreshWindowExposure(); };
        windowVisibilityConnection_ = connect(window, &QWindow::visibilityChanged,
                                               this, refresh);
        refreshWindowExposure();
    }
    emit renderingRequestedChanged();
    scheduleIfRunnable();
}

bool TerrainReactorItem::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == trackedWindow_ && event != nullptr) {
        switch (event->type()) {
        case QEvent::Expose:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::WindowStateChange:
            QMetaObject::invokeMethod(this,
                &TerrainReactorItem::refreshWindowExposure,
                Qt::QueuedConnection);
            break;
        default:
            break;
        }
    }
    return QQuickRhiItem::eventFilter(watched, event);
}

void TerrainReactorItem::refreshWindowExposure()
{
    QQuickWindow* const window = trackedWindow_;
    const bool exposed = window == nullptr
        || (window->isExposed()
            && window->visibility() != QWindow::Hidden
            && window->visibility() != QWindow::Minimized);
    if (window != nullptr && window->rendererInterface()->graphicsApi()
        == QSGRendererInterface::Software) {
        reportRenderStatus(RenderStatus::SoftwareBackend,
            QStringLiteral("Terrain Reactor is disabled on the software scene graph"));
    }
    if (windowExposed_ == exposed) return;
    windowExposed_ = exposed;
    emit renderingRequestedChanged();
    scheduleIfRunnable();
}

void TerrainReactorItem::reportRenderStatus(RenderStatus status,
                                             const QString& diagnostic)
{
    if (renderStatus_ == RenderStatus::SoftwareBackend
        && status == RenderStatus::Ready) {
        return;
    }
    if (renderStatus_ == status && diagnostic_ == diagnostic) return;
    const bool wasRequested = renderingRequested();
    renderStatus_ = status;
    diagnostic_ = diagnostic;
    emit renderStatusChanged();
    if (wasRequested != renderingRequested()) {
        emit renderingRequestedChanged();
    }
}
