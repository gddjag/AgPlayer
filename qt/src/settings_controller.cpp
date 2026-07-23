#include "settings_controller.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

template<typename T>
T clampValue(T value, T min, T max) noexcept
{
    return std::max(min, std::min(value, max));
}

QString colorToString(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QColor colorFromString(const QString& value)
{
    if (value.isEmpty()) {
        return QColor();
    }
    return QColor(value);
}

QString validatedLanguage(const QString& value)
{
    if (value.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("en");
    }
    return QStringLiteral("zh");
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
bool SettingsController::startupAutoPlay() const noexcept { return startupAutoPlay_; }
bool SettingsController::minimizeOnStartup() const noexcept { return minimizeOnStartup_; }
int SettingsController::closeBehavior() const noexcept { return closeBehavior_; }
bool SettingsController::rememberWindowState() const noexcept { return rememberWindowState_; }
QString SettingsController::language() const { return language_; }
bool SettingsController::setAsDefaultPlayer() const noexcept { return setAsDefaultPlayer_; }
QString SettingsController::defaultExportDirectory() const { return defaultExportDirectory_; }

// Appearance getters
int SettingsController::listWindowPosition() const noexcept { return listWindowPosition_; }
int SettingsController::themeMode() const noexcept { return themeMode_; }
QColor SettingsController::accentColor() const { return accentColor_; }
double SettingsController::windowTransparency() const noexcept { return windowTransparency_; }
double SettingsController::fontTransparency() const noexcept { return fontTransparency_; }
int SettingsController::cornerRadius() const noexcept { return cornerRadius_; }

// Playback getters
QString SettingsController::outputDevice() const { return outputDevice_; }
int SettingsController::outputFormat() const noexcept { return outputFormat_; }
bool SettingsController::autoSampleRate() const noexcept { return autoSampleRate_; }
double SettingsController::defaultVolume() const noexcept { return defaultVolume_; }
int SettingsController::fadeInDuration() const noexcept { return fadeInDuration_; }
int SettingsController::fadeOutDuration() const noexcept { return fadeOutDuration_; }
QStringList SettingsController::fileAssociations() const { return fileAssociations_; }

// Waveform getters
int SettingsController::waveformMode() const noexcept { return waveformMode_; }
QColor SettingsController::waveformColor() const { return waveformColor_; }
double SettingsController::waveformBrightness() const noexcept { return waveformBrightness_; }
int SettingsController::waveformThickness() const noexcept { return waveformThickness_; }
int SettingsController::waveformDensity() const noexcept { return waveformDensity_; }

// Audio Tools getters
QString SettingsController::defaultOutputFormat() const { return defaultOutputFormat_; }
int SettingsController::defaultBitrate() const noexcept { return defaultBitrate_; }
QString SettingsController::defaultOutputDirectory() const { return defaultOutputDirectory_; }

// Shortcuts getters
QString SettingsController::shortcutPlayPause() const { return shortcutPlayPause_; }
QString SettingsController::shortcutStop() const { return shortcutStop_; }
QString SettingsController::shortcutNext() const { return shortcutNext_; }
QString SettingsController::shortcutPrev() const { return shortcutPrev_; }
QString SettingsController::shortcutVolumeUp() const { return shortcutVolumeUp_; }
QString SettingsController::shortcutVolumeDown() const { return shortcutVolumeDown_; }

// Cache getters
int SettingsController::cacheSizeLimitMB() const noexcept { return cacheSizeLimitMB_; }
QString SettingsController::cacheDirectory() const { return cacheDirectory_; }
bool SettingsController::clearCacheOnExit() const noexcept { return clearCacheOnExit_; }
int SettingsController::currentCacheSizeMB() const noexcept { return currentCacheSizeMB_; }

// About getters
bool SettingsController::checkUpdatesOnStartup() const noexcept { return checkUpdatesOnStartup_; }
QString SettingsController::version() const { return QStringLiteral("v1.0"); }
QString SettingsController::buildNumber() const { return QStringLiteral("2024.05.18.001"); }
QString SettingsController::releaseDate() const { return QStringLiteral("2024-05-18"); }

// General setters
void SettingsController::setStartupAutoPlay(bool value)
{
    if (startupAutoPlay_ == value) {
        return;
    }
    startupAutoPlay_ = value;
    settings_.setValue(QStringLiteral("general/startupAutoPlay"), value);
    emit startupAutoPlayChanged();
}

void SettingsController::setMinimizeOnStartup(bool value)
{
    if (minimizeOnStartup_ == value) {
        return;
    }
    minimizeOnStartup_ = value;
    settings_.setValue(QStringLiteral("general/minimizeOnStartup"), value);
    emit minimizeOnStartupChanged();
}

void SettingsController::setCloseBehavior(int value)
{
    value = clampValue(value, 0, 2);
    if (closeBehavior_ == value) {
        return;
    }
    closeBehavior_ = value;
    settings_.setValue(QStringLiteral("general/closeBehavior"), value);
    emit closeBehaviorChanged();
}

void SettingsController::setRememberWindowState(bool value)
{
    if (rememberWindowState_ == value) {
        return;
    }
    rememberWindowState_ = value;
    settings_.setValue(QStringLiteral("general/rememberWindowState"), value);
    emit rememberWindowStateChanged();
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

void SettingsController::setDefaultExportDirectory(const QString& value)
{
    if (defaultExportDirectory_ == value) {
        return;
    }
    defaultExportDirectory_ = value;
    settings_.setValue(QStringLiteral("general/defaultExportDirectory"), value);
    emit defaultExportDirectoryChanged();
}

// Appearance setters
void SettingsController::setListWindowPosition(int value)
{
    value = clampValue(value, 0, 3);
    if (listWindowPosition_ == value) {
        return;
    }
    listWindowPosition_ = value;
    settings_.setValue(QStringLiteral("appearance/listWindowPosition"), value);
    emit listWindowPositionChanged();
}

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

void SettingsController::setAccentColor(const QColor& value)
{
    if (accentColor_ == value) {
        return;
    }
    accentColor_ = value;
    settings_.setValue(QStringLiteral("appearance/accentColor"), colorToString(value));
    emit accentColorChanged();
}

void SettingsController::setWindowTransparency(double value)
{
    value = clampValue(value, 0.5, 1.0);
    if (std::fabs(windowTransparency_ - value) < 0.001) {
        return;
    }
    windowTransparency_ = value;
    settings_.setValue(QStringLiteral("appearance/windowTransparency"), value);
    emit windowTransparencyChanged();
}

void SettingsController::setFontTransparency(double value)
{
    value = clampValue(value, 0.5, 1.0);
    if (std::fabs(fontTransparency_ - value) < 0.001) {
        return;
    }
    fontTransparency_ = value;
    settings_.setValue(QStringLiteral("appearance/fontTransparency"), value);
    emit fontTransparencyChanged();
}

void SettingsController::setCornerRadius(int value)
{
    value = clampValue(value, 0, 16);
    if (cornerRadius_ == value) {
        return;
    }
    cornerRadius_ = value;
    settings_.setValue(QStringLiteral("appearance/cornerRadius"), value);
    emit cornerRadiusChanged();
}

// Playback setters
void SettingsController::setOutputDevice(const QString& value)
{
    if (outputDevice_ == value) {
        return;
    }
    outputDevice_ = value;
    settings_.setValue(QStringLiteral("playback/outputDevice"), value);
    emit outputDeviceChanged();
}

void SettingsController::setOutputFormat(int value)
{
    value = clampValue(value, 0, 2);
    if (outputFormat_ == value) {
        return;
    }
    outputFormat_ = value;
    settings_.setValue(QStringLiteral("playback/outputFormat"), value);
    emit outputFormatChanged();
}

void SettingsController::setAutoSampleRate(bool value)
{
    if (autoSampleRate_ == value) {
        return;
    }
    autoSampleRate_ = value;
    settings_.setValue(QStringLiteral("playback/autoSampleRate"), value);
    emit autoSampleRateChanged();
}

void SettingsController::setDefaultVolume(double value)
{
    value = clampValue(value, 0.0, 1.0);
    if (std::fabs(defaultVolume_ - value) < 0.001) {
        return;
    }
    defaultVolume_ = value;
    settings_.setValue(QStringLiteral("playback/defaultVolume"), value);
    emit defaultVolumeChanged();
}

void SettingsController::setFadeInDuration(int value)
{
    value = clampValue(value, 0, 5000);
    if (fadeInDuration_ == value) {
        return;
    }
    fadeInDuration_ = value;
    settings_.setValue(QStringLiteral("playback/fadeInDuration"), value);
    emit fadeInDurationChanged();
}

void SettingsController::setFadeOutDuration(int value)
{
    value = clampValue(value, 0, 5000);
    if (fadeOutDuration_ == value) {
        return;
    }
    fadeOutDuration_ = value;
    settings_.setValue(QStringLiteral("playback/fadeOutDuration"), value);
    emit fadeOutDurationChanged();
}

void SettingsController::setFileAssociations(const QStringList& value)
{
    if (fileAssociations_ == value) {
        return;
    }
    fileAssociations_ = value;
    settings_.setValue(QStringLiteral("playback/fileAssociations"), value);
    emit fileAssociationsChanged();
}

// Waveform setters
void SettingsController::setWaveformMode(int value)
{
    value = clampValue(value, 0, 2);
    if (waveformMode_ == value) {
        return;
    }
    waveformMode_ = value;
    settings_.setValue(QStringLiteral("waveform/mode"), value);
    emit waveformModeChanged();
}

void SettingsController::setWaveformColor(const QColor& value)
{
    if (waveformColor_ == value) {
        return;
    }
    waveformColor_ = value;
    settings_.setValue(QStringLiteral("waveform/color"), colorToString(value));
    emit waveformColorChanged();
}

void SettingsController::setWaveformBrightness(double value)
{
    value = clampValue(value, 0.5, 2.0);
    if (std::fabs(waveformBrightness_ - value) < 0.001) {
        return;
    }
    waveformBrightness_ = value;
    settings_.setValue(QStringLiteral("waveform/brightness"), value);
    emit waveformBrightnessChanged();
}

void SettingsController::setWaveformThickness(int value)
{
    value = clampValue(value, 1, 4);
    if (waveformThickness_ == value) {
        return;
    }
    waveformThickness_ = value;
    settings_.setValue(QStringLiteral("waveform/thickness"), value);
    emit waveformThicknessChanged();
}

void SettingsController::setWaveformDensity(int value)
{
    value = clampValue(value, 1, 4);
    if (waveformDensity_ == value) {
        return;
    }
    waveformDensity_ = value;
    settings_.setValue(QStringLiteral("waveform/density"), value);
    emit waveformDensityChanged();
}

// Audio Tools setters
void SettingsController::setDefaultOutputFormat(const QString& value)
{
    if (defaultOutputFormat_ == value) {
        return;
    }
    defaultOutputFormat_ = value;
    settings_.setValue(QStringLiteral("audioTools/defaultOutputFormat"), value);
    emit defaultOutputFormatChanged();
}

void SettingsController::setDefaultBitrate(int value)
{
    value = clampValue(value, 64, 320);
    if (defaultBitrate_ == value) {
        return;
    }
    defaultBitrate_ = value;
    settings_.setValue(QStringLiteral("audioTools/defaultBitrate"), value);
    emit defaultBitrateChanged();
}

void SettingsController::setDefaultOutputDirectory(const QString& value)
{
    if (defaultOutputDirectory_ == value) {
        return;
    }
    defaultOutputDirectory_ = value;
    settings_.setValue(QStringLiteral("audioTools/defaultOutputDirectory"), value);
    emit defaultOutputDirectoryChanged();
}

// Shortcuts setters
void SettingsController::setShortcutPlayPause(const QString& value)
{
    if (shortcutPlayPause_ == value) {
        return;
    }
    shortcutPlayPause_ = value;
    settings_.setValue(QStringLiteral("shortcuts/playPause"), value);
    emit shortcutPlayPauseChanged();
}

void SettingsController::setShortcutStop(const QString& value)
{
    if (shortcutStop_ == value) {
        return;
    }
    shortcutStop_ = value;
    settings_.setValue(QStringLiteral("shortcuts/stop"), value);
    emit shortcutStopChanged();
}

void SettingsController::setShortcutNext(const QString& value)
{
    if (shortcutNext_ == value) {
        return;
    }
    shortcutNext_ = value;
    settings_.setValue(QStringLiteral("shortcuts/next"), value);
    emit shortcutNextChanged();
}

void SettingsController::setShortcutPrev(const QString& value)
{
    if (shortcutPrev_ == value) {
        return;
    }
    shortcutPrev_ = value;
    settings_.setValue(QStringLiteral("shortcuts/prev"), value);
    emit shortcutPrevChanged();
}

void SettingsController::setShortcutVolumeUp(const QString& value)
{
    if (shortcutVolumeUp_ == value) {
        return;
    }
    shortcutVolumeUp_ = value;
    settings_.setValue(QStringLiteral("shortcuts/volumeUp"), value);
    emit shortcutVolumeUpChanged();
}

void SettingsController::setShortcutVolumeDown(const QString& value)
{
    if (shortcutVolumeDown_ == value) {
        return;
    }
    shortcutVolumeDown_ = value;
    settings_.setValue(QStringLiteral("shortcuts/volumeDown"), value);
    emit shortcutVolumeDownChanged();
}

// Cache setters
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

void SettingsController::setCacheDirectory(const QString& value)
{
    if (cacheDirectory_ == value) {
        return;
    }
    cacheDirectory_ = value;
    settings_.setValue(QStringLiteral("cache/directory"), value);
    emit cacheDirectoryChanged();
}

void SettingsController::setClearCacheOnExit(bool value)
{
    if (clearCacheOnExit_ == value) {
        return;
    }
    clearCacheOnExit_ = value;
    settings_.setValue(QStringLiteral("cache/clearOnExit"), value);
    emit clearCacheOnExitChanged();
}

// About setters
void SettingsController::setCheckUpdatesOnStartup(bool value)
{
    if (checkUpdatesOnStartup_ == value) {
        return;
    }
    checkUpdatesOnStartup_ = value;
    settings_.setValue(QStringLiteral("about/checkUpdatesOnStartup"), value);
    emit checkUpdatesOnStartupChanged();
}

void SettingsController::resetToDefaults()
{
    restoreDefaults();
    saveAll();

    emit startupAutoPlayChanged();
    emit minimizeOnStartupChanged();
    emit closeBehaviorChanged();
    emit rememberWindowStateChanged();
    emit languageChanged();
    emit setAsDefaultPlayerChanged();
    emit defaultExportDirectoryChanged();

    emit listWindowPositionChanged();
    emit themeModeChanged();
    emit accentColorChanged();
    emit windowTransparencyChanged();
    emit fontTransparencyChanged();
    emit cornerRadiusChanged();

    emit outputDeviceChanged();
    emit outputFormatChanged();
    emit autoSampleRateChanged();
    emit defaultVolumeChanged();
    emit fadeInDurationChanged();
    emit fadeOutDurationChanged();
    emit fileAssociationsChanged();

    emit waveformModeChanged();
    emit waveformColorChanged();
    emit waveformBrightnessChanged();
    emit waveformThicknessChanged();
    emit waveformDensityChanged();

    emit defaultOutputFormatChanged();
    emit defaultBitrateChanged();
    emit defaultOutputDirectoryChanged();

    emit shortcutPlayPauseChanged();
    emit shortcutStopChanged();
    emit shortcutNextChanged();
    emit shortcutPrevChanged();
    emit shortcutVolumeUpChanged();
    emit shortcutVolumeDownChanged();

    emit cacheSizeLimitMBChanged();
    emit cacheDirectoryChanged();
    emit clearCacheOnExitChanged();
    emit currentCacheSizeMBChanged();

    emit checkUpdatesOnStartupChanged();
}

void SettingsController::clearCache()
{
    const QString dir = cacheDirectory_.isEmpty() ? defaultCacheDirectory() : cacheDirectory_;
    if (!dir.isEmpty() && QDir(dir).exists()) {
        QDir directory(dir);
        for (const QString& entry : directory.entryList(QDir::NoDotAndDotDot | QDir::Files
                                                        | QDir::Dirs | QDir::Hidden)) {
            const QString path = directory.absoluteFilePath(entry);
            QFileInfo info(path);
            if (info.isDir() && !info.isSymLink()) {
                QDir(path).removeRecursively();
            } else {
                QFile::remove(path);
            }
        }
    }
    recalculateCacheSize();
}

void SettingsController::checkForUpdates()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/AgPlayer/AgPlayer")));
}

void SettingsController::load()
{
    restoreDefaults();

    settings_.beginGroup(QStringLiteral("general"));
    startupAutoPlay_ = settings_.value(QStringLiteral("startupAutoPlay"), startupAutoPlay_).toBool();
    minimizeOnStartup_ = settings_.value(QStringLiteral("minimizeOnStartup"), minimizeOnStartup_).toBool();
    closeBehavior_ = settings_.value(QStringLiteral("closeBehavior"), closeBehavior_).toInt();
    rememberWindowState_ = settings_.value(QStringLiteral("rememberWindowState"), rememberWindowState_).toBool();
    language_ = validatedLanguage(settings_.value(QStringLiteral("language"), language_).toString());
    setAsDefaultPlayer_ = settings_.value(QStringLiteral("setAsDefaultPlayer"), setAsDefaultPlayer_).toBool();
    defaultExportDirectory_ = settings_.value(QStringLiteral("defaultExportDirectory"), defaultExportDirectory_).toString();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("appearance"));
    listWindowPosition_ = settings_.value(QStringLiteral("listWindowPosition"), listWindowPosition_).toInt();
    themeMode_ = settings_.value(QStringLiteral("themeMode"), themeMode_).toInt();
    accentColor_ = colorFromString(settings_.value(QStringLiteral("accentColor"), colorToString(accentColor_)).toString());
    windowTransparency_ = settings_.value(QStringLiteral("windowTransparency"), windowTransparency_).toDouble();
    fontTransparency_ = settings_.value(QStringLiteral("fontTransparency"), fontTransparency_).toDouble();
    cornerRadius_ = settings_.value(QStringLiteral("cornerRadius"), cornerRadius_).toInt();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("playback"));
    outputDevice_ = settings_.value(QStringLiteral("outputDevice"), outputDevice_).toString();
    outputFormat_ = settings_.value(QStringLiteral("outputFormat"), outputFormat_).toInt();
    autoSampleRate_ = settings_.value(QStringLiteral("autoSampleRate"), autoSampleRate_).toBool();
    defaultVolume_ = settings_.value(QStringLiteral("defaultVolume"), defaultVolume_).toDouble();
    fadeInDuration_ = settings_.value(QStringLiteral("fadeInDuration"), fadeInDuration_).toInt();
    fadeOutDuration_ = settings_.value(QStringLiteral("fadeOutDuration"), fadeOutDuration_).toInt();
    fileAssociations_ = settings_.value(QStringLiteral("fileAssociations"), fileAssociations_).toStringList();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("waveform"));
    waveformMode_ = settings_.value(QStringLiteral("mode"), waveformMode_).toInt();
    waveformColor_ = colorFromString(settings_.value(QStringLiteral("color"), colorToString(waveformColor_)).toString());
    waveformBrightness_ = settings_.value(QStringLiteral("brightness"), waveformBrightness_).toDouble();
    waveformThickness_ = settings_.value(QStringLiteral("thickness"), waveformThickness_).toInt();
    waveformDensity_ = settings_.value(QStringLiteral("density"), waveformDensity_).toInt();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("audioTools"));
    defaultOutputFormat_ = settings_.value(QStringLiteral("defaultOutputFormat"), defaultOutputFormat_).toString();
    defaultBitrate_ = settings_.value(QStringLiteral("defaultBitrate"), defaultBitrate_).toInt();
    defaultOutputDirectory_ = settings_.value(QStringLiteral("defaultOutputDirectory"), defaultOutputDirectory_).toString();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("shortcuts"));
    shortcutPlayPause_ = settings_.value(QStringLiteral("playPause"), shortcutPlayPause_).toString();
    shortcutStop_ = settings_.value(QStringLiteral("stop"), shortcutStop_).toString();
    shortcutNext_ = settings_.value(QStringLiteral("next"), shortcutNext_).toString();
    shortcutPrev_ = settings_.value(QStringLiteral("prev"), shortcutPrev_).toString();
    shortcutVolumeUp_ = settings_.value(QStringLiteral("volumeUp"), shortcutVolumeUp_).toString();
    shortcutVolumeDown_ = settings_.value(QStringLiteral("volumeDown"), shortcutVolumeDown_).toString();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("cache"));
    cacheSizeLimitMB_ = settings_.value(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_).toInt();
    cacheDirectory_ = settings_.value(QStringLiteral("directory"), cacheDirectory_).toString();
    clearCacheOnExit_ = settings_.value(QStringLiteral("clearOnExit"), clearCacheOnExit_).toBool();
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("about"));
    checkUpdatesOnStartup_ = settings_.value(QStringLiteral("checkUpdatesOnStartup"), checkUpdatesOnStartup_).toBool();
    settings_.endGroup();

    // Ensure clamped values are stored within valid ranges.
    closeBehavior_ = clampValue(closeBehavior_, 0, 2);
    listWindowPosition_ = clampValue(listWindowPosition_, 0, 3);
    themeMode_ = clampValue(themeMode_, 0, 2);
    windowTransparency_ = clampValue(windowTransparency_, 0.5, 1.0);
    fontTransparency_ = clampValue(fontTransparency_, 0.5, 1.0);
    cornerRadius_ = clampValue(cornerRadius_, 0, 16);
    outputFormat_ = clampValue(outputFormat_, 0, 2);
    defaultVolume_ = clampValue(defaultVolume_, 0.0, 1.0);
    fadeInDuration_ = clampValue(fadeInDuration_, 0, 5000);
    fadeOutDuration_ = clampValue(fadeOutDuration_, 0, 5000);
    waveformBrightness_ = clampValue(waveformBrightness_, 0.5, 2.0);
    waveformThickness_ = clampValue(waveformThickness_, 1, 4);
    waveformDensity_ = clampValue(waveformDensity_, 1, 4);
    defaultBitrate_ = clampValue(defaultBitrate_, 64, 320);
    cacheSizeLimitMB_ = std::max(cacheSizeLimitMB_, 100);
}

void SettingsController::saveAll()
{
    settings_.beginGroup(QStringLiteral("general"));
    settings_.setValue(QStringLiteral("startupAutoPlay"), startupAutoPlay_);
    settings_.setValue(QStringLiteral("minimizeOnStartup"), minimizeOnStartup_);
    settings_.setValue(QStringLiteral("closeBehavior"), closeBehavior_);
    settings_.setValue(QStringLiteral("rememberWindowState"), rememberWindowState_);
    settings_.setValue(QStringLiteral("language"), language_);
    settings_.setValue(QStringLiteral("setAsDefaultPlayer"), setAsDefaultPlayer_);
    settings_.setValue(QStringLiteral("defaultExportDirectory"), defaultExportDirectory_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("appearance"));
    settings_.setValue(QStringLiteral("listWindowPosition"), listWindowPosition_);
    settings_.setValue(QStringLiteral("themeMode"), themeMode_);
    settings_.setValue(QStringLiteral("accentColor"), colorToString(accentColor_));
    settings_.setValue(QStringLiteral("windowTransparency"), windowTransparency_);
    settings_.setValue(QStringLiteral("fontTransparency"), fontTransparency_);
    settings_.setValue(QStringLiteral("cornerRadius"), cornerRadius_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("playback"));
    settings_.setValue(QStringLiteral("outputDevice"), outputDevice_);
    settings_.setValue(QStringLiteral("outputFormat"), outputFormat_);
    settings_.setValue(QStringLiteral("autoSampleRate"), autoSampleRate_);
    settings_.setValue(QStringLiteral("defaultVolume"), defaultVolume_);
    settings_.setValue(QStringLiteral("fadeInDuration"), fadeInDuration_);
    settings_.setValue(QStringLiteral("fadeOutDuration"), fadeOutDuration_);
    settings_.setValue(QStringLiteral("fileAssociations"), fileAssociations_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("waveform"));
    settings_.setValue(QStringLiteral("mode"), waveformMode_);
    settings_.setValue(QStringLiteral("color"), colorToString(waveformColor_));
    settings_.setValue(QStringLiteral("brightness"), waveformBrightness_);
    settings_.setValue(QStringLiteral("thickness"), waveformThickness_);
    settings_.setValue(QStringLiteral("density"), waveformDensity_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("audioTools"));
    settings_.setValue(QStringLiteral("defaultOutputFormat"), defaultOutputFormat_);
    settings_.setValue(QStringLiteral("defaultBitrate"), defaultBitrate_);
    settings_.setValue(QStringLiteral("defaultOutputDirectory"), defaultOutputDirectory_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("shortcuts"));
    settings_.setValue(QStringLiteral("playPause"), shortcutPlayPause_);
    settings_.setValue(QStringLiteral("stop"), shortcutStop_);
    settings_.setValue(QStringLiteral("next"), shortcutNext_);
    settings_.setValue(QStringLiteral("prev"), shortcutPrev_);
    settings_.setValue(QStringLiteral("volumeUp"), shortcutVolumeUp_);
    settings_.setValue(QStringLiteral("volumeDown"), shortcutVolumeDown_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("cache"));
    settings_.setValue(QStringLiteral("sizeLimitMB"), cacheSizeLimitMB_);
    settings_.setValue(QStringLiteral("directory"), cacheDirectory_);
    settings_.setValue(QStringLiteral("clearOnExit"), clearCacheOnExit_);
    settings_.endGroup();

    settings_.beginGroup(QStringLiteral("about"));
    settings_.setValue(QStringLiteral("checkUpdatesOnStartup"), checkUpdatesOnStartup_);
    settings_.endGroup();
}

void SettingsController::restoreDefaults()
{
    startupAutoPlay_ = false;
    minimizeOnStartup_ = false;
    closeBehavior_ = 0;
    rememberWindowState_ = true;
    language_ = QStringLiteral("zh");
    setAsDefaultPlayer_ = false;
    defaultExportDirectory_ = defaultMusicDirectory();

    listWindowPosition_ = 0;
    themeMode_ = 0;
    accentColor_ = QColor(QStringLiteral("#00D4FF"));
    windowTransparency_ = 1.0;
    fontTransparency_ = 1.0;
    cornerRadius_ = 12;

    outputDevice_ = QStringLiteral("Default");
    outputFormat_ = 0;
    autoSampleRate_ = true;
    defaultVolume_ = 0.6;
    fadeInDuration_ = 0;
    fadeOutDuration_ = 0;
    fileAssociations_ = {QStringLiteral("mp3"), QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("aac"), QStringLiteral("m4a"),
        QStringLiteral("ogg")};

    waveformMode_ = 0;
    waveformColor_ = QColor(QStringLiteral("#00D4FF"));
    waveformBrightness_ = 1.0;
    waveformThickness_ = 2;
    waveformDensity_ = 2;

    defaultOutputFormat_ = QStringLiteral("mp3");
    defaultBitrate_ = 320;
    defaultOutputDirectory_ = defaultMusicDirectory() + QStringLiteral("/AgPlayer Export");

    shortcutPlayPause_ = QStringLiteral("Space");
    shortcutStop_ = QStringLiteral("Ctrl+S");
    shortcutNext_ = QStringLiteral("Ctrl+Right");
    shortcutPrev_ = QStringLiteral("Ctrl+Left");
    shortcutVolumeUp_ = QStringLiteral("Ctrl+Up");
    shortcutVolumeDown_ = QStringLiteral("Ctrl+Down");

    cacheSizeLimitMB_ = 1024;
    cacheDirectory_ = defaultCacheDirectory();
    clearCacheOnExit_ = false;

    checkUpdatesOnStartup_ = true;
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
    const QString location = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (location.isEmpty()) {
        return QDir::homePath() + QStringLiteral("/AgPlayer/cache");
    }
    return location;
}
