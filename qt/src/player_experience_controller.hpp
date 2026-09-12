#pragma once

#include <QObject>
#include <QColor>
#include <QPointer>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

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
    Q_PROPERTY(int materialMode READ materialMode WRITE setMaterialMode NOTIFY materialModeChanged)
    Q_PROPERTY(int materialSoftness READ materialSoftness WRITE setMaterialSoftness NOTIFY materialSoftnessChanged)
    Q_PROPERTY(int jellyElasticity READ jellyElasticity WRITE setJellyElasticity NOTIFY jellyElasticityChanged)
    Q_PROPERTY(int inkDensity READ inkDensity WRITE setInkDensity NOTIFY inkDensityChanged)
    Q_PROPERTY(int rippleStrength READ rippleStrength WRITE setRippleStrength NOTIFY rippleStrengthChanged)
    Q_PROPERTY(int rippleWidth READ rippleWidth WRITE setRippleWidth NOTIFY rippleWidthChanged)
    Q_PROPERTY(int rippleDecay READ rippleDecay WRITE setRippleDecay NOTIFY rippleDecayChanged)
    Q_PROPERTY(int floatingBlockMinSize READ floatingBlockMinSize WRITE setFloatingBlockMinSize NOTIFY floatingBlockMinSizeChanged)
    Q_PROPERTY(int floatingBlockMaxSize READ floatingBlockMaxSize WRITE setFloatingBlockMaxSize NOTIFY floatingBlockMaxSizeChanged)
    Q_PROPERTY(int floatingBlockSpeed READ floatingBlockSpeed WRITE setFloatingBlockSpeed NOTIFY floatingBlockSpeedChanged)
    Q_PROPERTY(int floatingBlockIntensity READ floatingBlockIntensity WRITE setFloatingBlockIntensity NOTIFY floatingBlockIntensityChanged)
    Q_PROPERTY(int columnSize READ columnSize WRITE setColumnSize NOTIFY columnSizeChanged)
    Q_PROPERTY(int columnDensity READ columnDensity WRITE setColumnDensity NOTIFY columnDensityChanged)
    Q_PROPERTY(int topographyDensity READ topographyDensity WRITE setTopographyDensity
                   NOTIFY topographyDensityChanged)
    Q_PROPERTY(int columnOpacity READ columnOpacity WRITE setColumnOpacity NOTIFY columnOpacityChanged)
    Q_PROPERTY(int reactorBrightness READ reactorBrightness WRITE setReactorBrightness NOTIFY reactorBrightnessChanged)
    Q_PROPERTY(int columnInnerLight READ columnInnerLight WRITE setColumnInnerLight NOTIFY columnInnerLightChanged)
    Q_PROPERTY(int columnLightSpill READ columnLightSpill WRITE setColumnLightSpill NOTIFY columnLightSpillChanged)
    Q_PROPERTY(int columnLightRadius READ columnLightRadius WRITE setColumnLightRadius NOTIFY columnLightRadiusChanged)
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
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap customColors READ customColors NOTIFY customColorsChanged)
    Q_PROPERTY(QVariantList builtInThemeChoices READ builtInThemeChoices CONSTANT)
    Q_PROPERTY(float themeGlow READ themeGlow NOTIFY themeChanged)
    Q_PROPERTY(QColor themeBackground READ themeBackground NOTIFY themeChanged)
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
    Q_PROPERTY(bool burstEnabled READ burstEnabled WRITE setBurstEnabled
                   NOTIFY burstEnabledChanged)
    Q_PROPERTY(bool floatingCubesEnabled READ floatingCubesEnabled
                   WRITE setFloatingCubesEnabled NOTIFY floatingCubesEnabledChanged)
    Q_PROPERTY(bool meteorsEnabled READ meteorsEnabled WRITE setMeteorsEnabled
                   NOTIFY meteorsEnabledChanged)
    Q_PROPERTY(bool idleBreathingEnabled READ idleBreathingEnabled
                   WRITE setIdleBreathingEnabled NOTIFY idleBreathingEnabledChanged)
    Q_PROPERTY(bool themeCycleEnabled READ themeCycleEnabled
                   WRITE setThemeCycleEnabled NOTIFY themeCycleEnabledChanged)
    Q_PROPERTY(bool themeSongCycleEnabled READ themeSongCycleEnabled
                   WRITE setThemeSongCycleEnabled NOTIFY themeSongCycleEnabledChanged)
    Q_PROPERTY(int themeCycleIntervalSeconds READ themeCycleIntervalSeconds
                   WRITE setThemeCycleIntervalSeconds
                   NOTIFY themeCycleIntervalSecondsChanged)
    Q_PROPERTY(bool streamHighlightEnabled READ streamHighlightEnabled
                   WRITE setStreamHighlightEnabled
                   NOTIFY streamHighlightEnabledChanged)
    Q_PROPERTY(bool songAdaptiveColorEnabled READ songAdaptiveColorEnabled
                   WRITE setSongAdaptiveColorEnabled
                   NOTIFY songAdaptiveColorEnabledChanged)
    Q_PROPERTY(QVariantList visualEqEnabled READ visualEqEnabled WRITE setVisualEqEnabled NOTIFY visualEqEnabledChanged)
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
    enum ColorMode { MultiRegion = 0, Custom = 1, RgbSweep = 2, RainbowColumn = 3 };
    Q_ENUM(ColorMode)
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
    int materialMode() const noexcept;
    int materialSoftness() const noexcept;
    int jellyElasticity() const noexcept;
    int inkDensity() const noexcept;
    int rippleStrength() const noexcept;
    int rippleWidth() const noexcept;
    int rippleDecay() const noexcept;
    int floatingBlockMinSize() const noexcept { return floatingBlockMinSize_; }
    int floatingBlockMaxSize() const noexcept { return floatingBlockMaxSize_; }
    int floatingBlockSpeed() const noexcept { return floatingBlockSpeed_; }
    int floatingBlockIntensity() const noexcept { return floatingBlockIntensity_; }
    int columnSize() const noexcept;
    int columnDensity() const noexcept;
    int topographyDensity() const noexcept;
    int columnOpacity() const noexcept;
    int reactorBrightness() const noexcept;
    int columnInnerLight() const noexcept;
    int columnLightSpill() const noexcept;
    int columnLightRadius() const noexcept;
    QString coolColor() const;
    QString warmColor() const;
    QString accentColor() const;
    QString peakColor() const;
    QString baseColor() const;
    QString themeId() const;
    QVariantList builtInThemeChoices() const;
    float themeGlow() const noexcept;
    QColor themeBackground() const;
    int terrainAmplitude() const noexcept;
    int motionResponse() const noexcept;
    int gradientLayers() const noexcept;
    int glowIntensity() const noexcept;
    double cinemaShake() const noexcept;
    int autoRotate() const noexcept;
    int peakBoost() const noexcept;
    bool ripplesEnabled() const noexcept;
    bool burstEnabled() const noexcept;
    bool floatingCubesEnabled() const noexcept;
    bool meteorsEnabled() const noexcept;
    bool idleBreathingEnabled() const noexcept;
    bool themeCycleEnabled() const noexcept;
    bool themeSongCycleEnabled() const noexcept;
    int themeCycleIntervalSeconds() const noexcept;
    bool streamHighlightEnabled() const noexcept;
    bool songAdaptiveColorEnabled() const noexcept;
    QVariantList visualEqGains() const;
    QVariantList visualEqEnabled() const { return visualEqEnabled_; }
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
    void setMaterialMode(int value);
    void setMaterialSoftness(int value);
    void setJellyElasticity(int value);
    void setInkDensity(int value);
    void setRippleStrength(int value);
    void setRippleWidth(int value);
    void setRippleDecay(int value);
    void setColumnSize(int value);
    void setColumnDensity(int value);
    void setTopographyDensity(int value);
    void setColumnOpacity(int value);
    void setReactorBrightness(int value);
    void setColumnInnerLight(int value);
    void setColumnLightSpill(int value);
    void setColumnLightRadius(int value);
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
    void setBurstEnabled(bool value);
    void setFloatingCubesEnabled(bool value);
    void setFloatingBlockMinSize(int value);
    void setFloatingBlockMaxSize(int value);
    void setFloatingBlockSpeed(int value);
    void setFloatingBlockIntensity(int value);
    void setMeteorsEnabled(bool value);
    void setIdleBreathingEnabled(bool value);
    void setThemeCycleEnabled(bool value);
    void setThemeSongCycleEnabled(bool value);
    void setThemeCycleIntervalSeconds(int value);
    void setStreamHighlightEnabled(bool value);
    void setSongAdaptiveColorEnabled(bool value);
    void setVisualEqGains(const QVariantList& values);
    void setVisualEqEnabled(const QVariantList& values);
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

    Q_INVOKABLE bool applyTheme(const QString& id);
    QVariantMap customColors() const { return customColors_; }
    Q_INVOKABLE void applyCustomColors();
    Q_INVOKABLE bool setCustomColor(const QString& key, const QString& value);
    Q_INVOKABLE bool previewCustomColor(const QString& key, const QString& value);
    Q_INVOKABLE void restoreDynamicDefaults();
    Q_INVOKABLE void toggleImmersiveMode();
    Q_INVOKABLE void toggleLyricsVisible();
    Q_INVOKABLE void togglePanelVisible();
    Q_INVOKABLE void togglePlayerShellMode();
    Q_INVOKABLE void cycleExperienceTheme();

signals:
    void customColorsChanged();
    void immersiveModeChanged();
    void hostModeChanged();
    void lyricsVisibleChanged();
    void panelVisibleChanged();
    void desktopMousePassthroughChanged();
    void qualityPresetChanged();
    void colorModeChanged();
    void materialModeChanged();
    void materialSoftnessChanged();
    void jellyElasticityChanged();
    void inkDensityChanged();
    void rippleStrengthChanged();
    void rippleWidthChanged();
    void rippleDecayChanged();
    void floatingBlockMinSizeChanged();
    void floatingBlockMaxSizeChanged();
    void floatingBlockSpeedChanged();
    void floatingBlockIntensityChanged();
    void columnSizeChanged();
    void columnDensityChanged();
    void topographyDensityChanged();
    void columnOpacityChanged();
    void reactorBrightnessChanged();
    void columnInnerLightChanged();
    void columnLightSpillChanged();
    void columnLightRadiusChanged();
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
    void burstEnabledChanged();
    void floatingCubesEnabledChanged();
    void meteorsEnabledChanged();
    void idleBreathingEnabledChanged();
    void themeCycleEnabledChanged();
    void themeSongCycleEnabledChanged();
    void themeCycleIntervalSecondsChanged();
    void streamHighlightEnabledChanged();
    void songAdaptiveColorEnabledChanged();
    void visualEqGainsChanged();
    void visualEqEnabledChanged();
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
    void themeChanged();

private:
    void load();
    void persist(const QString& key, const QVariant& value);
    static int clampPercent(int value) noexcept;
    static int clampRange(int value, int minimum, int maximum) noexcept;
    static QString normalizedColor(const QString& value, const QString& fallback);
    static QVariantList defaultVisualEqGains();
    static QVariantList normalizedVisualEqEnabled(const QVariantList& values);
    static QVariantList normalizedVisualEqGains(const QVariantList& values);
    void setThemeId(const QString& id);
    void clearThemeForManualColor();
    void applyCustomPalette(const QVariantMap& colors, bool preview);

    QSettings settings_;
    QPointer<SettingsController> settingsController_;
    int immersiveMode_ = Off;
    int hostMode_ = Windowed;
    bool lyricsVisible_ = false;
    bool panelVisible_ = true;
    bool desktopMousePassthrough_ = false;
    int qualityPreset_ = Auto;
    int colorMode_ = MultiRegion;
    int materialMode_ = 0;
    int materialSoftness_ = 45;
    int jellyElasticity_ = 35;
    int inkDensity_ = 60;
    int rippleStrength_ = 100;
    int rippleWidth_ = 100;
    int rippleDecay_ = 100;
    int floatingBlockMinSize_ = 9;
    int floatingBlockMaxSize_ = 26;
    int floatingBlockSpeed_ = 77;
    int floatingBlockIntensity_ = 55;
    int columnSize_ = 100;
    int columnDensity_ = 130;
    int topographyDensity_ = 46;
    int columnOpacity_ = 100;
    int reactorBrightness_ = 100;
    int columnInnerLight_ = 100;
    int columnLightSpill_ = 20;
    int columnLightRadius_ = 100;
    QString coolColor_ = QStringLiteral("#6553DD");
    QString warmColor_ = QStringLiteral("#F467A9");
    QString accentColor_ = QStringLiteral("#AD62ED");
    QString peakColor_ = QStringLiteral("#FFE2EE");
    QString baseColor_ = QStringLiteral("#030817");
    QString themeId_;
    float themeGlow_ = 1.0F;
    QColor themeBackground_ = QColor(QStringLiteral("#030817"));
    bool applyingTheme_ = false;
    bool previewingCustomColor_ = false;
    QVariantMap customColors_{
        {QStringLiteral("coolColor"), QStringLiteral("#8B4AF0")},
        {QStringLiteral("warmColor"), QStringLiteral("#FF469E")},
        {QStringLiteral("accentColor"), QStringLiteral("#BE6AFF")},
        {QStringLiteral("peakColor"), QStringLiteral("#FF9ACD")},
        {QStringLiteral("baseColor"), QStringLiteral("#05020A")},
    };
    int terrainAmplitude_ = 50;
    int motionResponse_ = 50;
    int gradientLayers_ = 74;
    int glowIntensity_ = 38;
    double cinemaShake_ = 0.30;
    int autoRotate_ = 54;
    int peakBoost_ = 58;
    bool ripplesEnabled_ = true;
    bool burstEnabled_ = true;
    bool floatingCubesEnabled_ = true;
    bool meteorsEnabled_ = true;
    bool idleBreathingEnabled_ = true;
    bool themeCycleEnabled_ = false;
    bool themeSongCycleEnabled_ = false;
    int themeCycleIntervalSeconds_ = 10;
    bool streamHighlightEnabled_ = true;
    bool songAdaptiveColorEnabled_ = false;
    QVariantList visualEqGains_ = defaultVisualEqGains();
    QVariantList visualEqEnabled_{true,true,true,true,true,true,true,true};
    int lyricClarity_ = 78;
    int lyricDepth_ = 62;
    int lyricSize_ = 100;
    int lyricOpacity_ = 88;
    int lyricPosition_ = Center;
    int lyricPositionX_ = 50;
    int lyricPositionY_ = 42;
    int inputCompression_ = 82;
    int audioResponse_ = 136;
    int responseRange_ = 100;
    int centerHighlight_ = 64;
    int rhythmStrength_ = 30;
    int depthOfField_ = 86;
    int subjectClarity_ = 112;
    int autoRotateSpeed_ = 15;
    int rhythmSensitivity_ = 100;
};
