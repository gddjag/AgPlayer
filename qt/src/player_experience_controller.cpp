#include "player_experience_controller.hpp"

#include "settings_controller.hpp"

#include <QColor>
#include <QMetaType>

#include <algorithm>
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
QVariantList PlayerExperienceController::visualEqGains() const { return visualEqGains_; }

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

void PlayerExperienceController::setVisualEqGains(const QVariantList& values)
{
    const QVariantList normalized = normalizedVisualEqGains(values);
    if (visualEqGains_ == normalized) return;
    visualEqGains_ = normalized;
    persist(QStringLiteral("visualEqGains"), normalized);
    emit visualEqGainsChanged();
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
                                             QStringLiteral("#4F6FFF")),
                                 QStringLiteral("#4F6FFF"));
    warmColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("warmColor")),
                                             QStringLiteral("#FF4778")),
                                 QStringLiteral("#FF4778"));
    accentColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("accentColor")),
                                               QStringLiteral("#77EAFF")),
                                   QStringLiteral("#77EAFF"));
    peakColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("peakColor")),
                                             QStringLiteral("#D7FF58")),
                                 QStringLiteral("#D7FF58"));
    baseColor_ = normalizedColor(storedColor(settings_.value(QStringLiteral("baseColor")),
                                             QStringLiteral("#080616")),
                                 QStringLiteral("#080616"));
    terrainAmplitude_ = clampPercent(integer(QStringLiteral("terrainAmplitude"), 62));
    motionResponse_ = clampPercent(integer(QStringLiteral("motionResponse"), 56));
    gradientLayers_ = clampPercent(integer(QStringLiteral("gradientLayers"), 74));
    glowIntensity_ = clampPercent(integer(QStringLiteral("glowIntensity"), 38));
    cinemaShake_ = std::clamp(decimal(QStringLiteral("cinemaShake"), 0.40), 0.0, 1.8);
    autoRotate_ = clampPercent(integer(QStringLiteral("autoRotate"), 54));
    peakBoost_ = clampPercent(integer(QStringLiteral("peakBoost"), 58));
    ripplesEnabled_ = boolean(QStringLiteral("ripplesEnabled"), true);
    floatingCubesEnabled_ = boolean(QStringLiteral("floatingCubesEnabled"), true);
    meteorsEnabled_ = boolean(QStringLiteral("meteorsEnabled"), true);
    idleBreathingEnabled_ = boolean(QStringLiteral("idleBreathingEnabled"), true);
    themeCycleEnabled_ = boolean(QStringLiteral("themeCycleEnabled"), false);
    const QVariant persistedGains = settings_.value(QStringLiteral("visualEqGains"));
    const int persistedGainsType = persistedGains.metaType().id();
    visualEqGains_ = (persistedGainsType == QMetaType::QVariantList
                       || persistedGainsType == QMetaType::QStringList)
        ? normalizedVisualEqGains(persistedGains.toList()) : defaultVisualEqGains();

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
    settings_.setValue(QStringLiteral("floatingCubesEnabled"), floatingCubesEnabled_);
    settings_.setValue(QStringLiteral("meteorsEnabled"), meteorsEnabled_);
    settings_.setValue(QStringLiteral("idleBreathingEnabled"), idleBreathingEnabled_);
    settings_.setValue(QStringLiteral("themeCycleEnabled"), themeCycleEnabled_);
    settings_.setValue(QStringLiteral("visualEqGains"), visualEqGains_);
    settings_.endGroup();
}

void PlayerExperienceController::persist(const QString& key, const QVariant& value)
{
    settings_.setValue(QLatin1String(kSettingsGroup) + QLatin1Char('/') + key, value);
}

int PlayerExperienceController::clampPercent(int value) noexcept
{
    return std::clamp(value, 0, 100);
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
