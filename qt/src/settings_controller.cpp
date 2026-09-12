#include "settings_controller.hpp"
#include "update_checker.hpp"
#include "agplayer_update_config.hpp"

#include "agplayer_version.hpp"
#include "frequency_color_waveform_settings.hpp"

#ifdef Q_OS_MACOS
#include "macos_system_integration.hpp"
#endif

#include <QCoreApplication>
#include <QColor>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QList>
#include <QKeySequence>
#include <QMetaType>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "cache_janitor.hpp"
#include "audio_file_discovery.hpp"
#include "runtime_log.hpp"
#include "file_association_controller.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

QString SettingsController::shortcutDisplayText(const QString& portable) const
{
#ifdef Q_OS_MACOS
    QStringList labels;
    for (const QString& member : portable.split(QStringLiteral(" / "))) {
        QString parseable = member.trimmed();
        parseable.replace(QRegularExpression(QStringLiteral("\\s*\\+\\s*")),
                          QStringLiteral("+"));
        const QKeySequence key = QKeySequence::fromString(parseable, QKeySequence::PortableText);
        const QString label = key.toString(QKeySequence::NativeText);
        labels.append(label.isEmpty() ? member : label);
    }
    return labels.join(QStringLiteral(" / "));
#else
    return portable;
#endif
}

namespace {

QVariantMap defaultRollingKeyboardShortcuts()
{
    QVariantMap shortcuts{
        {QStringLiteral("cue"), QStringLiteral("C")},
        {QStringLiteral("cueJump"), QStringLiteral("Shift+C")},
        {QStringLiteral("cueDelete"), QStringLiteral("Alt+C")},
        {QStringLiteral("gridOrigin"), QStringLiteral("Q")},
        {QStringLiteral("gridLeft"), QStringLiteral("Left")},
        {QStringLiteral("gridRight"), QStringLiteral("Right")},
    };
    for (int index = 1; index <= 8; ++index) {
        shortcuts.insert(QStringLiteral("hotCue%1").arg(index),
                         QString::number(index));
        shortcuts.insert(QStringLiteral("hotCueDelete%1").arg(index),
                         QStringLiteral("Alt+%1").arg(index));
    }
    return shortcuts;
}

QString normalizedSingleShortcut(const QString& value)
{
    QString parseable = value.trimmed();
    if (parseable.isEmpty()) return {};
    static const QRegularExpression plusWhitespace(
        QStringLiteral("\\s*\\+\\s*"));
    static const QRegularExpression commaWhitespace(
        QStringLiteral("\\s*,\\s*"));
    parseable.replace(plusWhitespace, QStringLiteral("+"));
    parseable.replace(commaWhitespace, QStringLiteral(","));
    const QKeySequence parsed = QKeySequence::fromString(
        parseable, QKeySequence::PortableText);
    if (parsed.count() != 1) return {};
    const QString normalized = parsed.toString(QKeySequence::PortableText);
    if (normalized.isEmpty() || normalized.endsWith(QLatin1Char('+')))
        return {};
    return normalized;
}

QStringList normalizedShortcutMembers(const QString& value)
{
    QStringList members;
    const QStringList rawMembers = value.split(QLatin1Char('/'));
    for (const QString& rawMember : rawMembers) {
        const QString normalized = normalizedSingleShortcut(rawMember);
        if (!normalized.isEmpty()) members.append(normalized);
    }
    return members;
}

bool rollingShortcutConflicts(const QVariantMap& shortcuts,
                              const QString& action,
                              const QString& sequence)
{
    if (sequence.isEmpty()) return false;
    for (auto iterator = shortcuts.constBegin();
         iterator != shortcuts.constEnd(); ++iterator) {
        if (iterator.key() != action && iterator.value().toString() == sequence)
            return true;
    }
    return false;
}

template<typename T>
T clampValue(T value, T min, T max) noexcept
{
    return std::max(min, std::min(value, max));
}

double quantize(double value, double minimum, double maximum, double step) noexcept
{
    const double clamped = clampValue(value, minimum, maximum);
    return std::round(clamped / step) * step;
}

QString normalizedColor(const QString& value)
{
    const QColor color(value);
    return color.isValid() ? color.name(QColor::HexRgb) : QString();
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
            ? std::optional<int>(parsed)
            : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

void retireLegacySmartPlaylists()
{
    const QDir appData(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QDir().mkpath(appData.path());

    const QString smartPlaylists =
        appData.filePath(QStringLiteral("smart-playlists.json"));
    const QString retiredBackup = smartPlaylists + QStringLiteral(".retired.bak");
    if (QFile::exists(smartPlaylists) && !QFile::exists(retiredBackup)) {
        QFile::rename(smartPlaylists, retiredBackup);
    }
}

} // namespace

SettingsController::SettingsController(QObject* parent)
    : QObject(parent),
      settings_(this),
      fileAssociationController_(std::make_unique<FileAssociationController>(this)),
      frequencyColorWaveform_(
          std::make_unique<FrequencyColorWaveformSettings>(this))
{
    connect(frequencyColorWaveform_.get(),
            &FrequencyColorWaveformSettings::changed,
            this,
            [this] {
                if (!loading_ && !editActive_) {
                    settings_.beginGroup(QStringLiteral("appearance"));
                    frequencyColorWaveform_->save(settings_);
                    settings_.endGroup();
                }
            });
    fileAssociations_ = agplayer::qt::supportedAudioExtensions();
    retireLegacySmartPlaylists();
    load();
    updateChecker_ = new UpdateChecker(QUrl(QString::fromUtf8(agplayer::updates::kEndpoint)),
                                      QString::fromUtf8(agplayer::version::kVersion), this);
    // Refresh translated C++ status after the application has installed its translator.
    connect(this, &SettingsController::languageChanged, updateChecker_,
            &UpdateChecker::changed, Qt::QueuedConnection);
    applyAutoStartWithWindows();
    applyFileAssociations();
    recalculateCacheSize();
    QTimer::singleShot(10000, this, [this] { enforceCacheSizeLimit(); });
}

SettingsController::~SettingsController() = default;

QObject* SettingsController::updateChecker() const { return updateChecker_; }

// General getters
bool SettingsController::autoStartWithWindows() const noexcept { return autoStartWithWindows_; }
bool SettingsController::restoreLastPlaybackOnStartup() const noexcept { return restoreLastPlaybackOnStartup_; }
bool SettingsController::showListWindowPanel() const noexcept { return showListWindowPanel_; }
bool SettingsController::windowMagneticSnap() const noexcept { return windowMagneticSnap_; }
int SettingsController::listWindowPosition() const noexcept { return listWindowPosition_; }
int SettingsController::closeBehavior() const noexcept { return closeBehavior_; }
QString SettingsController::language() const { return language_; }
bool SettingsController::setAsDefaultPlayer() const noexcept { return setAsDefaultPlayer_; }
QStringList SettingsController::fileAssociations() const { return fileAssociations_; }
QString SettingsController::systemIntegrationError() const { return systemIntegrationError_; }
QString SettingsController::globalHotkeyError() const { return globalHotkeyError_; }

// Playback & Engine getters
QString SettingsController::outputDevice() const { return outputDevice_; }
bool SettingsController::exclusiveMode() const noexcept { return exclusiveMode_; }
bool SettingsController::matchTrackSampleRate() const noexcept { return matchTrackSampleRate_; }
int SettingsController::transitionFadeMs() const noexcept { return transitionFadeMs_; }
int SettingsController::defaultPlaybackMode() const noexcept { return defaultPlaybackMode_; }
bool SettingsController::autoReadBpm() const noexcept { return autoReadBpm_; }
bool SettingsController::autoReadRating() const noexcept { return autoReadRating_; }

// Appearance & Visualizer getters
int SettingsController::themeMode() const noexcept { return themeMode_; }
QString SettingsController::windowLayoutTheme() const { return windowLayoutTheme_; }
int SettingsController::playerShellMode() const noexcept { return playerShellMode_; }
bool SettingsController::rollingBeatGridEnabled() const noexcept
{
    return rollingBeatGridEnabled_;
}
int SettingsController::rollingBeatGridGrouping() const noexcept
{
    return rollingBeatGridGrouping_;
}
int SettingsController::waveformMode() const noexcept { return waveformMode_; }
double SettingsController::waveformHeight() const noexcept { return waveformHeight_; }
double SettingsController::waveformDensity() const noexcept { return waveformDensity_; }
double SettingsController::waveformThickness() const noexcept { return waveformThickness_; }
int SettingsController::waveformPeakAlgorithm() const noexcept { return waveformPeakAlgorithm_; }
QString SettingsController::waveformSolidBaseColor() const { return waveformSolidBaseColor_; }
QString SettingsController::waveformSolidProgressColor() const { return waveformSolidProgressColor_; }
QString SettingsController::waveformRgbBaseColor() const { return waveformRgbBaseColor_; }
QString SettingsController::waveformRgbStartColor() const { return waveformRgbStartColor_; }
QString SettingsController::waveformRgbMiddleColor() const { return waveformRgbMiddleColor_; }
QString SettingsController::waveformRgbEndColor() const { return waveformRgbEndColor_; }
FrequencyColorWaveformSettings* SettingsController::frequencyColorWaveform() const noexcept
{
    return frequencyColorWaveform_.get();
}
bool SettingsController::waveformRgbProgress() const noexcept { return waveformRgbProgress_; }
bool SettingsController::waveformHoverTimePreview() const noexcept { return waveformHoverTimePreview_; }
bool SettingsController::waveformPlaybackGuide() const noexcept { return waveformPlaybackGuide_; }
int SettingsController::waveformCanvasHeight() const noexcept { return waveformCanvasHeight_; }
bool SettingsController::waveformCanvasLocked() const noexcept { return waveformCanvasLocked_; }
bool SettingsController::listWaveformThumbnailEnabled() const noexcept
{
    return listWaveformThumbnailEnabled_;
}
QString SettingsController::listWaveformThumbnailMode() const
{
    return listWaveformThumbnailMode_;
}
double SettingsController::trackWaveformBrightness() const noexcept
{
    return trackWaveformBrightness_;
}
int SettingsController::spectrumColorMode() const noexcept { return spectrumColorMode_; }
QString SettingsController::spectrumSolidColor() const { return spectrumSolidColor_; }
QString SettingsController::spectrumRgbStartColor() const { return spectrumRgbStartColor_; }
QString SettingsController::spectrumRgbMiddleColor() const { return spectrumRgbMiddleColor_; }
QString SettingsController::spectrumRgbEndColor() const { return spectrumRgbEndColor_; }
int SettingsController::replayGainMode() const noexcept { return replayGainMode_; }
bool SettingsController::replayGainClipProtection() const noexcept { return replayGainClipProtection_; }

// Audio Tools getters
QString SettingsController::defaultOutputDirectory() const { return defaultOutputDirectory_; }
int SettingsController::parallelJobs() const noexcept { return parallelJobs_; }
int SettingsController::overwritePolicy() const noexcept { return overwritePolicy_; }
QString SettingsController::transcodeFormat() const { return transcodeFormat_; }
int SettingsController::transcodeBitrateKbps() const noexcept { return transcodeBitrateKbps_; }
int SettingsController::transcodeSampleRateHz() const noexcept { return transcodeSampleRateHz_; }
int SettingsController::transcodeChannels() const noexcept { return transcodeChannels_; }
bool SettingsController::preserveMetadata() const noexcept { return preserveMetadata_; }
bool SettingsController::preserveCover() const noexcept { return preserveCover_; }
bool SettingsController::preserveDirectoryStructure() const noexcept { return preserveDirectoryStructure_; }
bool SettingsController::extractVideoAudio() const noexcept { return extractVideoAudio_; }
bool SettingsController::keepPitchWhileSpeedChange() const noexcept { return keepPitchWhileSpeedChange_; }
bool SettingsController::vocalProtection() const noexcept { return vocalProtection_; }

// Hotkeys getters
QString SettingsController::hkPlayPause() const { return hkPlayPause_; }
QString SettingsController::hkPrevNext() const { return hkPrevNext_; }
QString SettingsController::hkVolumeUpDown() const { return hkVolumeUpDown_; }
QString SettingsController::hkToggleMiniPlayer() const { return hkToggleMiniPlayer_; }
QString SettingsController::hkSearch() const { return hkSearch_; }
QString SettingsController::hkWaveformMode() const { return hkWaveformMode_; }
QString SettingsController::hkAudioTools() const { return hkAudioTools_; }
QVariantMap SettingsController::rollingKeyboardShortcuts() const
{
    return rollingKeyboardShortcuts_;
}

// Cache & Storage getters
QString SettingsController::cacheDirectory() const { return cacheDirectory_; }
bool SettingsController::autoCleanCache() const noexcept { return autoCleanCache_; }
bool SettingsController::cleanTempOnExit() const noexcept { return cleanTempOnExit_; }
int SettingsController::cacheSizeLimitMB() const noexcept { return cacheSizeLimitMB_; }
int SettingsController::currentCacheSizeMB() const noexcept { return currentCacheSizeMB_; }

// About getters
QString SettingsController::version() const
{
    return QStringLiteral("v")
        + QString::fromLatin1(agplayer::version::kVersion);
}
QString SettingsController::releaseDate() const { return QStringLiteral("2026.10"); }
// General setters
void SettingsController::setAutoStartWithWindows(bool value)
{
    if (autoStartWithWindows_ == value) {
        return;
    }
    autoStartWithWindows_ = value;
    persistValue(QStringLiteral("general/autoStartWithWindows"), value);
    if (!editActive_) {
        applyAutoStartWithWindows();
    }

    emit autoStartWithWindowsChanged();
}

void SettingsController::setRestoreLastPlaybackOnStartup(bool value)
{
    if (restoreLastPlaybackOnStartup_ == value) {
        return;
    }
    restoreLastPlaybackOnStartup_ = value;
    persistValue(QStringLiteral("general/restoreLastPlaybackOnStartup"), value);
    emit restoreLastPlaybackOnStartupChanged();
}

void SettingsController::setShowListWindowPanel(bool value)
{
    if (showListWindowPanel_ == value) {
        return;
    }
    showListWindowPanel_ = value;
    persistValue(QStringLiteral("general/showListWindowPanel"), value);
    emit showListWindowPanelChanged();
}

void SettingsController::setWindowMagneticSnap(bool value)
{
    if (windowMagneticSnap_ == value) {
        return;
    }
    windowMagneticSnap_ = value;
    persistValue(QStringLiteral("general/windowMagneticSnap"), value);
    emit windowMagneticSnapChanged();
}

void SettingsController::setListWindowPosition(int value)
{
    value = clampValue(value, 0, 3);
    if (listWindowPosition_ == value) {
        return;
    }
    listWindowPosition_ = value;
    persistValue(QStringLiteral("general/listWindowPosition"), value);
    emit listWindowPositionChanged();
}

void SettingsController::setCloseBehavior(int value)
{
    value = clampValue(value, 0, 1);
    if (closeBehavior_ == value) {
        return;
    }
    closeBehavior_ = value;
    persistValue(QStringLiteral("general/closeBehavior"), value);
    emit closeBehaviorChanged();
}

void SettingsController::setLanguage(const QString& value)
{
    const QString normalized = validatedLanguage(value);
    if (language_ == normalized) {
        return;
    }
    language_ = normalized;
    persistValue(QStringLiteral("general/language"), normalized);
    emit languageChanged();
}

void SettingsController::setSetAsDefaultPlayer(bool value)
{
    if (setAsDefaultPlayer_ == value) {
        return;
    }
    setAsDefaultPlayer_ = value;
    persistValue(QStringLiteral("general/setAsDefaultPlayer"), value);

    if (!editActive_) {
        applyFileAssociations();
    }

    emit setAsDefaultPlayerChanged();
}

void SettingsController::setFileAssociations(const QStringList& value)
{
    if (fileAssociations_ == value) {
        return;
    }
    fileAssociations_ = value;
    persistValue(QStringLiteral("general/fileAssociations"), value);

    if (!editActive_) {
        applyFileAssociations();
    }

    emit fileAssociationsChanged();
}

void SettingsController::setSystemIntegrationError(const QString& value)
{
    if (systemIntegrationError_ == value) return;
    systemIntegrationError_ = value;
    emit systemIntegrationErrorChanged();
}

void SettingsController::setGlobalHotkeyError(const QString& value)
{
    if (globalHotkeyError_ == value) return;
    globalHotkeyError_ = value;
    emit globalHotkeyErrorChanged();
}

// Playback & Engine setters
void SettingsController::setOutputDevice(const QString& value)
{
    if (outputDevice_ == value) {
        return;
    }
    outputDevice_ = value;
    persistValue(QStringLiteral("playback/outputDevice"), value);
    emit outputDeviceChanged();
}

void SettingsController::setExclusiveMode(const bool value)
{
    if (exclusiveMode_ == value) {
        return;
    }
    exclusiveMode_ = value;
    persistValue(QStringLiteral("playback/exclusiveMode"), value);
    emit exclusiveModeChanged();
}

void SettingsController::setMatchTrackSampleRate(const bool value)
{
    if (matchTrackSampleRate_ == value) {
        return;
    }
    matchTrackSampleRate_ = value;
    persistValue(QStringLiteral("playback/matchTrackSampleRate"), value);
    emit matchTrackSampleRateChanged();
}

void SettingsController::setTransitionFadeMs(const int value)
{
    if (value != 0 && value != 200 && value != 500) {
        return;
    }
    if (transitionFadeMs_ == value) {
        return;
    }
    transitionFadeMs_ = value;
    persistValue(QStringLiteral("playback/transitionFadeMs"), value);
    emit transitionFadeMsChanged();
}

void SettingsController::setDefaultPlaybackMode(int value)
{
    value = clampValue(value, 0, 3);
    if (defaultPlaybackMode_ == value) {
        return;
    }
    defaultPlaybackMode_ = value;
    persistValue(QStringLiteral("playback/defaultPlaybackMode"), value);
    persistValue(QStringLiteral("playback/modeSchemaVersion"), 2);
    emit defaultPlaybackModeChanged();
}

void SettingsController::setAutoReadBpm(bool value)
{
    if (autoReadBpm_ == value) {
        return;
    }
    autoReadBpm_ = value;
    persistValue(QStringLiteral("playback/autoReadBpm"), value);
    emit autoReadBpmChanged();
}

void SettingsController::setAutoReadRating(bool value)
{
    if (autoReadRating_ == value) {
        return;
    }
    autoReadRating_ = value;
    persistValue(QStringLiteral("playback/autoReadRating"), value);
    emit autoReadRatingChanged();
}

// Appearance & Visualizer setters
void SettingsController::setThemeMode(int value)
{
    value = value >= 0 && value <= 2 ? value : 0;
    if (themeMode_ == value) {
        return;
    }
    themeMode_ = value;
    persistValue(QStringLiteral("appearance/themeMode"), value);
    emit themeModeChanged();
}

void SettingsController::setPlayerShellMode(int value)
{
    value = value >= Classic && value <= Rolling ? value : Classic;
    const QString layout = value == Integrated
        ? QStringLiteral("single-window")
        : value == Rolling ? QStringLiteral("rolling-player")
                           : QStringLiteral("dual-window");
    const bool modeChanged = playerShellMode_ != value;
    const bool layoutChanged = windowLayoutTheme_ != layout;
    if (!modeChanged && !layoutChanged) {
        persistValue(QStringLiteral("appearance/playerShellMode"), value);
        persistValue(QStringLiteral("appearance/windowLayoutTheme"), layout);
        return;
    }
    playerShellMode_ = value;
    windowLayoutTheme_ = layout;
    persistValue(QStringLiteral("appearance/playerShellMode"), value);
    persistValue(QStringLiteral("appearance/windowLayoutTheme"), layout);
    if (modeChanged) {
        emit playerShellModeChanged();
    }
    if (layoutChanged) {
        emit windowLayoutThemeChanged();
    }
}

void SettingsController::setRollingBeatGridEnabled(const bool value)
{
    if (rollingBeatGridEnabled_ == value) return;
    rollingBeatGridEnabled_ = value;
    persistValue(QStringLiteral("appearance/rollingBeatGridEnabled"), value);
    emit rollingBeatGridEnabledChanged();
}

void SettingsController::setRollingBeatGridGrouping(int value)
{
    value = value == 8 ? 8 : 4;
    if (rollingBeatGridGrouping_ == value) return;
    rollingBeatGridGrouping_ = value;
    persistValue(QStringLiteral("appearance/rollingBeatGridGrouping"), value);
    emit rollingBeatGridGroupingChanged();
}

void SettingsController::setWindowLayoutTheme(const QString& value)
{
    const QString normalized = value == QStringLiteral("single-window")
        ? QStringLiteral("single-window")
        : value == QStringLiteral("rolling-player")
            ? QStringLiteral("rolling-player") : QStringLiteral("dual-window");
    const int mode = normalized == QStringLiteral("single-window") ? Integrated
        : normalized == QStringLiteral("rolling-player") ? Rolling : Classic;
    const bool layoutChanged = windowLayoutTheme_ != normalized;
    const bool modeChanged = playerShellMode_ != mode;
    if (!layoutChanged && !modeChanged) {
        persistValue(QStringLiteral("appearance/windowLayoutTheme"), normalized);
        persistValue(QStringLiteral("appearance/playerShellMode"), mode);
        return;
    }
    windowLayoutTheme_ = normalized;
    playerShellMode_ = mode;
    persistValue(QStringLiteral("appearance/windowLayoutTheme"), normalized);
    persistValue(QStringLiteral("appearance/playerShellMode"), mode);
    if (layoutChanged) {
        emit windowLayoutThemeChanged();
    }
    if (modeChanged) {
        emit playerShellModeChanged();
    }
}

void SettingsController::setWaveformMode(int value)
{
    value = clampValue(value, 0, 3);
    if (waveformMode_ == value) {
        return;
    }
    waveformMode_ = value;
    persistValue(QStringLiteral("appearance/waveformMode"), value);
    emit waveformModeChanged();
}

void SettingsController::cycleWaveformMode()
{
    switch (waveformMode_) {
    case 0: setWaveformMode(3); break;
    case 3: setWaveformMode(1); break;
    case 1: setWaveformMode(2); break;
    default: setWaveformMode(0); break;
    }
}

void SettingsController::setWaveformHeight(double value)
{
    value = quantize(value, 0.3, 1.5, 0.1);
    if (qFuzzyCompare(waveformHeight_, value)) {
        return;
    }
    waveformHeight_ = value;
    persistValue(QStringLiteral("appearance/waveformHeight"), value);
    emit waveformHeightChanged();
}

void SettingsController::setWaveformDensity(double value)
{
    value = quantize(value, 0.5, 5.0, 0.5);
    if (qFuzzyCompare(waveformDensity_, value)) {
        return;
    }
    waveformDensity_ = value;
    persistValue(QStringLiteral("appearance/waveformDensity"), value);
    emit waveformDensityChanged();
}

void SettingsController::setWaveformThickness(double value)
{
    value = quantize(value, 0.3, 3.0, 0.1);
    if (qFuzzyCompare(waveformThickness_, value)) {
        return;
    }
    waveformThickness_ = value;
    persistValue(QStringLiteral("appearance/waveformThickness"), value);
    emit waveformThicknessChanged();
}

void SettingsController::setWaveformPeakAlgorithm(int value)
{
    value = clampValue(value, 0, 1);
    if (waveformPeakAlgorithm_ == value) {
        return;
    }
    waveformPeakAlgorithm_ = value;
    persistValue(QStringLiteral("appearance/waveformPeakAlgorithm"), value);
    emit waveformPeakAlgorithmChanged();
}

#define AGPLAYER_COLOR_SETTER(Name, member, key, signalName) \
    void SettingsController::Name(const QString& value)      \
    {                                                        \
        const QString color = normalizedColor(value);        \
        if (color.isEmpty() || member == color) return;       \
        member = color;                                      \
        persistValue(QStringLiteral(key), color);             \
        emit signalName();                                   \
    }

AGPLAYER_COLOR_SETTER(setWaveformSolidBaseColor, waveformSolidBaseColor_,
                      "appearance/waveformSolidBaseColor", waveformSolidBaseColorChanged)
AGPLAYER_COLOR_SETTER(setWaveformSolidProgressColor, waveformSolidProgressColor_,
                      "appearance/waveformSolidProgressColor", waveformSolidProgressColorChanged)
AGPLAYER_COLOR_SETTER(setWaveformRgbBaseColor, waveformRgbBaseColor_,
                      "appearance/waveformRgbBaseColor", waveformRgbBaseColorChanged)
AGPLAYER_COLOR_SETTER(setWaveformRgbStartColor, waveformRgbStartColor_,
                      "appearance/waveformRgbStartColor", waveformRgbStartColorChanged)
AGPLAYER_COLOR_SETTER(setWaveformRgbMiddleColor, waveformRgbMiddleColor_,
                      "appearance/waveformRgbMiddleColor", waveformRgbMiddleColorChanged)
AGPLAYER_COLOR_SETTER(setWaveformRgbEndColor, waveformRgbEndColor_,
                      "appearance/waveformRgbEndColor", waveformRgbEndColorChanged)
void SettingsController::setWaveformRgbProgress(bool value)
{
    if (waveformRgbProgress_ == value) return;
    waveformRgbProgress_ = value;
    persistValue(QStringLiteral("appearance/waveformRgbProgress"), value);
    emit waveformRgbProgressChanged();
}

void SettingsController::setWaveformHoverTimePreview(bool value)
{
    if (waveformHoverTimePreview_ == value) {
        return;
    }
    waveformHoverTimePreview_ = value;
    persistValue(QStringLiteral("appearance/waveformHoverTimePreview"), value);
    emit waveformHoverTimePreviewChanged();
}

void SettingsController::setWaveformPlaybackGuide(bool value)
{
    if (waveformPlaybackGuide_ == value) {
        return;
    }
    waveformPlaybackGuide_ = value;
    persistValue(QStringLiteral("appearance/waveformPlaybackGuide"), value);
    emit waveformPlaybackGuideChanged();
}

void SettingsController::setWaveformCanvasHeight(int value)
{
    value = clampValue(value, 48, 84);
    if (waveformCanvasHeight_ == value) return;
    waveformCanvasHeight_ = value;
    persistValue(QStringLiteral("appearance/waveformCanvasHeight"), value);
    emit waveformCanvasHeightChanged();
}

#define AGPLAYER_BOOL_SETTER(Name, member, key, signalName) \
    void SettingsController::Name(bool value)               \
    {                                                        \
        if (member == value) return;                         \
        member = value;                                      \
        persistValue(QStringLiteral(key), value);             \
        emit signalName();                                   \
    }

AGPLAYER_BOOL_SETTER(setWaveformCanvasLocked, waveformCanvasLocked_,
                     "appearance/waveformCanvasLocked", waveformCanvasLockedChanged)
AGPLAYER_BOOL_SETTER(setListWaveformThumbnailEnabled,
                     listWaveformThumbnailEnabled_,
                     "appearance/listWaveformThumbnailEnabled",
                     listWaveformThumbnailEnabledChanged)
AGPLAYER_BOOL_SETTER(setReplayGainClipProtection, replayGainClipProtection_,
                     "playback/replayGainClipProtection", replayGainClipProtectionChanged)

#undef AGPLAYER_BOOL_SETTER

void SettingsController::setListWaveformThumbnailMode(const QString& value)
{
    const QString normalized = value == QStringLiteral("Mono")
        ? QStringLiteral("Mono") : QStringLiteral("Spectral");
    if (listWaveformThumbnailMode_ == normalized) {
        return;
    }
    listWaveformThumbnailMode_ = normalized;
    persistValue(QStringLiteral("appearance/listWaveformThumbnailMode"),
                 normalized);
    emit listWaveformThumbnailModeChanged();
}

void SettingsController::setTrackWaveformBrightness(double value)
{
    const double normalized = std::isfinite(value)
        ? clampValue(value, 0.20, 1.00) : 0.50;
    if (trackWaveformBrightness_ == normalized) {
        return;
    }
    trackWaveformBrightness_ = normalized;
    persistValue(QStringLiteral("appearance/trackWaveformBrightness"),
                 normalized);
    emit trackWaveformBrightnessChanged();
}

void SettingsController::setSpectrumColorMode(int value)
{
    value = clampValue(value, 0, 1);
    if (spectrumColorMode_ == value) return;
    spectrumColorMode_ = value;
    persistValue(QStringLiteral("appearance/spectrumColorMode"), value);
    emit spectrumColorModeChanged();
}

AGPLAYER_COLOR_SETTER(setSpectrumSolidColor, spectrumSolidColor_,
                      "appearance/spectrumSolidColor", spectrumSolidColorChanged)
AGPLAYER_COLOR_SETTER(setSpectrumRgbStartColor, spectrumRgbStartColor_,
                      "appearance/spectrumRgbStartColor", spectrumRgbStartColorChanged)
AGPLAYER_COLOR_SETTER(setSpectrumRgbMiddleColor, spectrumRgbMiddleColor_,
                      "appearance/spectrumRgbMiddleColor", spectrumRgbMiddleColorChanged)
AGPLAYER_COLOR_SETTER(setSpectrumRgbEndColor, spectrumRgbEndColor_,
                      "appearance/spectrumRgbEndColor", spectrumRgbEndColorChanged)

#undef AGPLAYER_COLOR_SETTER


void SettingsController::setReplayGainMode(int value)
{
    value = clampValue(value, 0, 2);
    if (replayGainMode_ == value) return;
    replayGainMode_ = value;
    persistValue(QStringLiteral("playback/replayGainMode"), value);
    emit replayGainModeChanged();
}

// Audio Tools setters
void SettingsController::setDefaultOutputDirectory(const QString& value)
{
    if (defaultOutputDirectory_ == value) {
        return;
    }
    defaultOutputDirectory_ = value;
    persistValue(QStringLiteral("audioTools/defaultOutputDirectory"), value);
    emit defaultOutputDirectoryChanged();
}

void SettingsController::setParallelJobs(int value)
{
    value = clampValue(value, 1, 10);
    if (parallelJobs_ == value) {
        return;
    }
    parallelJobs_ = value;
    persistValue(QStringLiteral("audioTools/parallelJobs"), value);
    emit parallelJobsChanged();
}

void SettingsController::setOverwritePolicy(int value)
{
    value = clampValue(value, 0, 1);
    if (overwritePolicy_ == value) {
        return;
    }
    overwritePolicy_ = value;
    persistValue(QStringLiteral("audioTools/overwritePolicy"), value);
    emit overwritePolicyChanged();
}

void SettingsController::setTranscodeFormat(const QString& value)
{
    const QString normalized = value.trimmed().toUpper();
    static const QSet<QString> supportedFormats = {
        QStringLiteral("MP3"), QStringLiteral("FLAC"),
        QStringLiteral("WAV"), QStringLiteral("AAC"),
        QStringLiteral("OPUS"), QStringLiteral("OGG"),
        QStringLiteral("ALAC"), QStringLiteral("AIFF")};
    const QString accepted = supportedFormats.contains(normalized)
        ? normalized : QStringLiteral("MP3");
    if (transcodeFormat_ == accepted) {
        return;
    }
    transcodeFormat_ = accepted;
    persistValue(QStringLiteral("audioTools/transcodeFormat"), accepted);
    emit transcodeFormatChanged();
}

void SettingsController::setTranscodeBitrateKbps(int value)
{
    static const QList<int> allowed = {128, 192, 256, 320};
    if (!allowed.contains(value) || transcodeBitrateKbps_ == value) {
        return;
    }
    transcodeBitrateKbps_ = value;
    persistValue(QStringLiteral("audioTools/transcodeBitrateKbps"), value);
    emit transcodeBitrateKbpsChanged();
}

void SettingsController::setTranscodeSampleRateHz(int value)
{
    static const QList<int> allowed = {
        44100, 48000, 88200, 96000, 176400, 192000};
    if (!allowed.contains(value) || transcodeSampleRateHz_ == value) {
        return;
    }
    transcodeSampleRateHz_ = value;
    persistValue(QStringLiteral("audioTools/transcodeSampleRateHz"), value);
    emit transcodeSampleRateHzChanged();
}

void SettingsController::setTranscodeChannels(int value)
{
    if ((value != 1 && value != 2) || transcodeChannels_ == value) {
        return;
    }
    transcodeChannels_ = value;
    persistValue(QStringLiteral("audioTools/transcodeChannels"), value);
    emit transcodeChannelsChanged();
}

void SettingsController::setPreserveMetadata(bool value)
{
    if (preserveMetadata_ == value) {
        return;
    }
    preserveMetadata_ = value;
    persistValue(QStringLiteral("audioTools/preserveMetadata"), value);
    emit preserveMetadataChanged();
}

void SettingsController::setPreserveCover(bool value)
{
    if (preserveCover_ == value) return;
    preserveCover_ = value;
    persistValue(QStringLiteral("audioTools/preserveCover"), value);
    emit preserveCoverChanged();
}

void SettingsController::setPreserveDirectoryStructure(bool value)
{
    if (preserveDirectoryStructure_ == value) return;
    preserveDirectoryStructure_ = value;
    persistValue(QStringLiteral("audioTools/preserveDirectoryStructure"), value);
    emit preserveDirectoryStructureChanged();
}

void SettingsController::setExtractVideoAudio(bool value)
{
    if (extractVideoAudio_ == value) return;
    extractVideoAudio_ = value;
    persistValue(QStringLiteral("audioTools/extractVideoAudio"), value);
    emit extractVideoAudioChanged();
}

void SettingsController::setKeepPitchWhileSpeedChange(bool value)
{
    if (keepPitchWhileSpeedChange_ == value) {
        return;
    }
    keepPitchWhileSpeedChange_ = value;
    persistValue(QStringLiteral("audioTools/keepPitchWhileSpeedChange"), value);
    emit keepPitchWhileSpeedChangeChanged();
}

void SettingsController::setVocalProtection(bool value)
{
    if (vocalProtection_ == value) {
        return;
    }
    vocalProtection_ = value;
    persistValue(QStringLiteral("audioTools/vocalProtection"), value);
    emit vocalProtectionChanged();
}

// Hotkeys setters
bool SettingsController::conflictsWithLegacyKeyboardShortcuts(
    const QString& sequence) const
{
    const QString normalized = normalizedSingleShortcut(sequence);
    if (normalized.isEmpty()) return false;
    // Main.qml owns Space as an unconditional window-local transport key;
    // it remains reserved even when the configurable global key changes.
    if (normalized == QStringLiteral("Space")) return true;
    const QStringList legacyValues{
        hkPlayPause_, hkPrevNext_, hkVolumeUpDown_, hkToggleMiniPlayer_,
        hkSearch_, hkWaveformMode_, hkAudioTools_};
    for (const QString& legacyValue : legacyValues) {
        if (normalizedShortcutMembers(legacyValue).contains(normalized))
            return true;
    }
    return false;
}

bool SettingsController::conflictsWithRollingKeyboardShortcuts(
    const QString& value) const
{
    const QStringList candidates = normalizedShortcutMembers(value);
    for (const QString& candidate : candidates) {
        for (auto iterator = rollingKeyboardShortcuts_.constBegin();
             iterator != rollingKeyboardShortcuts_.constEnd(); ++iterator) {
            if (iterator.value().toString() == candidate) return true;
        }
    }
    return false;
}

void SettingsController::setHkPlayPause(const QString& value)
{
    if (hkPlayPause_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkPlayPause_ = value;
    persistValue(QStringLiteral("hotkeys/playPause"), value);
    emit hkPlayPauseChanged();
}

void SettingsController::setHkPrevNext(const QString& value)
{
    if (hkPrevNext_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkPrevNext_ = value;
    persistValue(QStringLiteral("hotkeys/prevNext"), value);
    emit hkPrevNextChanged();
}

void SettingsController::setHkVolumeUpDown(const QString& value)
{
    if (hkVolumeUpDown_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkVolumeUpDown_ = value;
    persistValue(QStringLiteral("hotkeys/volumeUpDown"), value);
    emit hkVolumeUpDownChanged();
}

void SettingsController::setHkToggleMiniPlayer(const QString& value)
{
    if (hkToggleMiniPlayer_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkToggleMiniPlayer_ = value;
    persistValue(QStringLiteral("hotkeys/toggleMiniPlayer"), value);
    emit hkToggleMiniPlayerChanged();
}

void SettingsController::setHkSearch(const QString& value)
{
    if (hkSearch_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkSearch_ = value;
    persistValue(QStringLiteral("hotkeys/search"), value);
    emit hkSearchChanged();
}

void SettingsController::setHkWaveformMode(const QString& value)
{
    if (hkWaveformMode_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkWaveformMode_ = value;
    persistValue(QStringLiteral("hotkeys/waveformMode"), value);
    emit hkWaveformModeChanged();
}

void SettingsController::setHkAudioTools(const QString& value)
{
    if (hkAudioTools_ == value) {
        return;
    }
    if (conflictsWithRollingKeyboardShortcuts(value)) return;
    hkAudioTools_ = value;
    persistValue(QStringLiteral("hotkeys/audioTools"), value);
    emit hkAudioToolsChanged();
}

bool SettingsController::setRollingKeyboardShortcut(const QString& action,
                                                     const QString& sequence)
{
    const QVariantMap defaults = defaultRollingKeyboardShortcuts();
    if (!defaults.contains(action)) return false;
    const QString trimmed = sequence.trimmed();
    const QString normalized = normalizedSingleShortcut(trimmed);
    if (!trimmed.isEmpty() && normalized.isEmpty()) return false;
    if (rollingShortcutConflicts(rollingKeyboardShortcuts_, action,
                                 normalized)
        || conflictsWithLegacyKeyboardShortcuts(normalized)) {
        return false;
    }
    if (rollingKeyboardShortcuts_.value(action).toString() == normalized)
        return true;
    rollingKeyboardShortcuts_.insert(action, normalized);
    persistValue(QStringLiteral("hotkeys/rollingKeyboardShortcuts"),
                 rollingKeyboardShortcuts_);
    emit rollingKeyboardShortcutsChanged();
    return true;
}

void SettingsController::resetRollingKeyboardShortcuts()
{
    QVariantMap defaults = defaultRollingKeyboardShortcuts();
    for (auto iterator = defaults.begin(); iterator != defaults.end();
         ++iterator) {
        if (conflictsWithLegacyKeyboardShortcuts(iterator.value().toString()))
            iterator.value() = QString();
    }
    if (rollingKeyboardShortcuts_ == defaults) return;
    rollingKeyboardShortcuts_ = defaults;
    persistValue(QStringLiteral("hotkeys/rollingKeyboardShortcuts"),
                 rollingKeyboardShortcuts_);
    emit rollingKeyboardShortcutsChanged();
}

// Cache & Storage setters
void SettingsController::setCacheDirectory(const QString& value)
{
    if (cacheDirectory_ == value) {
        return;
    }
    cacheDirectory_ = value;
    persistValue(QStringLiteral("cache/directory"), value);
    emit cacheDirectoryChanged();
}

void SettingsController::setAutoCleanCache(bool value)
{
    if (autoCleanCache_ == value) {
        return;
    }
    autoCleanCache_ = value;
    persistValue(QStringLiteral("cache/autoCleanCache"), value);
    emit autoCleanCacheChanged();
    if (!editActive_ && autoCleanCache_) {
        enforceCacheSizeLimit();
    }
}

void SettingsController::setCleanTempOnExit(bool value)
{
    if (cleanTempOnExit_ == value) {
        return;
    }
    cleanTempOnExit_ = value;
    persistValue(QStringLiteral("cache/cleanTempOnExit"), value);
    emit cleanTempOnExitChanged();
}

void SettingsController::setCacheSizeLimitMB(int value)
{
    value = std::max(value, 100);
    if (cacheSizeLimitMB_ == value) {
        return;
    }
    cacheSizeLimitMB_ = value;
    persistValue(QStringLiteral("cache/sizeLimitMB"), value);
    persistValue(QStringLiteral("cache/sizeLimitUserModified"), true);
    emit cacheSizeLimitMBChanged();
    if (!editActive_ && autoCleanCache_ && cacheSizeLimitMB_ > 0) {
        enforceCacheSizeLimit();
    }
}

void SettingsController::resetToDefaults()
{
    restoreDefaults(false);
    saveAll(false);
    if (!editActive_) {
        applyCommittedEffects();
    }
    emitAllChanged(false);
}

void SettingsController::resetWaveformDefaults()
{
    setWaveformMode(0);
    setWaveformHeight(0.8);
    setWaveformDensity(2.0);
    setWaveformThickness(1.0);
    setWaveformPeakAlgorithm(0);
    setWaveformSolidBaseColor(QStringLiteral("#9098a6"));
    setWaveformSolidProgressColor(QStringLiteral("#d27722"));
    setWaveformRgbBaseColor(QStringLiteral("#00b4a0"));
    setWaveformRgbStartColor(QStringLiteral("#00d4ff"));
    setWaveformRgbMiddleColor(QStringLiteral("#7b2ff7"));
    setWaveformRgbEndColor(QStringLiteral("#e62e9b"));
    frequencyColorWaveform_->resetToDefault();
    setWaveformRgbProgress(false);
    setWaveformPlaybackGuide(false);
    setWaveformCanvasHeight(78);
    setWaveformCanvasLocked(true);
    setSpectrumColorMode(1);
    setSpectrumSolidColor(QStringLiteral("#7b2ff7"));
    setSpectrumRgbStartColor(QStringLiteral("#00d4ff"));
    setSpectrumRgbMiddleColor(QStringLiteral("#7b2ff7"));
    setSpectrumRgbEndColor(QStringLiteral("#e62e9b"));
    setListWaveformThumbnailEnabled(true);
    setListWaveformThumbnailMode(QStringLiteral("Spectral"));
    setTrackWaveformBrightness(0.50);
}

void SettingsController::beginEdit()
{
    if (editActive_) {
        return;
    }
    saveAll();
    settings_.sync();
    editActive_ = true;
}

void SettingsController::commitEdit()
{
    if (!editActive_) {
        return;
    }
    editActive_ = false;
    saveAll();
    settings_.sync();
    applyCommittedEffects();
}

void SettingsController::cancelEdit()
{
    if (!editActive_) {
        return;
    }
    editActive_ = false;
    load();
    emitAllChanged();
}

void SettingsController::persistValue(const QString& key, const QVariant& value)
{
    if (!editActive_) {
        settings_.setValue(key, value);
    }
}

void SettingsController::applyAutoStartWithWindows()
{
    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }
#ifdef Q_OS_MACOS
    const bool requested = autoStartWithWindows_;
    QString operationError;
    const bool operationSucceeded = agplayer::qt::macos::setLoginItemEnabled(
        requested, &operationError);
    QString statusError;
    const bool actual = agplayer::qt::macos::loginItemEnabled(&statusError);
    if (operationSucceeded && actual == requested) {
        return;
    }
    QString error = operationError;
    if (error.isEmpty() && !statusError.isEmpty()) error = statusError;
    if (error.isEmpty()) error = QStringLiteral("macOS 登录项状态未能应用");
    RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
        QStringLiteral("macOS login item state did not match the requested setting: %1")
            .arg(error));
    if (!systemIntegrationError_.contains(error)) {
        setSystemIntegrationError(systemIntegrationError_.isEmpty()
                                      ? error
                                      : systemIntegrationError_ + QLatin1Char('\n') + error);
    }
    if (autoStartWithWindows_ != actual) {
        autoStartWithWindows_ = actual;
        persistValue(QStringLiteral("general/autoStartWithWindows"), actual);
        emit autoStartWithWindowsChanged();
    }
#elif defined(Q_OS_WIN)
    const QString appPath = QCoreApplication::applicationFilePath();
    QSettings run(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    if (autoStartWithWindows_) {
        run.setValue(QStringLiteral("AgPlayer"),
                     QStringLiteral("\"%1\"").arg(appPath));
    } else {
        run.remove(QStringLiteral("AgPlayer"));
    }
#else
    Q_UNUSED(autoStartWithWindows_)
#endif
}

void SettingsController::applyCommittedEffects()
{
    applyAutoStartWithWindows();
    applyFileAssociations();
    if (autoCleanCache_ && cacheSizeLimitMB_ > 0) {
        enforceCacheSizeLimit();
    }
}

void SettingsController::emitAllChanged(const bool includeMediaSettings)
{
    emit autoStartWithWindowsChanged();
    emit restoreLastPlaybackOnStartupChanged();
    emit showListWindowPanelChanged();
    emit windowMagneticSnapChanged();
    emit listWindowPositionChanged();
    emit closeBehaviorChanged();
    emit languageChanged();
    emit setAsDefaultPlayerChanged();
    emit fileAssociationsChanged();

    emit outputDeviceChanged();
    emit exclusiveModeChanged();
    emit matchTrackSampleRateChanged();
    emit transitionFadeMsChanged();
    emit defaultPlaybackModeChanged();
    emit autoReadBpmChanged();
    emit autoReadRatingChanged();

    emit themeModeChanged();
    emit windowLayoutThemeChanged();
    emit playerShellModeChanged();
    emit rollingBeatGridEnabledChanged();
    emit rollingBeatGridGroupingChanged();
    if (includeMediaSettings) {
        emit waveformModeChanged();
        emit waveformHeightChanged();
        emit waveformDensityChanged();
        emit waveformThicknessChanged();
        emit waveformPeakAlgorithmChanged();
        emit waveformSolidBaseColorChanged();
        emit waveformSolidProgressColorChanged();
        emit waveformRgbBaseColorChanged();
        emit waveformRgbStartColorChanged();
        emit waveformRgbMiddleColorChanged();
        emit waveformRgbEndColorChanged();
        emit waveformRgbProgressChanged();
        emit waveformHoverTimePreviewChanged();
        emit waveformPlaybackGuideChanged();
        emit waveformCanvasHeightChanged();
        emit waveformCanvasLockedChanged();
        emit listWaveformThumbnailEnabledChanged();
        emit listWaveformThumbnailModeChanged();
        emit trackWaveformBrightnessChanged();
        emit spectrumColorModeChanged();
        emit spectrumSolidColorChanged();
        emit spectrumRgbStartColorChanged();
        emit spectrumRgbMiddleColorChanged();
        emit spectrumRgbEndColorChanged();
    }
    emit replayGainModeChanged();
    emit replayGainClipProtectionChanged();

    emit defaultOutputDirectoryChanged();
    emit parallelJobsChanged();
    emit overwritePolicyChanged();
    emit transcodeFormatChanged();
    emit transcodeBitrateKbpsChanged();
    emit transcodeSampleRateHzChanged();
    emit transcodeChannelsChanged();
    emit preserveMetadataChanged();
    emit preserveCoverChanged();
    emit preserveDirectoryStructureChanged();
    emit extractVideoAudioChanged();
    emit keepPitchWhileSpeedChangeChanged();
    emit vocalProtectionChanged();

    emit hkPlayPauseChanged();
    emit hkPrevNextChanged();
    emit hkVolumeUpDownChanged();
    emit hkToggleMiniPlayerChanged();
    emit hkSearchChanged();
    emit hkWaveformModeChanged();
    emit hkAudioToolsChanged();
    emit rollingKeyboardShortcutsChanged();

    emit cacheDirectoryChanged();
    emit autoCleanCacheChanged();
    emit cleanTempOnExitChanged();
    emit cacheSizeLimitMBChanged();
    emit currentCacheSizeMBChanged();
}

static bool isSafeCachePath(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.isAbsolute()) {
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    // Refuse root/system paths (e.g. C:/, D:/).
    if (canonical.length() <= 3) {
        return false;
    }
    // Require the path to contain "AgPlayer" to avoid wiping arbitrary directories.
    if (!canonical.contains(QStringLiteral("AgPlayer"), Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

static bool removeDirectoryContents(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return true;
    }
    bool ok = true;
    for (const QString& entry : dir.entryList(QDir::NoDotAndDotDot | QDir::Files
                                              | QDir::Dirs | QDir::Hidden)) {
        const QString fullPath = dir.absoluteFilePath(entry);
        const QFileInfo info(fullPath);
        if (info.isDir() && !info.isSymLink()) {
            ok &= QDir(fullPath).removeRecursively();
        } else {
            ok &= QFile::remove(fullPath);
        }
    }
    return ok;
}

void SettingsController::rebindFileAssociations()
{
    // Rebinding is an explicit opt-in action.  It must also work while the
    // settings transaction is open; otherwise the button silently unregisters
    // everything when the switch was previously off.
    setSetAsDefaultPlayer(true);
    applyFileAssociations();
}

void SettingsController::clearWaveformCache()
{
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    if (isSafeCachePath(dir)) {
        removeDirectoryContents(dir + QStringLiteral("/waveforms"));
    }
    recalculateCacheSize();
}

void SettingsController::clearCoverCache()
{
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    if (isSafeCachePath(dir)) {
        removeDirectoryContents(dir + QStringLiteral("/covers"));
    }
    recalculateCacheSize();
}

void SettingsController::clearTempFiles()
{
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    if (isSafeCachePath(dir)) {
        removeDirectoryContents(dir + QStringLiteral("/temp"));
    }
    recalculateCacheSize();
}

void SettingsController::clearAllCache()
{
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    if (isSafeCachePath(dir)) {
        removeDirectoryContents(dir);
    }
    recalculateCacheSize();
}

void SettingsController::openOfficialWebsite()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.agplayer.com")));
}

void SettingsController::trimCacheNow()
{
    enforceCacheSizeLimit();
}

void SettingsController::onWaveformCacheSaved()
{
    enforceCacheSizeLimit();
}

void SettingsController::enforceCacheSizeLimit()
{
    if (!autoCleanCache_ || cacheSizeLimitMB_ <= 0) {
        return;
    }
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    if (!isSafeCachePath(dir)) {
        return;
    }
    const qint64 limitBytes = static_cast<qint64>(cacheSizeLimitMB_) * 1024 * 1024;
    const CacheJanitor::TrimReport report = CacheJanitor::trimToSize(dir, limitBytes);
    if (report.filesRemoved > 0) {
        recalculateCacheSize();
        emit cacheTrimReport(report.bytesFreed, report.filesRemoved);
    }
}

void SettingsController::load()
{
    loading_ = true;
    restoreDefaults();

    QString legacyDefaultExportDirectory;
    settings_.beginGroup(QStringLiteral("general"));
    autoStartWithWindows_ = settings_.value(QStringLiteral("autoStartWithWindows"), autoStartWithWindows_).toBool();
    restoreLastPlaybackOnStartup_ = settings_.value(QStringLiteral("restoreLastPlaybackOnStartup"), restoreLastPlaybackOnStartup_).toBool();
    showListWindowPanel_ = settings_.value(QStringLiteral("showListWindowPanel"), showListWindowPanel_).toBool();
    windowMagneticSnap_ = settings_.value(QStringLiteral("windowMagneticSnap"), windowMagneticSnap_).toBool();
    listWindowPosition_ = settings_.value(QStringLiteral("listWindowPosition"), listWindowPosition_).toInt();
    closeBehavior_ = settings_.value(QStringLiteral("closeBehavior"), closeBehavior_).toInt();
    language_ = validatedLanguage(settings_.value(QStringLiteral("language"), language_).toString());
    setAsDefaultPlayer_ = settings_.value(QStringLiteral("setAsDefaultPlayer"), setAsDefaultPlayer_).toBool();
    fileAssociations_ = settings_.value(QStringLiteral("fileAssociations"), fileAssociations_).toStringList();
    legacyDefaultExportDirectory =
        settings_.value(QStringLiteral("defaultExportDirectory")).toString();
    if (!legacyDefaultExportDirectory.isEmpty()) {
        settings_.remove(QStringLiteral("defaultExportDirectory"));
    }
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("playback"));
    outputDevice_ =
        settings_.value(QStringLiteral("outputDevice"), outputDevice_).toString();
    if (outputDevice_
        == QStringLiteral("\u81EA\u52A8 / \u7CFB\u7EDF\u9ED8\u8BA4\u8BBE\u5907")) {
        outputDevice_.clear();
        settings_.setValue(QStringLiteral("outputDevice"), outputDevice_);
    }
    exclusiveMode_ =
        settings_.value(QStringLiteral("exclusiveMode"), exclusiveMode_).toBool();
    matchTrackSampleRate_ = settings_
                                .value(QStringLiteral("matchTrackSampleRate"),
                                       matchTrackSampleRate_)
                                .toBool();
    transitionFadeMs_ =
        settings_.value(QStringLiteral("transitionFadeMs"),
                        transitionFadeMs_).toInt();
    if (transitionFadeMs_ != 0 && transitionFadeMs_ != 200
        && transitionFadeMs_ != 500) {
        transitionFadeMs_ = 200;
        settings_.setValue(QStringLiteral("transitionFadeMs"),
                           transitionFadeMs_);
    }
    settings_.remove(QStringLiteral("playButtonRgbGlow"));
    const bool hasStoredPlaybackMode =
        settings_.contains(QStringLiteral("defaultPlaybackMode"));
    const int playbackModeSchema =
        settings_.value(QStringLiteral("modeSchemaVersion"), 1).toInt();
    defaultPlaybackMode_ = settings_.value(QStringLiteral("defaultPlaybackMode"), defaultPlaybackMode_).toInt();
    if (hasStoredPlaybackMode && playbackModeSchema < 2) {
        if (defaultPlaybackMode_ == 1) {
            defaultPlaybackMode_ = 2;
        } else if (defaultPlaybackMode_ == 2) {
            defaultPlaybackMode_ = 1;
        }
        settings_.setValue(QStringLiteral("defaultPlaybackMode"),
                           defaultPlaybackMode_);
        settings_.setValue(QStringLiteral("modeSchemaVersion"), 2);
    }
    autoReadBpm_ = settings_.value(QStringLiteral("autoReadBpm"), autoReadBpm_).toBool();
    autoReadRating_ = settings_.value(QStringLiteral("autoReadRating"), autoReadRating_).toBool();
    replayGainMode_ = settings_.value(QStringLiteral("replayGainMode"), replayGainMode_).toInt();
    replayGainClipProtection_ = settings_.value(
        QStringLiteral("replayGainClipProtection"), replayGainClipProtection_).toBool();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("appearance"));
    if (settings_.contains(QStringLiteral("themeMode"))) {
        const std::optional<int> storedThemeMode =
            storedInteger(settings_.value(QStringLiteral("themeMode")));
        themeMode_ = storedThemeMode.has_value()
                && *storedThemeMode >= 0 && *storedThemeMode <= 2
            ? *storedThemeMode
            : 0;
        if (!storedThemeMode.has_value()
            || *storedThemeMode < 0 || *storedThemeMode > 2) {
            settings_.setValue(QStringLiteral("themeMode"), themeMode_);
        }
    }
    const QString storedWindowLayoutTheme = settings_.value(
        QStringLiteral("windowLayoutTheme")).toString();
    const bool validStoredLayout = storedWindowLayoutTheme == QStringLiteral("dual-window")
        || storedWindowLayoutTheme == QStringLiteral("single-window")
        || storedWindowLayoutTheme == QStringLiteral("rolling-player");
    const std::optional<int> storedPlayerShellMode =
        storedInteger(settings_.value(QStringLiteral("playerShellMode")));
    const bool validStoredMode = storedPlayerShellMode.has_value()
        && (*storedPlayerShellMode == Classic || *storedPlayerShellMode == Integrated
            || *storedPlayerShellMode == Rolling);
    if (validStoredLayout) {
        windowLayoutTheme_ = storedWindowLayoutTheme;
        playerShellMode_ = windowLayoutTheme_ == QStringLiteral("single-window")
            ? Integrated
            : windowLayoutTheme_ == QStringLiteral("rolling-player") ? Rolling : Classic;
    } else if (validStoredMode) {
        playerShellMode_ = *storedPlayerShellMode;
        windowLayoutTheme_ = playerShellMode_ == Integrated
            ? QStringLiteral("single-window")
            : playerShellMode_ == Rolling ? QStringLiteral("rolling-player")
                                        : QStringLiteral("dual-window");
    } else {
        windowLayoutTheme_ = QStringLiteral("dual-window");
        playerShellMode_ = 0;
    }
    settings_.setValue(QStringLiteral("windowLayoutTheme"), windowLayoutTheme_);
    settings_.setValue(QStringLiteral("playerShellMode"), playerShellMode_);
    rollingBeatGridEnabled_ = settings_
        .value(QStringLiteral("rollingBeatGridEnabled"), false).toBool();
    const std::optional<int> storedRollingGridGrouping = storedInteger(
        settings_.value(QStringLiteral("rollingBeatGridGrouping")));
    rollingBeatGridGrouping_ = storedRollingGridGrouping.has_value()
            && *storedRollingGridGrouping == 8 ? 8 : 4;
    if (!storedRollingGridGrouping.has_value()
        || (*storedRollingGridGrouping != 4
            && *storedRollingGridGrouping != 8)) {
        settings_.setValue(QStringLiteral("rollingBeatGridGrouping"), 4);
    }
    settings_.remove(QStringLiteral("glassEffect"));
    waveformMode_ = settings_.value(QStringLiteral("waveformMode"), waveformMode_).toInt();
    waveformHeight_ =
        settings_.value(QStringLiteral("waveformHeight"), waveformHeight_).toDouble();
    waveformDensity_ =
        settings_.value(QStringLiteral("waveformDensity"), waveformDensity_).toDouble();
    waveformThickness_ = settings_.value(QStringLiteral("waveformThickness"), waveformThickness_).toDouble();
    waveformPeakAlgorithm_ =
        settings_.value(QStringLiteral("waveformPeakAlgorithm"),
                        waveformPeakAlgorithm_).toInt();
    spectrumColorMode_ = settings_.value(
        QStringLiteral("spectrumColorMode"), spectrumColorMode_).toInt();
    const auto restoredColor = [this](const QString& legacyKey,
                                      const QString& simplifiedKey,
                                      const QString& fallback) {
        return settings_.value(
            legacyKey,
            settings_.value(simplifiedKey, fallback)).toString();
    };
    waveformSolidBaseColor_ = restoredColor(
        QStringLiteral("waveformSolidBaseColor"),
        QStringLiteral("waveformUnplayedColor"), waveformSolidBaseColor_);
    waveformSolidProgressColor_ = restoredColor(
        QStringLiteral("waveformSolidProgressColor"),
        QStringLiteral("waveformPlayedColor"), waveformSolidProgressColor_);
    waveformRgbBaseColor_ = restoredColor(
        QStringLiteral("waveformRgbBaseColor"),
        QStringLiteral("waveformUnplayedColor"), waveformRgbBaseColor_);
    waveformRgbStartColor_ = restoredColor(
        QStringLiteral("waveformRgbStartColor"),
        QStringLiteral("waveformPlayedColor"), waveformRgbStartColor_);
    waveformRgbMiddleColor_ = settings_.value(
        QStringLiteral("waveformRgbMiddleColor"), waveformRgbMiddleColor_).toString();
    waveformRgbEndColor_ = settings_.value(
        QStringLiteral("waveformRgbEndColor"), waveformRgbEndColor_).toString();
    frequencyColorWaveform_->load(settings_);
    waveformRgbProgress_ = settings_.value(
        QStringLiteral("waveformRgbProgress"), waveformRgbProgress_).toBool();
    spectrumSolidColor_ = restoredColor(
        QStringLiteral("spectrumSolidColor"),
        QStringLiteral("spectrumUnplayedColor"), spectrumSolidColor_);
    spectrumRgbStartColor_ = restoredColor(
        QStringLiteral("spectrumRgbStartColor"),
        QStringLiteral("spectrumUnplayedColor"), spectrumRgbStartColor_);
    spectrumRgbMiddleColor_ = restoredColor(
        QStringLiteral("spectrumRgbMiddleColor"),
        QStringLiteral("spectrumPlayedColor"), spectrumRgbMiddleColor_);
    spectrumRgbEndColor_ = settings_.value(
        QStringLiteral("spectrumRgbEndColor"), spectrumRgbEndColor_).toString();
    waveformHoverTimePreview_ = settings_.value(QStringLiteral("waveformHoverTimePreview"), waveformHoverTimePreview_).toBool();
    waveformPlaybackGuide_ = settings_.value(
        QStringLiteral("waveformPlaybackGuide"), waveformPlaybackGuide_).toBool();
    waveformCanvasHeight_ = settings_.value(
        QStringLiteral("waveformCanvasHeight"), waveformCanvasHeight_).toInt();
    waveformCanvasLocked_ = settings_.value(
        QStringLiteral("waveformCanvasLocked"), waveformCanvasLocked_).toBool();
    listWaveformThumbnailEnabled_ = settings_.value(
        QStringLiteral("listWaveformThumbnailEnabled"),
        listWaveformThumbnailEnabled_).toBool();
    const QString storedListWaveformThumbnailMode = settings_.value(
        QStringLiteral("listWaveformThumbnailMode"),
        listWaveformThumbnailMode_).toString();
    listWaveformThumbnailMode_ =
        storedListWaveformThumbnailMode == QStringLiteral("Mono")
            ? QStringLiteral("Mono") : QStringLiteral("Spectral");
    if (storedListWaveformThumbnailMode != listWaveformThumbnailMode_) {
        settings_.setValue(QStringLiteral("listWaveformThumbnailMode"),
                           listWaveformThumbnailMode_);
    }
    bool brightnessOk = false;
    const double storedTrackWaveformBrightness = settings_
        .value(QStringLiteral("trackWaveformBrightness"), 0.50)
        .toDouble(&brightnessOk);
    trackWaveformBrightness_ = brightnessOk
            && std::isfinite(storedTrackWaveformBrightness)
        ? clampValue(storedTrackWaveformBrightness, 0.20, 1.00)
        : 0.50;
    if (!brightnessOk || !std::isfinite(storedTrackWaveformBrightness)
        || trackWaveformBrightness_ != storedTrackWaveformBrightness) {
        settings_.setValue(QStringLiteral("trackWaveformBrightness"),
                           trackWaveformBrightness_);
    }
    for (const QString& obsoleteKey : {
             QStringLiteral("spectrumHeight"),
             QStringLiteral("spectrumDensity"),
             QStringLiteral("spectrumBarWidth"),
             QStringLiteral("spectrumAttack"),
             QStringLiteral("spectrumRelease")}) {
        settings_.remove(obsoleteKey);
    }
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("audioTools"));
    const bool hasDefaultOutputDirectory =
        settings_.contains(QStringLiteral("defaultOutputDirectory"));
    const QString storedOutputDirectory =
        settings_
            .value(QStringLiteral("defaultOutputDirectory"),
                   legacyDefaultExportDirectory.isEmpty()
                       ? defaultOutputDirectory_
                       : legacyDefaultExportDirectory)
            .toString();
    const QString oldBuiltInExportDirectory =
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
        + QStringLiteral("/AgPlayer_Export");
    const bool usesOldBuiltInDirectory =
        QDir::cleanPath(storedOutputDirectory)
        == QDir::cleanPath(oldBuiltInExportDirectory);
    defaultOutputDirectory_ = usesOldBuiltInDirectory
        ? defaultExportDir() : storedOutputDirectory;
    if (usesOldBuiltInDirectory
        || (!hasDefaultOutputDirectory
            && !legacyDefaultExportDirectory.isEmpty())) {
        settings_.setValue(QStringLiteral("defaultOutputDirectory"),
                           defaultOutputDirectory_);
    }
    if (!QDir().mkpath(defaultOutputDirectory_)) {
        RuntimeLog::log(
            AG_IO_ERROR, QStringLiteral("Settings"),
            QStringLiteral("Failed to create default output directory: ")
                + defaultOutputDirectory_);
    }
    parallelJobs_ = clampValue(
        settings_.value(QStringLiteral("parallelJobs"), parallelJobs_).toInt(), 1, 10);
    overwritePolicy_ = settings_.value(QStringLiteral("overwritePolicy"), overwritePolicy_).toInt();
    const bool hasSplitTranscodeSettings =
        settings_.contains(QStringLiteral("transcodeFormat"))
        || settings_.contains(QStringLiteral("transcodeBitrateKbps"))
        || settings_.contains(QStringLiteral("transcodeSampleRateHz"))
        || settings_.contains(QStringLiteral("transcodeChannels"));
    const QString legacyTranscodePreset =
        settings_.value(QStringLiteral("defaultTranscodeFormat")).toString();
    transcodeFormat_ =
        settings_.value(QStringLiteral("transcodeFormat"), transcodeFormat_).toString();
    transcodeBitrateKbps_ =
        settings_.value(QStringLiteral("transcodeBitrateKbps"),
                        transcodeBitrateKbps_).toInt();
    transcodeSampleRateHz_ =
        settings_.value(QStringLiteral("transcodeSampleRateHz"),
                        transcodeSampleRateHz_).toInt();
    transcodeChannels_ =
        settings_.value(QStringLiteral("transcodeChannels"),
                        transcodeChannels_).toInt();
    if (!hasSplitTranscodeSettings && !legacyTranscodePreset.isEmpty()) {
        const QString preset = legacyTranscodePreset.toUpper();
        transcodeFormat_ = preset.startsWith(QStringLiteral("WAV"))
            ? QStringLiteral("WAV")
            : preset.startsWith(QStringLiteral("FLAC"))
                ? QStringLiteral("FLAC") : QStringLiteral("MP3");
        transcodeBitrateKbps_ = preset.contains(QStringLiteral("128KBPS")) ? 128
            : preset.contains(QStringLiteral("192KBPS")) ? 192
            : preset.contains(QStringLiteral("256KBPS")) ? 256 : 320;
        transcodeSampleRateHz_ = preset.contains(QStringLiteral("96KHZ")) ? 96000
            : preset.contains(QStringLiteral("48KHZ")) ? 48000 : 44100;
        transcodeChannels_ = preset.contains(QStringLiteral("MONO")) ? 1 : 2;
        settings_.setValue(QStringLiteral("transcodeFormat"), transcodeFormat_);
        settings_.setValue(QStringLiteral("transcodeBitrateKbps"),
                           transcodeBitrateKbps_);
        settings_.setValue(QStringLiteral("transcodeSampleRateHz"),
                           transcodeSampleRateHz_);
        settings_.setValue(QStringLiteral("transcodeChannels"),
                           transcodeChannels_);
    }
    if (!legacyTranscodePreset.isEmpty()) {
        settings_.remove(QStringLiteral("defaultTranscodeFormat"));
    }
    preserveMetadata_ = settings_.value(QStringLiteral("preserveMetadata"), preserveMetadata_).toBool();
    preserveCover_ = settings_.value(QStringLiteral("preserveCover"), preserveCover_).toBool();
    preserveDirectoryStructure_ = settings_.value(QStringLiteral("preserveDirectoryStructure"), preserveDirectoryStructure_).toBool();
    extractVideoAudio_ = settings_.value(QStringLiteral("extractVideoAudio"), extractVideoAudio_).toBool();
    keepPitchWhileSpeedChange_ = settings_.value(QStringLiteral("keepPitchWhileSpeedChange"), keepPitchWhileSpeedChange_).toBool();
    vocalProtection_ = settings_.value(QStringLiteral("vocalProtection"), vocalProtection_).toBool();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("hotkeys"));
    hkPlayPause_ = settings_.value(QStringLiteral("playPause"), hkPlayPause_).toString();
    hkPrevNext_ = settings_.value(QStringLiteral("prevNext"), hkPrevNext_).toString();
    hkVolumeUpDown_ = settings_.value(QStringLiteral("volumeUpDown"), hkVolumeUpDown_).toString();
    if (hkPlayPause_ == QStringLiteral("Global + Space")) {
        hkPlayPause_ = QStringLiteral("MediaPlayPause");
        settings_.setValue(QStringLiteral("playPause"), hkPlayPause_);
    }
    if (hkPrevNext_ == QStringLiteral("Global + Left / Global + Right")) {
        hkPrevNext_ = QStringLiteral("MediaPrevTrack / MediaNextTrack");
        settings_.setValue(QStringLiteral("prevNext"), hkPrevNext_);
    }
    if (hkVolumeUpDown_ == QStringLiteral("Global + Up / Global + Down")) {
        hkVolumeUpDown_ = QStringLiteral("VolumeUp / VolumeDown");
        settings_.setValue(QStringLiteral("volumeUpDown"), hkVolumeUpDown_);
    }
    hkToggleMiniPlayer_ = settings_.value(QStringLiteral("toggleMiniPlayer"), hkToggleMiniPlayer_).toString();
    hkSearch_ = settings_.value(QStringLiteral("search"), hkSearch_).toString();
    hkWaveformMode_ = settings_.value(QStringLiteral("waveformMode"), hkWaveformMode_).toString();
    hkAudioTools_ = settings_.value(QStringLiteral("audioTools"), hkAudioTools_).toString();
    const QVariantMap storedRollingShortcuts = settings_
        .value(QStringLiteral("rollingKeyboardShortcuts")).toMap();
    rollingKeyboardShortcuts_ = defaultRollingKeyboardShortcuts();
    for (auto iterator = storedRollingShortcuts.constBegin();
         iterator != storedRollingShortcuts.constEnd(); ++iterator) {
        if (!rollingKeyboardShortcuts_.contains(iterator.key())) continue;
        const QString raw = iterator.value().toString().trimmed();
        if (raw.isEmpty()) {
            rollingKeyboardShortcuts_.insert(iterator.key(), QString());
            continue;
        }
        const QString normalized = normalizedSingleShortcut(raw);
        if (!normalized.isEmpty()) {
            rollingKeyboardShortcuts_.insert(iterator.key(), normalized);
        }
    }
    QStringList assignedRollingShortcuts;
    const QVariantMap rollingDefaults = defaultRollingKeyboardShortcuts();
    for (auto iterator = rollingKeyboardShortcuts_.begin();
         iterator != rollingKeyboardShortcuts_.end(); ++iterator) {
        QString sequence = iterator.value().toString();
        if (sequence.isEmpty()) continue;
        if (assignedRollingShortcuts.contains(sequence)) {
            const QString fallback = rollingDefaults
                .value(iterator.key()).toString();
            sequence = assignedRollingShortcuts.contains(fallback)
                ? QString() : fallback;
            iterator.value() = sequence;
        }
        if (!sequence.isEmpty()) assignedRollingShortcuts.append(sequence);
    }
    for (auto iterator = rollingKeyboardShortcuts_.begin();
         iterator != rollingKeyboardShortcuts_.end(); ++iterator) {
        if (conflictsWithLegacyKeyboardShortcuts(iterator.value().toString()))
            iterator.value() = QString();
    }
    if (storedRollingShortcuts != rollingKeyboardShortcuts_) {
        settings_.setValue(QStringLiteral("rollingKeyboardShortcuts"),
                           rollingKeyboardShortcuts_);
    }
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("cache"));
    const QString storedCacheDirectory =
        settings_.value(QStringLiteral("directory"), QString()).toString();
    const QString oldBuiltInCacheLocation =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const bool usesOldBuiltInCacheDirectory =
        !oldBuiltInCacheLocation.isEmpty()
        && !storedCacheDirectory.isEmpty()
        && QDir::cleanPath(storedCacheDirectory)
            == QDir::cleanPath(QDir(oldBuiltInCacheLocation).filePath(
                QStringLiteral("waveform")));
    cacheDirectory_ = storedCacheDirectory;
    if (cacheDirectory_.isEmpty() || usesOldBuiltInCacheDirectory) {
        cacheDirectory_ = defaultCacheDirectory();
        if (usesOldBuiltInCacheDirectory) {
            settings_.setValue(QStringLiteral("directory"), cacheDirectory_);
        }
        if (!QDir().mkpath(cacheDirectory_)) {
            RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
                QStringLiteral("Failed to create default cache directory: ") + cacheDirectory_);
        }
    }
    autoCleanCache_ = settings_.value(QStringLiteral("autoCleanCache"), autoCleanCache_).toBool();
    cleanTempOnExit_ = settings_.value(QStringLiteral("cleanTempOnExit"), cleanTempOnExit_).toBool();
    const bool hasCacheSizeLimit = settings_.contains(QStringLiteral("sizeLimitMB"));
    const int cacheSchemaVersion = settings_.value(QStringLiteral("schemaVersion"), 0).toInt();
    const bool hasCacheSizeLimitUserModified =
        settings_.contains(QStringLiteral("sizeLimitUserModified"));
    const bool cacheSizeLimitUserModified =
        settings_.value(QStringLiteral("sizeLimitUserModified"), false).toBool();
    cacheSizeLimitMB_ = settings_.value(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_).toInt();
    // Schema-less releases cannot distinguish their historical 1024 MB
    // default from a deliberate 1 GB choice. Preserve it conservatively;
    // only schema 1 with an explicit untouched marker is safe to migrate.
    if (hasCacheSizeLimit && !cacheSizeLimitUserModified
        && cacheSchemaVersion == 1 && hasCacheSizeLimitUserModified
        && cacheSizeLimitMB_ == 1024) {
        cacheSizeLimitMB_ = 10 * 1024;
        settings_.setValue(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_);
    }
    settings_.setValue(QStringLiteral("schemaVersion"), 2);
    if (!hasCacheSizeLimitUserModified) {
        settings_.setValue(QStringLiteral("sizeLimitUserModified"), false);
    }
    settings_.endGroup();

    // Ensure clamped values are stored within valid ranges.
    listWindowPosition_ = clampValue(listWindowPosition_, 0, 3);
    closeBehavior_ = clampValue(closeBehavior_, 0, 1);
    defaultPlaybackMode_ = clampValue(defaultPlaybackMode_, 0, 3);
    themeMode_ = themeMode_ >= 0 && themeMode_ <= 2 ? themeMode_ : 0;
    playerShellMode_ = playerShellMode_ >= Classic && playerShellMode_ <= Rolling
        ? playerShellMode_ : Classic;
    waveformMode_ = clampValue(waveformMode_, 0, 3);
    waveformHeight_ = quantize(waveformHeight_, 0.3, 1.5, 0.1);
    waveformDensity_ = quantize(waveformDensity_, 0.5, 5.0, 0.5);
    waveformThickness_ = quantize(waveformThickness_, 0.3, 3.0, 0.1);
    waveformPeakAlgorithm_ = clampValue(waveformPeakAlgorithm_, 0, 1);
    waveformCanvasHeight_ = clampValue(waveformCanvasHeight_, 48, 84);
    spectrumColorMode_ = clampValue(spectrumColorMode_, 0, 1);
    replayGainMode_ = clampValue(replayGainMode_, 0, 2);
    const auto validOr = [](const QString& value, const QString& fallback) {
        const QString normalized = normalizedColor(value);
        return normalized.isEmpty() ? fallback : normalized;
    };
    waveformSolidBaseColor_ = validOr(waveformSolidBaseColor_, QStringLiteral("#9098a6"));
    waveformSolidProgressColor_ = validOr(waveformSolidProgressColor_, QStringLiteral("#d27722"));
    waveformRgbBaseColor_ = validOr(waveformRgbBaseColor_, QStringLiteral("#00b4a0"));
    waveformRgbStartColor_ = validOr(waveformRgbStartColor_, QStringLiteral("#00d4ff"));
    waveformRgbMiddleColor_ = validOr(waveformRgbMiddleColor_, QStringLiteral("#7b2ff7"));
    waveformRgbEndColor_ = validOr(waveformRgbEndColor_, QStringLiteral("#e62e9b"));
    spectrumSolidColor_ = validOr(spectrumSolidColor_, QStringLiteral("#7b2ff7"));
    spectrumRgbStartColor_ = validOr(spectrumRgbStartColor_, QStringLiteral("#00d4ff"));
    spectrumRgbMiddleColor_ = validOr(spectrumRgbMiddleColor_, QStringLiteral("#7b2ff7"));
    spectrumRgbEndColor_ = validOr(spectrumRgbEndColor_, QStringLiteral("#e62e9b"));
    overwritePolicy_ = clampValue(overwritePolicy_, 0, 1);
    transcodeFormat_ = transcodeFormat_.trimmed().toUpper();
    static const QSet<QString> supportedFormats = {
        QStringLiteral("MP3"), QStringLiteral("FLAC"),
        QStringLiteral("WAV"), QStringLiteral("AAC"),
        QStringLiteral("OPUS"), QStringLiteral("OGG"),
        QStringLiteral("ALAC"), QStringLiteral("AIFF")};
    if (!supportedFormats.contains(transcodeFormat_)) {
        transcodeFormat_ = QStringLiteral("MP3");
    }
    if (!QList<int>{128, 192, 256, 320}.contains(transcodeBitrateKbps_)) {
        transcodeBitrateKbps_ = 320;
    }
    if (!QList<int>{44100, 48000, 88200, 96000, 176400, 192000}
             .contains(transcodeSampleRateHz_)) {
        transcodeSampleRateHz_ = 44100;
    }
    transcodeChannels_ = transcodeChannels_ == 1 ? 1 : 2;
    cacheSizeLimitMB_ = std::max(cacheSizeLimitMB_, 100);

    loading_ = false;

}

bool SettingsController::openDefaultAppsSettings()
{
#ifdef Q_OS_WIN
    // Windows only lets the user choose the final default application.  Make
    // sure AgPlayer is registered first so it is present on that page.
    rebindFileAssociations();
    return QDesktopServices::openUrl(
        QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=AgPlayer")));
#elif defined(Q_OS_MACOS)
    return QDesktopServices::openUrl(
        QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.general")));
#else
    return false;
#endif
}

void SettingsController::saveAll(const bool includeMediaSettings)
{
    settings_.beginGroup(QStringLiteral("general"));
    persistValue(QStringLiteral("autoStartWithWindows"), autoStartWithWindows_);
    persistValue(QStringLiteral("restoreLastPlaybackOnStartup"), restoreLastPlaybackOnStartup_);
    persistValue(QStringLiteral("showListWindowPanel"), showListWindowPanel_);
    persistValue(QStringLiteral("windowMagneticSnap"), windowMagneticSnap_);
    persistValue(QStringLiteral("listWindowPosition"), listWindowPosition_);
    persistValue(QStringLiteral("closeBehavior"), closeBehavior_);
    persistValue(QStringLiteral("language"), language_);
    persistValue(QStringLiteral("setAsDefaultPlayer"), setAsDefaultPlayer_);
    persistValue(QStringLiteral("fileAssociations"), fileAssociations_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("playback"));
    persistValue(QStringLiteral("outputDevice"), outputDevice_);
    persistValue(QStringLiteral("exclusiveMode"), exclusiveMode_);
    persistValue(QStringLiteral("matchTrackSampleRate"),
                 matchTrackSampleRate_);
    persistValue(QStringLiteral("transitionFadeMs"), transitionFadeMs_);
    persistValue(QStringLiteral("defaultPlaybackMode"), defaultPlaybackMode_);
    persistValue(QStringLiteral("modeSchemaVersion"), 2);
    persistValue(QStringLiteral("autoReadBpm"), autoReadBpm_);
    persistValue(QStringLiteral("autoReadRating"), autoReadRating_);
    persistValue(QStringLiteral("replayGainMode"), replayGainMode_);
    persistValue(QStringLiteral("replayGainClipProtection"), replayGainClipProtection_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("appearance"));
    persistValue(QStringLiteral("windowLayoutTheme"), windowLayoutTheme_);
    persistValue(QStringLiteral("themeMode"), themeMode_);
    persistValue(QStringLiteral("playerShellMode"), playerShellMode_);
    persistValue(QStringLiteral("rollingBeatGridEnabled"),
                 rollingBeatGridEnabled_);
    persistValue(QStringLiteral("rollingBeatGridGrouping"),
                 rollingBeatGridGrouping_);
    if (includeMediaSettings) {
        persistValue(QStringLiteral("waveformMode"), waveformMode_);
        persistValue(QStringLiteral("waveformHeight"), waveformHeight_);
        persistValue(QStringLiteral("waveformDensity"), waveformDensity_);
        persistValue(QStringLiteral("waveformThickness"), waveformThickness_);
        persistValue(QStringLiteral("waveformPeakAlgorithm"), waveformPeakAlgorithm_);
        persistValue(QStringLiteral("waveformSolidBaseColor"), waveformSolidBaseColor_);
        persistValue(QStringLiteral("waveformSolidProgressColor"), waveformSolidProgressColor_);
        persistValue(QStringLiteral("waveformRgbBaseColor"), waveformRgbBaseColor_);
        persistValue(QStringLiteral("waveformRgbStartColor"), waveformRgbStartColor_);
        persistValue(QStringLiteral("waveformRgbMiddleColor"), waveformRgbMiddleColor_);
        persistValue(QStringLiteral("waveformRgbEndColor"), waveformRgbEndColor_);
        if (!editActive_) {
            frequencyColorWaveform_->save(settings_);
        }
        persistValue(QStringLiteral("waveformRgbProgress"), waveformRgbProgress_);
        persistValue(QStringLiteral("waveformHoverTimePreview"), waveformHoverTimePreview_);
        persistValue(QStringLiteral("waveformPlaybackGuide"), waveformPlaybackGuide_);
        persistValue(QStringLiteral("waveformCanvasHeight"), waveformCanvasHeight_);
        persistValue(QStringLiteral("waveformCanvasLocked"), waveformCanvasLocked_);
        persistValue(QStringLiteral("listWaveformThumbnailEnabled"),
                     listWaveformThumbnailEnabled_);
        persistValue(QStringLiteral("listWaveformThumbnailMode"),
                     listWaveformThumbnailMode_);
        persistValue(QStringLiteral("trackWaveformBrightness"),
                     trackWaveformBrightness_);
        persistValue(QStringLiteral("spectrumColorMode"), spectrumColorMode_);
        persistValue(QStringLiteral("spectrumSolidColor"), spectrumSolidColor_);
        persistValue(QStringLiteral("spectrumRgbStartColor"), spectrumRgbStartColor_);
        persistValue(QStringLiteral("spectrumRgbMiddleColor"), spectrumRgbMiddleColor_);
        persistValue(QStringLiteral("spectrumRgbEndColor"), spectrumRgbEndColor_);
    }
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("audioTools"));
    persistValue(QStringLiteral("defaultOutputDirectory"), defaultOutputDirectory_);
    persistValue(QStringLiteral("parallelJobs"), parallelJobs_);
    persistValue(QStringLiteral("overwritePolicy"), overwritePolicy_);
    persistValue(QStringLiteral("transcodeFormat"), transcodeFormat_);
    persistValue(QStringLiteral("transcodeBitrateKbps"),
                 transcodeBitrateKbps_);
    persistValue(QStringLiteral("transcodeSampleRateHz"),
                 transcodeSampleRateHz_);
    persistValue(QStringLiteral("transcodeChannels"), transcodeChannels_);
    persistValue(QStringLiteral("preserveMetadata"), preserveMetadata_);
    persistValue(QStringLiteral("preserveCover"), preserveCover_);
    persistValue(QStringLiteral("preserveDirectoryStructure"), preserveDirectoryStructure_);
    persistValue(QStringLiteral("extractVideoAudio"), extractVideoAudio_);
    persistValue(QStringLiteral("keepPitchWhileSpeedChange"), keepPitchWhileSpeedChange_);
    persistValue(QStringLiteral("vocalProtection"), vocalProtection_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("hotkeys"));
    persistValue(QStringLiteral("playPause"), hkPlayPause_);
    persistValue(QStringLiteral("prevNext"), hkPrevNext_);
    persistValue(QStringLiteral("volumeUpDown"), hkVolumeUpDown_);
    persistValue(QStringLiteral("toggleMiniPlayer"), hkToggleMiniPlayer_);
    persistValue(QStringLiteral("search"), hkSearch_);
    persistValue(QStringLiteral("waveformMode"), hkWaveformMode_);
    persistValue(QStringLiteral("audioTools"), hkAudioTools_);
    persistValue(QStringLiteral("rollingKeyboardShortcuts"),
                 rollingKeyboardShortcuts_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("cache"));
    persistValue(QStringLiteral("directory"), cacheDirectory_);
    persistValue(QStringLiteral("autoCleanCache"), autoCleanCache_);
    persistValue(QStringLiteral("cleanTempOnExit"), cleanTempOnExit_);
    persistValue(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_);
    persistValue(QStringLiteral("schemaVersion"), 2);
    if (!settings_.contains(QStringLiteral("sizeLimitUserModified"))) {
        persistValue(QStringLiteral("sizeLimitUserModified"), false);
    }
    settings_.endGroup();
}

void SettingsController::restoreDefaults(const bool includeMediaSettings)
{
    autoStartWithWindows_ = false;
    restoreLastPlaybackOnStartup_ = true;
    showListWindowPanel_ = true;
    windowMagneticSnap_ = true;
    listWindowPosition_ = 1;
    closeBehavior_ = 0;
    language_ = QStringLiteral("zh");
    setAsDefaultPlayer_ = false;
    fileAssociations_ = agplayer::qt::supportedAudioExtensions();
    outputDevice_.clear();
    exclusiveMode_ = false;
    matchTrackSampleRate_ = true;
    transitionFadeMs_ = 200;
    defaultPlaybackMode_ = 3;
    autoReadBpm_ = true;
    autoReadRating_ = true;

    themeMode_ = 0;
    windowLayoutTheme_ = QStringLiteral("dual-window");
    playerShellMode_ = Classic;
    rollingBeatGridEnabled_ = false;
    rollingBeatGridGrouping_ = 4;
    if (includeMediaSettings) {
        waveformMode_ = 0;
        waveformHeight_ = 0.8;
        waveformDensity_ = 2.0;
        waveformThickness_ = 1.0;
        waveformPeakAlgorithm_ = 0;
        waveformSolidBaseColor_ = QStringLiteral("#9098a6");
        waveformSolidProgressColor_ = QStringLiteral("#d27722");
        waveformRgbBaseColor_ = QStringLiteral("#00b4a0");
        waveformRgbStartColor_ = QStringLiteral("#00d4ff");
        waveformRgbMiddleColor_ = QStringLiteral("#7b2ff7");
        waveformRgbEndColor_ = QStringLiteral("#e62e9b");
        frequencyColorWaveform_->resetToDefault();
        waveformRgbProgress_ = false;
        waveformHoverTimePreview_ = true;
        waveformPlaybackGuide_ = false;
        waveformCanvasHeight_ = 78;
        waveformCanvasLocked_ = true;
        listWaveformThumbnailEnabled_ = true;
        listWaveformThumbnailMode_ = QStringLiteral("Spectral");
        trackWaveformBrightness_ = 0.50;
        spectrumColorMode_ = 1;
        spectrumSolidColor_ = QStringLiteral("#7b2ff7");
        spectrumRgbStartColor_ = QStringLiteral("#00d4ff");
        spectrumRgbMiddleColor_ = QStringLiteral("#7b2ff7");
        spectrumRgbEndColor_ = QStringLiteral("#e62e9b");
    }
    replayGainMode_ = 0;
    replayGainClipProtection_ = true;

    defaultOutputDirectory_ = defaultExportDir();
    parallelJobs_ = 5;
    overwritePolicy_ = 0;
    transcodeFormat_ = QStringLiteral("MP3");
    transcodeBitrateKbps_ = 320;
    transcodeSampleRateHz_ = 44100;
    transcodeChannels_ = 2;
    preserveMetadata_ = true;
    preserveCover_ = true;
    preserveDirectoryStructure_ = true;
    extractVideoAudio_ = true;
    keepPitchWhileSpeedChange_ = true;
    vocalProtection_ = true;

    hkPlayPause_ = QStringLiteral("MediaPlayPause");
    hkPrevNext_ = QStringLiteral("MediaPrevTrack / MediaNextTrack");
    hkVolumeUpDown_ = QStringLiteral("VolumeUp / VolumeDown");
    hkToggleMiniPlayer_ = QStringLiteral("Alt + P");
    hkSearch_ = QStringLiteral("Ctrl + F");
    hkWaveformMode_ = QStringLiteral("Tab");
    hkAudioTools_ = QStringLiteral("Alt + D");
    rollingKeyboardShortcuts_ = defaultRollingKeyboardShortcuts();

    cacheDirectory_ = defaultCacheDirectory();
    autoCleanCache_ = true;
    cleanTempOnExit_ = true;
    cacheSizeLimitMB_ = 10 * 1024;
}

void SettingsController::recalculateCacheSize()
{
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    const qint64 bytes = directorySizeBytes(dir);
    const int mb = static_cast<int>(bytes / (1024 * 1024));
    if (currentCacheSizeMB_ != mb) {
        currentCacheSizeMB_ = mb;
        emit currentCacheSizeMBChanged();
    }
}

qint64 SettingsController::directorySizeBytes(const QString& path)
{
    if (path.isEmpty() || !QDir(path).exists()) {
        return 0;
    }
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString SettingsController::defaultMusicDirectory()
{
    const QString location = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return location.isEmpty() ? QDir::homePath() : location;
}

QString SettingsController::defaultCacheDirectory()
{
    if (QStandardPaths::isTestModeEnabled()) {
        return resolveTestCacheDirectory(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
            QStandardPaths::writableLocation(QStandardPaths::TempLocation),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }
    QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath() + QStringLiteral("/Documents");
    }
    return documents + QStringLiteral("/AgPlayer/Cache");
}

QString SettingsController::resolveTestCacheDirectory(
    const QString& cacheLocation, const QString& tempLocation,
    const QString& appDataLocation)
{
    QString base = cacheLocation;
    if (base.isEmpty()) base = tempLocation;
    if (base.isEmpty()) base = appDataLocation;
    if (base.isEmpty()) base = QDir::tempPath();
    if (base.isEmpty()) {
        base = QDir(QDir::currentPath()).filePath(
            QStringLiteral(".agplayer-test-cache"));
    }
    return QDir(base).filePath(QStringLiteral("AgPlayer/Cache"));
}

QString SettingsController::defaultExportDir()
{
    const QString desktop =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return desktop.isEmpty() ? QDir::homePath() : desktop;
}

QString SettingsController::validatedLanguage(const QString& value)
{
    static const QStringList supported = {
        QStringLiteral("zh"), QStringLiteral("en")};
    const QString lower = value.toLower();
    if (supported.contains(lower)) {
        return lower;
    }
    return QStringLiteral("zh");
}

void SettingsController::applyFileAssociations()
{
    if (fileAssociationController_ == nullptr
        || (QStandardPaths::isTestModeEnabled()
            && !fileAssociationController_->hasCustomRegistryRoot())) {
        return;
    }

    if (!fileAssociationController_->unregisterAll()) {
        const QString error = fileAssociationController_->lastError();
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
            QStringLiteral("Failed to clear file associations: %1")
                .arg(error));
        if (!error.isEmpty() && !systemIntegrationError_.contains(error)) {
            setSystemIntegrationError(systemIntegrationError_.isEmpty()
                                          ? error
                                          : systemIntegrationError_ + QLatin1Char('\n') + error);
        }
        return;
    }

    if (!setAsDefaultPlayer_) {
        return;
    }

    QStringList registrationExtensions = fileAssociations_;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    registrationExtensions.append(agplayer::qt::supportedVideoExtensions());
#endif
    registrationExtensions.removeDuplicates();
    if (!fileAssociationController_->registerForExtensions(registrationExtensions)) {
        const QString error = fileAssociationController_->lastError();
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
            QStringLiteral("Failed to register file associations: %1")
                .arg(error));
        if (!error.isEmpty() && !systemIntegrationError_.contains(error)) {
            setSystemIntegrationError(systemIntegrationError_.isEmpty()
                                          ? error
                                          : systemIntegrationError_ + QLatin1Char('\n') + error);
        }
    }
}
