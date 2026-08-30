#include "player_experience_controller.hpp"

#include "settings_controller.hpp"

#include <QColor>
#include <QMetaType>

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

struct StylePreset {
    int colorMode;
    const char* coolColor;
    const char* warmColor;
    const char* accentColor;
    const char* peakColor;
    const char* baseColor;
    int terrainAmplitude;
    int motionResponse;
    int gradientLayers;
    int glowIntensity;
    double cinemaShake;
    int autoRotate;
    int peakBoost;
    bool ripplesEnabled;
    bool burstEnabled;
    bool floatingCubesEnabled;
    bool meteorsEnabled;
    bool idleBreathingEnabled;
    bool themeCycleEnabled;
    bool streamHighlightEnabled;
    QVariantList visualEqGains;
    int inputCompression;
    int audioResponse;
    int responseRange;
    int centerHighlight;
    int rhythmStrength;
    int depthOfField;
    int subjectClarity;
    int autoRotateSpeed;
    int rhythmSensitivity;
};

const std::array<StylePreset, 6>& visualPresets()
{
    static const std::array<StylePreset, 6> presets = {{
        {0, "#8BDCFF", "#EB7894", "#FFD7DF", "#FFF7FB", "#050206",
         62, 56, 74, 38, 0.30, 54, 58, true, true, true, true, true, true, true,
         {90, 92, 50, 50, 50, 50, 50, 48}, 82, 136, 100, 64, 30, 86, 112, 42, 80},
        {2, "#7F5CFF", "#FF4FD8", "#22F0FF", "#F7F2FF", "#070310",
         70, 80, 84, 58, 0.48, 64, 72, true, true, true, true, false, true, true,
         {92, 84, 58, 48, 54, 72, 96, 100}, 84, 144, 178, 68, 106, 94, 108, 48, 84},
        {1, "#6F8DB8", "#D2645E", "#E9D7D1", "#F6F0E8", "#111317",
         48, 36, 42, 22, 0.12, 30, 36, true, true, false, false, true, false, true,
         {62, 58, 54, 50, 48, 44, 42, 40}, 88, 122, 160, 48, 82, 116, 120, 30, 72},
        {1, "#8EDFFF", "#D9B9FF", "#9EF2D1", "#F7FFFF", "#0B1117",
         52, 48, 36, 24, 0.18, 34, 42, true, true, false, false, false, false, true,
         {70, 68, 62, 58, 58, 62, 68, 72}, 80, 134, 166, 56, 92, 70, 126, 34, 80},
        {0, "#5264D9", "#B89CFF", "#46C7BC", "#DAF3EE", "#0A1018",
         28, 22, 30, 16, 0.08, 26, 24, true, true, false, false, true, false, true,
         {48, 46, 44, 42, 42, 40, 38, 36}, 92, 108, 154, 46, 64, 112, 106, 26, 66},
        {2, "#44D9FF", "#FF4FA7", "#FF8A45", "#FFF1D1", "#05030D",
         72, 68, 76, 52, 0.45, 58, 68, true, true, true, true, true, true, true,
         {96, 88, 66, 54, 58, 76, 94, 100}, 80, 140, 182, 64, 104, 96, 110, 46, 84},
    }};
    return presets;
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
QString PlayerExperienceController::coolColor() const { return coolColor_; }
QString PlayerExperienceController::warmColor() const { return warmColor_; }
QString PlayerExperienceController::accentColor() const { return accentColor_; }
QString PlayerExperienceController::peakColor() const { return peakColor_; }
QString PlayerExperienceController::baseColor() const { return baseColor_; }
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

void PlayerExperienceController::setImmersiveMode(int value)
{
    value = value == TerrainReactor ? TerrainReactor : Off;
    if (immersiveMode_ == value) return;
    immersiveMode_ = value;
    persist(QStringLiteral("mode"), value);
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
    value = enumOrDefault(value, MultiRegion, RgbSweep, MultiRegion);
    if (colorMode_ == value) return;
    colorMode_ = value;
    persist(QStringLiteral("colorMode"), value);
    emit colorModeChanged();
}

void PlayerExperienceController::setCoolColor(const QString& value)
{
    const QString normalized = normalizedColor(value, QStringLiteral("#4F6FFF"));
    if (coolColor_ == normalized) return;
    coolColor_ = normalized;
    persist(QStringLiteral("coolColor"), normalized);
    emit coolColorChanged();
}

void PlayerExperienceController::setWarmColor(const QString& value)
{
    const QString normalized = normalizedColor(value, QStringLiteral("#FF4778"));
    if (warmColor_ == normalized) return;
    warmColor_ = normalized;
    persist(QStringLiteral("warmColor"), normalized);
    emit warmColorChanged();
}

void PlayerExperienceController::setAccentColor(const QString& value)
{
    const QString normalized = normalizedColor(value, QStringLiteral("#77EAFF"));
    if (accentColor_ == normalized) return;
    accentColor_ = normalized;
    persist(QStringLiteral("accentColor"), normalized);
    emit accentColorChanged();
}

void PlayerExperienceController::setPeakColor(const QString& value)
{
    const QString normalized = normalizedColor(value, QStringLiteral("#D7FF58"));
    if (peakColor_ == normalized) return;
    peakColor_ = normalized;
    persist(QStringLiteral("peakColor"), normalized);
    emit peakColorChanged();
}

void PlayerExperienceController::setBaseColor(const QString& value)
{
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

void PlayerExperienceController::setStreamHighlightEnabled(bool value)
{
    if (streamHighlightEnabled_ == value) return;
    streamHighlightEnabled_ = value;
    persist(QStringLiteral("streamHighlightEnabled"), value);
    emit streamHighlightEnabledChanged();
}

void PlayerExperienceController::setSongAdaptiveColorEnabled(bool value)
{
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

bool PlayerExperienceController::applyPreset(int preset)
{
    if (preset < AudioRangeEcho || preset > Galaxy) return false;
    const StylePreset& values = visualPresets().at(static_cast<size_t>(preset));
    setColorMode(values.colorMode);
    setCoolColor(QLatin1String(values.coolColor));
    setWarmColor(QLatin1String(values.warmColor));
    setAccentColor(QLatin1String(values.accentColor));
    setPeakColor(QLatin1String(values.peakColor));
    setBaseColor(QLatin1String(values.baseColor));
    setTerrainAmplitude(values.terrainAmplitude);
    setMotionResponse(values.motionResponse);
    setGradientLayers(values.gradientLayers);
    setGlowIntensity(values.glowIntensity);
    setCinemaShake(values.cinemaShake);
    setAutoRotate(values.autoRotate);
    setPeakBoost(values.peakBoost);
    setRipplesEnabled(values.ripplesEnabled);
    setBurstEnabled(values.burstEnabled);
    setFloatingCubesEnabled(values.floatingCubesEnabled);
    setMeteorsEnabled(values.meteorsEnabled);
    setIdleBreathingEnabled(values.idleBreathingEnabled);
    setThemeCycleEnabled(values.themeCycleEnabled);
    setStreamHighlightEnabled(values.streamHighlightEnabled);
    setVisualEqGains(values.visualEqGains);
    setInputCompression(values.inputCompression);
    setAudioResponse(values.audioResponse);
    setResponseRange(values.responseRange);
    setCenterHighlight(values.centerHighlight);
    setRhythmStrength(values.rhythmStrength);
    setDepthOfField(values.depthOfField);
    setSubjectClarity(values.subjectClarity);
    setAutoRotateSpeed(values.autoRotateSpeed);
    setRhythmSensitivity(values.rhythmSensitivity);
    return true;
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
        settingsController_->setPlayerShellMode(0);
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
    const auto integer = [this](const QString& key, const int fallback) {
        return storedInteger(settings_.value(key)).value_or(fallback);
    };
    const auto boolean = [this](const QString& key, const bool fallback) {
        return storedBoolean(settings_.value(key)).value_or(fallback);
    };
    const auto decimal = [this](const QString& key, const double fallback) {
        return storedDouble(settings_.value(key)).value_or(fallback);
    };

    immersiveMode_ = enumOrDefault(integer(QStringLiteral("mode"), Off),
                                   Off, TerrainReactor, Off);
    hostMode_ = enumOrDefault(integer(QStringLiteral("hostMode"), Windowed),
                              Windowed, Desktop, Windowed);
    lyricsVisible_ = boolean(QStringLiteral("lyricsVisible"), false);
    panelVisible_ = boolean(QStringLiteral("panelVisible"), true);
    desktopMousePassthrough_ = boolean(
        QStringLiteral("desktopMousePassthrough"), false);
    qualityPreset_ = enumOrDefault(integer(QStringLiteral("qualityPreset"), Auto),
                                   Auto, Ultra, Auto);
    colorMode_ = enumOrDefault(integer(QStringLiteral("colorMode"), MultiRegion),
                               MultiRegion, RgbSweep, MultiRegion);
    coolColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("coolColor")),
                                             QStringLiteral("#8BDCFF")),
                                 QStringLiteral("#8BDCFF"));
    warmColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("warmColor")),
                                             QStringLiteral("#EB7894")),
                                 QStringLiteral("#EB7894"));
    accentColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("accentColor")),
                                               QStringLiteral("#FFD7DF")),
                                   QStringLiteral("#FFD7DF"));
    peakColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("peakColor")),
                                             QStringLiteral("#FFF7FB")),
                                 QStringLiteral("#FFF7FB"));
    baseColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("baseColor")),
                                             QStringLiteral("#050206")),
                                 QStringLiteral("#050206"));
    terrainAmplitude_ = clampPercent(integer(QStringLiteral("terrainAmplitude"), 62));
    motionResponse_ = clampPercent(integer(QStringLiteral("motionResponse"), 56));
    gradientLayers_ = clampPercent(integer(QStringLiteral("gradientLayers"), 74));
    glowIntensity_ = clampPercent(integer(QStringLiteral("glowIntensity"), 38));
    cinemaShake_ = std::clamp(decimal(QStringLiteral("cinemaShake"), 0.40), 0.0, 1.8);
    autoRotate_ = clampPercent(integer(QStringLiteral("autoRotate"), 54));
    peakBoost_ = clampPercent(integer(QStringLiteral("peakBoost"), 58));
    ripplesEnabled_ = boolean(QStringLiteral("ripplesEnabled"), true);
    burstEnabled_ = boolean(QStringLiteral("burstEnabled"), true);
    floatingCubesEnabled_ = boolean(QStringLiteral("floatingCubesEnabled"), true);
    meteorsEnabled_ = boolean(QStringLiteral("meteorsEnabled"), true);
    idleBreathingEnabled_ = boolean(QStringLiteral("idleBreathingEnabled"), true);
    themeCycleEnabled_ = boolean(QStringLiteral("themeCycleEnabled"), false);
    streamHighlightEnabled_ = boolean(
        QStringLiteral("streamHighlightEnabled"), true);
    songAdaptiveColorEnabled_ = boolean(
        QStringLiteral("songAdaptiveColorEnabled"), true);
    const QVariant persistedGains = settings_.value(QStringLiteral("visualEqGains"));
    const int persistedGainsType = persistedGains.metaType().id();
    visualEqGains_ = (persistedGainsType == QMetaType::QVariantList
                       || persistedGainsType == QMetaType::QStringList)
        ? normalizedVisualEqGains(persistedGains.toList()) : defaultVisualEqGains();
    lyricClarity_ = clampPercent(integer(QStringLiteral("lyricClarity"), 78));
    lyricDepth_ = clampPercent(integer(QStringLiteral("lyricDepth"), 62));
    lyricSize_ = clampRange(integer(QStringLiteral("lyricSize"), 100), 60, 140);
    lyricOpacity_ = clampRange(integer(QStringLiteral("lyricOpacity"), 88), 10, 100);
    lyricPosition_ = enumOrDefault(integer(QStringLiteral("lyricPosition"), Center),
                                   Left, Right, Center);
    lyricPositionX_ = clampPercent(integer(QStringLiteral("lyricPositionX"), 50));
    lyricPositionY_ = clampPercent(integer(QStringLiteral("lyricPositionY"), 42));
    inputCompression_ = clampRange(integer(QStringLiteral("inputCompression"), 82), 20, 150);
    audioResponse_ = clampRange(integer(QStringLiteral("audioResponse"), 128), 20, 200);
    responseRange_ = clampRange(integer(QStringLiteral("responseRange"), 100), 50, 220);
    centerHighlight_ = clampPercent(integer(QStringLiteral("centerHighlight"), 58));
    rhythmStrength_ = clampRange(integer(QStringLiteral("rhythmStrength"), 30), 0, 140);
    depthOfField_ = clampRange(integer(QStringLiteral("depthOfField"), 86), 0, 150);
    subjectClarity_ = clampRange(integer(QStringLiteral("subjectClarity"), 110), 20, 140);
    autoRotateSpeed_ = clampPercent(integer(QStringLiteral("autoRotateSpeed"), 42));
    rhythmSensitivity_ = clampPercent(integer(QStringLiteral("rhythmSensitivity"), 78));

    settings_.setValue(QStringLiteral("mode"), immersiveMode_);
    settings_.setValue(QStringLiteral("hostMode"), hostMode_);
    settings_.setValue(QStringLiteral("lyricsVisible"), lyricsVisible_);
    settings_.setValue(QStringLiteral("panelVisible"), panelVisible_);
    settings_.setValue(QStringLiteral("desktopMousePassthrough"), desktopMousePassthrough_);
    settings_.setValue(QStringLiteral("qualityPreset"), qualityPreset_);
    settings_.setValue(QStringLiteral("colorMode"), colorMode_);
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
    settings_.setValue(QStringLiteral("meteorsEnabled"), meteorsEnabled_);
    settings_.setValue(QStringLiteral("idleBreathingEnabled"), idleBreathingEnabled_);
    settings_.setValue(QStringLiteral("themeCycleEnabled"), themeCycleEnabled_);
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
    settings_.endGroup();
}

void PlayerExperienceController::persist(const QString& key, const QVariant& value)
{
    settings_.setValue(QLatin1String(kSettingsGroup) + QLatin1Char('/') + key, value);
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
