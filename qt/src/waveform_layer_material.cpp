#include "waveform_layer_material.hpp"

#include <QByteArray>
#include <QSGMaterialShader>

#include <algorithm>
#include <cstring>

namespace {

const std::array<QSGGeometry::Attribute, 2> waveformAttributes{{
    QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
    QSGGeometry::Attribute::create(1, 1, QSGGeometry::FloatType),
}};

const QSGGeometry::AttributeSet waveformAttributeSet{
    static_cast<int>(waveformAttributes.size()),
    sizeof(FrequencyWaveformVertex),
    waveformAttributes.data(),
};

class WaveformLayerShader final : public QSGMaterialShader {
public:
    WaveformLayerShader()
    {
        setShaderFileName(VertexStage,
                          QStringLiteral(":/agplayer/shaders/waveform_layer.vert.qsb"));
        setShaderFileName(FragmentStage,
                          QStringLiteral(":/agplayer/shaders/waveform_layer.frag.qsb"));
    }

    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial,
                           QSGMaterial*) override
    {
        QByteArray* buffer = state.uniformData();
        if (buffer->size() < 112) {
            return false;
        }
        if (state.isMatrixDirty()) {
            const QMatrix4x4 matrix = state.combinedMatrix();
            std::memcpy(buffer->data(), matrix.constData(), 64);
        }
        const auto* material = static_cast<const WaveformLayerMaterial*>(newMaterial);
        const QColor color = material->baseColor();
        const std::array<float, 4> baseColor{{
            color.redF(), color.greenF(), color.blueF(), 1.0F,
        }};
        const std::array<float, 4> alphas{{
            material->fillAlpha(), material->outlineAlpha(),
            material->layerFade(), state.opacity(),
        }};
        const std::array<float, 4> progress{{
            material->progressX(), material->featherWidth(),
            material->unplayedAlpha(), material->unplayedDesaturation(),
        }};
        std::memcpy(buffer->data() + 64, baseColor.data(), 16);
        std::memcpy(buffer->data() + 80, alphas.data(), 16);
        std::memcpy(buffer->data() + 96, progress.data(), 16);
        return true;
    }
};

} // namespace

WaveformLayerMaterial::WaveformLayerMaterial()
{
    setFlag(QSGMaterial::Blending, true);
}

QSGMaterialType* WaveformLayerMaterial::type() const
{
    static QSGMaterialType materialType;
    return &materialType;
}

QSGMaterialShader* WaveformLayerMaterial::createShader(
    QSGRendererInterface::RenderMode) const
{
    return new WaveformLayerShader();
}

int WaveformLayerMaterial::compare(const QSGMaterial* other) const
{
    const auto* rhs = static_cast<const WaveformLayerMaterial*>(other);
    if (baseColor_.rgba() != rhs->baseColor_.rgba()) {
        return baseColor_.rgba() < rhs->baseColor_.rgba() ? -1 : 1;
    }
    const std::array<float, 5> lhsValues{{fillAlpha_, outlineAlpha_, layerFade_,
                                         progressX_, featherWidth_}};
    const std::array<float, 5> rhsValues{{rhs->fillAlpha_, rhs->outlineAlpha_,
                                         rhs->layerFade_, rhs->progressX_,
                                         rhs->featherWidth_}};
    if (lhsValues == rhsValues) {
        return 0;
    }
    return lhsValues < rhsValues ? -1 : 1;
}

QColor WaveformLayerMaterial::baseColor() const noexcept { return baseColor_; }
float WaveformLayerMaterial::fillAlpha() const noexcept { return fillAlpha_; }
float WaveformLayerMaterial::outlineAlpha() const noexcept { return outlineAlpha_; }
float WaveformLayerMaterial::layerFade() const noexcept { return layerFade_; }
float WaveformLayerMaterial::progressX() const noexcept { return progressX_; }
float WaveformLayerMaterial::featherWidth() const noexcept { return featherWidth_; }
float WaveformLayerMaterial::unplayedAlpha() const noexcept { return unplayedAlpha_; }
float WaveformLayerMaterial::unplayedDesaturation() const noexcept
{
    return unplayedDesaturation_;
}

void WaveformLayerMaterial::setStyle(const QColor& color, float fillAlpha,
                                     float outlineAlpha, float layerFade)
{
    baseColor_ = color;
    fillAlpha_ = std::clamp(fillAlpha, 0.0F, 1.0F);
    outlineAlpha_ = std::clamp(outlineAlpha, 0.0F, 1.0F);
    layerFade_ = std::clamp(layerFade, 0.0F, 1.0F);
}

void WaveformLayerMaterial::setProgress(float x, float featherWidth)
{
    progressX_ = x;
    featherWidth_ = std::max(0.0F, featherWidth);
}

FrequencyWaveformLayerNode::FrequencyWaveformLayerNode()
    : triangleGeometry_(waveformAttributeSet, 0, 0,
                        QSGGeometry::UnsignedIntType)
    , lineGeometry_(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)
{
    triangleGeometry_.setDrawingMode(QSGGeometry::DrawTriangles);
    triangleGeometry_.setVertexDataPattern(QSGGeometry::StaticPattern);
    triangleGeometry_.setIndexDataPattern(QSGGeometry::StaticPattern);
    lineGeometry_.setDrawingMode(QSGGeometry::DrawLines);
    lineGeometry_.setVertexDataPattern(QSGGeometry::StaticPattern);
    waveformMaterial_.setFlag(QSGMaterial::Blending, true);
    lineMaterial_.setFlag(QSGMaterial::Blending, true);
    useTriangleMaterial();
}

void FrequencyWaveformLayerNode::useTriangleMaterial()
{
    lineFallback_ = false;
    setGeometry(&triangleGeometry_);
    setMaterial(&waveformMaterial_);
}

void FrequencyWaveformLayerNode::useLineFallback()
{
    lineFallback_ = true;
    setGeometry(&lineGeometry_);
    setMaterial(&lineMaterial_);
}

WaveformLayerMaterial* FrequencyWaveformLayerNode::waveformMaterial() noexcept
{
    return lineFallback_ ? nullptr : &waveformMaterial_;
}

const WaveformLayerMaterial* FrequencyWaveformLayerNode::waveformMaterial() const noexcept
{
    return lineFallback_ ? nullptr : &waveformMaterial_;
}

QSGVertexColorMaterial* FrequencyWaveformLayerNode::lineMaterial() noexcept
{
    return &lineMaterial_;
}

QSGGeometry& FrequencyWaveformLayerNode::triangleGeometry() noexcept
{
    return triangleGeometry_;
}

QSGGeometry& FrequencyWaveformLayerNode::lineGeometry() noexcept
{
    return lineGeometry_;
}

FrequencyWaveformRootNode::FrequencyWaveformRootNode()
{
    baseline_ = new QSGGeometryNode();
    auto* baselineGeometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    baselineGeometry->setDrawingMode(QSGGeometry::DrawLines);
    baseline_->setGeometry(baselineGeometry);
    baseline_->setFlag(QSGNode::OwnsGeometry, true);
    auto* baselineMaterial = new QSGVertexColorMaterial();
    baselineMaterial->setFlag(QSGMaterial::Blending, true);
    baseline_->setMaterial(baselineMaterial);
    baseline_->setFlag(QSGNode::OwnsMaterial, true);
    appendChildNode(baseline_);

    for (auto*& layerNode : layers_) {
        layerNode = new FrequencyWaveformLayerNode();
        appendChildNode(layerNode);
    }

    focusTransform_ = new QSGTransformNode();
    focusGeometryNode_ = new QSGGeometryNode();
    auto* focusGeometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    focusGeometry->setDrawingMode(QSGGeometry::DrawTriangles);
    focusGeometryNode_->setGeometry(focusGeometry);
    focusGeometryNode_->setFlag(QSGNode::OwnsGeometry, true);
    auto* focusMaterial = new QSGVertexColorMaterial();
    focusMaterial->setFlag(QSGMaterial::Blending, true);
    focusGeometryNode_->setMaterial(focusMaterial);
    focusGeometryNode_->setFlag(QSGNode::OwnsMaterial, true);
    focusTransform_->appendChildNode(focusGeometryNode_);
    appendChildNode(focusTransform_);
}

QSGGeometryNode* FrequencyWaveformRootNode::baseline() noexcept { return baseline_; }

FrequencyWaveformLayerNode* FrequencyWaveformRootNode::layer(
    FrequencyWaveformNodeRole role) noexcept
{
    return layers_[static_cast<std::size_t>(role)];
}

const FrequencyWaveformLayerNode* FrequencyWaveformRootNode::layer(
    FrequencyWaveformNodeRole role) const noexcept
{
    return layers_[static_cast<std::size_t>(role)];
}

QSGTransformNode* FrequencyWaveformRootNode::focusTransform() noexcept
{
    return focusTransform_;
}

QSGGeometryNode* FrequencyWaveformRootNode::focusGeometryNode() noexcept
{
    return focusGeometryNode_;
}

bool FrequencyWaveformRootNode::usesLineFallback() const noexcept
{
    return lineFallback_;
}
