#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

class SettingsController final : public QObject {
    Q_OBJECT

    // General
    Q_PROPERTY(bool startupAutoPlay READ startupAutoPlay WRITE setStartupAutoPlay
                   NOTIFY startupAutoPlayChanged)
    Q_PROPERTY(bool minimizeOnStartup READ minimizeOnStartup WRITE setMinimizeOnStartup
                   NOTIFY minimizeOnStartupChanged)
    Q_PROPERTY(int closeBehavior READ closeBehavior WRITE setCloseBehavior
                   NOTIFY closeBehaviorChanged)
    Q_PROPERTY(bool rememberWindowState READ rememberWindowState WRITE setRememberWindowState
                   NOTIFY rememberWindowStateChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool setAsDefaultPlayer READ setAsDefaultPlayer WRITE setSetAsDefaultPlayer
                   NOTIFY setAsDefaultPlayerChanged)
    Q_PROPERTY(QString defaultExportDirectory READ defaultExportDirectory
                   WRITE setDefaultExportDirectory NOTIFY defaultExportDirectoryChanged)

    // Appearance
    Q_PROPERTY(int listWindowPosition READ listWindowPosition WRITE setListWindowPosition
                   NOTIFY listWindowPositionChanged)
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor
                   NOTIFY accentColorChanged)
    Q_PROPERTY(double windowTransparency READ windowTransparency WRITE setWindowTransparency
                   NOTIFY windowTransparencyChanged)
    Q_PROPERTY(double fontTransparency READ fontTransparency WRITE setFontTransparency
                   NOTIFY fontTransparencyChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)

    // Playback
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice
                   NOTIFY outputDeviceChanged)
    Q_PROPERTY(int outputFormat READ outputFormat WRITE setOutputFormat NOTIFY outputFormatChanged)
    Q_PROPERTY(bool autoSampleRate READ autoSampleRate WRITE setAutoSampleRate
                   NOTIFY autoSampleRateChanged)
    Q_PROPERTY(double defaultVolume READ defaultVolume WRITE setDefaultVolume
                   NOTIFY defaultVolumeChanged)
    Q_PROPERTY(int fadeInDuration READ fadeInDuration WRITE setFadeInDuration
                   NOTIFY fadeInDurationChanged)
    Q_PROPERTY(int fadeOutDuration READ fadeOutDuration WRITE setFadeOutDuration
                   NOTIFY fadeOutDurationChanged)
    Q_PROPERTY(QStringList fileAssociations READ fileAssociations WRITE setFileAssociations
                   NOTIFY fileAssociationsChanged)

    // Waveform
    Q_PROPERTY(int waveformMode READ waveformMode WRITE setWaveformMode NOTIFY waveformModeChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor
                   NOTIFY waveformColorChanged)
    Q_PROPERTY(double waveformBrightness READ waveformBrightness WRITE setWaveformBrightness
                   NOTIFY waveformBrightnessChanged)
    Q_PROPERTY(int waveformThickness READ waveformThickness WRITE setWaveformThickness
                   NOTIFY waveformThicknessChanged)
    Q_PROPERTY(int waveformDensity READ waveformDensity WRITE setWaveformDensity
                   NOTIFY waveformDensityChanged)

    // Audio Tools
    Q_PROPERTY(QString defaultOutputFormat READ defaultOutputFormat WRITE setDefaultOutputFormat
                   NOTIFY defaultOutputFormatChanged)
    Q_PROPERTY(int defaultBitrate READ defaultBitrate WRITE setDefaultBitrate
                   NOTIFY defaultBitrateChanged)
    Q_PROPERTY(QString defaultOutputDirectory READ defaultOutputDirectory
                   WRITE setDefaultOutputDirectory NOTIFY defaultOutputDirectoryChanged)

    // Shortcuts
    Q_PROPERTY(QString shortcutPlayPause READ shortcutPlayPause WRITE setShortcutPlayPause
                   NOTIFY shortcutPlayPauseChanged)
    Q_PROPERTY(QString shortcutStop READ shortcutStop WRITE setShortcutStop
                   NOTIFY shortcutStopChanged)
    Q_PROPERTY(QString shortcutNext READ shortcutNext WRITE setShortcutNext
                   NOTIFY shortcutNextChanged)
    Q_PROPERTY(QString shortcutPrev READ shortcutPrev WRITE setShortcutPrev
                   NOTIFY shortcutPrevChanged)
    Q_PROPERTY(QString shortcutVolumeUp READ shortcutVolumeUp WRITE setShortcutVolumeUp
                   NOTIFY shortcutVolumeUpChanged)
    Q_PROPERTY(QString shortcutVolumeDown READ shortcutVolumeDown WRITE setShortcutVolumeDown
                   NOTIFY shortcutVolumeDownChanged)

    // Cache
    Q_PROPERTY(int cacheSizeLimitMB READ cacheSizeLimitMB WRITE setCacheSizeLimitMB
                   NOTIFY cacheSizeLimitMBChanged)
    Q_PROPERTY(QString cacheDirectory READ cacheDirectory WRITE setCacheDirectory
                   NOTIFY cacheDirectoryChanged)
    Q_PROPERTY(bool clearCacheOnExit READ clearCacheOnExit WRITE setClearCacheOnExit
                   NOTIFY clearCacheOnExitChanged)
    Q_PROPERTY(int currentCacheSizeMB READ currentCacheSizeMB NOTIFY currentCacheSizeMBChanged)

    // About
    Q_PROPERTY(bool checkUpdatesOnStartup READ checkUpdatesOnStartup
                   WRITE setCheckUpdatesOnStartup NOTIFY checkUpdatesOnStartupChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT)
    Q_PROPERTY(QString releaseDate READ releaseDate CONSTANT)

public:
    explicit SettingsController(QObject* parent = nullptr);

    // General getters
    bool startupAutoPlay() const noexcept;
    bool minimizeOnStartup() const noexcept;
    int closeBehavior() const noexcept;
    bool rememberWindowState() const noexcept;
    QString language() const;
    bool setAsDefaultPlayer() const noexcept;
    QString defaultExportDirectory() const;

    // Appearance getters
    int listWindowPosition() const noexcept;
    int themeMode() const noexcept;
    QColor accentColor() const;
    double windowTransparency() const noexcept;
    double fontTransparency() const noexcept;
    int cornerRadius() const noexcept;

    // Playback getters
    QString outputDevice() const;
    int outputFormat() const noexcept;
    bool autoSampleRate() const noexcept;
    double defaultVolume() const noexcept;
    int fadeInDuration() const noexcept;
    int fadeOutDuration() const noexcept;
    QStringList fileAssociations() const;

    // Waveform getters
    int waveformMode() const noexcept;
    QColor waveformColor() const;
    double waveformBrightness() const noexcept;
    int waveformThickness() const noexcept;
    int waveformDensity() const noexcept;

    // Audio Tools getters
    QString defaultOutputFormat() const;
    int defaultBitrate() const noexcept;
    QString defaultOutputDirectory() const;

    // Shortcuts getters
    QString shortcutPlayPause() const;
    QString shortcutStop() const;
    QString shortcutNext() const;
    QString shortcutPrev() const;
    QString shortcutVolumeUp() const;
    QString shortcutVolumeDown() const;

    // Cache getters
    int cacheSizeLimitMB() const noexcept;
    QString cacheDirectory() const;
    bool clearCacheOnExit() const noexcept;
    int currentCacheSizeMB() const noexcept;

    // About getters
    bool checkUpdatesOnStartup() const noexcept;
    QString version() const;
    QString buildNumber() const;
    QString releaseDate() const;

    // General setters
    void setStartupAutoPlay(bool value);
    void setMinimizeOnStartup(bool value);
    void setCloseBehavior(int value);
    void setRememberWindowState(bool value);
    void setLanguage(const QString& value);
    void setSetAsDefaultPlayer(bool value);
    void setDefaultExportDirectory(const QString& value);

    // Appearance setters
    void setListWindowPosition(int value);
    void setThemeMode(int value);
    void setAccentColor(const QColor& value);
    void setWindowTransparency(double value);
    void setFontTransparency(double value);
    void setCornerRadius(int value);

    // Playback setters
    void setOutputDevice(const QString& value);
    void setOutputFormat(int value);
    void setAutoSampleRate(bool value);
    void setDefaultVolume(double value);
    void setFadeInDuration(int value);
    void setFadeOutDuration(int value);
    void setFileAssociations(const QStringList& value);

    // Waveform setters
    void setWaveformMode(int value);
    void setWaveformColor(const QColor& value);
    void setWaveformBrightness(double value);
    void setWaveformThickness(int value);
    void setWaveformDensity(int value);

    // Audio Tools setters
    void setDefaultOutputFormat(const QString& value);
    void setDefaultBitrate(int value);
    void setDefaultOutputDirectory(const QString& value);

    // Shortcuts setters
    void setShortcutPlayPause(const QString& value);
    void setShortcutStop(const QString& value);
    void setShortcutNext(const QString& value);
    void setShortcutPrev(const QString& value);
    void setShortcutVolumeUp(const QString& value);
    void setShortcutVolumeDown(const QString& value);

    // Cache setters
    void setCacheSizeLimitMB(int value);
    void setCacheDirectory(const QString& value);
    void setClearCacheOnExit(bool value);

    // About setters
    void setCheckUpdatesOnStartup(bool value);

    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void checkForUpdates();

signals:
    void startupAutoPlayChanged();
    void minimizeOnStartupChanged();
    void closeBehaviorChanged();
    void rememberWindowStateChanged();
    void languageChanged();
    void setAsDefaultPlayerChanged();
    void defaultExportDirectoryChanged();

    void listWindowPositionChanged();
    void themeModeChanged();
    void accentColorChanged();
    void windowTransparencyChanged();
    void fontTransparencyChanged();
    void cornerRadiusChanged();

    void outputDeviceChanged();
    void outputFormatChanged();
    void autoSampleRateChanged();
    void defaultVolumeChanged();
    void fadeInDurationChanged();
    void fadeOutDurationChanged();
    void fileAssociationsChanged();

    void waveformModeChanged();
    void waveformColorChanged();
    void waveformBrightnessChanged();
    void waveformThicknessChanged();
    void waveformDensityChanged();

    void defaultOutputFormatChanged();
    void defaultBitrateChanged();
    void defaultOutputDirectoryChanged();

    void shortcutPlayPauseChanged();
    void shortcutStopChanged();
    void shortcutNextChanged();
    void shortcutPrevChanged();
    void shortcutVolumeUpChanged();
    void shortcutVolumeDownChanged();

    void cacheSizeLimitMBChanged();
    void cacheDirectoryChanged();
    void clearCacheOnExitChanged();
    void currentCacheSizeMBChanged();

    void checkUpdatesOnStartupChanged();

private:
    void load();
    void saveAll();
    void restoreDefaults();
    void recalculateCacheSize();
    static qint64 directorySizeBytes(const QString& path);
    static QString defaultMusicDirectory();
    static QString defaultCacheDirectory();

    QSettings settings_;

    // General
    bool startupAutoPlay_ = false;
    bool minimizeOnStartup_ = false;
    int closeBehavior_ = 0;
    bool rememberWindowState_ = true;
    QString language_ = QStringLiteral("zh");
    bool setAsDefaultPlayer_ = false;
    QString defaultExportDirectory_;

    // Appearance
    int listWindowPosition_ = 0;
    int themeMode_ = 0;
    QColor accentColor_ = QColor(QStringLiteral("#00D4FF"));
    double windowTransparency_ = 1.0;
    double fontTransparency_ = 1.0;
    int cornerRadius_ = 12;

    // Playback
    QString outputDevice_ = QStringLiteral("Default");
    int outputFormat_ = 0;
    bool autoSampleRate_ = true;
    double defaultVolume_ = 0.6;
    int fadeInDuration_ = 0;
    int fadeOutDuration_ = 0;
    QStringList fileAssociations_ = {QStringLiteral("mp3"), QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("aac"), QStringLiteral("m4a"),
        QStringLiteral("ogg")};

    // Waveform
    int waveformMode_ = 0;
    QColor waveformColor_ = QColor(QStringLiteral("#00D4FF"));
    double waveformBrightness_ = 1.0;
    int waveformThickness_ = 2;
    int waveformDensity_ = 2;

    // Audio Tools
    QString defaultOutputFormat_ = QStringLiteral("mp3");
    int defaultBitrate_ = 320;
    QString defaultOutputDirectory_;

    // Shortcuts
    QString shortcutPlayPause_ = QStringLiteral("Space");
    QString shortcutStop_ = QStringLiteral("Ctrl+S");
    QString shortcutNext_ = QStringLiteral("Ctrl+Right");
    QString shortcutPrev_ = QStringLiteral("Ctrl+Left");
    QString shortcutVolumeUp_ = QStringLiteral("Ctrl+Up");
    QString shortcutVolumeDown_ = QStringLiteral("Ctrl+Down");

    // Cache
    int cacheSizeLimitMB_ = 1024;
    QString cacheDirectory_;
    bool clearCacheOnExit_ = false;
    int currentCacheSizeMB_ = 0;

    // About
    bool checkUpdatesOnStartup_ = true;
};
