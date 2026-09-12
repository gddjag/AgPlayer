#pragma once
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <QFile>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <memory>
#include <cstring>
#include <algorithm>

namespace agplayer::terrain::gpu {
// A curved world-space text mesh in the existing native render pass. Texture
// uploads happen only on lyric changes, never for each playback clock tick.
class TerrainSpatialLyrics {
public:
    void reset() { pipeline_.reset(); bindings_.reset(); sampler_.reset();
        old_.reset(); current_.reset(); uniform_.reset(); lines_.clear(); image_ = {}; initialized_ = false; }
    bool prepare(QRhi* rhi, QRhiRenderTarget* target, QRhiResourceUpdateBatch* updates,
                 const float* mvp, const QStringList& lines, bool enabled,
                 float dt, float opacity, float orbit, float elevation,
                 float scale, float depth, QColor color) {
        if (!enabled) { if (pipeline_) reset(); return true; }
        if (!pipeline_ && !create(rhi, target)) return false;
        if (!initialized_ || lines != lines_ || color != color_) {
            QImage image(2048, 512, QImage::Format_RGBA8888_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::TextAntialiasing);
            QFont font(QStringLiteral("Microsoft YaHei"));
            for (int i = 0; i < 3; ++i) {
                const QString text = lines.value(i);
                font.setPixelSize(i == 1 ? 76 : 48);
                font.setWeight(i == 1 ? QFont::DemiBold : QFont::Normal);
                painter.setFont(font);
                QColor ink = color;
                if (i != 1) ink.setAlpha(145);
                painter.setPen(ink);
                painter.drawText(QRect(70, i == 0 ? 0 : i == 1 ? 110 : 375,
                    1908, i == 1 ? 260 : 110), Qt::AlignCenter | Qt::TextWordWrap, text);
            }
            painter.end();
            updates->uploadTexture(old_.get(), initialized_ ? image_ : image);
            updates->uploadTexture(current_.get(), image);
            image_ = image; lines_ = lines; color_ = color;
            blend_ = initialized_ ? 0.0F : 1.0F;
            initialized_ = true;
        }
        blend_ = std::min(1.0F, blend_ + std::max(0.0F, dt) / .35F);
        struct Uniforms { float matrix[16]; float params[4]; float geometry[4]; } u{};
        std::memcpy(u.matrix, mvp, sizeof(u.matrix));
        u.params[0] = blend_; u.params[1] = opacity;
        u.params[2] = orbit; u.params[3] = elevation;
        u.geometry[0] = scale; u.geometry[1] = depth;
        updates->updateDynamicBuffer(uniform_.get(), 0, sizeof(u), &u);
        return true;
    }
    void draw(QRhiCommandBuffer* cb) {
        if (!pipeline_ || !initialized_) return;
        cb->setGraphicsPipeline(pipeline_.get());
        cb->setShaderResources(bindings_.get());
        cb->draw(64 * 6);
    }
private:
    static QShader shader(const QString& file) {
        QFile source(file); if (!source.open(QIODevice::ReadOnly)) return {};
        return QShader::fromSerialized(source.readAll());
    }
    bool create(QRhi* rhi, QRhiRenderTarget* target) {
        uniform_.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
        sampler_.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        old_.reset(rhi->newTexture(QRhiTexture::RGBA8, {2048,512}));
        current_.reset(rhi->newTexture(QRhiTexture::RGBA8, {2048,512}));
        if (!uniform_->create() || !sampler_->create() || !old_->create() || !current_->create()) return false;
        bindings_.reset(rhi->newShaderResourceBindings());
        bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform_.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, old_.get(), sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, current_.get(), sampler_.get())});
        if (!bindings_->create()) return false;
        pipeline_.reset(rhi->newGraphicsPipeline());
        pipeline_->setShaderStages({{QRhiShaderStage::Vertex, shader(QStringLiteral(":/terrain/shaders/terrain_lyrics.vert.qsb"))},
            {QRhiShaderStage::Fragment, shader(QStringLiteral(":/terrain/shaders/terrain_lyrics.frag.qsb"))}});
        pipeline_->setShaderResourceBindings(bindings_.get());
        pipeline_->setRenderPassDescriptor(target->renderPassDescriptor());
        pipeline_->setSampleCount(target->sampleCount());
        pipeline_->setCullMode(QRhiGraphicsPipeline::None);
        pipeline_->setDepthTest(true);
        pipeline_->setDepthWrite(false);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline_->setTargetBlends({blend});
        return pipeline_->create();
    }
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiBuffer> uniform_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiTexture> old_, current_;
    QStringList lines_;
    QImage image_;
    QColor color_;
    float blend_ = 1;
    bool initialized_ = false;
};
}
