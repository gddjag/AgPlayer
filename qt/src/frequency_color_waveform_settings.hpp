#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantList>

class FrequencyColorWaveformSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList palette READ palette WRITE setPalette NOTIFY changed)
    Q_PROPERTY(double unplayedOpacity READ unplayedOpacity
                   WRITE setUnplayedOpacity NOTIFY changed)

public:
    explicit FrequencyColorWaveformSettings(QObject* parent = nullptr);

    QVariantList palette() const;
    void setPalette(const QVariantList& palette);
    double unplayedOpacity() const noexcept;
    void setUnplayedOpacity(double opacity);

    Q_INVOKABLE void setPaletteColor(int index, const QString& color);
    Q_INVOKABLE void resetToDefault();
    void load(QSettings& settings);
    void save(QSettings& settings) const;

signals:
    void changed();

private:
    static QVariantList defaultPalette();
    static QVariantList normalizedPalette(const QVariantList& palette);

    QVariantList palette_ = defaultPalette();
    double unplayedOpacity_ = 0.88;
};
