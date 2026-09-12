#include "player_experience_controller.hpp"

#include "immersive_theme_catalog.hpp"
#include "settings_controller.hpp"

#include <QColor>
#include <QMetaType>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {

constexpr auto kSettingsGroup = "immersiveVisual";

int enumOrDefault(const int value, const int first, const int last,
                  const int fallback) noexcept
{
    return value >= first && value <= last ? value : fallback;
}

std::optional<int> storedInteger(const QVariant& value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: {
        bool ok = false;
        const int parsed = value.toInt(&ok);
        return ok ? std::optional<int>(parsed) : std::nullopt;
    }
    case QMetaType::QString: {
        const QString stored = value.toString();
        bool ok = false;
        const int parsed = stored.toInt(&ok);
        return ok && stored == QString::number(parsed)
            ? std::optional<int>(parsed) : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

std::optional<double> storedDouble(const QVariant& value)
{
    double parsed = 0.0;
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Float:
    case QMetaType::Double: {
        bool ok = false;
        parsed = value.toDouble(&ok);
        if (!ok) return std::nullopt;
        break;
    }
    case QMetaType::QString: {
        const QString stored = value.toString();
        bool ok = false;
        parsed = stored.toDouble(&ok);
        if (!ok || stored != QString::number(parsed, 'g', 15)) {
            return std::nullopt;
        }
        break;
    }
    default:
        return std::nullopt;
    }
    return std::isfinite(parsed) ? std::optional<double>(parsed) : std::nullopt;
}

std::optional<bool> storedBoolean(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    if (value.metaType().id() == QMetaType::QString) {
        const QString stored = value.toString();
        if (stored == QStringLiteral("true")) return true;
        if (stored == QStringLiteral("false")) return false;
    }
    return std::nullopt;
}

QString storedColor(const QVariant& value, const QString& fallback)
{
    return value.metaType().id() == QMetaType::QString
        ? value.toString() : fallback;
}

} // namespace

PlayerExperienceController::PlayerExperienceController(SettingsController* settings,
                                                       QObject* parent)
    : QObject(parent), settings_(this), settingsController_(settings)
{
    load();
}

int PlayerExperienceController::immersiveMode() const noexcept { return immersiveMode_; }
int PlayerExperienceController::hostMode() const noexcept { return hostMode_; }
bool PlayerExperienceController::lyricsVisible() const noexcept { return lyricsVisible_; }
bool PlayerExperienceController::panelVisible() const noexcept { return panelVisible_; }
bool PlayerExperienceController::desktopMousePassthrough() const noexcept
{
    return desktopMousePassthrough_;
}
int PlayerExperienceController::qualityPreset() const noexcept { return qualityPreset_; }
int PlayerExperienceController::colorMode() const noexcept { return colorMode_; }
int PlayerExperienceController::materialMode() const noexcept { return materialMode_; }
int PlayerExperienceController::materialSoftness() const noexcept { return materialSoftness_; }
int PlayerExperienceController::jellyElasticity() const noexcept { return jellyElasticity_; }
int PlayerExperienceController::inkDensity() const noexcept { return inkDensity_; }
int PlayerExperienceController::rippleStrength() const noexcept { return rippleStrength_; }
int PlayerExperienceController::rippleWidth() const noexcept { return rippleWidth_; }
int PlayerExperienceController::rippleDecay() const noexcept { return rippleDecay_; }
int PlayerExperienceController::columnSize() const noexcept { return columnSize_; }
int PlayerExperienceController::columnDensity() const noexcept { return columnDensity_; }
int PlayerExperienceController::topographyDensity() const noexcept { return topographyDensity_; }
int PlayerExperienceController::columnOpacity() const noexcept { return columnOpacity_; }
int PlayerExperienceController::reactorBrightness() const noexcept { return reactorBrightness_; }
int PlayerExperienceController::columnInnerLight() const noexcept { return columnInnerLight_; }
int PlayerExperienceController::columnLightSpill() const noexcept { return columnLightSpill_; }
int PlayerExperienceController::columnLightRadius() const noexcept { return columnLightRadius_; }
QString PlayerExperienceController::coolColor() const { return coolColor_; }
QString PlayerExperienceController::warmColor() const { return warmColor_; }
QString PlayerExperienceController::accentColor() const { return accentColor_; }
QString PlayerExperienceController::peakColor() const { return peakColor_; }
QString PlayerExperienceController::baseColor() const { return baseColor_; }
QString PlayerExperienceController::themeId() const { return themeId_; }
QVariantList PlayerExperienceController::builtInThemeChoices() const
{
    static const std::array<QString, 13> titles = {
        QStringLiteral("水墨"), QStringLiteral("夜色"), QStringLiteral("东京霓虹"),
        QStringLiteral("赛博森林"), QStringLiteral("极简黑白"), QStringLiteral("冰川白昼"),
        QStringLiteral("锦鲤池"), QStringLiteral("珊瑚礁"), QStringLiteral("苔藓玻璃"),
        QStringLiteral("蓝调时刻"), QStringLiteral("青瓷"), QStringLiteral("绯红信号"),
        QStringLiteral("紫夜霓心"),
    };
    QVariantList choices;
    choices.reserve(static_cast<qsizetype>(agplayer::immersive::builtInThemes().size()));
    for (std::size_t index = 0; index < agplayer::immersive::builtInThemes().size(); ++index) {
        const auto& theme = agplayer::immersive::builtInThemes().at(index);
        const auto color = [&theme](const agplayer::immersive::ThemeColorRole role) {
            const auto rgb = agplayer::immersive::workingLinearToSrgb(
                agplayer::immersive::toWorkingLinear(
                    theme.colors.at(agplayer::immersive::themeColorIndex(role))));
            return QColor::fromRgbF(rgb.red, rgb.green, rgb.blue).name(QColor::HexRgb)
                .toUpper();
        };
        choices.append(QVariantMap{{QStringLiteral("id"), QString::fromUtf8(theme.id.data(),
                                                               static_cast<qsizetype>(theme.id.size()))},
                                   {QStringLiteral("title"), titles.at(index)},
                                   {QStringLiteral("from"), color(agplayer::immersive::ThemeColorRole::CoolCore)},
                                   {QStringLiteral("to"), color(agplayer::immersive::ThemeColorRole::WarmCore)},
                                   {QStringLiteral("background"), color(agplayer::immersive::ThemeColorRole::Fog)}});
    }
    return choices;
}
float PlayerExperienceController::themeGlow() const noexcept { return themeGlow_; }
QColor PlayerExperienceController::themeBackground() const { return themeBackground_; }
int PlayerExperienceController::terrainAmplitude() const noexcept { return terrainAmplitude_; }
int PlayerExperienceController::motionResponse() const noexcept { return motionResponse_; }
int PlayerExperienceController::gradientLayers() const noexcept { return gradientLayers_; }
int PlayerExperienceController::glowIntensity() const noexcept { return glowIntensity_; }
double PlayerExperienceController::cinemaShake() const noexcept { return cinemaShake_; }
int PlayerExperienceController::autoRotate() const noexcept { return autoRotate_; }
int PlayerExperienceController::peakBoost() const noexcept { return peakBoost_; }
bool PlayerExperienceController::ripplesEnabled() const noexcept { return ripplesEnabled_; }
bool PlayerExperienceController::burstEnabled() const noexcept { return burstEnabled_; }
bool PlayerExperienceController::floatingCubesEnabled() const noexcept
{
    return floatingCubesEnabled_;
}
bool PlayerExperienceController::meteorsEnabled() const noexcept { return meteorsEnabled_; }
bool PlayerExperienceController::idleBreathingEnabled() const noexcept
{
    return idleBreathingEnabled_;
}
bool PlayerExperienceController::themeCycleEnabled() const noexcept
{
    return themeCycleEnabled_;
}
bool PlayerExperienceController::themeSongCycleEnabled() const noexcept
{
    return themeSongCycleEnabled_;
}
int PlayerExperienceController::themeCycleIntervalSeconds() const noexcept
{
    return themeCycleIntervalSeconds_;
}
bool PlayerExperienceController::streamHighlightEnabled() const noexcept
{
    return streamHighlightEnabled_;
}

bool PlayerExperienceController::songAdaptiveColorEnabled() const noexcept
{
    return songAdaptiveColorEnabled_;
}

QVariantList PlayerExperienceController::visualEqGains() const { return visualEqGains_; }
int PlayerExperienceController::lyricClarity() const noexcept { return lyricClarity_; }
int PlayerExperienceController::lyricDepth() const noexcept { return lyricDepth_; }
int PlayerExperienceController::lyricSize() const noexcept { return lyricSize_; }
int PlayerExperienceController::lyricOpacity() const noexcept { return lyricOpacity_; }
int PlayerExperienceController::lyricPosition() const noexcept { return lyricPosition_; }
int PlayerExperienceController::lyricPositionX() const noexcept { return lyricPositionX_; }
int PlayerExperienceController::lyricPositionY() const noexcept { return lyricPositionY_; }
int PlayerExperienceController::inputCompression() const noexcept { return inputCompression_; }
int PlayerExperienceController::audioResponse() const noexcept { return audioResponse_; }
int PlayerExperienceController::responseRange() const noexcept { return responseRange_; }
int PlayerExperienceController::centerHighlight() const noexcept { return centerHighlight_; }
int PlayerExperienceController::rhythmStrength() const noexcept { return rhythmStrength_; }
int PlayerExperienceController::depthOfField() const noexcept { return depthOfField_; }
int PlayerExperienceController::subjectClarity() const noexcept { return subjectClarity_; }
int PlayerExperienceController::autoRotateSpeed() const noexcept { return autoRotateSpeed_; }
int PlayerExperienceController::rhythmSensitivity() const noexcept
{
    return rhythmSensitivity_;
}

void PlayerExperienceController::setMaterialMode(int value)
{
    value = enumOrDefault(value, 0, 2, 0);
    if (!themeId_.isEmpty() && !applyingTheme_ && value != 0) setThemeId({});
    if (materialMode_ == value) return;
    materialMode_ = value;
    persist(QStringLiteral("materialMode"), value);
    emit materialModeChanged();
}

void PlayerExperienceController::setMaterialSoftness(int value)
{
    value = clampRange(value, 0, 100);
    if (materialSoftness_ == value) return;
    materialSoftness_ = value;
    persist(QStringLiteral("materialSoftness"), value);
    emit materialSoftnessChanged();
}

void PlayerExperienceController::setJellyElasticity(int value)
{
    value = clampRange(value, 0, 100);
    if (jellyElasticity_ == value) return;
    jellyElasticity_ = value;
    persist(QStringLiteral("jellyElasticity"), value);
    emit jellyElasticityChanged();
}

void PlayerExperienceController::setInkDensity(int value)
{
    value = clampRange(value, 0, 100);
    if (inkDensity_ == value) return;
    inkDensity_ = value;
    persist(QStringLiteral("inkDensity"), value);
    emit inkDensityChanged();
}

void PlayerExperienceController::setRippleStrength(int value)
{
    value = clampRange(value, 0, 200);
    if (rippleStrength_ == value) return;
    rippleStrength_ = value;
    persist(QStringLiteral("rippleStrength"), value);
    emit rippleStrengthChanged();
}

void PlayerExperienceController::setRippleWidth(int value)
{
    value = clampRange(value, 20, 200);
    if (rippleWidth_ == value) return;
    rippleWidth_ = value;
    persist(QStringLiteral("rippleWidth"), value);
    emit rippleWidthChanged();
}

void PlayerExperienceController::setRippleDecay(int value)
{
    value = clampRange(value, 20, 200);
    if (rippleDecay_ == value) return;
    rippleDecay_ = value;
    persist(QStringLiteral("rippleDecay"), value);
    emit rippleDecayChanged();
}

void PlayerExperienceController::setColumnDensity(int value)
{
    value = clampRange(value, 50, 200);
    if (columnDensity_ == value) return;
    columnDensity_ = value;
    persist(QStringLiteral("columnDensity"), value);
    emit columnDensityChanged();
}

void PlayerExperienceController::setTopographyDensity(int value)
{
    value = clampPercent(value);
    if (topographyDensity_ == value) return;
    topographyDensity_ = value;
    persist(QStringLiteral("topographyDensity"), value);
    emit topographyDensityChanged();
}

void PlayerExperienceController::setColumnSize(int value)
{
    value = clampRange(value, 50, 200);
    if (columnSize_ == value) return;
    columnSize_ = value;
    persist(QStringLiteral("columnSize"), value);
    emit columnSizeChanged();
}

void PlayerExperienceController::setColumnOpacity(int value)
{
    value = clampRange(value, 0, 100);
    if (columnOpacity_ == value) return;
    columnOpacity_ = value;
    persist(QStringLiteral("columnOpacity"), value);
    emit columnOpacityChanged();
}

void PlayerExperienceController::setReactorBrightness(int value)
{
    value = clampRange(value, 0, 200);
    if (reactorBrightness_ == value) return;
    reactorBrightness_ = value;
    persist(QStringLiteral("reactorBrightness"), value);
    emit reactorBrightnessChanged();
}

void PlayerExperienceController::setColumnInnerLight(int value)
{
    value = clampRange(value, 0, 200);
    if (columnInnerLight_ == value) return;
    columnInnerLight_ = value;
    persist(QStringLiteral("columnInnerLight"), value);
    emit columnInnerLightChanged();
}

void PlayerExperienceController::setColumnLightSpill(int value)
{
    value = clampRange(value, 0, 200);
    if (columnLightSpill_ == value) return;
    columnLightSpill_ = value;
    persist(QStringLiteral("columnLightSpill"), value);
    emit columnLightSpillChanged();
}

void PlayerExperienceController::setColumnLightRadius(int value)
{
    value = clampRange(value, 20, 200);
    if (columnLightRadius_ == value) return;
    columnLightRadius_ = value;
    persist(QStringLiteral("columnLightRadius"), value);
    emit columnLightRadiusChanged();
}

void PlayerExperienceController::setImmersiveMode(int value)
{
    value = value == TerrainReactor ? TerrainReactor : Off;
    if (immersiveMode_ == value) return;
    immersiveMode_ = value;
    emit immersiveModeChanged();
}

void PlayerExperienceController::setHostMode(int value)
{
    value = enumOrDefault(value, Windowed, Desktop, Windowed);
    if (hostMode_ == value) return;
    hostMode_ = value;
    persist(QStringLiteral("hostMode"), value);
    emit hostModeChanged();
}

void PlayerExperienceController::setLyricsVisible(bool value)
{
    if (lyricsVisible_ == value) return;
    lyricsVisible_ = value;
    persist(QStringLiteral("lyricsVisible"), value);
    emit lyricsVisibleChanged();
}

void PlayerExperienceController::setPanelVisible(bool value)
{
    if (panelVisible_ == value) return;
    panelVisible_ = value;
    persist(QStringLiteral("panelVisible"), value);
    emit panelVisibleChanged();
}

void PlayerExperienceController::setDesktopMousePassthrough(bool value)
{
    if (desktopMousePassthrough_ == value) return;
    desktopMousePassthrough_ = value;
    persist(QStringLiteral("desktopMousePassthrough"), value);
    emit desktopMousePassthroughChanged();
}

void PlayerExperienceController::setQualityPreset(int value)
{
    value = enumOrDefault(value, Auto, Ultra, Auto);
    if (qualityPreset_ == value) return;
    qualityPreset_ = value;
    persist(QStringLiteral("qualityPreset"), value);
    emit qualityPresetChanged();
}

void PlayerExperienceController::setColorMode(int value)
{
    value = enumOrDefault(value, MultiRegion, RainbowColumn, MultiRegion);
    if (!themeId_.isEmpty() && !applyingTheme_ && value != MultiRegion) setThemeId({});
    if (colorMode_ == value) return;
    colorMode_ = value;
    persist(QStringLiteral("colorMode"), value);
    emit colorModeChanged();
}

void PlayerExperienceController::setCoolColor(const QString& value)
{
    clearThemeForManualColor();
    const QString normalized = normalizedColor(value, QStringLiteral("#4F6FFF"));
    if (coolColor_ == normalized) return;
    coolColor_ = normalized;
    persist(QStringLiteral("coolColor"), normalized);
    emit coolColorChanged();
}

void PlayerExperienceController::setWarmColor(const QString& value)
{
    clearThemeForManualColor();
    const QString normalized = normalizedColor(value, QStringLiteral("#FF4778"));
    if (warmColor_ == normalized) return;
    warmColor_ = normalized;
    persist(QStringLiteral("warmColor"), normalized);
    emit warmColorChanged();
}

void PlayerExperienceController::setAccentColor(const QString& value)
{
    clearThemeForManualColor();
    const QString normalized = normalizedColor(value, QStringLiteral("#77EAFF"));
    if (accentColor_ == normalized) return;
    accentColor_ = normalized;
    persist(QStringLiteral("accentColor"), normalized);
    emit accentColorChanged();
}

void PlayerExperienceController::setPeakColor(const QString& value)
{
    clearThemeForManualColor();
    const QString normalized = normalizedColor(value, QStringLiteral("#D7FF58"));
    if (peakColor_ == normalized) return;
    peakColor_ = normalized;
    persist(QStringLiteral("peakColor"), normalized);
    emit peakColorChanged();
}

void PlayerExperienceController::setBaseColor(const QString& value)
{
    clearThemeForManualColor();
    const QString normalized = normalizedColor(value, QStringLiteral("#080616"));
    if (baseColor_ == normalized) return;
    baseColor_ = normalized;
    persist(QStringLiteral("baseColor"), normalized);
    emit baseColorChanged();
}

void PlayerExperienceController::setTerrainAmplitude(int value)
{
    value = clampPercent(value);
    if (terrainAmplitude_ == value) return;
    terrainAmplitude_ = value;
    persist(QStringLiteral("terrainAmplitude"), value);
    emit terrainAmplitudeChanged();
}

void PlayerExperienceController::setMotionResponse(int value)
{
    value = clampPercent(value);
    if (motionResponse_ == value) return;
    motionResponse_ = value;
    persist(QStringLiteral("motionResponse"), value);
    emit motionResponseChanged();
}

void PlayerExperienceController::setGradientLayers(int value)
{
    value = clampPercent(value);
    if (gradientLayers_ == value) return;
    gradientLayers_ = value;
    persist(QStringLiteral("gradientLayers"), value);
    emit gradientLayersChanged();
}

void PlayerExperienceController::setGlowIntensity(int value)
{
    value = clampPercent(value);
    if (glowIntensity_ == value) return;
    glowIntensity_ = value;
    persist(QStringLiteral("glowIntensity"), value);
    emit glowIntensityChanged();
}

void PlayerExperienceController::setCinemaShake(double value)
{
    value = std::clamp(value, 0.0, 1.8);
    if (qFuzzyCompare(cinemaShake_, value)) return;
    cinemaShake_ = value;
    persist(QStringLiteral("cinemaShake"), value);
    emit cinemaShakeChanged();
}

void PlayerExperienceController::setAutoRotate(int value)
{
    value = clampPercent(value);
    if (autoRotate_ == value) return;
    autoRotate_ = value;
    persist(QStringLiteral("autoRotate"), value);
    emit autoRotateChanged();
}

void PlayerExperienceController::setPeakBoost(int value)
{
    value = clampPercent(value);
    if (peakBoost_ == value) return;
    peakBoost_ = value;
    persist(QStringLiteral("peakBoost"), value);
    emit peakBoostChanged();
}

void PlayerExperienceController::setRipplesEnabled(bool value)
{
    if (ripplesEnabled_ == value) return;
    ripplesEnabled_ = value;
    persist(QStringLiteral("ripplesEnabled"), value);
    emit ripplesEnabledChanged();
}

void PlayerExperienceController::setBurstEnabled(bool value)
{
    if (burstEnabled_ == value) return;
    burstEnabled_ = value;
    persist(QStringLiteral("burstEnabled"), value);
    emit burstEnabledChanged();
}

void PlayerExperienceController::setFloatingBlockMinSize(int value)
{
    value = std::clamp(value, 0, 100);
    if (floatingBlockMinSize_ == value) return;
    floatingBlockMinSize_ = value;
    persist(QStringLiteral("floatingBlockMinSize"), value);
    emit floatingBlockMinSizeChanged();
}

void PlayerExperienceController::setFloatingBlockMaxSize(int value)
{
    value = std::clamp(value, 0, 100);
    if (floatingBlockMaxSize_ == value) return;
    floatingBlockMaxSize_ = value;
    persist(QStringLiteral("floatingBlockMaxSize"), value);
    emit floatingBlockMaxSizeChanged();
}

void PlayerExperienceController::setFloatingBlockSpeed(int value)
{
    value = std::clamp(value, 0, 100);
    if (floatingBlockSpeed_ == value) return;
    floatingBlockSpeed_ = value;
    persist(QStringLiteral("floatingBlockSpeed"), value);
    emit floatingBlockSpeedChanged();
}

void PlayerExperienceController::setFloatingBlockIntensity(int value)
{
    value = std::clamp(value, 0, 100);
    if (floatingBlockIntensity_ == value) return;
    floatingBlockIntensity_ = value;
    persist(QStringLiteral("floatingBlockIntensity"), value);
    emit floatingBlockIntensityChanged();
}

void PlayerExperienceController::setFloatingCubesEnabled(bool value)
{
    if (floatingCubesEnabled_ == value) return;
    floatingCubesEnabled_ = value;
    persist(QStringLiteral("floatingCubesEnabled"), value);
    emit floatingCubesEnabledChanged();
}

void PlayerExperienceController::setMeteorsEnabled(bool value)
{
    if (meteorsEnabled_ == value) return;
    meteorsEnabled_ = value;
    persist(QStringLiteral("meteorsEnabled"), value);
    emit meteorsEnabledChanged();
}

void PlayerExperienceController::setIdleBreathingEnabled(bool value)
{
    if (idleBreathingEnabled_ == value) return;
    idleBreathingEnabled_ = value;
    persist(QStringLiteral("idleBreathingEnabled"), value);
    emit idleBreathingEnabledChanged();
}

void PlayerExperienceController::setThemeCycleEnabled(bool value)
{
    if (themeCycleEnabled_ == value) return;
    themeCycleEnabled_ = value;
    persist(QStringLiteral("themeCycleEnabled"), value);
    emit themeCycleEnabledChanged();
}

void PlayerExperienceController::setThemeSongCycleEnabled(bool value)
{
    if (themeSongCycleEnabled_ == value) return;
    themeSongCycleEnabled_ = value;
    persist(QStringLiteral("themeSongCycleEnabled"), value);
    emit themeSongCycleEnabledChanged();
}

void PlayerExperienceController::setThemeCycleIntervalSeconds(int value)
{
    value = clampRange(value, 3, 120);
    if (themeCycleIntervalSeconds_ == value) return;
    themeCycleIntervalSeconds_ = value;
    persist(QStringLiteral("themeCycleIntervalSeconds"), value);
    emit themeCycleIntervalSecondsChanged();
}

void PlayerExperienceController::setStreamHighlightEnabled(bool value)
{
    if (streamHighlightEnabled_ == value) return;
    streamHighlightEnabled_ = value;
    persist(QStringLiteral("streamHighlightEnabled"), value);
    emit streamHighlightEnabledChanged();
}

void PlayerExperienceController::setSongAdaptiveColorEnabled(bool value)
{
    if (!themeId_.isEmpty() && !applyingTheme_) value = false;
    if (songAdaptiveColorEnabled_ == value) return;
    songAdaptiveColorEnabled_ = value;
    persist(QStringLiteral("songAdaptiveColorEnabled"), value);
    emit songAdaptiveColorEnabledChanged();
}

void PlayerExperienceController::setVisualEqGains(const QVariantList& values)
{
    const QVariantList normalized = normalizedVisualEqGains(values);
    if (visualEqGains_ == normalized) return;
    visualEqGains_ = normalized;
    persist(QStringLiteral("visualEqGains"), normalized);
    emit visualEqGainsChanged();
}

QVariantList PlayerExperienceController::normalizedVisualEqEnabled(const QVariantList& values)
{
    QVariantList result;
    for (int i = 0; i < 8; ++i)
        result.append(i < values.size() && values[i].metaType().id() == QMetaType::Bool
                          ? values[i].toBool() : true);
    return result;
}

void PlayerExperienceController::setVisualEqEnabled(const QVariantList& values)
{
    const auto normalized = normalizedVisualEqEnabled(values);
    if (visualEqEnabled_ == normalized) return;
    visualEqEnabled_ = normalized;
    persist(QStringLiteral("visualEqEnabled"), normalized);
    emit visualEqEnabledChanged();
}

void PlayerExperienceController::setLyricClarity(int value)
{
    value = clampPercent(value);
    if (lyricClarity_ == value) return;
    lyricClarity_ = value;
    persist(QStringLiteral("lyricClarity"), value);
    emit lyricClarityChanged();
}

void PlayerExperienceController::setLyricDepth(int value)
{
    value = clampPercent(value);
    if (lyricDepth_ == value) return;
    lyricDepth_ = value;
    persist(QStringLiteral("lyricDepth"), value);
    emit lyricDepthChanged();
}

void PlayerExperienceController::setLyricSize(int value)
{
    value = clampRange(value, 60, 140);
    if (lyricSize_ == value) return;
    lyricSize_ = value;
    persist(QStringLiteral("lyricSize"), value);
    emit lyricSizeChanged();
}

void PlayerExperienceController::setLyricOpacity(int value)
{
    value = clampRange(value, 10, 100);
    if (lyricOpacity_ == value) return;
    lyricOpacity_ = value;
    persist(QStringLiteral("lyricOpacity"), value);
    emit lyricOpacityChanged();
}

void PlayerExperienceController::setLyricPosition(int value)
{
    value = enumOrDefault(value, Left, Right, Center);
    if (lyricPosition_ == value) return;
    lyricPosition_ = value;
    persist(QStringLiteral("lyricPosition"), value);
    emit lyricPositionChanged();
}

void PlayerExperienceController::setLyricPositionX(int value)
{
    value = clampPercent(value);
    if (lyricPositionX_ == value) return;
    lyricPositionX_ = value;
    persist(QStringLiteral("lyricPositionX"), value);
    emit lyricPositionXChanged();
}

void PlayerExperienceController::setLyricPositionY(int value)
{
    value = clampPercent(value);
    if (lyricPositionY_ == value) return;
    lyricPositionY_ = value;
    persist(QStringLiteral("lyricPositionY"), value);
    emit lyricPositionYChanged();
}

void PlayerExperienceController::setInputCompression(int value)
{
    value = clampRange(value, 20, 150);
    if (inputCompression_ == value) return;
    inputCompression_ = value;
    persist(QStringLiteral("inputCompression"), value);
    emit inputCompressionChanged();
}

void PlayerExperienceController::setAudioResponse(int value)
{
    value = clampRange(value, 20, 200);
    if (audioResponse_ == value) return;
    audioResponse_ = value;
    persist(QStringLiteral("audioResponse"), value);
    emit audioResponseChanged();
}

void PlayerExperienceController::setResponseRange(int value)
{
    value = clampRange(value, 50, 220);
    if (responseRange_ == value) return;
    responseRange_ = value;
    persist(QStringLiteral("responseRange"), value);
    emit responseRangeChanged();
}

void PlayerExperienceController::setCenterHighlight(int value)
{
    value = clampPercent(value);
    if (centerHighlight_ == value) return;
    centerHighlight_ = value;
    persist(QStringLiteral("centerHighlight"), value);
    emit centerHighlightChanged();
}

void PlayerExperienceController::setRhythmStrength(int value)
{
    value = clampRange(value, 0, 140);
    if (rhythmStrength_ == value) return;
    rhythmStrength_ = value;
    persist(QStringLiteral("rhythmStrength"), value);
    emit rhythmStrengthChanged();
}

void PlayerExperienceController::setDepthOfField(int value)
{
    value = clampRange(value, 0, 150);
    if (depthOfField_ == value) return;
    depthOfField_ = value;
    persist(QStringLiteral("depthOfField"), value);
    emit depthOfFieldChanged();
}

void PlayerExperienceController::setSubjectClarity(int value)
{
    value = clampRange(value, 20, 140);
    if (subjectClarity_ == value) return;
    subjectClarity_ = value;
    persist(QStringLiteral("subjectClarity"), value);
    emit subjectClarityChanged();
}

void PlayerExperienceController::setAutoRotateSpeed(int value)
{
    value = clampPercent(value);
    if (autoRotateSpeed_ == value) return;
    autoRotateSpeed_ = value;
    persist(QStringLiteral("autoRotateSpeed"), value);
    emit autoRotateSpeedChanged();
}

void PlayerExperienceController::setRhythmSensitivity(int value)
{
    value = clampPercent(value);
    if (rhythmSensitivity_ == value) return;
    rhythmSensitivity_ = value;
    persist(QStringLiteral("rhythmSensitivity"), value);
    emit rhythmSensitivityChanged();
}

bool PlayerExperienceController::applyTheme(const QString& id)
{
    const auto* theme = agplayer::immersive::findBuiltInTheme(id.toStdString());
    if (theme == nullptr) return false;

    const auto encodedColor = [theme](const agplayer::immersive::ThemeColorRole role) {
        const auto rgb = agplayer::immersive::workingLinearToSrgb(
            agplayer::immersive::toWorkingLinear(
                theme->colors.at(agplayer::immersive::themeColorIndex(role))));
        return QColor::fromRgbF(rgb.red, rgb.green, rgb.blue);
    };

    applyingTheme_ = true;
    setMaterialMode(0);
    setColorMode(MultiRegion);
    setSongAdaptiveColorEnabled(false);
    setCoolColor(encodedColor(agplayer::immersive::ThemeColorRole::CoolCore)
                     .name(QColor::HexRgb));
    setWarmColor(encodedColor(agplayer::immersive::ThemeColorRole::WarmCore)
                     .name(QColor::HexRgb));
    setAccentColor(encodedColor(agplayer::immersive::ThemeColorRole::Ripple)
                       .name(QColor::HexRgb));
    setPeakColor(encodedColor(agplayer::immersive::ThemeColorRole::WarmEdge)
                     .name(QColor::HexRgb));
    setBaseColor(encodedColor(agplayer::immersive::ThemeColorRole::BasePrimary)
                     .name(QColor::HexRgb));
    themeGlow_ = theme->glowIntensity;
    // App.tsx uses uFogColor as its CSS backdrop, not the terrain base color.
    themeBackground_ = encodedColor(agplayer::immersive::ThemeColorRole::Fog);
    applyingTheme_ = false;
    setThemeId(id);
    return true;
}

void PlayerExperienceController::applyCustomPalette(const QVariantMap& colors, bool preview)
{
    // Preview only changes the live render state. The independent saved palette
    // and settings on disk are untouched until the user confirms.
    previewingCustomColor_ = preview;
    applyingTheme_ = true;
    setMaterialMode(0);
    setColorMode(MultiRegion);
    setSongAdaptiveColorEnabled(false);
    setThemeCycleEnabled(false);
    setThemeSongCycleEnabled(false);
    setCoolColor(colors.value(QStringLiteral("coolColor")).toString());
    setWarmColor(colors.value(QStringLiteral("warmColor")).toString());
    setAccentColor(colors.value(QStringLiteral("accentColor")).toString());
    setPeakColor(colors.value(QStringLiteral("peakColor")).toString());
    setBaseColor(colors.value(QStringLiteral("baseColor")).toString());
    themeGlow_ = 1.0F;
    const QColor background(baseColor_);
    const bool backgroundChanged = themeBackground_ != background;
    themeBackground_ = background;
    applyingTheme_ = false;
    if (themeId_ != QStringLiteral("custom")) setThemeId(QStringLiteral("custom"));
    else if (backgroundChanged) emit themeChanged();
    previewingCustomColor_ = false;
}

void PlayerExperienceController::applyCustomColors()
{
    applyCustomPalette(customColors_, false);
    persist(QStringLiteral("themeId"), themeId_);
}

bool PlayerExperienceController::setCustomColor(const QString& key, const QString& value)
{
    const QString normalized = normalizedColor(value, {});
    if (!customColors_.contains(key) || normalized.isEmpty()) return false;
    if (customColors_.value(key).toString() != normalized) {
        customColors_[key] = normalized;
        persist(QStringLiteral("customColors"), customColors_);
        emit customColorsChanged();
    }
    applyCustomColors();
    return true;
}

bool PlayerExperienceController::previewCustomColor(const QString& key, const QString& value)
{
    const QString normalized = normalizedColor(value, {});
    if (!customColors_.contains(key) || normalized.isEmpty()) return false;
    auto colors = customColors_;
    colors[key] = normalized;
    applyCustomPalette(colors, true);
    return true;
}

void PlayerExperienceController::restoreDynamicDefaults()
{
    setRippleStrength(100);
    setRippleWidth(100);
    setRippleDecay(100);
    setColumnSize(100);
    setColumnDensity(50);
    setTopographyDensity(46);
    setColumnOpacity(100);
    setReactorBrightness(100);
    setColumnInnerLight(155);
    setColumnLightSpill(176);
    setColumnLightRadius(86);
    setTerrainAmplitude(50);
    setMotionResponse(50);
    setGradientLayers(74);
    setGlowIntensity(100);
    setCinemaShake(0.81);
    setAutoRotate(54);
    setPeakBoost(55);
    setInputCompression(99);
    setAudioResponse(121);
    setResponseRange(129);
    setCenterHighlight(40);
    setRhythmStrength(30);
    setDepthOfField(83);
    setSubjectClarity(114);
    setAutoRotateSpeed(15);
    setRhythmSensitivity(100);
    setRipplesEnabled(true);
    setBurstEnabled(true);
    setFloatingCubesEnabled(true);
    setMeteorsEnabled(true);
    setIdleBreathingEnabled(true);
    setStreamHighlightEnabled(true);
    setSongAdaptiveColorEnabled(false);
    setFloatingBlockMinSize(9);
    setFloatingBlockMaxSize(26);
    setFloatingBlockSpeed(77);
    setFloatingBlockIntensity(55);
    setVisualEqEnabled(QVariantList({true, true, true, true,
                                     true, true, true, true}));
    setVisualEqGains(defaultVisualEqGains());
}

void PlayerExperienceController::toggleImmersiveMode()
{
    setImmersiveMode(immersiveMode_ == Off ? TerrainReactor : Off);
}

void PlayerExperienceController::toggleLyricsVisible()
{
    setLyricsVisible(!lyricsVisible_);
}

void PlayerExperienceController::togglePanelVisible()
{
    setPanelVisible(!panelVisible_);
}

void PlayerExperienceController::togglePlayerShellMode()
{
    if (settingsController_ != nullptr) {
        settingsController_->setPlayerShellMode(
            settingsController_->playerShellMode() == 0 ? 1 : 0);
    }
}

void PlayerExperienceController::cycleExperienceTheme()
{
    if (settingsController_ == nullptr) return;
    if (immersiveMode_ != Off) {
        setImmersiveMode(Off);
        return;
    }
    if (settingsController_->playerShellMode() == 0) {
        settingsController_->setPlayerShellMode(1);
        return;
    }
    setHostMode(Windowed);
    setImmersiveMode(TerrainReactor);
}

void PlayerExperienceController::load()
{
    settings_.beginGroup(QLatin1String(kSettingsGroup));
    const bool isNewInstall = settings_.allKeys().isEmpty();
    const auto savedCustomColors = settings_.value(QStringLiteral("customColors")).toMap();
    for (auto it = customColors_.begin(); it != customColors_.end(); ++it) {
        it.value() = normalizedColor(savedCustomColors.value(it.key()).toString(),
                                     it.value().toString());
    }
    const QVariant persistedTheme = settings_.value(QStringLiteral("themeId"));
    const QString storedThemeId = persistedTheme.metaType().id() == QMetaType::QString
        ? persistedTheme.toString() : QString{};
    const bool resetReferenceDefaults = !storedThemeId.isEmpty()
        && settings_.value(QStringLiteral("referenceDefaultsRevision"), 0).toInt() < 2;
    const auto integer = [this, resetReferenceDefaults](const QString& key, const int fallback) {
        // One-time upgrade of the original-theme dynamics; keep the library,
        // playback preferences, selected palette and unrelated settings intact.
        static const QStringList referenceKeys{
            QStringLiteral("terrainAmplitude"), QStringLiteral("motionResponse"),
            QStringLiteral("rippleStrength"), QStringLiteral("rippleWidth"),
            QStringLiteral("rippleDecay"), QStringLiteral("reactorBrightness"),
            QStringLiteral("rhythmSensitivity"), QStringLiteral("glowIntensity"),
            QStringLiteral("floatingBlockMinSize"), QStringLiteral("floatingBlockMaxSize"),
            QStringLiteral("floatingBlockSpeed"), QStringLiteral("floatingBlockIntensity"),
            QStringLiteral("autoRotateSpeed"), QStringLiteral("topographyDensity"),
            QStringLiteral("columnSize"), QStringLiteral("columnOpacity"),
            QStringLiteral("autoRotate"), QStringLiteral("materialMode")};
        if (resetReferenceDefaults && referenceKeys.contains(key)) return fallback;
        return storedInteger(settings_.value(key)).value_or(fallback);
    };
    const auto boolean = [this, resetReferenceDefaults](const QString& key, const bool fallback) {
        if (resetReferenceDefaults && key == QStringLiteral("themeCycleEnabled")) return false;
        if (resetReferenceDefaults && key == QStringLiteral("songAdaptiveColorEnabled")) return false;
        if (resetReferenceDefaults && (key == QStringLiteral("idleBreathingEnabled")
            || key == QStringLiteral("floatingCubesEnabled") || key == QStringLiteral("ripplesEnabled")
            || key == QStringLiteral("meteorsEnabled") || key == QStringLiteral("streamHighlightEnabled"))) return true;
        return storedBoolean(settings_.value(key)).value_or(fallback);
    };
    const auto decimal = [this](const QString& key, const double fallback) {
        return storedDouble(settings_.value(key)).value_or(fallback);
    };

    // Presentation is session state. Never reopen a GPU-heavy immersive
    // window on launch, including after an abnormal process termination.
    immersiveMode_ = Off;
    hostMode_ = enumOrDefault(integer(QStringLiteral("hostMode"), Windowed),
                              Windowed, Desktop, Windowed);
    // Lyrics are presentation state: every application launch starts clean and
    // lets the listener opt in explicitly instead of reopening the last panel.
    lyricsVisible_ = false;
    panelVisible_ = boolean(QStringLiteral("panelVisible"), true);
    desktopMousePassthrough_ = boolean(
        QStringLiteral("desktopMousePassthrough"), false);
    qualityPreset_ = enumOrDefault(integer(QStringLiteral("qualityPreset"), Auto),
                                   Auto, Ultra, Auto);
    colorMode_ = enumOrDefault(integer(QStringLiteral("colorMode"), MultiRegion),
                               MultiRegion, RainbowColumn, MultiRegion);
    coolColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("coolColor")),
                                             QStringLiteral("#5276E8")),
                                 QStringLiteral("#5276E8"));
    warmColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("warmColor")),
                                             QStringLiteral("#F58DAD")),
                                 QStringLiteral("#F58DAD"));
    accentColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("accentColor")),
                                               QStringLiteral("#A880ED")),
                                   QStringLiteral("#A880ED"));
    peakColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("peakColor")),
                                             QStringLiteral("#F5DBEC")),
                                 QStringLiteral("#F5DBEC"));
    baseColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("baseColor")),
                                             QStringLiteral("#040A1C")),
                                 QStringLiteral("#040A1C"));
    themeBackground_ = QColor(baseColor_);
    terrainAmplitude_ = clampPercent(integer(QStringLiteral("terrainAmplitude"), 50));
    materialMode_ = enumOrDefault(integer(QStringLiteral("materialMode"), 0), 0, 2, 0);
    materialSoftness_ = clampRange(integer(QStringLiteral("materialSoftness"), 45), 0, 100);
    jellyElasticity_ = clampRange(integer(QStringLiteral("jellyElasticity"), 35), 0, 100);
    inkDensity_ = clampRange(integer(QStringLiteral("inkDensity"), 60), 0, 100);
    rippleStrength_ = clampRange(integer(QStringLiteral("rippleStrength"), 100), 0, 200);
    rippleWidth_ = clampRange(integer(QStringLiteral("rippleWidth"), 100), 20, 200);
    rippleDecay_ = clampRange(integer(QStringLiteral("rippleDecay"), 100), 20, 200);
    columnSize_ = clampRange(integer(QStringLiteral("columnSize"), 100), 50, 200);
    columnDensity_ = clampRange(integer(QStringLiteral("columnDensity"), 50), 50, 200);
    topographyDensity_ = clampPercent(integer(QStringLiteral("topographyDensity"), 46));
    columnOpacity_ = clampRange(integer(QStringLiteral("columnOpacity"), 100), 0, 100);
    reactorBrightness_ = clampRange(integer(QStringLiteral("reactorBrightness"), 100), 0, 200);
    columnInnerLight_ = clampRange(integer(QStringLiteral("columnInnerLight"), 155), 0, 200);
    columnLightSpill_ = clampRange(integer(QStringLiteral("columnLightSpill"), 176), 0, 200);
    columnLightRadius_ = clampRange(integer(QStringLiteral("columnLightRadius"), 86), 20, 200);
    motionResponse_ = clampPercent(integer(QStringLiteral("motionResponse"), 50));
    gradientLayers_ = clampPercent(integer(QStringLiteral("gradientLayers"), 74));
    glowIntensity_ = clampPercent(integer(QStringLiteral("glowIntensity"), 100));
    cinemaShake_ = std::clamp(decimal(QStringLiteral("cinemaShake"), 0.81), 0.0, 1.8);
    autoRotate_ = clampPercent(integer(QStringLiteral("autoRotate"), 54));
    peakBoost_ = clampPercent(integer(QStringLiteral("peakBoost"), 55));
    ripplesEnabled_ = boolean(QStringLiteral("ripplesEnabled"), true);
    burstEnabled_ = boolean(QStringLiteral("burstEnabled"), true);
    floatingCubesEnabled_ = boolean(QStringLiteral("floatingCubesEnabled"), true);
    floatingBlockMinSize_ = std::clamp(integer(QStringLiteral("floatingBlockMinSize"), 9), 0, 100);
    floatingBlockMaxSize_ = std::clamp(integer(QStringLiteral("floatingBlockMaxSize"), 26), 0, 100);
    floatingBlockSpeed_ = std::clamp(integer(QStringLiteral("floatingBlockSpeed"), 77), 0, 100);
    floatingBlockIntensity_ = std::clamp(integer(QStringLiteral("floatingBlockIntensity"), 55), 0, 100);
    meteorsEnabled_ = boolean(QStringLiteral("meteorsEnabled"), true);
    idleBreathingEnabled_ = boolean(QStringLiteral("idleBreathingEnabled"), true);
    // Version 1 introduces user-visible rotation controls. The old hidden
    // setting was never actionable, so migrate it to the reference default.
    const bool hasThemeRotationSettings =
        settings_.value(QStringLiteral("themeRotationSettingsVersion"), 0).toInt() >= 1;
    themeCycleEnabled_ = hasThemeRotationSettings
        ? boolean(QStringLiteral("themeCycleEnabled"), false) : false;
    themeSongCycleEnabled_ = boolean(QStringLiteral("themeSongCycleEnabled"), false);
    themeCycleIntervalSeconds_ = clampRange(
        integer(QStringLiteral("themeCycleIntervalSeconds"), 10), 3, 120);
    streamHighlightEnabled_ = boolean(
        QStringLiteral("streamHighlightEnabled"), true);
    songAdaptiveColorEnabled_ = boolean(
        QStringLiteral("songAdaptiveColorEnabled"), false);
    const QVariant persistedGains = settings_.value(QStringLiteral("visualEqGains"));
    QVariantList storedEnabled = settings_.value(QStringLiteral("visualEqEnabled")).toList();
    // QSettings INI lists round-trip booleans as "true"/"false" strings.
    for (auto& value : storedEnabled) {
        if (value.metaType().id() == QMetaType::QString) {
            if (value.toString() == QStringLiteral("true")) value = true;
            else if (value.toString() == QStringLiteral("false")) value = false;
        }
    }
    visualEqEnabled_ = normalizedVisualEqEnabled(storedEnabled);
    const int persistedGainsType = persistedGains.metaType().id();
    visualEqGains_ = (persistedGainsType == QMetaType::QVariantList
                       || persistedGainsType == QMetaType::QStringList)
        ? normalizedVisualEqGains(persistedGains.toList()) : defaultVisualEqGains();
    if (resetReferenceDefaults) {
        visualEqGains_ = defaultVisualEqGains();
        visualEqEnabled_ = {true,true,true,true,true,true,true,true};
    }
    lyricClarity_ = clampPercent(integer(QStringLiteral("lyricClarity"), 78));
    lyricDepth_ = clampPercent(integer(QStringLiteral("lyricDepth"), 62));
    lyricSize_ = clampRange(integer(QStringLiteral("lyricSize"), 100), 60, 140);
    lyricOpacity_ = clampRange(integer(QStringLiteral("lyricOpacity"), 88), 10, 100);
    lyricPosition_ = enumOrDefault(integer(QStringLiteral("lyricPosition"), Center),
                                   Left, Right, Center);
    lyricPositionX_ = clampPercent(integer(QStringLiteral("lyricPositionX"), 50));
    lyricPositionY_ = clampPercent(integer(QStringLiteral("lyricPositionY"), 42));
    inputCompression_ = clampRange(integer(QStringLiteral("inputCompression"), 99), 20, 150);
    audioResponse_ = clampRange(integer(QStringLiteral("audioResponse"), 121), 20, 200);
    responseRange_ = clampRange(integer(QStringLiteral("responseRange"), 129), 50, 220);
    centerHighlight_ = clampPercent(integer(QStringLiteral("centerHighlight"), 40));
    rhythmStrength_ = clampRange(integer(QStringLiteral("rhythmStrength"), 30), 0, 140);
    depthOfField_ = clampRange(integer(QStringLiteral("depthOfField"), 83), 0, 150);
    subjectClarity_ = clampRange(integer(QStringLiteral("subjectClarity"), 114), 20, 140);
    autoRotateSpeed_ = clampPercent(integer(QStringLiteral("autoRotateSpeed"), 15));
    rhythmSensitivity_ = clampPercent(integer(QStringLiteral("rhythmSensitivity"), 100));

    settings_.remove(QStringLiteral("mode"));
    settings_.setValue(QStringLiteral("hostMode"), hostMode_);
    settings_.setValue(QStringLiteral("lyricsVisible"), lyricsVisible_);
    settings_.setValue(QStringLiteral("panelVisible"), panelVisible_);
    settings_.setValue(QStringLiteral("desktopMousePassthrough"), desktopMousePassthrough_);
    settings_.setValue(QStringLiteral("qualityPreset"), qualityPreset_);
    settings_.setValue(QStringLiteral("colorMode"), colorMode_);
    settings_.setValue(QStringLiteral("materialMode"), materialMode_);
    settings_.setValue(QStringLiteral("materialSoftness"), materialSoftness_);
    settings_.setValue(QStringLiteral("jellyElasticity"), jellyElasticity_);
    settings_.setValue(QStringLiteral("inkDensity"), inkDensity_);
    settings_.setValue(QStringLiteral("rippleStrength"), rippleStrength_);
    settings_.setValue(QStringLiteral("rippleWidth"), rippleWidth_);
    settings_.setValue(QStringLiteral("rippleDecay"), rippleDecay_);
    settings_.setValue(QStringLiteral("columnSize"), columnSize_);
    settings_.setValue(QStringLiteral("columnDensity"), columnDensity_);
    settings_.setValue(QStringLiteral("topographyDensity"), topographyDensity_);
    settings_.setValue(QStringLiteral("columnOpacity"), columnOpacity_);
    settings_.setValue(QStringLiteral("reactorBrightness"), reactorBrightness_);
    settings_.setValue(QStringLiteral("columnInnerLight"), columnInnerLight_);
    settings_.setValue(QStringLiteral("columnLightSpill"), columnLightSpill_);
    settings_.setValue(QStringLiteral("columnLightRadius"), columnLightRadius_);
    settings_.setValue(QStringLiteral("coolColor"), coolColor_);
    settings_.setValue(QStringLiteral("warmColor"), warmColor_);
    settings_.setValue(QStringLiteral("accentColor"), accentColor_);
    settings_.setValue(QStringLiteral("peakColor"), peakColor_);
    settings_.setValue(QStringLiteral("baseColor"), baseColor_);
    settings_.setValue(QStringLiteral("terrainAmplitude"), terrainAmplitude_);
    settings_.setValue(QStringLiteral("motionResponse"), motionResponse_);
    settings_.setValue(QStringLiteral("gradientLayers"), gradientLayers_);
    settings_.setValue(QStringLiteral("glowIntensity"), glowIntensity_);
    settings_.setValue(QStringLiteral("cinemaShake"), cinemaShake_);
    settings_.setValue(QStringLiteral("autoRotate"), autoRotate_);
    settings_.setValue(QStringLiteral("peakBoost"), peakBoost_);
    settings_.setValue(QStringLiteral("ripplesEnabled"), ripplesEnabled_);
    settings_.setValue(QStringLiteral("burstEnabled"), burstEnabled_);
    settings_.setValue(QStringLiteral("floatingCubesEnabled"), floatingCubesEnabled_);
    settings_.setValue(QStringLiteral("floatingBlockMinSize"), floatingBlockMinSize_);
    settings_.setValue(QStringLiteral("floatingBlockMaxSize"), floatingBlockMaxSize_);
    settings_.setValue(QStringLiteral("floatingBlockSpeed"), floatingBlockSpeed_);
    settings_.setValue(QStringLiteral("floatingBlockIntensity"), floatingBlockIntensity_);
    settings_.setValue(QStringLiteral("meteorsEnabled"), meteorsEnabled_);
    settings_.setValue(QStringLiteral("idleBreathingEnabled"), idleBreathingEnabled_);
    settings_.setValue(QStringLiteral("themeCycleEnabled"), themeCycleEnabled_);
    settings_.setValue(QStringLiteral("themeSongCycleEnabled"), themeSongCycleEnabled_);
    settings_.setValue(QStringLiteral("themeCycleIntervalSeconds"),
                       themeCycleIntervalSeconds_);
    settings_.setValue(QStringLiteral("themeRotationSettingsVersion"), 1);
    settings_.setValue(QStringLiteral("streamHighlightEnabled"),
                       streamHighlightEnabled_);
    settings_.setValue(QStringLiteral("songAdaptiveColorEnabled"),
                       songAdaptiveColorEnabled_);
    settings_.setValue(QStringLiteral("visualEqGains"), visualEqGains_);
    settings_.setValue(QStringLiteral("lyricClarity"), lyricClarity_);
    settings_.setValue(QStringLiteral("lyricDepth"), lyricDepth_);
    settings_.setValue(QStringLiteral("lyricSize"), lyricSize_);
    settings_.setValue(QStringLiteral("lyricOpacity"), lyricOpacity_);
    settings_.setValue(QStringLiteral("lyricPosition"), lyricPosition_);
    settings_.setValue(QStringLiteral("lyricPositionX"), lyricPositionX_);
    settings_.setValue(QStringLiteral("lyricPositionY"), lyricPositionY_);
    settings_.setValue(QStringLiteral("inputCompression"), inputCompression_);
    settings_.setValue(QStringLiteral("audioResponse"), audioResponse_);
    settings_.setValue(QStringLiteral("responseRange"), responseRange_);
    settings_.setValue(QStringLiteral("centerHighlight"), centerHighlight_);
    settings_.setValue(QStringLiteral("rhythmStrength"), rhythmStrength_);
    settings_.setValue(QStringLiteral("depthOfField"), depthOfField_);
    settings_.setValue(QStringLiteral("subjectClarity"), subjectClarity_);
    settings_.setValue(QStringLiteral("autoRotateSpeed"), autoRotateSpeed_);
    settings_.setValue(QStringLiteral("rhythmSensitivity"), rhythmSensitivity_);
    settings_.setValue(QStringLiteral("referenceDefaultsRevision"), 2);
    settings_.endGroup();

    if (storedThemeId == QStringLiteral("custom")) {
        applyCustomColors();
    } else if (storedThemeId == QStringLiteral("daybreak-lime")) {
        applyTheme(QStringLiteral("violet-heart"));
    } else if (agplayer::immersive::findBuiltInTheme(storedThemeId.toStdString()) != nullptr) {
        applyTheme(storedThemeId);
    } else if (isNewInstall) {
        applyTheme(QString::fromUtf8(
            agplayer::immersive::defaultBuiltInTheme().id.data(),
            static_cast<qsizetype>(agplayer::immersive::defaultBuiltInTheme().id.size())));
    }
}

void PlayerExperienceController::persist(const QString& key, const QVariant& value)
{
    if (previewingCustomColor_) return;
    settings_.setValue(QLatin1String(kSettingsGroup) + QLatin1Char('/') + key, value);
}

void PlayerExperienceController::setThemeId(const QString& id)
{
    if (themeId_ == id) return;
    themeId_ = id;
    persist(QStringLiteral("themeId"), themeId_);
    emit themeChanged();
}

void PlayerExperienceController::clearThemeForManualColor()
{
    if (!applyingTheme_) setThemeId({});
}

int PlayerExperienceController::clampPercent(int value) noexcept
{
    return clampRange(value, 0, 100);
}

int PlayerExperienceController::clampRange(int value, int minimum, int maximum) noexcept
{
    return std::clamp(value, minimum, maximum);
}

QString PlayerExperienceController::normalizedColor(const QString& value,
                                                    const QString& fallback)
{
    const QColor color(value);
    return color.isValid() && color.alpha() == 255
        ? color.name(QColor::HexRgb).toUpper()
        : fallback;
}

QVariantList PlayerExperienceController::defaultVisualEqGains()
{
    return {90, 92, 50, 50, 50, 50, 50, 48};
}

QVariantList PlayerExperienceController::normalizedVisualEqGains(
    const QVariantList& values)
{
    if (values.size() != 8) return defaultVisualEqGains();
    QVariantList normalized;
    normalized.reserve(8);
    for (const QVariant& value : values) {
        const auto parsed = storedInteger(value);
        if (!parsed.has_value()) return defaultVisualEqGains();
        normalized.append(clampPercent(*parsed));
    }
    return normalized;
}
