#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>

class FrequencyColorWaveformSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY changed)
    Q_PROPERTY(QColor midColor READ midColor WRITE setMidColor NOTIFY changed)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY changed)
    Q_PROPERTY(double unplayedOpacity READ unplayedOpacity
                   WRITE setUnplayedOpacity NOTIFY changed)

public:
    explicit FrequencyColorWaveformSettings(QObject* parent = nullptr);

    QColor lowColor() const;
    void setLowColor(const QColor& color);
    QColor midColor() const;
    void setMidColor(const QColor& color);
    QColor highColor() const;
    void setHighColor(const QColor& color);
    double unplayedOpacity() const noexcept;
    void setUnplayedOpacity(double opacity);

    Q_INVOKABLE void resetToDefault();
    void load(QSettings& settings);
    void save(QSettings& settings) const;

signals:
    void changed();

private:
    QColor lowColor_{QStringLiteral("#8B3DFF")};
    QColor midColor_{QStringLiteral("#FFB000")};
    QColor highColor_{QStringLiteral("#002FA7")};
    double unplayedOpacity_ = 0.88;
};
