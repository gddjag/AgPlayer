#include "settings_controller.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QList>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

template<typename T>
T clampValue(T value, T min, T max) noexcept
{
    return std::max(min, std::min(value, max));
}

} // namespace

SettingsController::SettingsController(QObject* parent)
    : QObject(parent),
      settings_(this)
{
    load();
    recalculateCacheSize();
}

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
QString SettingsController::defaultExportDirectory() const { return defaultExportDirectory_; }

// Playback & Engine getters
QString SettingsController::outputDevice() const { return outputDevice_; }
bool SettingsController::audioExclusiveMode() const noexcept { return audioExclusiveMode_; }
bool SettingsController::playButtonRgbGlow() const noexcept { return playButtonRgbGlow_; }
int SettingsController::defaultPlaybackMode() const noexcept { return defaultPlaybackMode_; }
bool SettingsController::gaplessPlayback() const noexcept { return gaplessPlayback_; }
int SettingsController::crossfadeMs() const noexcept { return crossfadeMs_; }
bool SettingsController::autoMatchSampleRate() const noexcept { return autoMatchSampleRate_; }
bool SettingsController::autoReadBpm() const noexcept { return autoReadBpm_; }
bool SettingsController::autoReadRating() const noexcept { return autoReadRating_; }

// Appearance & Visualizer getters
int SettingsController::themeMode() const noexcept { return themeMode_; }
bool SettingsController::glassEffect() const noexcept { return glassEffect_; }
int SettingsController::waveformMode() const noexcept { return waveformMode_; }
int SettingsController::waveformDensity() const noexcept { return waveformDensity_; }
double SettingsController::waveformThickness() const noexcept { return waveformThickness_; }
bool SettingsController::waveformHoverTimePreview() const noexcept { return waveformHoverTimePreview_; }

// Audio Tools getters
QString SettingsController::defaultOutputDirectory() const { return defaultOutputDirectory_; }
int SettingsController::overwritePolicy() const noexcept { return overwritePolicy_; }
QString SettingsController::defaultTranscodeFormat() const { return defaultTranscodeFormat_; }
bool SettingsController::preserveMetadata() const noexcept { return preserveMetadata_; }
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

// Cache & Storage getters
QString SettingsController::cacheDirectory() const { return cacheDirectory_; }
bool SettingsController::autoCleanCache() const noexcept { return autoCleanCache_; }
bool SettingsController::cleanTempOnExit() const noexcept { return cleanTempOnExit_; }
int SettingsController::cacheSizeLimitMB() const noexcept { return cacheSizeLimitMB_; }
int SettingsController::currentCacheSizeMB() const noexcept { return currentCacheSizeMB_; }

// About getters
QString SettingsController::version() const { return QStringLiteral("AgPlayer v1.0.0"); }
QString SettingsController::releaseDate() const { return QStringLiteral("2026.10"); }

// General setters
void SettingsController::setAutoStartWithWindows(bool value)
{
    if (autoStartWithWindows_ == value) {
        return;
    }
    autoStartWithWindows_ = value;
    settings_.setValue(QStringLiteral("general/autoStartWithWindows"), value);

    const QString appPath = QCoreApplication::applicationFilePath();
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (value) {
        run.setValue(QStringLiteral("AgPlayer"), QStringLiteral("\"%1\"").arg(appPath));
    } else {
        run.remove(QStringLiteral("AgPlayer"));
    }

    emit autoStartWithWindowsChanged();
}

void SettingsController::setRestoreLastPlaybackOnStartup(bool value)
{
    if (restoreLastPlaybackOnStartup_ == value) {
        return;
    }
    restoreLastPlaybackOnStartup_ = value;
    settings_.setValue(QStringLiteral("general/restoreLastPlaybackOnStartup"), value);
    emit restoreLastPlaybackOnStartupChanged();
}

void SettingsController::setShowListWindowPanel(bool value)
{
    if (showListWindowPanel_ == value) {
        return;
    }
    showListWindowPanel_ = value;
    settings_.setValue(QStringLiteral("general/showListWindowPanel"), value);
    emit showListWindowPanelChanged();
}

void SettingsController::setWindowMagneticSnap(bool value)
{
    if (windowMagneticSnap_ == value) {
        return;
    }
    windowMagneticSnap_ = value;
    settings_.setValue(QStringLiteral("general/windowMagneticSnap"), value);
    emit windowMagneticSnapChanged();
}

void SettingsController::setListWindowPosition(int value)
{
    value = clampValue(value, 0, 3);
    if (listWindowPosition_ == value) {
        return;
    }
    listWindowPosition_ = value;
    settings_.setValue(QStringLiteral("general/listWindowPosition"), value);
    emit listWindowPositionChanged();
}

void SettingsController::setCloseBehavior(int value)
{
    value = clampValue(value, 0, 1);
    if (closeBehavior_ == value) {
        return;
    }
    closeBehavior_ = value;
    settings_.setValue(QStringLiteral("general/closeBehavior"), value);
    emit closeBehaviorChanged();
}

void SettingsController::setLanguage(const QString& value)
{
    const QString normalized = validatedLanguage(value);
    if (language_ == normalized) {
        return;
    }
    language_ = normalized;
    settings_.setValue(QStringLiteral("general/language"), normalized);
    emit languageChanged();
}

void SettingsController::setSetAsDefaultPlayer(bool value)
{
    if (setAsDefaultPlayer_ == value) {
        return;
    }
    setAsDefaultPlayer_ = value;
    settings_.setValue(QStringLiteral("general/setAsDefaultPlayer"), value);
    emit setAsDefaultPlayerChanged();
}

void SettingsController::setFileAssociations(const QStringList& value)
{
    if (fileAssociations_ == value) {
        return;
    }
    fileAssociations_ = value;
    settings_.setValue(QStringLiteral("general/fileAssociations"), value);
    emit fileAssociationsChanged();
}

void SettingsController::setDefaultExportDirectory(const QString& value)
{
    if (defaultExportDirectory_ == value) {
        return;
    }
    defaultExportDirectory_ = value;
    settings_.setValue(QStringLiteral("general/defaultExportDirectory"), value);
    emit defaultExportDirectoryChanged();
}

// Playback & Engine setters
void SettingsController::setOutputDevice(const QString& value)
{
    if (outputDevice_ == value) {
        return;
    }
    outputDevice_ = value;
    settings_.setValue(QStringLiteral("playback/outputDevice"), value);
    emit outputDeviceChanged();
}

void SettingsController::setAudioExclusiveMode(bool value)
{
    if (audioExclusiveMode_ == value) {
        return;
    }
    audioExclusiveMode_ = value;
    settings_.setValue(QStringLiteral("playback/audioExclusiveMode"), value);
    emit audioExclusiveModeChanged();
}

void SettingsController::setPlayButtonRgbGlow(bool value)
{
    if (playButtonRgbGlow_ == value) {
        return;
    }
    playButtonRgbGlow_ = value;
    settings_.setValue(QStringLiteral("playback/playButtonRgbGlow"), value);
    emit playButtonRgbGlowChanged();
}

void SettingsController::setDefaultPlaybackMode(int value)
{
    value = clampValue(value, 0, 3);
    if (defaultPlaybackMode_ == value) {
        return;
    }
    defaultPlaybackMode_ = value;
    settings_.setValue(QStringLiteral("playback/defaultPlaybackMode"), value);
    emit defaultPlaybackModeChanged();
}

void SettingsController::setGaplessPlayback(bool value)
{
    if (gaplessPlayback_ == value) {
        return;
    }
    gaplessPlayback_ = value;
    settings_.setValue(QStringLiteral("playback/gaplessPlayback"), value);
    emit gaplessPlaybackChanged();
}

void SettingsController::setCrossfadeMs(int value)
{
    value = clampValue(value, 0, 2);
    if (crossfadeMs_ == value) {
        return;
    }
    crossfadeMs_ = value;
    settings_.setValue(QStringLiteral("playback/crossfadeMs"), value);
    emit crossfadeMsChanged();
}

void SettingsController::setAutoMatchSampleRate(bool value)
{
    if (autoMatchSampleRate_ == value) {
        return;
    }
    autoMatchSampleRate_ = value;
    settings_.setValue(QStringLiteral("playback/autoMatchSampleRate"), value);
    emit autoMatchSampleRateChanged();
}

void SettingsController::setAutoReadBpm(bool value)
{
    if (autoReadBpm_ == value) {
        return;
    }
    autoReadBpm_ = value;
    settings_.setValue(QStringLiteral("playback/autoReadBpm"), value);
    emit autoReadBpmChanged();
}

void SettingsController::setAutoReadRating(bool value)
{
    if (autoReadRating_ == value) {
        return;
    }
    autoReadRating_ = value;
    settings_.setValue(QStringLiteral("playback/autoReadRating"), value);
    emit autoReadRatingChanged();
}

// Appearance & Visualizer setters
void SettingsController::setThemeMode(int value)
{
    value = clampValue(value, 0, 2);
    if (themeMode_ == value) {
        return;
    }
    themeMode_ = value;
    settings_.setValue(QStringLiteral("appearance/themeMode"), value);
    emit themeModeChanged();
}

void SettingsController::setGlassEffect(bool value)
{
    if (glassEffect_ == value) {
        return;
    }
    glassEffect_ = value;
    settings_.setValue(QStringLiteral("appearance/glassEffect"), value);
    emit glassEffectChanged();
}

void SettingsController::setWaveformMode(int value)
{
    value = clampValue(value, 0, 2);
    if (waveformMode_ == value) {
        return;
    }
    waveformMode_ = value;
    settings_.setValue(QStringLiteral("appearance/waveformMode"), value);
    emit waveformModeChanged();
}

void SettingsController::setWaveformDensity(int value)
{
    value = clampValue(value, 0, 2);
    if (waveformDensity_ == value) {
        return;
    }
    waveformDensity_ = value;
    settings_.setValue(QStringLiteral("appearance/waveformDensity"), value);
    emit waveformDensityChanged();
}

void SettingsController::setWaveformThickness(double value)
{
    static const QList<double> kValidThicknesses = {1.0, 1.5, 2.0, 3.0, 4.0, 6.0};
    double closest = kValidThicknesses.first();
    double bestDelta = std::abs(value - closest);
    for (double v : kValidThicknesses) {
        const double delta = std::abs(value - v);
        if (delta < bestDelta) {
            bestDelta = delta;
            closest = v;
        }
    }
    value = closest;
    if (qFuzzyCompare(waveformThickness_, value)) {
        return;
    }
    waveformThickness_ = value;
    settings_.setValue(QStringLiteral("appearance/waveformThickness"), value);
    emit waveformThicknessChanged();
}

void SettingsController::setWaveformHoverTimePreview(bool value)
{
    if (waveformHoverTimePreview_ == value) {
        return;
    }
    waveformHoverTimePreview_ = value;
    settings_.setValue(QStringLiteral("appearance/waveformHoverTimePreview"), value);
    emit waveformHoverTimePreviewChanged();
}

// Audio Tools setters
void SettingsController::setDefaultOutputDirectory(const QString& value)
{
    if (defaultOutputDirectory_ == value) {
        return;
    }
    defaultOutputDirectory_ = value;
    settings_.setValue(QStringLiteral("audioTools/defaultOutputDirectory"), value);
    emit defaultOutputDirectoryChanged();
}

void SettingsController::setOverwritePolicy(int value)
{
    value = clampValue(value, 0, 1);
    if (overwritePolicy_ == value) {
        return;
    }
    overwritePolicy_ = value;
    settings_.setValue(QStringLiteral("audioTools/overwritePolicy"), value);
    emit overwritePolicyChanged();
}

void SettingsController::setDefaultTranscodeFormat(const QString& value)
{
    if (defaultTranscodeFormat_ == value) {
        return;
    }
    defaultTranscodeFormat_ = value;
    settings_.setValue(QStringLiteral("audioTools/defaultTranscodeFormat"), value);
    emit defaultTranscodeFormatChanged();
}

void SettingsController::setPreserveMetadata(bool value)
{
    if (preserveMetadata_ == value) {
        return;
    }
    preserveMetadata_ = value;
    settings_.setValue(QStringLiteral("audioTools/preserveMetadata"), value);
    emit preserveMetadataChanged();
}

void SettingsController::setKeepPitchWhileSpeedChange(bool value)
{
    if (keepPitchWhileSpeedChange_ == value) {
        return;
    }
    keepPitchWhileSpeedChange_ = value;
    settings_.setValue(QStringLiteral("audioTools/keepPitchWhileSpeedChange"), value);
    emit keepPitchWhileSpeedChangeChanged();
}

void SettingsController::setVocalProtection(bool value)
{
    if (vocalProtection_ == value) {
        return;
    }
    vocalProtection_ = value;
    settings_.setValue(QStringLiteral("audioTools/vocalProtection"), value);
    emit vocalProtectionChanged();
}

// Hotkeys setters
void SettingsController::setHkPlayPause(const QString& value)
{
    if (hkPlayPause_ == value) {
        return;
    }
    hkPlayPause_ = value;
    settings_.setValue(QStringLiteral("hotkeys/playPause"), value);
    emit hkPlayPauseChanged();
}

void SettingsController::setHkPrevNext(const QString& value)
{
    if (hkPrevNext_ == value) {
        return;
    }
    hkPrevNext_ = value;
    settings_.setValue(QStringLiteral("hotkeys/prevNext"), value);
    emit hkPrevNextChanged();
}

void SettingsController::setHkVolumeUpDown(const QString& value)
{
    if (hkVolumeUpDown_ == value) {
        return;
    }
    hkVolumeUpDown_ = value;
    settings_.setValue(QStringLiteral("hotkeys/volumeUpDown"), value);
    emit hkVolumeUpDownChanged();
}

void SettingsController::setHkToggleMiniPlayer(const QString& value)
{
    if (hkToggleMiniPlayer_ == value) {
        return;
    }
    hkToggleMiniPlayer_ = value;
    settings_.setValue(QStringLiteral("hotkeys/toggleMiniPlayer"), value);
    emit hkToggleMiniPlayerChanged();
}

void SettingsController::setHkSearch(const QString& value)
{
    if (hkSearch_ == value) {
        return;
    }
    hkSearch_ = value;
    settings_.setValue(QStringLiteral("hotkeys/search"), value);
    emit hkSearchChanged();
}

void SettingsController::setHkWaveformMode(const QString& value)
{
    if (hkWaveformMode_ == value) {
        return;
    }
    hkWaveformMode_ = value;
    settings_.setValue(QStringLiteral("hotkeys/waveformMode"), value);
    emit hkWaveformModeChanged();
}

void SettingsController::setHkAudioTools(const QString& value)
{
    if (hkAudioTools_ == value) {
        return;
    }
    hkAudioTools_ = value;
    settings_.setValue(QStringLiteral("hotkeys/audioTools"), value);
    emit hkAudioToolsChanged();
}

// Cache & Storage setters
void SettingsController::setCacheDirectory(const QString& value)
{
    if (cacheDirectory_ == value) {
        return;
    }
    cacheDirectory_ = value;
    settings_.setValue(QStringLiteral("cache/directory"), value);
    emit cacheDirectoryChanged();
}

void SettingsController::setAutoCleanCache(bool value)
{
    if (autoCleanCache_ == value) {
        return;
    }
    autoCleanCache_ = value;
    settings_.setValue(QStringLiteral("cache/autoCleanCache"), value);
    emit autoCleanCacheChanged();
}

void SettingsController::setCleanTempOnExit(bool value)
{
    if (cleanTempOnExit_ == value) {
        return;
    }
    cleanTempOnExit_ = value;
    settings_.setValue(QStringLiteral("cache/cleanTempOnExit"), value);
    emit cleanTempOnExitChanged();
}

void SettingsController::setCacheSizeLimitMB(int value)
{
    value = std::max(value, 100);
    if (cacheSizeLimitMB_ == value) {
        return;
    }
    cacheSizeLimitMB_ = value;
    settings_.setValue(QStringLiteral("cache/sizeLimitMB"), value);
    emit cacheSizeLimitMBChanged();
}

void SettingsController::resetToDefaults()
{
    restoreDefaults();
    saveAll();

    emit autoStartWithWindowsChanged();
    emit restoreLastPlaybackOnStartupChanged();
    emit showListWindowPanelChanged();
    emit windowMagneticSnapChanged();
    emit listWindowPositionChanged();
    emit closeBehaviorChanged();
    emit languageChanged();
    emit setAsDefaultPlayerChanged();
    emit fileAssociationsChanged();
    emit defaultExportDirectoryChanged();

    emit outputDeviceChanged();
    emit audioExclusiveModeChanged();
    emit playButtonRgbGlowChanged();
    emit defaultPlaybackModeChanged();
    emit gaplessPlaybackChanged();
    emit crossfadeMsChanged();
    emit autoMatchSampleRateChanged();
    emit autoReadBpmChanged();
    emit autoReadRatingChanged();

    emit themeModeChanged();
    emit glassEffectChanged();
    emit waveformModeChanged();
    emit waveformDensityChanged();
    emit waveformThicknessChanged();
    emit waveformHoverTimePreviewChanged();

    emit defaultOutputDirectoryChanged();
    emit overwritePolicyChanged();
    emit defaultTranscodeFormatChanged();
    emit preserveMetadataChanged();
    emit keepPitchWhileSpeedChangeChanged();
    emit vocalProtectionChanged();

    emit hkPlayPauseChanged();
    emit hkPrevNextChanged();
    emit hkVolumeUpDownChanged();
    emit hkToggleMiniPlayerChanged();
    emit hkSearchChanged();
    emit hkWaveformModeChanged();
    emit hkAudioToolsChanged();

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
    qDebug() << "Rebinding file associations for:" << fileAssociations_;
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

void SettingsController::checkForUpdates()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/AgPlayer/AgPlayer/releases")));
}

void SettingsController::openOfficialWebsite()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/AgPlayer/AgPlayer")));
}

void SettingsController::load()
{
    restoreDefaults();

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
    defaultExportDirectory_ = settings_.value(QStringLiteral("defaultExportDirectory"), QString()).toString();
    if (defaultExportDirectory_.isEmpty()) {
        defaultExportDirectory_ = defaultExportDir();
        QDir().mkpath(defaultExportDirectory_);
    }
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("playback"));
    outputDevice_ = settings_.value(QStringLiteral("outputDevice"), outputDevice_).toString();
    audioExclusiveMode_ = settings_.value(QStringLiteral("audioExclusiveMode"), audioExclusiveMode_).toBool();
    playButtonRgbGlow_ = settings_.value(QStringLiteral("playButtonRgbGlow"), playButtonRgbGlow_).toBool();
    defaultPlaybackMode_ = settings_.value(QStringLiteral("defaultPlaybackMode"), defaultPlaybackMode_).toInt();
    gaplessPlayback_ = settings_.value(QStringLiteral("gaplessPlayback"), gaplessPlayback_).toBool();
    crossfadeMs_ = settings_.value(QStringLiteral("crossfadeMs"), crossfadeMs_).toInt();
    autoMatchSampleRate_ = settings_.value(QStringLiteral("autoMatchSampleRate"), autoMatchSampleRate_).toBool();
    autoReadBpm_ = settings_.value(QStringLiteral("autoReadBpm"), autoReadBpm_).toBool();
    autoReadRating_ = settings_.value(QStringLiteral("autoReadRating"), autoReadRating_).toBool();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("appearance"));
    themeMode_ = settings_.value(QStringLiteral("themeMode"), themeMode_).toInt();
    glassEffect_ = settings_.value(QStringLiteral("glassEffect"), glassEffect_).toBool();
    waveformMode_ = settings_.value(QStringLiteral("waveformMode"), waveformMode_).toInt();
    waveformDensity_ = settings_.value(QStringLiteral("waveformDensity"), waveformDensity_).toInt();
    waveformThickness_ = settings_.value(QStringLiteral("waveformThickness"), waveformThickness_).toDouble();
    waveformHoverTimePreview_ = settings_.value(QStringLiteral("waveformHoverTimePreview"), waveformHoverTimePreview_).toBool();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("audioTools"));
    defaultOutputDirectory_ = settings_.value(QStringLiteral("defaultOutputDirectory"), defaultOutputDirectory_).toString();
    overwritePolicy_ = settings_.value(QStringLiteral("overwritePolicy"), overwritePolicy_).toInt();
    defaultTranscodeFormat_ = settings_.value(QStringLiteral("defaultTranscodeFormat"), defaultTranscodeFormat_).toString();
    preserveMetadata_ = settings_.value(QStringLiteral("preserveMetadata"), preserveMetadata_).toBool();
    keepPitchWhileSpeedChange_ = settings_.value(QStringLiteral("keepPitchWhileSpeedChange"), keepPitchWhileSpeedChange_).toBool();
    vocalProtection_ = settings_.value(QStringLiteral("vocalProtection"), vocalProtection_).toBool();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("hotkeys"));
    hkPlayPause_ = settings_.value(QStringLiteral("playPause"), hkPlayPause_).toString();
    hkPrevNext_ = settings_.value(QStringLiteral("prevNext"), hkPrevNext_).toString();
    hkVolumeUpDown_ = settings_.value(QStringLiteral("volumeUpDown"), hkVolumeUpDown_).toString();
    hkToggleMiniPlayer_ = settings_.value(QStringLiteral("toggleMiniPlayer"), hkToggleMiniPlayer_).toString();
    hkSearch_ = settings_.value(QStringLiteral("search"), hkSearch_).toString();
    hkWaveformMode_ = settings_.value(QStringLiteral("waveformMode"), hkWaveformMode_).toString();
    hkAudioTools_ = settings_.value(QStringLiteral("audioTools"), hkAudioTools_).toString();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("cache"));
    cacheDirectory_ = settings_.value(QStringLiteral("directory"), QString()).toString();
    if (cacheDirectory_.isEmpty()) {
        cacheDirectory_ = defaultCacheDirectory();
        QDir().mkpath(cacheDirectory_);
    }
    autoCleanCache_ = settings_.value(QStringLiteral("autoCleanCache"), autoCleanCache_).toBool();
    cleanTempOnExit_ = settings_.value(QStringLiteral("cleanTempOnExit"), cleanTempOnExit_).toBool();
    cacheSizeLimitMB_ = settings_.value(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_).toInt();
    settings_.endGroup();

    // Ensure clamped values are stored within valid ranges.
    listWindowPosition_ = clampValue(listWindowPosition_, 0, 3);
    closeBehavior_ = clampValue(closeBehavior_, 0, 1);
    defaultPlaybackMode_ = clampValue(defaultPlaybackMode_, 0, 3);
    crossfadeMs_ = clampValue(crossfadeMs_, 0, 2);
    themeMode_ = clampValue(themeMode_, 0, 2);
    waveformMode_ = clampValue(waveformMode_, 0, 2);
    waveformDensity_ = clampValue(waveformDensity_, 0, 2);
    {
        static const QList<double> kValid = {1.0, 1.5, 2.0, 3.0, 4.0, 6.0};
        double closest = kValid.first();
        double bestDelta = std::abs(waveformThickness_ - closest);
        for (double v : kValid) {
            const double delta = std::abs(waveformThickness_ - v);
            if (delta < bestDelta) {
                bestDelta = delta;
                closest = v;
            }
        }
        waveformThickness_ = closest;
    }
    overwritePolicy_ = clampValue(overwritePolicy_, 0, 1);
    cacheSizeLimitMB_ = std::max(cacheSizeLimitMB_, 100);
}

void SettingsController::saveAll()
{
    settings_.beginGroup(QStringLiteral("general"));
    settings_.setValue(QStringLiteral("autoStartWithWindows"), autoStartWithWindows_);
    settings_.setValue(QStringLiteral("restoreLastPlaybackOnStartup"), restoreLastPlaybackOnStartup_);
    settings_.setValue(QStringLiteral("showListWindowPanel"), showListWindowPanel_);
    settings_.setValue(QStringLiteral("windowMagneticSnap"), windowMagneticSnap_);
    settings_.setValue(QStringLiteral("listWindowPosition"), listWindowPosition_);
    settings_.setValue(QStringLiteral("closeBehavior"), closeBehavior_);
    settings_.setValue(QStringLiteral("language"), language_);
    settings_.setValue(QStringLiteral("setAsDefaultPlayer"), setAsDefaultPlayer_);
    settings_.setValue(QStringLiteral("fileAssociations"), fileAssociations_);
    settings_.setValue(QStringLiteral("defaultExportDirectory"), defaultExportDirectory_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("playback"));
    settings_.setValue(QStringLiteral("outputDevice"), outputDevice_);
    settings_.setValue(QStringLiteral("audioExclusiveMode"), audioExclusiveMode_);
    settings_.setValue(QStringLiteral("playButtonRgbGlow"), playButtonRgbGlow_);
    settings_.setValue(QStringLiteral("defaultPlaybackMode"), defaultPlaybackMode_);
    settings_.setValue(QStringLiteral("gaplessPlayback"), gaplessPlayback_);
    settings_.setValue(QStringLiteral("crossfadeMs"), crossfadeMs_);
    settings_.setValue(QStringLiteral("autoMatchSampleRate"), autoMatchSampleRate_);
    settings_.setValue(QStringLiteral("autoReadBpm"), autoReadBpm_);
    settings_.setValue(QStringLiteral("autoReadRating"), autoReadRating_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("appearance"));
    settings_.setValue(QStringLiteral("themeMode"), themeMode_);
    settings_.setValue(QStringLiteral("glassEffect"), glassEffect_);
    settings_.setValue(QStringLiteral("waveformMode"), waveformMode_);
    settings_.setValue(QStringLiteral("waveformDensity"), waveformDensity_);
    settings_.setValue(QStringLiteral("waveformThickness"), waveformThickness_);
    settings_.setValue(QStringLiteral("waveformHoverTimePreview"), waveformHoverTimePreview_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("audioTools"));
    settings_.setValue(QStringLiteral("defaultOutputDirectory"), defaultOutputDirectory_);
    settings_.setValue(QStringLiteral("overwritePolicy"), overwritePolicy_);
    settings_.setValue(QStringLiteral("defaultTranscodeFormat"), defaultTranscodeFormat_);
    settings_.setValue(QStringLiteral("preserveMetadata"), preserveMetadata_);
    settings_.setValue(QStringLiteral("keepPitchWhileSpeedChange"), keepPitchWhileSpeedChange_);
    settings_.setValue(QStringLiteral("vocalProtection"), vocalProtection_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("hotkeys"));
    settings_.setValue(QStringLiteral("playPause"), hkPlayPause_);
    settings_.setValue(QStringLiteral("prevNext"), hkPrevNext_);
    settings_.setValue(QStringLiteral("volumeUpDown"), hkVolumeUpDown_);
    settings_.setValue(QStringLiteral("toggleMiniPlayer"), hkToggleMiniPlayer_);
    settings_.setValue(QStringLiteral("search"), hkSearch_);
    settings_.setValue(QStringLiteral("waveformMode"), hkWaveformMode_);
    settings_.setValue(QStringLiteral("audioTools"), hkAudioTools_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("cache"));
    settings_.setValue(QStringLiteral("directory"), cacheDirectory_);
    settings_.setValue(QStringLiteral("autoCleanCache"), autoCleanCache_);
    settings_.setValue(QStringLiteral("cleanTempOnExit"), cleanTempOnExit_);
    settings_.setValue(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_);
    settings_.endGroup();
}

void SettingsController::restoreDefaults()
{
    autoStartWithWindows_ = false;
    restoreLastPlaybackOnStartup_ = true;
    showListWindowPanel_ = true;
    windowMagneticSnap_ = true;
    listWindowPosition_ = 1;
    closeBehavior_ = 0;
    language_ = QStringLiteral("zh");
    setAsDefaultPlayer_ = false;
    fileAssociations_ = {QStringLiteral("mp3"), QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("aac"), QStringLiteral("m4a"),
        QStringLiteral("ogg")};
    defaultExportDirectory_ = defaultExportDir();

    outputDevice_ = QStringLiteral("\u81EA\u52A8 / \u7CFB\u7EDF\u9ED8\u8BA4\u8BBE\u5907");
    audioExclusiveMode_ = false;
    playButtonRgbGlow_ = true;
    defaultPlaybackMode_ = 3;
    gaplessPlayback_ = true;
    crossfadeMs_ = 0;
    autoMatchSampleRate_ = true;
    autoReadBpm_ = true;
    autoReadRating_ = true;

    themeMode_ = 0;
    glassEffect_ = true;
    waveformMode_ = 1;
    waveformDensity_ = 1;
    waveformThickness_ = 2.0;
    waveformHoverTimePreview_ = true;

    defaultOutputDirectory_ = defaultExportDir();
    overwritePolicy_ = 0;
    defaultTranscodeFormat_ = QStringLiteral("MP3 / 320kbps / 44.1kHz / Stereo");
    preserveMetadata_ = true;
    keepPitchWhileSpeedChange_ = true;
    vocalProtection_ = true;

    hkPlayPause_ = QStringLiteral("Global + Space");
    hkPrevNext_ = QStringLiteral("Global + Left / Global + Right");
    hkVolumeUpDown_ = QStringLiteral("Global + Up / Global + Down");
    hkToggleMiniPlayer_ = QStringLiteral("Alt + P");
    hkSearch_ = QStringLiteral("Ctrl + F");
    hkWaveformMode_ = QStringLiteral("Tab");
    hkAudioTools_ = QStringLiteral("Alt + D");

    cacheDirectory_ = defaultCacheDirectory();
    autoCleanCache_ = true;
    cleanTempOnExit_ = true;
    cacheSizeLimitMB_ = 1024;
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
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/waveform");
}

QString SettingsController::defaultExportDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
           + QStringLiteral("/AgPlayer_Export");
}

QString SettingsController::validatedLanguage(const QString& value)
{
    static const QStringList supported = {QStringLiteral("zh"), QStringLiteral("en"),
        QStringLiteral("ko"), QStringLiteral("th"), QStringLiteral("my"),
        QStringLiteral("vi"), QStringLiteral("lo")};
    const QString lower = value.toLower();
    if (supported.contains(lower)) {
        return lower;
    }
    return QStringLiteral("zh");
}
