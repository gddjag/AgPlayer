#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>

#include <array>
#include <cstdint>
#include <vector>

class WaveformItem;

struct FrequencyWaveformVertex {
    float x = 0.0F;
    float y = 0.0F;
    float alphaRole = 0.0F;
};

enum class FrequencyWaveformNodeRole : int {
    Mix = 0,
    Low = 1,
    Mid = 2,
    High = 3,
};

class WaveformLayerMaterial final : public QSGMaterial {
public:
    WaveformLayerMaterial();

    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(
        QSGRendererInterface::RenderMode renderMode) const override;
    int compare(const QSGMaterial* other) const override;

    QColor baseColor() const noexcept;
    float fillAlpha() const noexcept;
    float outlineAlpha() const noexcept;
    float layerFade() const noexcept;
    float progressX() const noexcept;
    float featherWidth() const noexcept;
    float unplayedAlpha() const noexcept;
    float unplayedDesaturation() const noexcept;

    void setStyle(const QColor& color, float fillAlpha, float outlineAlpha,
                  float layerFade);
    void setProgress(float x, float featherWidth);

private:
    QColor baseColor_ = QColor(Qt::white);
    float fillAlpha_ = 1.0F;
    float outlineAlpha_ = 1.0F;
    float layerFade_ = 1.0F;
    float progressX_ = 0.0F;
    float featherWidth_ = 1.0F;
    static constexpr float unplayedAlpha_ = 0.72F;
    static constexpr float unplayedDesaturation_ = 0.08F;
};

class FrequencyWaveformLayerNode final : public QSGGeometryNode {
public:
    FrequencyWaveformLayerNode();

    void useTriangleMaterial();
    void useLineFallback();
    WaveformLayerMaterial* waveformMaterial() noexcept;
    const WaveformLayerMaterial* waveformMaterial() const noexcept;
    QSGVertexColorMaterial* lineMaterial() noexcept;

    QSGGeometry& triangleGeometry() noexcept;
    QSGGeometry& lineGeometry() noexcept;

private:
    QSGGeometry triangleGeometry_;
    QSGGeometry lineGeometry_;
    WaveformLayerMaterial waveformMaterial_;
    QSGVertexColorMaterial lineMaterial_;
    bool lineFallback_ = false;
};

class FrequencyWaveformRootNode final : public QSGNode {
public:
    FrequencyWaveformRootNode();

    QSGGeometryNode* baseline() noexcept;
    FrequencyWaveformLayerNode* layer(FrequencyWaveformNodeRole role) noexcept;
    const FrequencyWaveformLayerNode* layer(
        FrequencyWaveformNodeRole role) const noexcept;
    QSGTransformNode* focusTransform() noexcept;
    QSGGeometryNode* focusGeometryNode() noexcept;
    bool usesLineFallback() const noexcept;
    std::uint64_t compactBandBuildCount() const noexcept;

private:
    friend class WaveformItem;

    QSGGeometryNode* baseline_ = nullptr;
    std::array<FrequencyWaveformLayerNode*, 4> layers_{};
    QSGTransformNode* focusTransform_ = nullptr;
    QSGGeometryNode* focusGeometryNode_ = nullptr;
    bool lineFallback_ = false;

    std::uint64_t revision_ = 0;
    qreal width_ = -1.0;
    qreal height_ = -1.0;
    qreal devicePixelRatio_ = -1.0;
    qreal density_ = -1.0;
    qreal amplitudeScale_ = -1.0;
    qint64 duration_ = -1;
    qint64 visibleStartMs_ = -1;
    qint64 visibleEndMs_ = -1;
    unsigned char layerMask_ = 0;
    int quality_ = -1;
    QColor focusColor_;
    bool focusVisible_ = false;
    bool baselineStyleInitialized_ = false;
    bool baselineDarkSurface_ = true;
    std::vector<float> compactBandValues_;
    std::uint64_t compactBandBuildCount_ = 0;
    std::array<QColor, 4> fallbackColors_{};
    std::array<float, 4> fallbackAlphas_{};
    std::array<bool, 4> fallbackStyleInitialized_{};
};
