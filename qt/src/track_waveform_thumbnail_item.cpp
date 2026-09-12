#include "track_waveform_thumbnail_item.hpp"

#include "frequency_color_mix.hpp"

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
        : geometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                   0)
    {
        geometry.setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry.setLineWidth(1.0F);
        geometry.setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&geometry);
        setFlag(OwnsGeometry, false);
        material.setFlag(QSGMaterial::Blending, true);
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

    QSGGeometry geometry;
    QSGVertexColorMaterial material;
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

QByteArray TrackWaveformThumbnailItem::bass() const
{
    return bass_;
}

void TrackWaveformThumbnailItem::setBass(const QByteArray& bass)
{
    if (bass_ == bass) {
        return;
    }
    bass_ = bass;
    markColorDirty();
    emit bassChanged();
}

QByteArray TrackWaveformThumbnailItem::mid() const
{
    return mid_;
}

void TrackWaveformThumbnailItem::setMid(const QByteArray& mid)
{
    if (mid_ == mid) {
        return;
    }
    mid_ = mid;
    markColorDirty();
    emit midChanged();
}

QByteArray TrackWaveformThumbnailItem::high() const
{
    return high_;
}

void TrackWaveformThumbnailItem::setHigh(const QByteArray& high)
{
    if (high_ == high) {
        return;
    }
    high_ = high;
    markColorDirty();
    emit highChanged();
}

QColor TrackWaveformThumbnailItem::lowColor() const { return lowColor_; }
QColor TrackWaveformThumbnailItem::midColor() const { return midColor_; }
QColor TrackWaveformThumbnailItem::highColor() const { return highColor_; }

void TrackWaveformThumbnailItem::setLowColor(const QColor& color)
{
    if (!color.isValid() || lowColor_ == color) return;
    lowColor_ = color;
    markColorDirty();
    emit lowColorChanged();
}

void TrackWaveformThumbnailItem::setMidColor(const QColor& color)
{
    if (!color.isValid() || midColor_ == color) return;
    midColor_ = color;
    markColorDirty();
    emit midColorChanged();
}

void TrackWaveformThumbnailItem::setHighColor(const QColor& color)
{
    if (!color.isValid() || highColor_ == color) return;
    highColor_ = color;
    markColorDirty();
    emit highColorChanged();
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
        colorDirty_ = false;
    }
    if (colorDirty_) {
        recolorGeometry(node);
        node->markDirty(QSGNode::DirtyGeometry);
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

    auto* vertices = geometry->vertexDataAsColoredPoint2D();
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
            const QColor color = waveformColor_;
            vertices[column * 2].set(
                x, static_cast<float>(yForByte(minimum)),
                color.red(), color.green(), color.blue(), color.alpha());
            vertices[column * 2 + 1].set(
                x, static_cast<float>(yForByte(maximum)),
                color.red(), color.green(), color.blue(), color.alpha());
        }
        recolorGeometry(node);
        return;
    }

    const auto interpolatedEndpoint = [&byteAt, pixelColumns](
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
    const qreal coverageWidth = 1.0 / deviceScale;
    for (int column = 0; column < pixelColumns; ++column) {
        const float x = static_cast<float>(xForColumn(column));
        const qreal upperY = yForByte(interpolatedEndpoint(column, 0));
        const qreal lowerY = yForByte(interpolatedEndpoint(column, 1));
        const QColor color = waveformColor_;
        const uchar red = static_cast<uchar>(color.red());
        const uchar green = static_cast<uchar>(color.green());
        const uchar blue = static_cast<uchar>(color.blue());
        const uchar alpha = static_cast<uchar>(color.alpha());
        vertices[column * 2].set(x, static_cast<float>(upperY),
                                 red, green, blue, alpha);
        vertices[column * 2 + 1].set(x, static_cast<float>(lowerY),
                                     red, green, blue, alpha);
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
    recolorGeometry(node);
}

void TrackWaveformThumbnailItem::recolorGeometry(QSGNode* sceneNode)
{
    auto* node = static_cast<TrackWaveformThumbnailNode*>(sceneNode);
    QSGGeometry* geometry = &node->geometry;
    const int pixelColumns = geometry->vertexCount() / 2;
    if (pixelColumns < 2) return;

    const bool hasBands = bass_.size() == kPeakCount
        && mid_.size() == kPeakCount && high_.size() == kPeakCount;
    const auto bandAt = [pixelColumns](const QByteArray& band, int column) {
        const double position = static_cast<double>(column)
            * static_cast<double>(kPeakCount - 1)
            / static_cast<double>(pixelColumns - 1);
        const int left = static_cast<int>(std::floor(position));
        const int right = std::min(left + 1, kPeakCount - 1);
        const double fraction = position - left;
        const double first = static_cast<unsigned char>(band.at(left));
        const double second = static_cast<unsigned char>(band.at(right));
        return (first + (second - first) * fraction) / 255.0;
    };
    const auto colorAt = [&](int column) {
        if (!hasBands) return waveformColor_;
        QColor color = agplayer::ui::mixFrequencyColor(
            bandAt(bass_, column), bandAt(mid_, column), bandAt(high_, column),
            lowColor_, midColor_, highColor_);
        color.setAlpha(waveformColor_.alpha());
        return color;
    };
    const auto recolor = [&](QSGGeometry* target, bool coverage) {
        auto* vertices = target->vertexDataAsColoredPoint2D();
        for (int column = 0; column < pixelColumns; ++column) {
            const QColor color = colorAt(column);
            const uchar red = static_cast<uchar>(color.red());
            const uchar green = static_cast<uchar>(color.green());
            const uchar blue = static_cast<uchar>(color.blue());
            const uchar alpha = static_cast<uchar>(color.alpha());
            vertices[column * 2].r = red;
            vertices[column * 2].g = green;
            vertices[column * 2].b = blue;
            vertices[column * 2 + 1].r = red;
            vertices[column * 2 + 1].g = green;
            vertices[column * 2 + 1].b = blue;
            vertices[column * 2].a = coverage ? 0U : alpha;
            vertices[column * 2 + 1].a = alpha;
        }
        target->markVertexDataDirty();
    };
    recolor(geometry, false);
    if (node->upperCoverage != nullptr) {
        recolor(node->upperCoverage->geometry(), true);
        auto* lower = node->lowerCoverage->geometry()->vertexDataAsColoredPoint2D();
        recolor(node->lowerCoverage->geometry(), false);
        for (int column = 0; column < pixelColumns; ++column) {
            lower[column * 2 + 1].a = 0U;
        }
        node->upperCoverage->markDirty(QSGNode::DirtyGeometry);
        node->lowerCoverage->markDirty(QSGNode::DirtyGeometry);
    }
}
