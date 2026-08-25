#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QGuiApplication;
class QEvent;
class SettingsController;

struct ThemePalette final {
    QColor background;
    QColor surface;
    QColor surfaceElevated;
    QColor surfaceHover;
    QColor surfacePressed;

    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor textDisabled;

    QColor border;
    QColor borderStrong;
    QColor divider;
    QColor disabled;

    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentSoft;
    QColor accentText;

    QColor highlight;
    QColor highlightHover;
    QColor highlightPressed;
    QColor highlightSoft;
    QColor highlightText;
    QColor focus;

    QColor success;
    QColor warning;
    QColor error;
    QColor danger;
    QColor recording;
    QColor critical;

    bool operator==(const ThemePalette& other) const;
    bool operator!=(const ThemePalette& other) const { return !(*this == other); }
};

class ThemeManager final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QColor background READ background NOTIFY paletteChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceElevated READ surfaceElevated NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfacePressed READ surfacePressed NOTIFY paletteChanged)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY paletteChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY paletteChanged)
    Q_PROPERTY(QColor textTertiary READ textTertiary NOTIFY paletteChanged)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY paletteChanged)
    Q_PROPERTY(QColor border READ border NOTIFY paletteChanged)
    Q_PROPERTY(QColor borderStrong READ borderStrong NOTIFY paletteChanged)
    Q_PROPERTY(QColor divider READ divider NOTIFY paletteChanged)
    Q_PROPERTY(QColor disabled READ disabled NOTIFY paletteChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentPressed READ accentPressed NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY paletteChanged)
    Q_PROPERTY(QColor highlight READ highlight NOTIFY paletteChanged)
    Q_PROPERTY(QColor highlightHover READ highlightHover NOTIFY paletteChanged)
    Q_PROPERTY(QColor highlightPressed READ highlightPressed NOTIFY paletteChanged)
    Q_PROPERTY(QColor highlightSoft READ highlightSoft NOTIFY paletteChanged)
    Q_PROPERTY(QColor highlightText READ highlightText NOTIFY paletteChanged)
    Q_PROPERTY(QColor focus READ focus NOTIFY paletteChanged)
    Q_PROPERTY(QColor success READ success NOTIFY paletteChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY paletteChanged)
    Q_PROPERTY(QColor error READ error NOTIFY paletteChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY paletteChanged)
    Q_PROPERTY(QColor recording READ recording NOTIFY paletteChanged)
    Q_PROPERTY(QColor critical READ critical NOTIFY paletteChanged)
    Q_PROPERTY(bool isLight READ isLight NOTIFY paletteChanged)

public:
    enum class AppearanceMode {
        Dark = 0,
        Light = 1,
        System = 2,
    };
    Q_ENUM(AppearanceMode)

    struct Preset final {
        QString id;
        QColor seed;

        bool operator==(const Preset& other) const
        {
            return id == other.id && seed == other.seed;
        }
    };

    struct Preferences final {
        AppearanceMode appearanceMode = AppearanceMode::System;
        QColor accentSeed = defaultSeed();
        QColor highlightSeed = defaultSeed();
        bool highlightFollowsAccent = true;
    };

    explicit ThemeManager(QGuiApplication& application, QObject* parent = nullptr);

    static QColor defaultSeed();
    static QList<Preset> presets();

    void applyPreferences(const Preferences& preferences);
    const Preferences& preferences() const { return preferences_; }
    const ThemePalette& palette() const { return palette_; }

    QColor background() const { return palette_.background; }
    QColor surface() const { return palette_.surface; }
    QColor surfaceElevated() const { return palette_.surfaceElevated; }
    QColor surfaceHover() const { return palette_.surfaceHover; }
    QColor surfacePressed() const { return palette_.surfacePressed; }
    QColor textPrimary() const { return palette_.textPrimary; }
    QColor textSecondary() const { return palette_.textSecondary; }
    QColor textTertiary() const { return palette_.textTertiary; }
    QColor textDisabled() const { return palette_.textDisabled; }
    QColor border() const { return palette_.border; }
    QColor borderStrong() const { return palette_.borderStrong; }
    QColor divider() const { return palette_.divider; }
    QColor disabled() const { return palette_.disabled; }
    QColor accent() const { return palette_.accent; }
    QColor accentHover() const { return palette_.accentHover; }
    QColor accentPressed() const { return palette_.accentPressed; }
    QColor accentSoft() const { return palette_.accentSoft; }
    QColor accentText() const { return palette_.accentText; }
    QColor highlight() const { return palette_.highlight; }
    QColor highlightHover() const { return palette_.highlightHover; }
    QColor highlightPressed() const { return palette_.highlightPressed; }
    QColor highlightSoft() const { return palette_.highlightSoft; }
    QColor highlightText() const { return palette_.highlightText; }
    QColor focus() const { return palette_.focus; }
    QColor success() const { return palette_.success; }
    QColor warning() const { return palette_.warning; }
    QColor error() const { return palette_.error; }
    QColor danger() const { return palette_.danger; }
    QColor recording() const { return palette_.recording; }
    QColor critical() const { return palette_.critical; }
    bool isLight() const;

signals:
    void paletteChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void handleSystemColorSchemeChanged(Qt::ColorScheme scheme);

private:
    AppearanceMode effectiveAppearance() const;
    void refreshPalette();
    void applyApplicationPalette(const ThemePalette& palette);

    QGuiApplication& application_;
    Preferences preferences_;
    ThemePalette palette_;
    Qt::ColorScheme systemColorScheme_ = Qt::ColorScheme::Unknown;
    bool applyingApplicationPalette_ = false;
};

// Keeps the complete settings transaction as the only runtime input to the
// palette.  It deliberately shares one apply callback for every theme field,
// so previews and cancel restoration take the same path as startup.
class ThemeSettingsSynchronizer final {
public:
    ThemeSettingsSynchronizer(ThemeManager& manager, SettingsController& settings);

private:
    void applyFromCompleteSettings();

    ThemeManager& manager_;
    SettingsController& settings_;
};
