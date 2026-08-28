#include "terrain_reactor_item.hpp"

#include "audio_visual_feature_controller.hpp"

#include <QColor>
#include <QFile>
#include <QMatrix4x4>
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
};

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
    result.data[3] = 1.0F;
    return result;
}

} // namespace

class TerrainReactorRenderer final : public QQuickRhiItemRenderer {
public:
    explicit TerrainReactorRenderer(std::shared_ptr<TerrainReactorItem::Telemetry> telemetry)
        : telemetry_(std::move(telemetry)), rendererId_(++nextRendererId_)
    {
        resourceState_.acquireRenderer(rendererId_);
        frameTimer_.start();
    }

    ~TerrainReactorRenderer() override
    {
        releaseResources();
        resourceState_.releaseRenderer(rendererId_);
    }

protected:
    void initialize(QRhiCommandBuffer*) override
    {
        if (!snapshot_.running) return;
        if (rhi() == nullptr || rhi()->backend() == QRhi::Null) {
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
            resourceState_.invalidateResources();
        }
        if (pipeline_) return;
        if (!createResources()) {
            fail(TerrainReactorItem::RenderStatus::ResourceError,
                 QStringLiteral("Failed to create Terrain Reactor QRhi resources"));
            return;
        }
        const quint64 generation = resourceState_.initializeResources();
        telemetry_->resourceGeneration.store(generation, std::memory_order_relaxed);
        notifyCounters();
        fail(TerrainReactorItem::RenderStatus::Ready, QString());
    }

    void synchronize(QQuickRhiItem* item) override
    {
        auto* terrainItem = static_cast<TerrainReactorItem*>(item);
        item_ = terrainItem;
        const auto next = terrainItem->snapshotForRenderer();
        const bool layoutChanged = next.seed != snapshot_.seed
            || next.quality != snapshot_.quality
            || quality_.stage() != lastStage_;
        snapshot_ = next;
        if (syncedCameraRevision_ != snapshot_.cameraRevision) {
            camera_.synchronize(snapshot_.camera,
                                snapshot_.cameraManualUntilSeconds);
            syncedCameraRevision_ = snapshot_.cameraRevision;
        }
        if (layoutChanged) instancesDirty_ = true;
    }

    void render(QRhiCommandBuffer* commandBuffer) override
    {
        if (!snapshot_.running || failed_ || !pipeline_ || commandBuffer == nullptr) {
            return;
        }

        const qint64 elapsedNanoseconds = frameTimer_.nsecsElapsed();
        frameTimer_.restart();
        const double elapsedSeconds = std::clamp(
            static_cast<double>(elapsedNanoseconds) / 1'000'000'000.0,
            0.001, 0.25);
        quality_.observe(elapsedSeconds * 1000.0, elapsedSeconds);
        if (lastStage_ != quality_.stage()) {
            lastStage_ = quality_.stage();
            instancesDirty_ = true;
        }

        buildInstancesIfNeeded();
        const VisualParameters visual = mapVisualParameters(snapshot_.features,
                                                            snapshot_.timeSeconds);
        camera_.applyBeatPunch(visual.cameraPunch);
        camera_.advance(snapshot_.timeSeconds, float(elapsedSeconds), 1.0F);
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

        commandBuffer->beginPass(renderTarget(), QColor(0, 0, 0, 255),
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

        telemetry_->frames.fetch_add(1, std::memory_order_relaxed);
        telemetry_->animations.fetch_add(1, std::memory_order_relaxed);
        telemetry_->uploads.fetch_add(1, std::memory_order_relaxed);
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
        constexpr quint32 maximumInstances = 192U * 192U + 120U + 28U + 380U;
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
            config.gridSize = std::min(config.gridSize, 112);
            config.floatingCount = std::min(config.floatingCount, 60);
            config.meteorCount = std::min(config.meteorCount, 10);
            config.rippleCount = std::min(config.rippleCount, 5);
            break;
        case TerrainReactorItem::Quality::Balanced:
            break;
        case TerrainReactorItem::Quality::High:
            if (quality_.stage() < DegradationStage::ReducedGrid) {
                config.gridSize = 192;
            }
            break;
        }
        const SceneLayout layout = makeSceneLayout(snapshot_.seed,
            config.gridSize, config.floatingCount, config.meteorCount,
            config.particleCount);
        instances_.clear();
        instances_.reserve(layout.terrain.size() + layout.floating.size()
                           + layout.meteors.size() + layout.particles.size());
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
        // Camera state is supplied by the GUI snapshot; only the audio punch
        // envelope is advanced on the render thread.
        QMatrix4x4 projection;
        const QSize size = renderTarget()->pixelSize();
        const float aspect = size.height() > 0
            ? float(size.width()) / float(size.height()) : 1.0F;
        projection.perspective(48.0F - camera.punch * 2.15F,
                               aspect, 0.1F, 400.0F);
        const float yaw = camera.yaw;
        const float pitch = camera.pitch;
        const float radius = camera.distance - camera.punch * 0.6F;
        QVector3D eye(radius * std::cos(pitch) * std::sin(yaw),
                      14.5F + radius * std::sin(pitch) * 0.66F,
                      radius * std::cos(pitch) * std::cos(yaw));
        QMatrix4x4 view;
        view.lookAt(eye, QVector3D(0.0F, 3.2F, 0.0F),
                    QVector3D(0.0F, 1.0F, 0.0F));
        const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * projection * view;

        UniformBlock result;
        std::memcpy(result.mvp, mvp.constData(), sizeof(result.mvp));
        std::copy_n(visual.bands.begin(), 4, result.bandsLow);
        std::copy_n(visual.bands.begin() + 4, 4, result.bandsHigh);
        result.parameters[0] = visual.energy;
        result.parameters[1] = visual.spectralFlux;
        result.parameters[2] = visual.rippleStrength;
        result.parameters[3] = visual.timeSeconds;
        result.effects[0] = visual.particleActivity;
        result.effects[1] = visual.meteorActivity;
        result.effects[2] = visual.cameraPunch;
        result.effects[3] = float(currentRippleCount_);
        return result;
    }

    void releaseResources()
    {
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
        resourceState_.invalidateResources();
    }

    void fail(TerrainReactorItem::RenderStatus status, const QString& message)
    {
        failed_ = status == TerrainReactorItem::RenderStatus::SoftwareBackend
            || status == TerrainReactorItem::RenderStatus::ResourceError;
        if (item_ == nullptr) return;
        TerrainReactorItem* const target = item_;
        QMetaObject::invokeMethod(target, [target, status, message] {
            target->reportRenderStatus(status, message);
        }, Qt::QueuedConnection);
    }

    void notifyCounters()
    {
        if (item_ == nullptr) return;
        QMetaObject::invokeMethod(item_, &TerrainReactorItem::countersChanged,
                                  Qt::QueuedConnection);
    }

    static std::atomic<quint64> nextRendererId_;
    std::shared_ptr<TerrainReactorItem::Telemetry> telemetry_;
    quint64 rendererId_ = 0;
    RendererResourceState resourceState_;
    AutomaticQualityController quality_;
    DegradationStage lastStage_ = DegradationStage::Full;
    TerrainReactorItem::RenderSnapshot snapshot_;
    CameraMotion camera_;
    quint64 syncedCameraRevision_ = std::numeric_limits<quint64>::max();
    TerrainReactorItem* item_ = nullptr;
    QElapsedTimer frameTimer_;
    QVector<GpuInstance> instances_;
    bool instancesDirty_ = true;
    bool instancesDirtyUpload_ = false;
    int currentRippleCount_ = 10;
    float currentInternalScale_ = 1.0F;
    bool failed_ = false;
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
    : QQuickRhiItem(parent), telemetry_(std::make_shared<Telemetry>())
{
    setSampleCount(1);
    setAlphaBlending(false);
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
    disconnect(windowVisibilityConnection_);
    disconnect(featureConnection_);
    disconnect(sourceDestroyedConnection_);
    disconnect(this, nullptr, this, nullptr);
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
    return active_ && isVisible() && hostExposed_ && windowExposed_;
}

QObject* TerrainReactorItem::featureSource() const noexcept { return featureSource_; }
void TerrainReactorItem::setFeatureSource(QObject* source)
{
    auto* typed = qobject_cast<AudioVisualFeatureController*>(source);
    if (featureSource_ == typed) return;
    if (featureConnection_) disconnect(featureConnection_);
    if (sourceDestroyedConnection_) disconnect(sourceDestroyedConnection_);
    featureSource_ = typed;
    if (featureSource_ != nullptr) {
        featureConnection_ = connect(featureSource_,
            &AudioVisualFeatureController::featuresChanged,
            this, &TerrainReactorItem::copyFeatureSource);
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

qreal TerrainReactorItem::cameraYaw() const noexcept { return camera_.snapshot().yaw; }
qreal TerrainReactorItem::cameraPitch() const noexcept { return camera_.snapshot().pitch; }
qreal TerrainReactorItem::cameraDistance() const noexcept { return camera_.snapshot().distance; }
qreal TerrainReactorItem::cameraPunch() const noexcept { return camera_.snapshot().punch; }

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
quint64 TerrainReactorItem::resourceGeneration() const noexcept
{
    return telemetry_->resourceGeneration.load(std::memory_order_relaxed);
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
            ? std::clamp(float(bands.at(index).toDouble()), 0.0F, 1.0F) : 0.0F;
    }
    next.energy = std::clamp(float(energy), 0.0F, 1.0F);
    next.spectralFlux = std::clamp(float(spectralFlux), 0.0F, 1.0F);
    next.kick = kick ? 1.0F : 0.0F;
    next.snare = snare ? 1.0F : 0.0F;
    syntheticFeatures_ = next;
    if (useSyntheticFeatures_) applyCurrentFeatures(syntheticFeatures_);
}
void TerrainReactorItem::orbitBy(qreal yawDelta, qreal pitchDelta,
                                  qreal nowSeconds)
{
    camera_.orbitBy(float(yawDelta), float(pitchDelta), double(nowSeconds));
    ++cameraRevision_;
    emit cameraChanged();
    scheduleIfRunnable();
}
void TerrainReactorItem::zoomBy(qreal wheelDelta, qreal nowSeconds)
{
    camera_.zoomBy(float(wheelDelta), double(nowSeconds));
    ++cameraRevision_;
    emit cameraChanged();
    scheduleIfRunnable();
}
void TerrainReactorItem::triggerCameraPunch(qreal strength)
{
    camera_.applyBeatPunch(float(strength));
    ++cameraRevision_;
    emit cameraChanged();
    scheduleIfRunnable();
}

QQuickRhiItemRenderer* TerrainReactorItem::createRenderer()
{
    return new TerrainReactorRenderer(telemetry_);
}

TerrainReactorItem::RenderSnapshot TerrainReactorItem::snapshotForRenderer() const
{
    RenderSnapshot result;
    result.features = useSyntheticFeatures_ ? syntheticFeatures_ : liveFeatures_;
    result.camera = camera_.snapshot();
    result.cameraManualUntilSeconds = camera_.manualUntilSeconds();
    result.cameraRevision = cameraRevision_;
    result.seed = deterministicSeed_;
    result.quality = quality_;
    result.running = renderingRequested();
    result.timeSeconds = float(clock_.elapsed()) / 1000.0F;
    result.featureRevision = featureRevision_;
    return result;
}

void TerrainReactorItem::copyFeatureSource()
{
    if (featureSource_ == nullptr) return;
    AudioFeatures next;
    const QVariantList bands = featureSource_->bands();
    for (int index = 0; index < 8; ++index) {
        next.bands[std::size_t(index)] = index < bands.size()
            ? std::clamp(float(bands.at(index).toDouble()), 0.0F, 1.0F) : 0.0F;
    }
    next.energy = std::clamp(float(featureSource_->energy()), 0.0F, 1.0F);
    next.spectralFlux = std::clamp(float(featureSource_->spectralFlux()), 0.0F, 1.0F);
    next.kick = featureSource_->kickPulse() ? 1.0F : 0.0F;
    next.snare = featureSource_->snarePulse() ? 1.0F : 0.0F;
    liveFeatures_ = next;
    if (!useSyntheticFeatures_) applyCurrentFeatures(liveFeatures_);
}

void TerrainReactorItem::applyCurrentFeatures(const AudioFeatures&)
{
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
    setFixedColorBufferWidth(std::max(1, qRound(float(width()) * internalScale_)));
    setFixedColorBufferHeight(std::max(1, qRound(float(height()) * internalScale_)));
}

void TerrainReactorItem::updateWindowState(QQuickWindow* window)
{
    if (windowVisibilityConnection_) disconnect(windowVisibilityConnection_);
    if (window == nullptr) {
        windowExposed_ = true;
    } else {
        const auto refresh = [this, window] {
            const bool exposed = window->isExposed()
                && window->visibility() != QWindow::Hidden
                && window->visibility() != QWindow::Minimized;
            if (windowExposed_ == exposed) return;
            windowExposed_ = exposed;
            emit renderingRequestedChanged();
            scheduleIfRunnable();
        };
        windowVisibilityConnection_ = connect(window, &QWindow::visibilityChanged,
                                               this, refresh);
        refresh();
        if (window->rendererInterface()->graphicsApi()
            == QSGRendererInterface::Software) {
            reportRenderStatus(RenderStatus::SoftwareBackend,
                QStringLiteral("Terrain Reactor is disabled on the software scene graph"));
        }
    }
    emit renderingRequestedChanged();
    scheduleIfRunnable();
}

void TerrainReactorItem::reportRenderStatus(RenderStatus status,
                                             const QString& diagnostic)
{
    if (renderStatus_ == status && diagnostic_ == diagnostic) return;
    renderStatus_ = status;
    diagnostic_ = diagnostic;
    emit renderStatusChanged();
}
