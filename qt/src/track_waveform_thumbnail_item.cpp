#include "track_waveform_thumbnail_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
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

    QSGGeometry geometry;
    QSGFlatColorMaterial material;
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
        rebuildGeometry(&node->geometry);
        node->markDirty(QSGNode::DirtyGeometry);
        geometryDirty_ = false;
    }
    if (colorDirty_) {
        node->material.setColor(waveformColor_);
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

void TrackWaveformThumbnailItem::rebuildGeometry(QSGGeometry* geometry)
{
    const qreal deviceScale = window()
        ? std::max<qreal>(1.0, window()->effectiveDevicePixelRatio()) : 1.0;
    const int pixelColumns = std::clamp(
        static_cast<int>(std::ceil(width() * deviceScale)), 2, 16384);
    const bool sampledPolyline = pixelColumns > kPeakCount;
    const int vertexCount = sampledPolyline
        ? pixelColumns * 2 + 1 : pixelColumns * 2;
    if (geometry->vertexCount() != vertexCount) {
        geometry->allocate(vertexCount);
    }
    geometry->setDrawingMode(sampledPolyline
        ? QSGGeometry::DrawLineStrip : QSGGeometry::DrawTriangleStrip);

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
    for (int column = 0; column < pixelColumns; ++column) {
        vertices[column].set(
            static_cast<float>(xForColumn(column)),
            static_cast<float>(yForByte(interpolatedEndpoint(column, 0))));
        const int reverse = pixelColumns - 1 - column;
        vertices[pixelColumns + column].set(
            static_cast<float>(xForColumn(reverse)),
            static_cast<float>(yForByte(interpolatedEndpoint(reverse, 1))));
    }
    vertices[vertexCount - 1] = vertices[0];
}
