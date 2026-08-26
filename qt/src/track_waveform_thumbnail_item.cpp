#include "track_waveform_thumbnail_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

namespace {

class TrackWaveformThumbnailNode final : public QSGGeometryNode {
public:
    TrackWaveformThumbnailNode()
        : geometry(QSGGeometry::defaultAttributes_Point2D(),
                   0)
    {
        geometry.setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry.setLineWidth(1.0F);
        geometry.setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&geometry);
        setFlag(OwnsGeometry, false);
        setMaterial(&material);
        setFlag(OwnsMaterial, false);
    }

    void ensureCoverageNodes()
    {
        if (upperCoverage != nullptr) {
            return;
        }
        upperCoverage = createCoverageNode();
        lowerCoverage = createCoverageNode();
        appendChildNode(upperCoverage);
        appendChildNode(lowerCoverage);
    }

    void clearCoverageNodes()
    {
        if (upperCoverage == nullptr) {
            return;
        }
        removeChildNode(upperCoverage);
        removeChildNode(lowerCoverage);
        delete upperCoverage;
        delete lowerCoverage;
        upperCoverage = nullptr;
        lowerCoverage = nullptr;
    }

    void setWaveformColor(const QColor& color)
    {
        material.setColor(color);
        recolorCoverage(upperCoverage, color, true);
        recolorCoverage(lowerCoverage, color, false);
    }

    QSGGeometry geometry;
    QSGFlatColorMaterial material;
    QSGGeometryNode* upperCoverage = nullptr;
    QSGGeometryNode* lowerCoverage = nullptr;

private:
    static QSGGeometryNode* createCoverageNode()
    {
        auto* node = new QSGGeometryNode();
        auto* coverageGeometry = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        coverageGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        coverageGeometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        node->setGeometry(coverageGeometry);
        node->setFlag(QSGNode::OwnsGeometry, true);
        auto* coverageMaterial = new QSGVertexColorMaterial();
        coverageMaterial->setFlag(QSGMaterial::Blending, true);
        node->setMaterial(coverageMaterial);
        node->setFlag(QSGNode::OwnsMaterial, true);
        return node;
    }

    static void recolorCoverage(QSGGeometryNode* node, const QColor& color,
                                const bool transparentFirst)
    {
        if (node == nullptr) {
            return;
        }
        QSGGeometry* coverageGeometry = node->geometry();
        auto* vertices = coverageGeometry->vertexDataAsColoredPoint2D();
        for (int index = 0; index < coverageGeometry->vertexCount(); ++index) {
            vertices[index].r = static_cast<uchar>(color.red());
            vertices[index].g = static_cast<uchar>(color.green());
            vertices[index].b = static_cast<uchar>(color.blue());
            const bool transparent = (index % 2 == 0)
                ? transparentFirst : !transparentFirst;
            vertices[index].a = transparent
                ? 0U : static_cast<uchar>(color.alpha());
        }
        node->markDirty(QSGNode::DirtyGeometry);
    }
};

} // namespace

TrackWaveformThumbnailItem::TrackWaveformThumbnailItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAntialiasing(true);
}

QByteArray TrackWaveformThumbnailItem::peaks() const
{
    return peaks_;
}

void TrackWaveformThumbnailItem::setPeaks(const QByteArray& peaks)
{
    if (peaks_ == peaks) {
        return;
    }
    peaks_ = peaks;
    markGeometryDirty();
    emit peaksChanged();
}

QColor TrackWaveformThumbnailItem::waveformColor() const
{
    return waveformColor_;
}

void TrackWaveformThumbnailItem::setWaveformColor(const QColor& color)
{
    if (waveformColor_ == color) {
        return;
    }
    waveformColor_ = color;
    markColorDirty();
    emit waveformColorChanged();
}

void TrackWaveformThumbnailItem::geometryChange(const QRectF& newGeometry,
                                                const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        markGeometryDirty();
    }
}

QSGNode* TrackWaveformThumbnailItem::updatePaintNode(
    QSGNode* oldNode, UpdatePaintNodeData*)
{
    if (peaks_.size() != kPeakDataSize
        || width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<TrackWaveformThumbnailNode*>(oldNode);
    if (node == nullptr) {
        node = new TrackWaveformThumbnailNode();
        geometryDirty_ = true;
        colorDirty_ = true;
    }
    if (geometryDirty_) {
        rebuildGeometry(node);
        node->markDirty(QSGNode::DirtyGeometry);
        geometryDirty_ = false;
    }
    if (colorDirty_) {
        node->setWaveformColor(waveformColor_);
        node->markDirty(QSGNode::DirtyMaterial);
        colorDirty_ = false;
    }
    return node;
}

void TrackWaveformThumbnailItem::markGeometryDirty()
{
    geometryDirty_ = true;
    update();
}

void TrackWaveformThumbnailItem::markColorDirty()
{
    colorDirty_ = true;
    update();
}

void TrackWaveformThumbnailItem::rebuildGeometry(QSGNode* sceneNode)
{
    auto* node = static_cast<TrackWaveformThumbnailNode*>(sceneNode);
    QSGGeometry* geometry = &node->geometry;
    const qreal deviceScale = window()
        ? std::max<qreal>(1.0, window()->effectiveDevicePixelRatio()) : 1.0;
    const int pixelColumns = std::clamp(
        static_cast<int>(std::ceil(width() * deviceScale)), 2, 16384);
    const bool sampledPolyline = pixelColumns > kPeakCount;
    const int vertexCount = pixelColumns * 2;
    if (geometry->vertexCount() != vertexCount) {
        geometry->allocate(vertexCount);
    }
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);

    auto* vertices = geometry->vertexDataAsPoint2D();
    const auto byteAt = [this](const int bucket, const int endpoint) {
        return static_cast<unsigned char>(
            peaks_.at(bucket * 2 + endpoint));
    };
    const auto xForColumn = [this, pixelColumns](const int column) {
        return static_cast<qreal>(column) * width()
            / static_cast<qreal>(pixelColumns - 1);
    };
    const auto yForByte = [this](const qreal value) {
        return height() * value / 255.0;
    };

    if (!sampledPolyline) {
        node->clearCoverageNodes();
        for (int column = 0; column < pixelColumns; ++column) {
            const int first = kPeakCount * column / pixelColumns;
            const int last = std::max(
                first + 1,
                (kPeakCount * (column + 1) + pixelColumns - 1)
                    / pixelColumns);
            unsigned char minimum = 255U;
            unsigned char maximum = 0U;
            for (int bucket = first; bucket < std::min(last, kPeakCount);
                 ++bucket) {
                minimum = std::min(minimum, byteAt(bucket, 0));
                maximum = std::max(maximum, byteAt(bucket, 1));
            }
            const float x = static_cast<float>(xForColumn(column));
            vertices[column * 2].set(
                x, static_cast<float>(yForByte(minimum)));
            vertices[column * 2 + 1].set(
                x, static_cast<float>(yForByte(maximum)));
        }
        return;
    }

    const auto interpolatedEndpoint = [this, &byteAt, pixelColumns](
                                           const int column,
                                           const int endpoint) {
        const qreal position = static_cast<qreal>(column)
            * static_cast<qreal>(kPeakCount - 1)
            / static_cast<qreal>(pixelColumns - 1);
        const int first = static_cast<int>(std::floor(position));
        const int second = std::min(first + 1, kPeakCount - 1);
        const qreal fraction = position - static_cast<qreal>(first);
        return static_cast<qreal>(byteAt(first, endpoint))
            + (static_cast<qreal>(byteAt(second, endpoint))
               - static_cast<qreal>(byteAt(first, endpoint))) * fraction;
    };
    node->ensureCoverageNodes();
    QSGGeometry* upperCoverage = node->upperCoverage->geometry();
    QSGGeometry* lowerCoverage = node->lowerCoverage->geometry();
    if (upperCoverage->vertexCount() != vertexCount) {
        upperCoverage->allocate(vertexCount);
        lowerCoverage->allocate(vertexCount);
    }
    auto* upperCoverageVertices =
        upperCoverage->vertexDataAsColoredPoint2D();
    auto* lowerCoverageVertices =
        lowerCoverage->vertexDataAsColoredPoint2D();
    const uchar red = static_cast<uchar>(waveformColor_.red());
    const uchar green = static_cast<uchar>(waveformColor_.green());
    const uchar blue = static_cast<uchar>(waveformColor_.blue());
    const uchar alpha = static_cast<uchar>(waveformColor_.alpha());
    const qreal coverageWidth = 1.0 / deviceScale;
    for (int column = 0; column < pixelColumns; ++column) {
        const float x = static_cast<float>(xForColumn(column));
        const qreal upperY = yForByte(interpolatedEndpoint(column, 0));
        const qreal lowerY = yForByte(interpolatedEndpoint(column, 1));
        vertices[column * 2].set(x, static_cast<float>(upperY));
        vertices[column * 2 + 1].set(x, static_cast<float>(lowerY));
        upperCoverageVertices[column * 2].set(
            x, static_cast<float>(std::max<qreal>(0.0,
                                                  upperY - coverageWidth)),
            red, green, blue, 0U);
        upperCoverageVertices[column * 2 + 1].set(
            x, static_cast<float>(upperY), red, green, blue, alpha);
        lowerCoverageVertices[column * 2].set(
            x, static_cast<float>(lowerY), red, green, blue, alpha);
        lowerCoverageVertices[column * 2 + 1].set(
            x, static_cast<float>(std::min<qreal>(height(),
                                                  lowerY + coverageWidth)),
            red, green, blue, 0U);
    }
    node->upperCoverage->markDirty(QSGNode::DirtyGeometry);
    node->lowerCoverage->markDirty(QSGNode::DirtyGeometry);
}
