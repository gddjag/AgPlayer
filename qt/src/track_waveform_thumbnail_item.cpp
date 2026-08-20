#include "track_waveform_thumbnail_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

namespace {

class TrackWaveformThumbnailNode final : public QSGGeometryNode {
public:
    TrackWaveformThumbnailNode()
        : geometry(QSGGeometry::defaultAttributes_Point2D(),
                   TrackWaveformThumbnailItem::kPeakCount * 2)
    {
        geometry.setDrawingMode(QSGGeometry::DrawLines);
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

int TrackWaveformThumbnailItem::geometryRevision() const noexcept
{
    return geometryRevision_;
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
    if (peaks_.size() != kPeakCount || width() <= 0.0 || height() <= 0.0) {
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
        ++geometryRevision_;
        emit geometryRevisionChanged();
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
    auto* vertices = geometry->vertexDataAsPoint2D();
    const qreal center = height() * 0.5;
    const qreal halfHeight = center;
    for (int index = 0; index < kPeakCount; ++index) {
        const qreal x = static_cast<qreal>(index) * width()
            / static_cast<qreal>(kPeakCount - 1);
        const auto value = static_cast<unsigned char>(peaks_.at(index));
        const qreal extent = halfHeight * static_cast<qreal>(value) / 255.0;
        vertices[index * 2].set(static_cast<float>(x),
                                static_cast<float>(center - extent));
        vertices[index * 2 + 1].set(static_cast<float>(x),
                                    static_cast<float>(center + extent));
    }
}
