#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVariantList>

class QSGNode;

class LosslessEvidenceItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList spectrum READ spectrum WRITE setSpectrum
                   NOTIFY spectrumChanged)
    Q_PROPERTY(QVariantList spectrogram READ spectrogram WRITE setSpectrogram
                   NOTIFY spectrogramChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate
                   NOTIFY sampleRateChanged)
    Q_PROPERTY(double cutoffHz READ cutoffHz WRITE setCutoffHz
                   NOTIFY cutoffHzChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor
                   NOTIFY gridColorChanged)
    Q_PROPERTY(QColor traceColor READ traceColor WRITE setTraceColor
                   NOTIFY traceColorChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor
                   NOTIFY fillColorChanged)
public:
    enum class Mode {
        Spectrum = 0,
        Spectrogram = 1,
    };
    Q_ENUM(Mode)

    explicit LosslessEvidenceItem(QQuickItem* parent = nullptr);

    QVariantList spectrum() const;
    void setSpectrum(const QVariantList& values);
    QVariantList spectrogram() const;
    void setSpectrogram(const QVariantList& values);
    Mode mode() const;
    void setMode(Mode mode);
    double sampleRate() const;
    void setSampleRate(double value);
    double cutoffHz() const;
    void setCutoffHz(double value);
    QColor gridColor() const;
    void setGridColor(const QColor& color);
    QColor traceColor() const;
    void setTraceColor(const QColor& color);
    QColor fillColor() const;
    void setFillColor(const QColor& color);

signals:
    void spectrumChanged();
    void spectrogramChanged();
    void modeChanged();
    void sampleRateChanged();
    void cutoffHzChanged();
    void gridColorChanged();
    void traceColorChanged();
    void fillColorChanged();

protected:
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode,
                             UpdatePaintNodeData* data) override;

private:
    void markGeometryDirty();
    void markMaterialDirty();

    QVariantList spectrum_;
    QVariantList spectrogram_;
    Mode mode_ = Mode::Spectrum;
    double sampleRate_ = 0.0;
    double cutoffHz_ = 0.0;
    QColor gridColor_ = QColor(QStringLiteral("#263747"));
    QColor traceColor_ = QColor(QStringLiteral("#18B9E7"));
    QColor fillColor_ = QColor(QStringLiteral("#3518B9E7"));
    bool geometryDirty_ = true;
    bool materialDirty_ = true;
};
