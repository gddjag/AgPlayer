#pragma once

#include "terrain_reactor_gpu_data.hpp"
#include <QImage>
#include <QMatrix4x4>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <cstring>
#include <memory>

namespace agplayer::terrain::gpu {

inline QRhiVertexInputLayout terrainVertexLayout() {
    QRhiVertexInputLayout layout;
    layout.setBindings({{sizeof(Vertex)}, {sizeof(GpuInstance), QRhiVertexInputBinding::PerInstance}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position)},
        {0, 1, QRhiVertexInputAttribute::Float3, offsetof(Vertex, normal)},
        {1, 2, QRhiVertexInputAttribute::Float3, offsetof(GpuInstance, position)},
        {1, 3, QRhiVertexInputAttribute::Float3, offsetof(GpuInstance, scale)},
        {1, 4, QRhiVertexInputAttribute::Float4, offsetof(GpuInstance, data)}});
    return layout;
}

// One scene-graph-thread-owned map, also used by the native material fixture.
// No timers, scene duplication, CPU deformation or extra audio buffers.
class TerrainShadowMap {
public:
    static constexpr int resolution = 1024;
    void reset() {
        pipeline_.reset(); bindings_.reset(); uniform_.reset();
        target_.reset(); pass_.reset(); depth_.reset(); sampler_.reset();
        fallback_.reset(); fallbackPending_ = false; depthInitialized_ = false;
    }
    bool create(QRhi* rhi, const QShader& vertex, const QShader& fragment,
                const QRhiVertexInputLayout& layout, bool allowReadback = false) {
        reset();
        sampler_.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
            QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!sampler_->create()) return false;
        if (rhi->isTextureFormatSupported(QRhiTexture::D32F, QRhiTexture::RenderTarget)) {
            auto flags = QRhiTexture::Flags(QRhiTexture::RenderTarget);
            if (allowReadback) flags |= QRhiTexture::UsedAsTransferSource;
            depth_.reset(rhi->newTexture(QRhiTexture::D32F, {resolution, resolution}, 1, flags));
            if (depth_->create()) {
                QRhiTextureRenderTargetDescription description;
                description.setDepthTexture(depth_.get());
                target_.reset(rhi->newTextureRenderTarget(description));
                pass_.reset(target_->newCompatibleRenderPassDescriptor());
                target_->setRenderPassDescriptor(pass_.get());
                uniform_.reset(rhi->newBuffer(QRhiBuffer::Dynamic,
                    QRhiBuffer::UniformBuffer, sizeof(UniformBlock)));
                bindings_.reset(rhi->newShaderResourceBindings());
                bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,
                    QRhiShaderResourceBinding::VertexStage, uniform_.get())});
                if (target_->create() && uniform_->create() && bindings_->create()) {
                    pipeline_.reset(rhi->newGraphicsPipeline());
                    pipeline_->setShaderStages({{QRhiShaderStage::Vertex, vertex},
                                                {QRhiShaderStage::Fragment, fragment}});
                    pipeline_->setVertexInputLayout(layout);
                    pipeline_->setShaderResourceBindings(bindings_.get());
                    pipeline_->setRenderPassDescriptor(pass_.get());
                    pipeline_->setCullMode(QRhiGraphicsPipeline::Back);
                    pipeline_->setDepthTest(true);
                    pipeline_->setDepthWrite(true);
                    // A 1024px map covers the entire stage. Account for the
                    // receiver depth slope across a texel to avoid diagonal
                    // self-shadow bands on smooth walls and caps.
                    pipeline_->setDepthBias(1);
                    pipeline_->setSlopeScaledDepthBias(1.5F);
                    if (pipeline_->create()) return true;
                }
            }
        }
        return createFallback(rhi);
    }
    // Initialization only. Destroy material references before replacing their
    // sampled texture; a failed unshadowed retry remains a real resource error.
    template<typename CreateMaterial, typename ReleaseMaterial>
    bool createMaterialOrFallback(QRhi* rhi, CreateMaterial createMaterial,
                                  ReleaseMaterial releaseMaterial) {
        if (createMaterial()) return true;
        releaseMaterial();
        if (available() && createFallback(rhi)) {
            if (createMaterial()) return true;
            releaseMaterial();
        }
        return false;
    }
    bool available() const { return bool(pipeline_); }
    QRhiTexture* texture() const { return depth_ ? depth_.get() : fallback_.get(); }
    QRhiSampler* sampler() const { return sampler_.get(); }
    void configure(QRhi* rhi, UniformBlock& u, bool enabled) const {
        // Stable in world space, including when the observer orbits the stage.
        QMatrix4x4 projection, view;
        projection.ortho(-164.0F, 164.0F, -164.0F, 164.0F, 1.0F, 600.0F);
        const QVector3D direction = QVector3D(-0.55F, 0.85F, 0.45F).normalized();
        const QVector3D center(0, 18, 0);
        view.lookAt(center + direction * 290.0F, center, QVector3D(0, 1, 0));
        const QMatrix4x4 matrix = rhi->clipSpaceCorrMatrix() * projection * view;
        std::memcpy(u.lightMvp, matrix.constData(), sizeof(u.lightMvp));
        u.shadowParameters[0] = enabled && available() ? 1.0F : 0.0F;
        u.shadowParameters[1] = 1.0F / resolution;
        u.shadowParameters[2] = rhi->isClipDepthZeroToOne() ? 1.0F : 0.0F;
        u.shadowParameters[3] = rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? 1.0F : 0.0F;
    }
    void render(QRhi* rhi, QRhiCommandBuffer* cb, const UniformBlock& u,
                QRhiBuffer* vertices, QRhiBuffer* indices, QRhiBuffer* instances,
                quint32 indexCount, quint32 instanceCount) {
        if (fallbackPending_) {
            auto* batch = rhi->nextResourceUpdateBatch();
            QImage white(1, 1, QImage::Format_RGBA8888); white.fill(Qt::white);
            batch->uploadTexture(fallback_.get(), white);
            cb->resourceUpdate(batch);
            fallbackPending_ = false;
        }
        if (!available() || (u.shadowParameters[0] <= 0 && depthInitialized_)) return;
        // Clear even when disabled: the bound depth texture is always initialized.
        // This is inside the existing running-frame gate, never on a new timer.
        auto shadowUniforms = u;
        std::memcpy(shadowUniforms.mvp, u.lightMvp, sizeof(u.mvp));
        auto* batch = rhi->nextResourceUpdateBatch();
        batch->updateDynamicBuffer(uniform_.get(), 0, sizeof(shadowUniforms), &shadowUniforms);
        cb->beginPass(target_.get(), Qt::transparent, {1, 0}, batch);
        if (u.shadowParameters[0] > 0 && instanceCount > 0) {
            cb->setGraphicsPipeline(pipeline_.get());
            cb->setShaderResources(bindings_.get());
            cb->setViewport(QRhiViewport(0, 0, resolution, resolution));
            const QRhiCommandBuffer::VertexInput inputs[] = {{vertices, 0}, {instances, 0}};
            cb->setVertexInput(0, 2, inputs, indices, 0, QRhiCommandBuffer::IndexUInt16);
            cb->drawIndexed(indexCount, instanceCount);
        }
        cb->endPass();
        depthInitialized_ = true;
    }
private:
    bool createFallback(QRhi* rhi) {
        // The existing sampler2D accepts white RGBA8; configure disables shadows.
        pipeline_.reset(); bindings_.reset(); uniform_.reset();
        target_.reset(); pass_.reset(); depth_.reset();
        depthInitialized_ = false;
        fallback_.reset(rhi->newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
        fallbackPending_ = fallback_->create();
        return fallbackPending_;
    }
    std::unique_ptr<QRhiTexture> depth_, fallback_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiRenderPassDescriptor> pass_;
    std::unique_ptr<QRhiTextureRenderTarget> target_;
    std::unique_ptr<QRhiBuffer> uniform_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    bool fallbackPending_ = false;
    bool depthInitialized_ = false;
};
} // namespace agplayer::terrain::gpu
