#pragma once

#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QVariantList>

class SettingsController;

class PlayerExperienceController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(int immersiveMode READ immersiveMode WRITE setImmersiveMode
                   NOTIFY immersiveModeChanged)
    Q_PROPERTY(int hostMode READ hostMode WRITE setHostMode NOTIFY hostModeChanged)
    Q_PROPERTY(bool lyricsVisible READ lyricsVisible WRITE setLyricsVisible
                   NOTIFY lyricsVisibleChanged)
    Q_PROPERTY(bool panelVisible READ panelVisible WRITE setPanelVisible
                   NOTIFY panelVisibleChanged)
    Q_PROPERTY(bool desktopMousePassthrough READ desktopMousePassthrough
                   WRITE setDesktopMousePassthrough
                   NOTIFY desktopMousePassthroughChanged)
    Q_PROPERTY(int qualityPreset READ qualityPreset WRITE setQualityPreset
                   NOTIFY qualityPresetChanged)
    Q_PROPERTY(int colorMode READ colorMode WRITE setColorMode NOTIFY colorModeChanged)
    Q_PROPERTY(QString coolColor READ coolColor WRITE setCoolColor
                   NOTIFY coolColorChanged)
    Q_PROPERTY(QString warmColor READ warmColor WRITE setWarmColor
                   NOTIFY warmColorChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor
                   NOTIFY accentColorChanged)
    Q_PROPERTY(QString peakColor READ peakColor WRITE setPeakColor
                   NOTIFY peakColorChanged)
    Q_PROPERTY(QString baseColor READ baseColor WRITE setBaseColor
                   NOTIFY baseColorChanged)
    Q_PROPERTY(int terrainAmplitude READ terrainAmplitude WRITE setTerrainAmplitude
                   NOTIFY terrainAmplitudeChanged)
    Q_PROPERTY(int motionResponse READ motionResponse WRITE setMotionResponse
                   NOTIFY motionResponseChanged)
    Q_PROPERTY(int gradientLayers READ gradientLayers WRITE setGradientLayers
                   NOTIFY gradientLayersChanged)
    Q_PROPERTY(int glowIntensity READ glowIntensity WRITE setGlowIntensity
                   NOTIFY glowIntensityChanged)
    Q_PROPERTY(double cinemaShake READ cinemaShake WRITE setCinemaShake
                   NOTIFY cinemaShakeChanged)
    Q_PROPERTY(int autoRotate READ autoRotate WRITE setAutoRotate
                   NOTIFY autoRotateChanged)
    Q_PROPERTY(int peakBoost READ peakBoost WRITE setPeakBoost NOTIFY peakBoostChanged)
    Q_PROPERTY(bool ripplesEnabled READ ripplesEnabled WRITE setRipplesEnabled
                   NOTIFY ripplesEnabledChanged)
    Q_PROPERTY(bool floatingCubesEnabled READ floatingCubesEnabled
                   WRITE setFloatingCubesEnabled NOTIFY floatingCubesEnabledChanged)
    Q_PROPERTY(bool meteorsEnabled READ meteorsEnabled WRITE setMeteorsEnabled
                   NOTIFY meteorsEnabledChanged)
    Q_PROPERTY(bool idleBreathingEnabled READ idleBreathingEnabled
                   WRITE setIdleBreathingEnabled NOTIFY idleBreathingEnabledChanged)
    Q_PROPERTY(bool themeCycleEnabled READ themeCycleEnabled
                   WRITE setThemeCycleEnabled NOTIFY themeCycleEnabledChanged)
    Q_PROPERTY(bool songAdaptiveColorEnabled READ songAdaptiveColorEnabled
                   WRITE setSongAdaptiveColorEnabled
                   NOTIFY songAdaptiveColorEnabledChanged)
    Q_PROPERTY(QVariantList visualEqGains READ visualEqGains WRITE setVisualEqGains
                   NOTIFY visualEqGainsChanged)
    Q_PROPERTY(int lyricClarity READ lyricClarity WRITE setLyricClarity
                   NOTIFY lyricClarityChanged)
    Q_PROPERTY(int lyricDepth READ lyricDepth WRITE setLyricDepth
                   NOTIFY lyricDepthChanged)
    Q_PROPERTY(int lyricSize READ lyricSize WRITE setLyricSize
                   NOTIFY lyricSizeChanged)
    Q_PROPERTY(int lyricOpacity READ lyricOpacity WRITE setLyricOpacity
                   NOTIFY lyricOpacityChanged)
    Q_PROPERTY(int lyricPosition READ lyricPosition WRITE setLyricPosition
                   NOTIFY lyricPositionChanged)
    Q_PROPERTY(int lyricPositionX READ lyricPositionX WRITE setLyricPositionX
                   NOTIFY lyricPositionXChanged)
    Q_PROPERTY(int lyricPositionY READ lyricPositionY WRITE setLyricPositionY
                   NOTIFY lyricPositionYChanged)
    Q_PROPERTY(int inputCompression READ inputCompression WRITE setInputCompression
                   NOTIFY inputCompressionChanged)
    Q_PROPERTY(int audioResponse READ audioResponse WRITE setAudioResponse
                   NOTIFY audioResponseChanged)
    Q_PROPERTY(int responseRange READ responseRange WRITE setResponseRange
                   NOTIFY responseRangeChanged)
    Q_PROPERTY(int centerHighlight READ centerHighlight WRITE setCenterHighlight
                   NOTIFY centerHighlightChanged)
    Q_PROPERTY(int rhythmStrength READ rhythmStrength WRITE setRhythmStrength
                   NOTIFY rhythmStrengthChanged)
    Q_PROPERTY(int depthOfField READ depthOfField WRITE setDepthOfField
                   NOTIFY depthOfFieldChanged)
    Q_PROPERTY(int subjectClarity READ subjectClarity WRITE setSubjectClarity
                   NOTIFY subjectClarityChanged)
    Q_PROPERTY(int autoRotateSpeed READ autoRotateSpeed WRITE setAutoRotateSpeed
                   NOTIFY autoRotateSpeedChanged)
    Q_PROPERTY(int rhythmSensitivity READ rhythmSensitivity WRITE setRhythmSensitivity
                   NOTIFY rhythmSensitivityChanged)

public:
    enum ImmersiveMode { Off = 0, TerrainReactor = 1 };
    Q_ENUM(ImmersiveMode)
    enum HostMode { Windowed = 0, Fullscreen = 1, Desktop = 2 };
    Q_ENUM(HostMode)
    enum QualityPreset { Auto = 0, Eco = 1, Balanced = 2, High = 3, Ultra = 4 };
    Q_ENUM(QualityPreset)
    enum ColorMode { MultiRegion = 0, Custom = 1, RgbSweep = 2 };
    Q_ENUM(ColorMode)
    enum VisualPreset {
        AudioRangeEcho = 0,
        NeonRainNight = 1,
        InkWash = 2,
        PureStage = 3,
        Quiet = 4,
        Galaxy = 5,
    };
    Q_ENUM(VisualPreset)
    enum LyricPosition { Left = 0, Center = 1, Right = 2 };
    Q_ENUM(LyricPosition)

    explicit PlayerExperienceController(SettingsController* settings = nullptr,
                                        QObject* parent = nullptr);

    int immersiveMode() const noexcept;
    int hostMode() const noexcept;
    bool lyricsVisible() const noexcept;
    bool panelVisible() const noexcept;
    bool desktopMousePassthrough() const noexcept;
    int qualityPreset() const noexcept;
    int colorMode() const noexcept;
    QString coolColor() const;
    QString warmColor() const;
    QString accentColor() const;
    QString peakColor() const;
    QString baseColor() const;
    int terrainAmplitude() const noexcept;
    int motionResponse() const noexcept;
    int gradientLayers() const noexcept;
    int glowIntensity() const noexcept;
    double cinemaShake() const noexcept;
    int autoRotate() const noexcept;
    int peakBoost() const noexcept;
    bool ripplesEnabled() const noexcept;
    bool floatingCubesEnabled() const noexcept;
    bool meteorsEnabled() const noexcept;
    bool idleBreathingEnabled() const noexcept;
    bool themeCycleEnabled() const noexcept;
    bool songAdaptiveColorEnabled() const noexcept;
    QVariantList visualEqGains() const;
    int lyricClarity() const noexcept;
    int lyricDepth() const noexcept;
    int lyricSize() const noexcept;
    int lyricOpacity() const noexcept;
    int lyricPosition() const noexcept;
    int lyricPositionX() const noexcept;
    int lyricPositionY() const noexcept;
    int inputCompression() const noexcept;
    int audioResponse() const noexcept;
    int responseRange() const noexcept;
    int centerHighlight() const noexcept;
    int rhythmStrength() const noexcept;
    int depthOfField() const noexcept;
    int subjectClarity() const noexcept;
    int autoRotateSpeed() const noexcept;
    int rhythmSensitivity() const noexcept;

    void setImmersiveMode(int value);
    void setHostMode(int value);
    void setLyricsVisible(bool value);
    void setPanelVisible(bool value);
    void setDesktopMousePassthrough(bool value);
    void setQualityPreset(int value);
    void setColorMode(int value);
    void setCoolColor(const QString& value);
    void setWarmColor(const QString& value);
    void setAccentColor(const QString& value);
    void setPeakColor(const QString& value);
    void setBaseColor(const QString& value);
    void setTerrainAmplitude(int value);
    void setMotionResponse(int value);
    void setGradientLayers(int value);
    void setGlowIntensity(int value);
    void setCinemaShake(double value);
    void setAutoRotate(int value);
    void setPeakBoost(int value);
    void setRipplesEnabled(bool value);
    void setFloatingCubesEnabled(bool value);
    void setMeteorsEnabled(bool value);
    void setIdleBreathingEnabled(bool value);
    void setThemeCycleEnabled(bool value);
    void setSongAdaptiveColorEnabled(bool value);
    void setVisualEqGains(const QVariantList& values);
    void setLyricClarity(int value);
    void setLyricDepth(int value);
    void setLyricSize(int value);
    void setLyricOpacity(int value);
    void setLyricPosition(int value);
    void setLyricPositionX(int value);
    void setLyricPositionY(int value);
    void setInputCompression(int value);
    void setAudioResponse(int value);
    void setResponseRange(int value);
    void setCenterHighlight(int value);
    void setRhythmStrength(int value);
    void setDepthOfField(int value);
    void setSubjectClarity(int value);
    void setAutoRotateSpeed(int value);
    void setRhythmSensitivity(int value);

    Q_INVOKABLE bool applyPreset(int preset);
    Q_INVOKABLE void toggleImmersiveMode();
    Q_INVOKABLE void toggleLyricsVisible();
    Q_INVOKABLE void togglePanelVisible();
    Q_INVOKABLE void togglePlayerShellMode();

signals:
    void immersiveModeChanged();
    void hostModeChanged();
    void lyricsVisibleChanged();
    void panelVisibleChanged();
    void desktopMousePassthroughChanged();
    void qualityPresetChanged();
    void colorModeChanged();
    void coolColorChanged();
    void warmColorChanged();
    void accentColorChanged();
    void peakColorChanged();
    void baseColorChanged();
    void terrainAmplitudeChanged();
    void motionResponseChanged();
    void gradientLayersChanged();
    void glowIntensityChanged();
    void cinemaShakeChanged();
    void autoRotateChanged();
    void peakBoostChanged();
    void ripplesEnabledChanged();
    void floatingCubesEnabledChanged();
    void meteorsEnabledChanged();
    void idleBreathingEnabledChanged();
    void themeCycleEnabledChanged();
    void songAdaptiveColorEnabledChanged();
    void visualEqGainsChanged();
    void lyricClarityChanged();
    void lyricDepthChanged();
    void lyricSizeChanged();
    void lyricOpacityChanged();
    void lyricPositionChanged();
    void lyricPositionXChanged();
    void lyricPositionYChanged();
    void inputCompressionChanged();
    void audioResponseChanged();
    void responseRangeChanged();
    void centerHighlightChanged();
    void rhythmStrengthChanged();
    void depthOfFieldChanged();
    void subjectClarityChanged();
    void autoRotateSpeedChanged();
    void rhythmSensitivityChanged();

private:
    void load();
    void persist(const QString& key, const QVariant& value);
    static int clampPercent(int value) noexcept;
    static int clampRange(int value, int minimum, int maximum) noexcept;
    static QString normalizedColor(const QString& value, const QString& fallback);
    static QVariantList defaultVisualEqGains();
    static QVariantList normalizedVisualEqGains(const QVariantList& values);

    QSettings settings_;
    QPointer<SettingsController> settingsController_;
    int immersiveMode_ = Off;
    int hostMode_ = Windowed;
    bool lyricsVisible_ = false;
    bool panelVisible_ = true;
    bool desktopMousePassthrough_ = false;
    int qualityPreset_ = Auto;
    int colorMode_ = MultiRegion;
    QString coolColor_ = QStringLiteral("#4F6FFF");
    QString warmColor_ = QStringLiteral("#FF4778");
    QString accentColor_ = QStringLiteral("#77EAFF");
    QString peakColor_ = QStringLiteral("#D7FF58");
    QString baseColor_ = QStringLiteral("#080616");
    int terrainAmplitude_ = 62;
    int motionResponse_ = 56;
    int gradientLayers_ = 74;
    int glowIntensity_ = 38;
    double cinemaShake_ = 0.40;
    int autoRotate_ = 54;
    int peakBoost_ = 58;
    bool ripplesEnabled_ = true;
    bool floatingCubesEnabled_ = true;
    bool meteorsEnabled_ = true;
    bool idleBreathingEnabled_ = true;
    bool themeCycleEnabled_ = false;
    bool songAdaptiveColorEnabled_ = true;
    QVariantList visualEqGains_ = defaultVisualEqGains();
    int lyricClarity_ = 78;
    int lyricDepth_ = 62;
    int lyricSize_ = 100;
    int lyricOpacity_ = 88;
    int lyricPosition_ = Center;
    int lyricPositionX_ = 50;
    int lyricPositionY_ = 42;
    int inputCompression_ = 82;
    int audioResponse_ = 128;
    int responseRange_ = 100;
    int centerHighlight_ = 58;
    int rhythmStrength_ = 30;
    int depthOfField_ = 86;
    int subjectClarity_ = 110;
    int autoRotateSpeed_ = 42;
    int rhythmSensitivity_ = 78;
};
