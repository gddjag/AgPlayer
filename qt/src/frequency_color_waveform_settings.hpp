#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

class FrequencyColorWaveformSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
    Q_PROPERTY(QString mixDarkColor READ mixDarkColor WRITE setMixDarkColor NOTIFY changed)
    Q_PROPERTY(QString lowDarkColor READ lowDarkColor WRITE setLowDarkColor NOTIFY changed)
    Q_PROPERTY(QString midDarkColor READ midDarkColor WRITE setMidDarkColor NOTIFY changed)
    Q_PROPERTY(QString highDarkColor READ highDarkColor WRITE setHighDarkColor NOTIFY changed)
    Q_PROPERTY(QString mixLightColor READ mixLightColor WRITE setMixLightColor NOTIFY changed)
    Q_PROPERTY(QString lowLightColor READ lowLightColor WRITE setLowLightColor NOTIFY changed)
    Q_PROPERTY(QString midLightColor READ midLightColor WRITE setMidLightColor NOTIFY changed)
    Q_PROPERTY(QString highLightColor READ highLightColor WRITE setHighLightColor NOTIFY changed)
    Q_PROPERTY(double mixDarkOpacity READ mixDarkOpacity WRITE setMixDarkOpacity NOTIFY changed)
    Q_PROPERTY(double lowDarkOpacity READ lowDarkOpacity WRITE setLowDarkOpacity NOTIFY changed)
    Q_PROPERTY(double midDarkOpacity READ midDarkOpacity WRITE setMidDarkOpacity NOTIFY changed)
    Q_PROPERTY(double highDarkOpacity READ highDarkOpacity WRITE setHighDarkOpacity NOTIFY changed)
    Q_PROPERTY(double mixLightOpacity READ mixLightOpacity WRITE setMixLightOpacity NOTIFY changed)
    Q_PROPERTY(double lowLightOpacity READ lowLightOpacity WRITE setLowLightOpacity NOTIFY changed)
    Q_PROPERTY(double midLightOpacity READ midLightOpacity WRITE setMidLightOpacity NOTIFY changed)
    Q_PROPERTY(double highLightOpacity READ highLightOpacity WRITE setHighLightOpacity NOTIFY changed)
    Q_PROPERTY(bool playFocus READ playFocus WRITE setPlayFocus NOTIFY changed)

public:
    explicit FrequencyColorWaveformSettings(QObject* parent = nullptr);

    QString preset() const;
    QString mixDarkColor() const;
    QString lowDarkColor() const;
    QString midDarkColor() const;
    QString highDarkColor() const;
    QString mixLightColor() const;
    QString lowLightColor() const;
    QString midLightColor() const;
    QString highLightColor() const;
    double mixDarkOpacity() const noexcept;
    double lowDarkOpacity() const noexcept;
    double midDarkOpacity() const noexcept;
    double highDarkOpacity() const noexcept;
    double mixLightOpacity() const noexcept;
    double lowLightOpacity() const noexcept;
    double midLightOpacity() const noexcept;
    double highLightOpacity() const noexcept;
    bool playFocus() const noexcept;

    void setMixDarkColor(const QString& value);
    void setLowDarkColor(const QString& value);
    void setMidDarkColor(const QString& value);
    void setHighDarkColor(const QString& value);
    void setMixLightColor(const QString& value);
    void setLowLightColor(const QString& value);
    void setMidLightColor(const QString& value);
    void setHighLightColor(const QString& value);
    void setMixDarkOpacity(double value);
    void setLowDarkOpacity(double value);
    void setMidDarkOpacity(double value);
    void setHighDarkOpacity(double value);
    void setMixLightOpacity(double value);
    void setLowLightOpacity(double value);
    void setMidLightOpacity(double value);
    void setHighLightOpacity(double value);
    void setPlayFocus(bool value);

    Q_INVOKABLE void resetToLuminousGlaze();
    void load(QSettings& settings);
    void save(QSettings& settings) const;

    void setLegacyStrength(double value);
    double legacyStrength() const noexcept;

signals:
    void changed();

private:
    enum class Role { Mix, Low, Mid, High };
    enum class Surface { Dark, Light };

    static int roleIndex(Role role) noexcept;
    void setDarkColor(Role role, const QString& value);
    void setLightColor(Role role, const QString& value);
    void setOpacity(Role role, Surface surface, double value);
    QString& color(Role role, Surface surface);
    const QString& color(Role role, Surface surface) const;
    double& opacity(Role role, Surface surface);
    double opacity(Role role, Surface surface) const;
    bool& manual(Role role, Surface surface);
    bool manual(Role role, Surface surface) const;
    void markCustomAndNotify(bool stateChanged);

    QString preset_ = QStringLiteral("luminousGlaze");
    QString mixDarkColor_ = QStringLiteral("#7a8490");
    QString lowDarkColor_ = QStringLiteral("#269a8e");
    QString midDarkColor_ = QStringLiteral("#c66b55");
    QString highDarkColor_ = QStringLiteral("#b5a4c6");
    QString mixLightColor_ = QStringLiteral("#59636d");
    QString lowLightColor_ = QStringLiteral("#146b64");
    QString midLightColor_ = QStringLiteral("#9d4938");
    QString highLightColor_ = QStringLiteral("#6e5a7d");
    double mixDarkOpacity_ = 0.18;
    double lowDarkOpacity_ = 0.44;
    double midDarkOpacity_ = 0.38;
    double highDarkOpacity_ = 0.46;
    double mixLightOpacity_ = 0.14;
    double lowLightOpacity_ = 0.36;
    double midLightOpacity_ = 0.32;
    double highLightOpacity_ = 0.40;
    bool playFocus_ = true;
    bool mixDarkManual_ = false;
    bool lowDarkManual_ = false;
    bool midDarkManual_ = false;
    bool highDarkManual_ = false;
    bool mixLightManual_ = false;
    bool lowLightManual_ = false;
    bool midLightManual_ = false;
    bool highLightManual_ = false;
};
