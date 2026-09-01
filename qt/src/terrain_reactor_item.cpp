#include "terrain_reactor_item.hpp"

#include "audio_visual_feature_controller.hpp"
#include "player_experience_controller.hpp"

#include <QColor>
#include <QEvent>
#include <QFile>
#include <QMatrix4x4>
#include <QMetaMethod>
#include <QMetaObject>
#include <QQuickWindow>
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

constexpr int cubeVertexCount = 24;
constexpr int cubeIndexCount = 36;

struct Vertex {
    float position[3];
    float normal[3];
};

struct GpuInstance {
    float position[3];
    float scale[3];
    float data[4];
};

struct alignas(16) UniformBlock {
    float mvp[16]{};
    float bandsLow[4]{};
    float bandsHigh[4]{};
    float parameters[4]{}; // energy, flux, ripple, time
    float effects[4]{}; // particles, meteors, punch, fog
    float colors[5][4]{}; // base, cool, warm, accent, peak
    float equalizerLow[4]{};
    float equalizerHigh[4]{};
    float styleParameters[4]{}; // amplitude, motion, glow, cinema
    float styleDynamics[4]{}; // rotate, peak, color mode, gradient
    float styleToggles[4]{}; // ripples, cubes, meteors, breathing
    float styleExtra[4]{}; // theme cycle, burst, stream highlight, reserved
    float styleAudio[4]{}; // compression, response, range, center highlight
    float stylePresentation[4]{}; // rhythm, depth, clarity, rotation speed
    float impact[4]{}; // strength, age, active, sensitivity
    float waveSources[8][4]{}; // stage x/z, normalized phase, strength
    float audioEnvelope[4]{}; // fast bass, slow bass, reserved
};

static_assert(alignof(UniformBlock) == 16);
static_assert(sizeof(UniformBlock) % 16 == 0);

constexpr quint32 maximumInstances = 192U * 192U + 120U
    + 28U * (1U + 3U + 16U + 12U) + 180U;

constexpr std::array<Vertex, cubeVertexCount> cubeVertices{{
    {{-0.5F, -0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{ 0.5F, -0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{ 0.5F,  0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{-0.5F,  0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{ 0.5F, -0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{-0.5F, -0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{-0.5F,  0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{ 0.5F,  0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{-0.5F, -0.5F, -0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{-0.5F, -0.5F,  0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{-0.5F,  0.5F,  0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{-0.5F,  0.5F, -0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{ 0.5F, -0.5F,  0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{ 0.5F, -0.5F, -0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{ 0.5F,  0.5F, -0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{ 0.5F,  0.5F,  0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{-0.5F,  0.5F,  0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{ 0.5F,  0.5F,  0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{ 0.5F,  0.5F, -0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{-0.5F,  0.5F, -0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{-0.5F, -0.5F, -0.5F}, { 0.0F, -1.0F,  0.0F}},
    {{ 0.5F, -0.5F, -0.5F}, { 0.0F, -1.0F,  0.0F}},
    {{ 0.5F, -0.5F,  0.5F}, { 0.0F, -1.0F,  0.0F}},
    {{-0.5F, -0.5F,  0.5F}, { 0.0F, -1.0F,  0.0F}},
}};

constexpr std::array<quint16, cubeIndexCount> cubeIndices{{
     0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
     8,  9, 10,  8, 10, 11, 12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
}};

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

        const bool targetChanged = renderTarget() != lastRenderTarget_;
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
        if (!snapshot_.running && next.running) frameTimer_.restart();
        if (next.running || failed_) publishStatus();
        const bool seedChanged = next.seed != snapshot_.seed;
        const bool layoutChanged = seedChanged
            || next.quality != snapshot_.quality
            || next.styleRevision != snapshot_.styleRevision
            || quality_.stage() != lastStage_;
        const TrackPalette nextPalette = next.trackPaletteActive
            ? next.trackPalette : next.style.colors;
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
        if (!framePacer_.shouldRender(snapshot_.timeSeconds, targetFps)) {
            update();
            return;
        }

        const qint64 elapsedNanoseconds = frameTimer_.nsecsElapsed();
        frameTimer_.restart();
        const double wallElapsedSeconds = std::max(0.0,
            static_cast<double>(elapsedNanoseconds) / 1'000'000'000.0);
        const double animationElapsedSeconds = std::clamp(
            wallElapsedSeconds, 0.001, 0.25);
        if (paletteProgress_ < 1.0F) {
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
        VisualParameters visual = mapVisualParameters(snapshot_.features,
            snapshot_.timeSeconds, snapshot_.style);
        bassEnvelope_.advance(visual.bands[0] * 0.72F
                                  + visual.bands[1] * 0.28F,
                              float(animationElapsedSeconds));
        punchEvents_.consume(snapshot_.punchEvent, camera_);
        impactEvents_.consume(snapshot_.impactEvent, snapshot_.timeSeconds);
        const ImpactPulseSnapshot impact = impactEvents_.snapshot(
            snapshot_.timeSeconds);
        visual.impactStrength = impact.strength;
        visual.impactAge = impact.age;
        const RenderDynamics dynamics = mapRenderDynamics(snapshot_.style);
        camera_.advance(snapshot_.timeSeconds, float(animationElapsedSeconds),
                        dynamics.autoRotateSpeed * snapshot_.style.motionResponse);
        UniformBlock uniforms = buildUniforms(visual, camera_.snapshot());
        QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();
        updates->updateDynamicBuffer(uniformBuffer_.get(), 0,
                                     sizeof(UniformBlock), &uniforms);
        if (instancesDirtyUpload_) {
            updates->updateDynamicBuffer(instanceBuffer_.get(), 0,
                static_cast<quint32>(instances_.size() * sizeof(GpuInstance)),
                instances_.constData());
            instancesDirtyUpload_ = false;
        }

        if (pendingStaticUploads_) {
            commandBuffer->resourceUpdate(
                std::exchange(pendingStaticUploads_, nullptr));
        }

        commandBuffer->beginPass(renderTarget(), QColor(4, 6, 11, 0),
                                 {1.0F, 0}, updates);
        commandBuffer->setGraphicsPipeline(pipeline_.get());
        commandBuffer->setShaderResources(bindings_.get());
        const QSize size = renderTarget()->pixelSize();
        commandBuffer->setViewport(QRhiViewport(0.0F, 0.0F,
                                                float(size.width()),
                                                float(size.height())));
        const QRhiCommandBuffer::VertexInput bindings[] = {
            {vertexBuffer_.get(), 0}, {instanceBuffer_.get(), 0}
        };
        commandBuffer->setVertexInput(0, 2, bindings, indexBuffer_.get(), 0,
                                      QRhiCommandBuffer::IndexUInt16);
        commandBuffer->drawIndexed(cubeIndexCount,
                                   static_cast<quint32>(instances_.size()));
        commandBuffer->endPass();

        const double workMilliseconds = static_cast<double>(workTimer.nsecsElapsed())
            / 1'000'000.0;
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
        if (snapshot_.running) update();
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
        instanceBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
            QRhiBuffer::VertexBuffer, maximumInstances * sizeof(GpuInstance)));
        uniformBuffer_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
            QRhiBuffer::UniformBuffer, sizeof(UniformBlock)));
        if (!vertexBuffer_->create() || !indexBuffer_->create()
            || !instanceBuffer_->create() || !uniformBuffer_->create()) {
            return false;
        }

        bindings_.reset(rhi()->newShaderResourceBindings());
        bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            uniformBuffer_.get())});
        if (!bindings_->create()) return false;

        pipeline_.reset(rhi()->newGraphicsPipeline());
        pipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, vertexShader},
            {QRhiShaderStage::Fragment, fragmentShader},
        });
        QRhiVertexInputLayout layout;
        layout.setBindings({
            {sizeof(Vertex)},
            {sizeof(GpuInstance), QRhiVertexInputBinding::PerInstance},
        });
        layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position)},
            {0, 1, QRhiVertexInputAttribute::Float3, offsetof(Vertex, normal)},
            {1, 2, QRhiVertexInputAttribute::Float3, offsetof(GpuInstance, position)},
            {1, 3, QRhiVertexInputAttribute::Float3, offsetof(GpuInstance, scale)},
            {1, 4, QRhiVertexInputAttribute::Float4, offsetof(GpuInstance, data)},
        });
        pipeline_->setVertexInputLayout(layout);
        pipeline_->setShaderResourceBindings(bindings_.get());
        pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        pipeline_->setSampleCount(renderTarget()->sampleCount());
        pipeline_->setCullMode(QRhiGraphicsPipeline::Back);
        pipeline_->setDepthTest(true);
        pipeline_->setDepthWrite(true);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline_->setTargetBlends({blend});
        if (!pipeline_->create()) return false;

        QRhiResourceUpdateBatch* uploads = rhi()->nextResourceUpdateBatch();
        uploads->uploadStaticBuffer(vertexBuffer_.get(), cubeVertices.data());
        uploads->uploadStaticBuffer(indexBuffer_.get(), cubeIndices.data());
        // initialize() receives a command buffer, but the first render pass can
        // safely consume this batch together with the dynamic frame uploads.
        pendingStaticUploads_ = uploads;
        lastRenderTarget_ = renderTarget();
        instancesDirty_ = true;
        failed_ = false;
        return true;
    }

    void buildInstancesIfNeeded()
    {
        if (!instancesDirty_) return;
        QualityConfiguration config = quality_.configuration();
        switch (snapshot_.quality) {
        case TerrainReactorItem::Quality::Eco:
            config.gridSize = std::min(config.gridSize, 96);
            config.floatingCount = std::min(config.floatingCount, 36);
            config.particleCount = std::min(config.particleCount, 52);
            config.meteorCount = std::min(config.meteorCount, 5);
            config.rippleCount = std::min(config.rippleCount, 4);
            config.internalScale = std::min(config.internalScale, 0.75F);
            break;
        case TerrainReactorItem::Quality::Balanced:
            break;
        case TerrainReactorItem::Quality::High:
            if (quality_.stage() < DegradationStage::ReducedGrid) {
                config.gridSize = 160;
                config.internalScale = 1.0F;
            }
            break;
        }
        if (!snapshot_.style.floatingCubesEnabled) config.floatingCount = 0;
        if (!snapshot_.style.meteorsEnabled) config.meteorCount = 0;
        if (!snapshot_.style.ripplesEnabled) config.rippleCount = 0;
        const SceneLayout layout = makeSceneLayout(snapshot_.seed,
            config.gridSize, config.floatingCount, config.meteorCount,
            config.particleCount);
        instances_.clear();
        instances_.reserve(layout.terrain.size() + layout.floating.size()
                           + layout.meteors.size() + layout.meteorTrails.size()
                           + layout.collisionRipples.size()
                           + layout.collisionParticles.size()
                           + layout.particles.size());
        for (const auto& instance : layout.terrain) {
            instances_.append(toGpuInstance(instance, 0.0F));
        }
        for (const auto& instance : layout.floating) {
            instances_.append(toGpuInstance(instance, 1.0F));
        }
        for (const auto& instance : layout.meteors) {
            instances_.append(toGpuInstance(instance, 2.0F));
        }
        for (const auto& instance : layout.meteorTrails) {
            instances_.append(toGpuInstance(instance, 4.0F));
        }
        for (const auto& instance : layout.collisionRipples) {
            instances_.append(toGpuInstance(instance, 5.0F));
        }
        for (const auto& instance : layout.collisionParticles) {
            instances_.append(toGpuInstance(instance, 6.0F));
        }
        for (const auto& instance : layout.particles) {
            instances_.append(toGpuInstance(instance, 3.0F));
        }
        if (quint32(instances_.size()) > maximumInstances) {
            qWarning("TerrainReactorItem instance capacity exceeded");
            instances_.resize(qsizetype(maximumInstances));
        }
        instancesDirty_ = false;
        instancesDirtyUpload_ = true;
        currentRippleCount_ = config.rippleCount;
        if (!qFuzzyCompare(currentInternalScale_, config.internalScale)) {
            currentInternalScale_ = config.internalScale;
            TerrainReactorItem* const target = item_;
            QMetaObject::invokeMethod(target, [target, scale = currentInternalScale_] {
                target->applyInternalScale(scale);
            }, Qt::QueuedConnection);
        }
    }

    UniformBlock buildUniforms(const VisualParameters& visual,
                               const CameraSnapshot& camera)
    {
        // Manual camera deltas come from the GUI snapshot. The punch envelope
        // is a revisioned event owned and advanced by the render thread.
        QMatrix4x4 projection;
        const QSize size = renderTarget()->pixelSize();
        const float aspect = size.height() > 0
            ? float(size.width()) / float(size.height()) : 1.0F;
        const CameraSnapshot defaults;
        const float yaw = finiteOr(camera.yaw, defaults.yaw);
        const float pitch = std::clamp(
            finiteOr(camera.pitch, defaults.pitch), 0.12F, 1.15F);
        const float distance = std::clamp(
            finiteOr(camera.distance, defaults.distance), 42.0F, 220.0F);
        const float punch = finiteUnit(camera.punch);
        projection.perspective(48.0F - punch * 2.15F,
                               aspect, 0.1F, 400.0F);
        const float radius = distance - punch * 0.6F;
        const RenderDynamics dynamics = mapRenderDynamics(snapshot_.style);
        const float lowAngleLift = 0.72F + dynamics.depthOfField * 0.08F;
        QVector3D eye(radius * std::cos(pitch) * std::sin(yaw),
                      9.0F + radius * std::sin(pitch) * lowAngleLift,
                      radius * std::cos(pitch) * std::cos(yaw));
        const float shake = snapshot_.style.cinemaShake
            * (visual.spectralFlux * 0.22F + punch * 0.12F);
        eye += QVector3D(std::sin(visual.timeSeconds * 21.0F) * shake,
                        std::cos(visual.timeSeconds * 17.0F) * shake * 0.55F,
                        std::sin(visual.timeSeconds * 13.0F) * shake * 0.7F);
        QMatrix4x4 view;
        view.lookAt(eye, QVector3D(0.0F, -1.5F, 0.0F),
                    QVector3D(0.0F, 1.0F, 0.0F));
        const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * projection * view;

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
        std::copy_n(snapshot_.style.visualEqGains.begin(), 4,
                    result.equalizerLow);
        std::copy_n(snapshot_.style.visualEqGains.begin() + 4, 4,
                    result.equalizerHigh);
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
        result.styleParameters[2] = snapshot_.style.glowIntensity;
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
        result.styleAudio[0] = dynamics.inputCompression;
        result.styleAudio[1] = dynamics.audioResponse;
        result.styleAudio[2] = dynamics.responseRadius;
        result.styleAudio[3] = dynamics.centerHighlight;
        result.stylePresentation[0] = dynamics.rhythmStrength;
        result.stylePresentation[1] = dynamics.depthOfField;
        result.stylePresentation[2] = dynamics.subjectClarity;
        result.stylePresentation[3] = dynamics.autoRotateSpeed;
        result.impact[0] = visual.impactStrength;
        result.impact[1] = visual.impactAge;
        result.impact[2] = visual.impactStrength > 0.001F ? 1.0F : 0.0F;
        result.impact[3] = dynamics.rhythmSensitivity;
        for (std::size_t index = 0; index < waveSources_.size(); ++index) {
            const QVector4D& source = waveSources_[index];
            result.waveSources[index][0] = source.x();
            result.waveSources[index][1] = source.y();
            result.waveSources[index][2] = source.z();
            result.waveSources[index][3] = source.w();
        }
        const BassEnvelopeSnapshot envelope = bassEnvelope_.snapshot();
        result.audioEnvelope[0] = envelope.fast;
        result.audioEnvelope[1] = envelope.slow;
        return result;
    }

    void releaseResources()
    {
        telemetry_->stableRenderedFrames.store(0, std::memory_order_release);
        telemetry_->renderedFeatureRevision.store(0, std::memory_order_release);
        telemetry_->renderedStyleRevision.store(0, std::memory_order_release);
        if (pendingStaticUploads_ != nullptr) {
            pendingStaticUploads_->release();
            pendingStaticUploads_ = nullptr;
        }
        pipeline_.reset();
        bindings_.reset();
        uniformBuffer_.reset();
        instanceBuffer_.reset();
        indexBuffer_.reset();
        vertexBuffer_.reset();
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
    CameraMotion camera_;
    PunchEventConsumer punchEvents_;
    ImpactEventConsumer impactEvents_;
    BassEnvelopeFollower bassEnvelope_;
    MultiWaveSources waveSources_{};
    bool waveSourcesInitialized_ = false;
    TrackPalette currentPalette_{};
    TrackPalette fromPalette_{};
    TrackPalette targetPalette_{};
    float paletteProgress_ = 1.0F;
    bool paletteInitialized_ = false;
    CameraSnapshot previousGuiCamera_;
    bool cameraSynchronized_ = false;
    quint64 syncedCameraRevision_ = std::numeric_limits<quint64>::max();
    TerrainReactorItem* item_ = nullptr;
    QElapsedTimer frameTimer_;
    QElapsedTimer counterTimer_;
    FramePacer framePacer_;
    QVector<GpuInstance> instances_;
    bool instancesDirty_ = true;
    bool instancesDirtyUpload_ = false;
    int currentRippleCount_ = 8;
    // Mirrors QQuickRhiItem's initial full-resolution buffer. The first
    // Balanced/Auto layout must therefore schedule the 0.90 scale update.
    float currentInternalScale_ = 1.0F;
    bool failed_ = false;
    const bool softwareBackend_ = false;
    TerrainReactorItem::RenderStatus status_ = TerrainReactorItem::RenderStatus::Inactive;
    QString diagnostic_;
    QRhiRenderTarget* lastRenderTarget_ = nullptr;
    std::unique_ptr<QRhiBuffer> vertexBuffer_;
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
    clock_.start();
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

TerrainReactorItem::~TerrainReactorItem()
{
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
    if (impactConnection_) disconnect(impactConnection_);
    if (sourceDestroyedConnection_) disconnect(sourceDestroyedConnection_);
    featureSource_ = accepted;
    featureSourceProvidesImpact_ = featureSource_ != nullptr
        && featureSource_->metaObject()->indexOfProperty("impactRevision") >= 0;
    if (featureSource_ != nullptr) {
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
                emit featureSourceChanged();
            });
        copyFeatureSource();
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
    applyCurrentFeatures(enabled ? syntheticFeatures_ : liveFeatures_);
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
    result.style = renderStyle_;
    result.camera = camera_.snapshot();
    result.punchEvent = pendingPunch_;
    result.impactEvent = pendingImpact_;
    result.cameraManualUntilSeconds = camera_.manualUntilSeconds();
    result.cameraRevision = cameraRevision_;
    result.seed = deterministicSeed_;
    result.trackPaletteActive = trackPaletteSeed_ != 0U;
    result.trackPalette = trackPalette(trackPaletteSeed_);
    result.paletteRevision = paletteRevision_;
    result.quality = quality_;
    result.running = renderingRequested();
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
    next.colors[0] = colorVector(styleSource_->baseColor());
    next.colors[1] = colorVector(styleSource_->coolColor());
    next.colors[2] = colorVector(styleSource_->warmColor());
    next.colors[3] = colorVector(styleSource_->accentColor());
    next.colors[4] = colorVector(styleSource_->peakColor());
    next.colorMode = static_cast<RenderColorMode>(styleSource_->colorMode());
    const QVariantList gains = styleSource_->visualEqGains();
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
    next.meteorsEnabled = styleSource_->meteorsEnabled();
    next.idleBreathingEnabled = styleSource_->idleBreathingEnabled();
    next.themeCycleEnabled = styleSource_->themeCycleEnabled();
    next.streamHighlightEnabled = styleSource_->streamHighlightEnabled();
    renderStyle_ = next;
    ++styleRevision_;
    emit styleRevisionChanged();
    scheduleIfRunnable();
}

void TerrainReactorItem::copyFeatureSource()
{
    if (featureSource_ == nullptr) return;
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

void TerrainReactorItem::applyCurrentFeatures(const AudioFeatures& features)
{
    const float punch = mapVisualParameters(features,
        float(clock_.elapsed()) / 1000.0F, renderStyle_).cameraPunch;
    if (punch > 0.0F) {
        pendingPunch_.strength = punch;
        ++pendingPunch_.revision;
        emit cameraChanged();
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
    if (renderingRequested()) update();
}

void TerrainReactorItem::applyInternalScale(float scale)
{
    const float bounded = std::clamp(scale, 0.5F, 1.0F);
    if (qFuzzyCompare(internalScale_, bounded)) return;
    internalScale_ = bounded;
    updateColorBufferSize();
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
