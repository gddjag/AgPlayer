#include "lossless_evidence_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr double kMinimumDb = -120.0;
constexpr double kMaximumDb = 0.0;
constexpr int kMaximumSpectrumPoints = 4096;
constexpr int kMaximumSpectrogramFrames = 256;
constexpr int kMaximumSpectrogramBins = 256;

class FlatGeometryNode final : public QSGGeometryNode {
public:
    explicit FlatGeometryNode(QSGGeometry::DrawingMode mode)
        : geometry(QSGGeometry::defaultAttributes_Point2D(), 0)
    {
        geometry.setDrawingMode(mode);
        geometry.setLineWidth(1.0F);
        geometry.setVertexDataPattern(QSGGeometry::DynamicPattern);
        material.setFlag(QSGMaterial::Blending, true);
        setGeometry(&geometry);
        setFlag(OwnsGeometry, false);
        setMaterial(&material);
        setFlag(OwnsMaterial, false);
    }

    QSGGeometry geometry;
    QSGFlatColorMaterial material;
};

class HeatmapGeometryNode final : public QSGGeometryNode {
public:
    HeatmapGeometryNode()
        : geometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)
    {
        geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        geometry.setVertexDataPattern(QSGGeometry::DynamicPattern);
        material.setFlag(QSGMaterial::Blending, true);
        setGeometry(&geometry);
        setFlag(OwnsGeometry, false);
        setMaterial(&material);
        setFlag(OwnsMaterial, false);
    }

    QSGGeometry geometry;
    QSGVertexColorMaterial material;
};

class EvidenceNode final : public QSGNode {
public:
    EvidenceNode()
        : grid(QSGGeometry::DrawLines),
          fill(QSGGeometry::DrawTriangles),
          trace(QSGGeometry::DrawLineStrip),
          marker(QSGGeometry::DrawLines)
    {
        appendChildNode(&heatmap);
        appendChildNode(&grid);
        appendChildNode(&fill);
        appendChildNode(&trace);
        appendChildNode(&marker);
    }

    ~EvidenceNode() override
    {
        removeChildNode(&grid);
        removeChildNode(&heatmap);
        removeChildNode(&fill);
        removeChildNode(&trace);
        removeChildNode(&marker);
    }

    FlatGeometryNode grid;
    HeatmapGeometryNode heatmap;
    FlatGeometryNode fill;
    FlatGeometryNode trace;
    FlatGeometryNode marker;
};

double finiteDb(const QVariant& value)
{
    bool ok = false;
    const double converted = value.toDouble(&ok);
    if (!ok || !std::isfinite(converted)) return kMinimumDb;
    return std::clamp(converted, kMinimumDb, kMaximumDb);
}

float yForDb(double db, qreal height)
{
    const double fraction = (kMaximumDb - db) / (kMaximumDb - kMinimumDb);
    return static_cast<float>(fraction * height);
}

void setPoint(QSGGeometry::Point2D& point, qreal x, qreal y)
{
    point.set(static_cast<float>(x), static_cast<float>(y));
}

QColor heatColor(double db)
{
    const double normalized = std::clamp(
        (db - kMinimumDb) / (kMaximumDb - kMinimumDb), 0.0, 1.0);
    constexpr std::array<std::array<int, 3>, 5> colors{{
        {{68, 1, 84}},
        {{59, 82, 139}},
        {{33, 145, 140}},
        {{94, 201, 98}},
        {{253, 231, 37}},
    }};
    const double scaled = normalized * static_cast<double>(colors.size() - 1U);
    const std::size_t first = std::min(
        static_cast<std::size_t>(scaled), colors.size() - 2U);
    const double fraction = scaled - static_cast<double>(first);
    const auto component = [&](const std::size_t index) {
        return static_cast<int>(std::lround(
            static_cast<double>(colors[first][index]) * (1.0 - fraction)
            + static_cast<double>(colors[first + 1U][index]) * fraction));
    };
    return QColor(component(0U), component(1U), component(2U), 255);
}

void writeColoredPoint(QSGGeometry::ColoredPoint2D& point, qreal x, qreal y,
                       const QColor& color)
{
    point.set(static_cast<float>(x), static_cast<float>(y),
              static_cast<uchar>(color.red()),
              static_cast<uchar>(color.green()),
              static_cast<uchar>(color.blue()),
              static_cast<uchar>(color.alpha()));
}

} // namespace

LosslessEvidenceItem::LosslessEvidenceItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAntialiasing(true);
}

QVariantList LosslessEvidenceItem::spectrum() const { return spectrum_; }

void LosslessEvidenceItem::setSpectrum(const QVariantList& values)
{
    if (spectrum_ == values) return;
    spectrum_ = values;
    markGeometryDirty();
    emit spectrumChanged();
}

QVariantList LosslessEvidenceItem::spectrogram() const { return spectrogram_; }

void LosslessEvidenceItem::setSpectrogram(const QVariantList& values)
{
    if (spectrogram_ == values) return;
    spectrogram_ = values;
    markGeometryDirty();
    emit spectrogramChanged();
}

LosslessEvidenceItem::Mode LosslessEvidenceItem::mode() const { return mode_; }

void LosslessEvidenceItem::setMode(Mode mode)
{
    if (mode_ == mode) return;
    mode_ = mode;
    markGeometryDirty();
    emit modeChanged();
}

double LosslessEvidenceItem::sampleRate() const { return sampleRate_; }

void LosslessEvidenceItem::setSampleRate(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (qFuzzyCompare(sampleRate_ + 1.0, value + 1.0)) return;
    sampleRate_ = value;
    markGeometryDirty();
    emit sampleRateChanged();
}

double LosslessEvidenceItem::cutoffHz() const { return cutoffHz_; }

void LosslessEvidenceItem::setCutoffHz(double value)
{
    value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
    if (qFuzzyCompare(cutoffHz_ + 1.0, value + 1.0)) return;
    cutoffHz_ = value;
    markGeometryDirty();
    emit cutoffHzChanged();
}

QColor LosslessEvidenceItem::gridColor() const { return gridColor_; }

void LosslessEvidenceItem::setGridColor(const QColor& color)
{
    if (!color.isValid() || gridColor_ == color) return;
    gridColor_ = color;
    markMaterialDirty();
    emit gridColorChanged();
}

QColor LosslessEvidenceItem::traceColor() const { return traceColor_; }

void LosslessEvidenceItem::setTraceColor(const QColor& color)
{
    if (!color.isValid() || traceColor_ == color) return;
    traceColor_ = color;
    markMaterialDirty();
    emit traceColorChanged();
}

QColor LosslessEvidenceItem::fillColor() const { return fillColor_; }

void LosslessEvidenceItem::setFillColor(const QColor& color)
{
    if (!color.isValid() || fillColor_ == color) return;
    fillColor_ = color;
    markMaterialDirty();
    emit fillColorChanged();
}

void LosslessEvidenceItem::geometryChange(const QRectF& newGeometry,
                                          const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) markGeometryDirty();
}

QSGNode* LosslessEvidenceItem::updatePaintNode(QSGNode* oldNode,
                                               UpdatePaintNodeData*)
{
    if (width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<EvidenceNode*>(oldNode);
    if (node == nullptr) {
        node = new EvidenceNode();
        geometryDirty_ = true;
        materialDirty_ = true;
    }

    if (materialDirty_) {
        node->grid.material.setColor(gridColor_);
        node->fill.material.setColor(fillColor_);
        node->trace.material.setColor(traceColor_);
        node->marker.material.setColor(traceColor_);
        node->grid.markDirty(QSGNode::DirtyMaterial);
        node->fill.markDirty(QSGNode::DirtyMaterial);
        node->trace.markDirty(QSGNode::DirtyMaterial);
        node->marker.markDirty(QSGNode::DirtyMaterial);
        node->heatmap.markDirty(QSGNode::DirtyMaterial);
        materialDirty_ = false;
    }

    if (!geometryDirty_) return node;

    constexpr int horizontalGridLines = 7;
    constexpr int verticalGridLines = 8;
    auto& grid = node->grid.geometry;
    grid.allocate((horizontalGridLines + verticalGridLines) * 2);
    auto* gridPoints = grid.vertexDataAsPoint2D();
    int gridIndex = 0;
    for (int line = 0; line < horizontalGridLines; ++line) {
        const qreal y = height() * line / (horizontalGridLines - 1);
        setPoint(gridPoints[gridIndex++], 0.0, y);
        setPoint(gridPoints[gridIndex++], width(), y);
    }
    for (int line = 0; line < verticalGridLines; ++line) {
        const qreal x = width() * line / (verticalGridLines - 1);
        setPoint(gridPoints[gridIndex++], x, 0.0);
        setPoint(gridPoints[gridIndex++], x, height());
    }
    node->grid.markDirty(QSGNode::DirtyGeometry);

    if (mode_ == Mode::Spectrum) {
        const int pointCount = std::min(static_cast<int>(spectrum_.size()),
                                        kMaximumSpectrumPoints);
        node->heatmap.geometry.allocate(0);
        if (pointCount >= 2) {
            node->trace.geometry.allocate(pointCount);
            node->fill.geometry.allocate((pointCount - 1) * 6);
            auto* linePoints = node->trace.geometry.vertexDataAsPoint2D();
            auto* fillPoints = node->fill.geometry.vertexDataAsPoint2D();
            for (int index = 0; index < pointCount; ++index) {
                const qreal x = width() * index / (pointCount - 1);
                const int sourceIndex = static_cast<int>(std::llround(
                    static_cast<double>(index) * (spectrum_.size() - 1)
                    / (pointCount - 1)));
                setPoint(linePoints[index], x,
                         yForDb(finiteDb(spectrum_.at(sourceIndex)), height()));
            }
            int fillIndex = 0;
            for (int index = 0; index + 1 < pointCount; ++index) {
                const auto first = linePoints[index];
                const auto second = linePoints[index + 1];
                setPoint(fillPoints[fillIndex++], first.x, first.y);
                setPoint(fillPoints[fillIndex++], first.x, height());
                setPoint(fillPoints[fillIndex++], second.x, height());
                setPoint(fillPoints[fillIndex++], first.x, first.y);
                setPoint(fillPoints[fillIndex++], second.x, height());
                setPoint(fillPoints[fillIndex++], second.x, second.y);
            }
        } else {
            node->trace.geometry.allocate(0);
            node->fill.geometry.allocate(0);
        }

        const double nyquist = sampleRate_ * 0.5;
        if (cutoffHz_ > 0.0 && nyquist > 0.0 && cutoffHz_ < nyquist) {
            node->marker.geometry.allocate(2);
            auto* points = node->marker.geometry.vertexDataAsPoint2D();
            const qreal x = width() * cutoffHz_ / nyquist;
            setPoint(points[0], x, 0.0);
            setPoint(points[1], x, height());
        } else {
            node->marker.geometry.allocate(0);
        }
    } else {
        node->trace.geometry.allocate(0);
        node->fill.geometry.allocate(0);
        node->marker.geometry.allocate(0);
        const int frameCount = std::min(static_cast<int>(spectrogram_.size()),
                                        kMaximumSpectrogramFrames);
        int binCount = 0;
        int sourceBinCount = 0;
        if (frameCount > 0) {
            sourceBinCount = static_cast<int>(
                spectrogram_.first().toList().size());
            binCount = std::min(sourceBinCount, kMaximumSpectrogramBins);
        }
        if (frameCount > 0 && binCount > 0) {
            node->heatmap.geometry.allocate(frameCount * binCount * 6);
            auto* points = node->heatmap.geometry.vertexDataAsColoredPoint2D();
            int vertex = 0;
            for (int frame = 0; frame < frameCount; ++frame) {
                const int sourceFrameBegin = static_cast<int>(
                    static_cast<qsizetype>(frame) * spectrogram_.size()
                    / frameCount);
                const int sourceFrameEnd = std::max(
                    sourceFrameBegin + 1,
                    static_cast<int>(static_cast<qsizetype>(frame + 1)
                                     * spectrogram_.size() / frameCount));
                std::vector<QVariantList> sourceRows;
                sourceRows.reserve(static_cast<std::size_t>(
                    sourceFrameEnd - sourceFrameBegin));
                for (int sourceFrame = sourceFrameBegin;
                     sourceFrame < sourceFrameEnd; ++sourceFrame) {
                    sourceRows.push_back(
                        spectrogram_.at(sourceFrame).toList());
                }
                const qreal left = width() * frame / frameCount;
                const qreal right = width() * (frame + 1) / frameCount;
                for (int bin = 0; bin < binCount; ++bin) {
                    const int sourceBinBegin = bin * sourceBinCount / binCount;
                    const int sourceBinEnd = std::max(
                        sourceBinBegin + 1,
                        (bin + 1) * sourceBinCount / binCount);
                    double peakDb = kMinimumDb;
                    for (const QVariantList& sourceRow : sourceRows) {
                        for (int sourceBin = sourceBinBegin;
                             sourceBin < sourceBinEnd
                             && sourceBin < sourceRow.size(); ++sourceBin) {
                            peakDb = std::max(
                                peakDb, finiteDb(sourceRow.at(sourceBin)));
                        }
                    }
                    const qreal top = height() * (binCount - bin - 1) / binCount;
                    const qreal bottom = height() * (binCount - bin) / binCount;
                    const QColor color = heatColor(peakDb);
                    writeColoredPoint(points[vertex++], left, top, color);
                    writeColoredPoint(points[vertex++], left, bottom, color);
                    writeColoredPoint(points[vertex++], right, bottom, color);
                    writeColoredPoint(points[vertex++], left, top, color);
                    writeColoredPoint(points[vertex++], right, bottom, color);
                    writeColoredPoint(points[vertex++], right, top, color);
                }
            }
        } else {
            node->heatmap.geometry.allocate(0);
        }
    }

    node->trace.markDirty(QSGNode::DirtyGeometry);
    node->fill.markDirty(QSGNode::DirtyGeometry);
    node->marker.markDirty(QSGNode::DirtyGeometry);
    node->heatmap.markDirty(QSGNode::DirtyGeometry);
    geometryDirty_ = false;
    return node;
}

void LosslessEvidenceItem::markGeometryDirty()
{
    geometryDirty_ = true;
    update();
}

void LosslessEvidenceItem::markMaterialDirty()
{
    materialDirty_ = true;
    update();
}
