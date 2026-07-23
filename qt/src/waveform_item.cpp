#include "waveform_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

struct Rgb {
    int red;
    int green;
    int blue;
};

constexpr std::array<Rgb, 5> gradientStops{{
    {0x00, 0xD4, 0xFF},
    {0x16, 0x88, 0xFF},
    {0x7B, 0x2F, 0xF7},
    {0xE6, 0x2E, 0x9B},
    {0xFF, 0x40, 0x57},
}};

Rgb gradientColor(double normalizedX)
{
    const double clamped = std::clamp(normalizedX, 0.0, 1.0);
    const double scaled = clamped * static_cast<double>(gradientStops.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(scaled));
    const auto upper = std::min(lower + 1U, gradientStops.size() - 1U);
    const double fraction = scaled - static_cast<double>(lower);
    const auto interpolate = [fraction](int from, int to) {
        return static_cast<int>(std::lround(static_cast<double>(from)
                                            + static_cast<double>(to - from) * fraction));
    };
    return {interpolate(gradientStops[lower].red, gradientStops[upper].red),
            interpolate(gradientStops[lower].green, gradientStops[upper].green),
            interpolate(gradientStops[lower].blue, gradientStops[upper].blue)};
}

class WaveformNode final : public QSGGeometryNode {
public:
    WaveformNode()
        : geometry_(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)
    {
        geometry_.setDrawingMode(QSGGeometry::DrawLines);
        geometry_.setLineWidth(1.0F);
        geometry_.setVertexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&geometry_);
        setMaterial(&material_);
    }

    QSGGeometry geometry_;
    QSGVertexColorMaterial material_;
    std::uint64_t revision_ = 0;
    qreal width_ = -1.0;
    qreal height_ = -1.0;
    qint64 position_ = -1;
    qint64 duration_ = -1;
    QColor waveformColor_;
};

} // namespace

WaveformItem::WaveformItem(QQuickItem* parent)
    : QQuickItem(parent)
    , peakSnapshot_(std::make_shared<const PeakSnapshot>())
{
    setFlag(ItemHasContents, true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

QVariantList WaveformItem::peaks() const
{
    return peaks_;
}

void WaveformItem::setPeaks(const QVariantList& peaks)
{
    auto snapshot = std::make_shared<PeakSnapshot>();
    snapshot->revision = nextRevision_++;
    snapshot->values.reserve(static_cast<std::size_t>(peaks.size()));

    QVariantList normalizedPeaks;
    normalizedPeaks.reserve(peaks.size());
    for (const QVariant& value : peaks) {
        bool converted = false;
        double peak = value.toDouble(&converted);
        if (!converted || !std::isfinite(peak)) {
            peak = 0.0;
        }
        peak = std::clamp(std::abs(peak), 0.0, 1.0);
        snapshot->values.push_back(static_cast<float>(peak));
        normalizedPeaks.append(peak);
    }

    peaks_ = std::move(normalizedPeaks);
    peakSnapshot_ = std::move(snapshot);
    emit peaksChanged();
    update();
}

qreal WaveformItem::position() const
{
    return static_cast<qreal>(position_);
}

void WaveformItem::setPosition(qreal position)
{
    const qint64 integralPosition = std::isfinite(position) ? qRound64(position) : 0;
    const qint64 upperBound = duration_ > 0 ? duration_ : 0;
    const qint64 clamped = std::clamp(integralPosition, qint64{0}, upperBound);
    if (clamped == position_) {
        return;
    }
    position_ = clamped;
    emit positionChanged();
    update();
}

qreal WaveformItem::duration() const
{
    return static_cast<qreal>(duration_);
}

void WaveformItem::setDuration(qreal duration)
{
    const qint64 integralDuration = std::isfinite(duration) ? qRound64(duration) : 0;
    const qint64 clamped = std::max(integralDuration, qint64{0});
    if (clamped == duration_) {
        return;
    }
    duration_ = clamped;
    emit durationChanged();

    if (position_ > duration_) {
        position_ = duration_;
        emit positionChanged();
    }
    if (hoverPosition_ > duration_) {
        setHoverPosition(duration_);
    }
    update();
}

QColor WaveformItem::waveformColor() const
{
    return waveformColor_;
}

void WaveformItem::setWaveformColor(const QColor& color)
{
    if (color == waveformColor_) {
        return;
    }
    waveformColor_ = color;
    emit waveformColorChanged();
    update();
}

qint64 WaveformItem::hoverPosition() const
{
    return hoverPosition_;
}

double WaveformItem::analysisProgress() const
{
    return analysisProgress_;
}

void WaveformItem::setAnalysisProgress(double progress)
{
    const double finite = std::isfinite(progress) ? progress : 0.0;
    const double clamped = std::clamp(finite, 0.0, 1.0);
    if (qFuzzyCompare(clamped, analysisProgress_)) {
        return;
    }
    analysisProgress_ = clamped;
    emit analysisProgressChanged();
}

qint64 WaveformItem::timeForX(qreal x) const
{
    if (width() <= 0.0 || duration_ <= 0 || !std::isfinite(x)) {
        return 0;
    }
    if (x <= 0.0) {
        return 0;
    }
    if (x >= width()) {
        return duration_;
    }

    const long double ratio = static_cast<long double>(x / width());
    const long double rounded = std::floor(
        ratio * static_cast<long double>(duration_) + static_cast<long double>(0.5));
    if (rounded >= static_cast<long double>(duration_)) {
        return duration_;
    }
    return static_cast<qint64>(rounded);
}

QSGNode* WaveformItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    // Qt calls this during Scene Graph synchronization while the GUI thread is blocked,
    // so copying the shared immutable snapshot here hands one stable revision to rendering.
    const std::shared_ptr<const PeakSnapshot> snapshot = peakSnapshot_;
    if (!snapshot || snapshot->values.empty() || width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<WaveformNode*>(oldNode);
    if (node == nullptr) {
        node = new WaveformNode();
    }

    const bool geometryChanged = node->revision_ != snapshot->revision
                                 || !qFuzzyCompare(node->width_, width())
                                 || !qFuzzyCompare(node->height_, height());
    const bool colorChanged = geometryChanged || node->position_ != position_
                              || node->duration_ != duration_
                              || node->waveformColor_ != waveformColor_;
    if (!colorChanged) {
        return node;
    }

    const auto peakCount = snapshot->values.size();
    const auto vertexCount = peakCount * 2U;
    if (vertexCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        delete node;
        return nullptr;
    }
    if (geometryChanged) {
        node->geometry_.allocate(static_cast<int>(vertexCount));
    }
    auto* vertices = node->geometry_.vertexDataAsColoredPoint2D();
    const float center = static_cast<float>(height() * 0.5);
    const double playedFraction = duration_ > 0
                                      ? std::clamp(static_cast<double>(position_)
                                                       / static_cast<double>(duration_),
                                                   0.0,
                                                   1.0)
                                      : 0.0;

    for (std::size_t index = 0; index < peakCount; ++index) {
        const double normalizedX = peakCount == 1U
                                       ? 0.5
                                       : static_cast<double>(index)
                                             / static_cast<double>(peakCount - 1U);
        const float x = static_cast<float>(normalizedX * width());
        const float amplitude = snapshot->values[index] * center;
        const auto alpha = static_cast<unsigned char>(
            normalizedX <= playedFraction ? 255 : unplayedAlpha());

        unsigned char red = 0;
        unsigned char green = 0;
        unsigned char blue = 0;
        if (waveformColor_.isValid()) {
            red = static_cast<unsigned char>(waveformColor_.red());
            green = static_cast<unsigned char>(waveformColor_.green());
            blue = static_cast<unsigned char>(waveformColor_.blue());
        } else {
            const Rgb color = gradientColor(normalizedX);
            red = static_cast<unsigned char>(color.red);
            green = static_cast<unsigned char>(color.green);
            blue = static_cast<unsigned char>(color.blue);
        }
        vertices[index * 2U].set(x, center - amplitude, red, green, blue, alpha);
        vertices[index * 2U + 1U].set(x, center + amplitude, red, green, blue, alpha);
    }

    node->revision_ = snapshot->revision;
    node->width_ = width();
    node->height_ = height();
    node->position_ = position_;
    node->duration_ = duration_;
    node->waveformColor_ = waveformColor_;
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}

void WaveformItem::hoverMoveEvent(QHoverEvent* event)
{
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::hoverLeaveEvent(QHoverEvent* event)
{
    if (!pointerPressed_) {
        setHoverPosition(-1);
    }
    event->accept();
}

void WaveformItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    pointerPressed_ = true;
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!pointerPressed_) {
        event->ignore();
        return;
    }
    setHoverPosition(timeForX(event->position().x()));
    event->accept();
}

void WaveformItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (!pointerPressed_ || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    pointerPressed_ = false;
    const qint64 committedPosition = timeForX(event->position().x());
    setHoverPosition(committedPosition);
    emit seekRequested(committedPosition);
    event->accept();
}

void WaveformItem::mouseUngrabEvent()
{
    pointerPressed_ = false;
}

void WaveformItem::setHoverPosition(qint64 position)
{
    if (position == hoverPosition_) {
        return;
    }
    hoverPosition_ = position;
    emit hoverPositionChanged();
}
