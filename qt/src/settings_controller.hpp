#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>

class FileAssociationController;

class SettingsController final : public QObject {
    Q_OBJECT

    // General
    Q_PROPERTY(bool autoStartWithWindows READ autoStartWithWindows
                   WRITE setAutoStartWithWindows NOTIFY autoStartWithWindowsChanged)
    Q_PROPERTY(bool restoreLastPlaybackOnStartup READ restoreLastPlaybackOnStartup
                   WRITE setRestoreLastPlaybackOnStartup
                   NOTIFY restoreLastPlaybackOnStartupChanged)
    Q_PROPERTY(bool showListWindowPanel READ showListWindowPanel
                   WRITE setShowListWindowPanel NOTIFY showListWindowPanelChanged)
    Q_PROPERTY(bool windowMagneticSnap READ windowMagneticSnap WRITE setWindowMagneticSnap
                   NOTIFY windowMagneticSnapChanged)
    Q_PROPERTY(int listWindowPosition READ listWindowPosition WRITE setListWindowPosition
                   NOTIFY listWindowPositionChanged)
    Q_PROPERTY(int closeBehavior READ closeBehavior WRITE setCloseBehavior
                   NOTIFY closeBehaviorChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool setAsDefaultPlayer READ setAsDefaultPlayer WRITE setSetAsDefaultPlayer
                   NOTIFY setAsDefaultPlayerChanged)
    Q_PROPERTY(QStringList fileAssociations READ fileAssociations WRITE setFileAssociations
                   NOTIFY fileAssociationsChanged)
    Q_PROPERTY(QString defaultExportDirectory READ defaultExportDirectory
                   WRITE setDefaultExportDirectory NOTIFY defaultExportDirectoryChanged)

    // Playback & Engine
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice
                   NOTIFY outputDeviceChanged)
    Q_PROPERTY(bool audioExclusiveMode READ audioExclusiveMode WRITE setAudioExclusiveMode
                   NOTIFY audioExclusiveModeChanged)
    Q_PROPERTY(bool playButtonRgbGlow READ playButtonRgbGlow WRITE setPlayButtonRgbGlow
                   NOTIFY playButtonRgbGlowChanged)
    Q_PROPERTY(int defaultPlaybackMode READ defaultPlaybackMode WRITE setDefaultPlaybackMode
                   NOTIFY defaultPlaybackModeChanged)
    Q_PROPERTY(bool gaplessPlayback READ gaplessPlayback WRITE setGaplessPlayback
                   NOTIFY gaplessPlaybackChanged)
    Q_PROPERTY(int crossfadeMs READ crossfadeMs WRITE setCrossfadeMs
                   NOTIFY crossfadeMsChanged)
    Q_PROPERTY(bool autoMatchSampleRate READ autoMatchSampleRate WRITE setAutoMatchSampleRate
                   NOTIFY autoMatchSampleRateChanged)
    Q_PROPERTY(bool autoReadBpm READ autoReadBpm WRITE setAutoReadBpm NOTIFY autoReadBpmChanged)
    Q_PROPERTY(bool autoReadRating READ autoReadRating WRITE setAutoReadRating
                   NOTIFY autoReadRatingChanged)

    // Appearance & Visualizer
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool glassEffect READ glassEffect WRITE setGlassEffect NOTIFY glassEffectChanged)
    Q_PROPERTY(int waveformMode READ waveformMode WRITE setWaveformMode NOTIFY waveformModeChanged)
    Q_PROPERTY(int waveformDensity READ waveformDensity WRITE setWaveformDensity
                   NOTIFY waveformDensityChanged)
    Q_PROPERTY(double waveformThickness READ waveformThickness WRITE setWaveformThickness
                   NOTIFY waveformThicknessChanged)
    Q_PROPERTY(bool waveformHoverTimePreview READ waveformHoverTimePreview
                   WRITE setWaveformHoverTimePreview NOTIFY waveformHoverTimePreviewChanged)

    // Audio Tools
    Q_PROPERTY(QString defaultOutputDirectory READ defaultOutputDirectory
                   WRITE setDefaultOutputDirectory NOTIFY defaultOutputDirectoryChanged)
    Q_PROPERTY(int overwritePolicy READ overwritePolicy WRITE setOverwritePolicy
                   NOTIFY overwritePolicyChanged)
    Q_PROPERTY(QString defaultTranscodeFormat READ defaultTranscodeFormat
                   WRITE setDefaultTranscodeFormat NOTIFY defaultTranscodeFormatChanged)
    Q_PROPERTY(bool preserveMetadata READ preserveMetadata WRITE setPreserveMetadata
                   NOTIFY preserveMetadataChanged)
    Q_PROPERTY(bool keepPitchWhileSpeedChange READ keepPitchWhileSpeedChange
                   WRITE setKeepPitchWhileSpeedChange NOTIFY keepPitchWhileSpeedChangeChanged)
    Q_PROPERTY(bool vocalProtection READ vocalProtection WRITE setVocalProtection
                   NOTIFY vocalProtectionChanged)

    // Hotkeys
    Q_PROPERTY(QString hkPlayPause READ hkPlayPause WRITE setHkPlayPause NOTIFY hkPlayPauseChanged)
    Q_PROPERTY(QString hkPrevNext READ hkPrevNext WRITE setHkPrevNext NOTIFY hkPrevNextChanged)
    Q_PROPERTY(QString hkVolumeUpDown READ hkVolumeUpDown WRITE setHkVolumeUpDown
                   NOTIFY hkVolumeUpDownChanged)
    Q_PROPERTY(QString hkToggleMiniPlayer READ hkToggleMiniPlayer WRITE setHkToggleMiniPlayer
                   NOTIFY hkToggleMiniPlayerChanged)
    Q_PROPERTY(QString hkSearch READ hkSearch WRITE setHkSearch NOTIFY hkSearchChanged)
    Q_PROPERTY(QString hkWaveformMode READ hkWaveformMode WRITE setHkWaveformMode
                   NOTIFY hkWaveformModeChanged)
    Q_PROPERTY(QString hkAudioTools READ hkAudioTools WRITE setHkAudioTools
                   NOTIFY hkAudioToolsChanged)

    // Cache & Storage
    Q_PROPERTY(QString cacheDirectory READ cacheDirectory WRITE setCacheDirectory
                   NOTIFY cacheDirectoryChanged)
    Q_PROPERTY(bool autoCleanCache READ autoCleanCache WRITE setAutoCleanCache
                   NOTIFY autoCleanCacheChanged)
    Q_PROPERTY(bool cleanTempOnExit READ cleanTempOnExit WRITE setCleanTempOnExit
                   NOTIFY cleanTempOnExitChanged)
    Q_PROPERTY(int cacheSizeLimitMB READ cacheSizeLimitMB WRITE setCacheSizeLimitMB
                   NOTIFY cacheSizeLimitMBChanged)
    Q_PROPERTY(int currentCacheSizeMB READ currentCacheSizeMB NOTIFY currentCacheSizeMBChanged)

    // About
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString releaseDate READ releaseDate CONSTANT)

public:
    explicit SettingsController(QObject* parent = nullptr);
    ~SettingsController();

    // General getters
    bool autoStartWithWindows() const noexcept;
    bool restoreLastPlaybackOnStartup() const noexcept;
    bool showListWindowPanel() const noexcept;
    bool windowMagneticSnap() const noexcept;
    int listWindowPosition() const noexcept;
    int closeBehavior() const noexcept;
    QString language() const;
    bool setAsDefaultPlayer() const noexcept;
    QStringList fileAssociations() const;
    QString defaultExportDirectory() const;

    // Playback & Engine getters
    QString outputDevice() const;
    bool audioExclusiveMode() const noexcept;
    bool playButtonRgbGlow() const noexcept;
    int defaultPlaybackMode() const noexcept;
    bool gaplessPlayback() const noexcept;
    int crossfadeMs() const noexcept;
    bool autoMatchSampleRate() const noexcept;
    bool autoReadBpm() const noexcept;
    bool autoReadRating() const noexcept;

    // Appearance & Visualizer getters
    int themeMode() const noexcept;
    bool glassEffect() const noexcept;
    int waveformMode() const noexcept;
    int waveformDensity() const noexcept;
    double waveformThickness() const noexcept;
    bool waveformHoverTimePreview() const noexcept;

    // Audio Tools getters
    QString defaultOutputDirectory() const;
    int overwritePolicy() const noexcept;
    QString defaultTranscodeFormat() const;
    bool preserveMetadata() const noexcept;
    bool keepPitchWhileSpeedChange() const noexcept;
    bool vocalProtection() const noexcept;

    // Hotkeys getters
    QString hkPlayPause() const;
    QString hkPrevNext() const;
    QString hkVolumeUpDown() const;
    QString hkToggleMiniPlayer() const;
    QString hkSearch() const;
    QString hkWaveformMode() const;
    QString hkAudioTools() const;

    // Cache & Storage getters
    QString cacheDirectory() const;
    bool autoCleanCache() const noexcept;
    bool cleanTempOnExit() const noexcept;
    int cacheSizeLimitMB() const noexcept;
    int currentCacheSizeMB() const noexcept;

    // About getters
    QString version() const;
    QString releaseDate() const;

    // General setters
    void setAutoStartWithWindows(bool value);
    void setRestoreLastPlaybackOnStartup(bool value);
    void setShowListWindowPanel(bool value);
    void setWindowMagneticSnap(bool value);
    void setListWindowPosition(int value);
    void setCloseBehavior(int value);
    void setLanguage(const QString& value);
    void setSetAsDefaultPlayer(bool value);
    void setFileAssociations(const QStringList& value);
    void setDefaultExportDirectory(const QString& value);

    // Playback & Engine setters
    void setOutputDevice(const QString& value);
    void setAudioExclusiveMode(bool value);
    void setPlayButtonRgbGlow(bool value);
    void setDefaultPlaybackMode(int value);
    void setGaplessPlayback(bool value);
    void setCrossfadeMs(int value);
    void setAutoMatchSampleRate(bool value);
    void setAutoReadBpm(bool value);
    void setAutoReadRating(bool value);

    // Appearance & Visualizer setters
    void setThemeMode(int value);
    void setGlassEffect(bool value);
    void setWaveformMode(int value);
    void setWaveformDensity(int value);
    void setWaveformThickness(double value);
    void setWaveformHoverTimePreview(bool value);

    // Audio Tools setters
    void setDefaultOutputDirectory(const QString& value);
    void setOverwritePolicy(int value);
    void setDefaultTranscodeFormat(const QString& value);
    void setPreserveMetadata(bool value);
    void setKeepPitchWhileSpeedChange(bool value);
    void setVocalProtection(bool value);

    // Hotkeys setters
    void setHkPlayPause(const QString& value);
    void setHkPrevNext(const QString& value);
    void setHkVolumeUpDown(const QString& value);
    void setHkToggleMiniPlayer(const QString& value);
    void setHkSearch(const QString& value);
    void setHkWaveformMode(const QString& value);
    void setHkAudioTools(const QString& value);

    // Cache & Storage setters
    void setCacheDirectory(const QString& value);
    void setAutoCleanCache(bool value);
    void setCleanTempOnExit(bool value);
    void setCacheSizeLimitMB(int value);

    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void rebindFileAssociations();
    Q_INVOKABLE void clearWaveformCache();
    Q_INVOKABLE void clearCoverCache();
    Q_INVOKABLE void clearTempFiles();
    Q_INVOKABLE void clearAllCache();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openOfficialWebsite();

signals:
    void updateCheckFinished(const QString& message, bool success);
    void autoStartWithWindowsChanged();
    void restoreLastPlaybackOnStartupChanged();
    void showListWindowPanelChanged();
    void windowMagneticSnapChanged();
    void listWindowPositionChanged();
    void closeBehaviorChanged();
    void languageChanged();
    void setAsDefaultPlayerChanged();
    void fileAssociationsChanged();
    void defaultExportDirectoryChanged();

    void outputDeviceChanged();
    void audioExclusiveModeChanged();
    void playButtonRgbGlowChanged();
    void defaultPlaybackModeChanged();
    void gaplessPlaybackChanged();
    void crossfadeMsChanged();
    void autoMatchSampleRateChanged();
    void autoReadBpmChanged();
    void autoReadRatingChanged();

    void themeModeChanged();
    void glassEffectChanged();
    void waveformModeChanged();
    void waveformDensityChanged();
    void waveformThicknessChanged();
    void waveformHoverTimePreviewChanged();

    void defaultOutputDirectoryChanged();
    void overwritePolicyChanged();
    void defaultTranscodeFormatChanged();
    void preserveMetadataChanged();
    void keepPitchWhileSpeedChangeChanged();
    void vocalProtectionChanged();

    void hkPlayPauseChanged();
    void hkPrevNextChanged();
    void hkVolumeUpDownChanged();
    void hkToggleMiniPlayerChanged();
    void hkSearchChanged();
    void hkWaveformModeChanged();
    void hkAudioToolsChanged();

    void cacheDirectoryChanged();
    void autoCleanCacheChanged();
    void cleanTempOnExitChanged();
    void cacheSizeLimitMBChanged();
    void currentCacheSizeMBChanged();

private:
    void load();
    void saveAll();
    void restoreDefaults();
    void recalculateCacheSize();
    static qint64 directorySizeBytes(const QString& path);
    static QString defaultMusicDirectory();
    static QString defaultCacheDirectory();
    static QString defaultExportDir();
    static QString validatedLanguage(const QString& value);

    QSettings settings_;
    std::unique_ptr<FileAssociationController> fileAssociationController_;

    void applyFileAssociations();

    // General
    bool autoStartWithWindows_ = false;
    bool restoreLastPlaybackOnStartup_ = true;
    bool showListWindowPanel_ = true;
    bool windowMagneticSnap_ = true;
    int listWindowPosition_ = 1;
    int closeBehavior_ = 0;
    QString language_ = QStringLiteral("zh");
    bool setAsDefaultPlayer_ = false;
    QStringList fileAssociations_ = {QStringLiteral("mp3"), QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("aac"), QStringLiteral("m4a"),
        QStringLiteral("ogg")};
    QString defaultExportDirectory_;

    // Playback & Engine
    QString outputDevice_ = QStringLiteral("\u81EA\u52A8 / \u7CFB\u7EDF\u9ED8\u8BA4\u8BBE\u5907");
    bool audioExclusiveMode_ = false;
    bool playButtonRgbGlow_ = true;
    int defaultPlaybackMode_ = 3;
    bool gaplessPlayback_ = true;
    int crossfadeMs_ = 0;
    bool autoMatchSampleRate_ = true;
    bool autoReadBpm_ = true;
    bool autoReadRating_ = true;

    // Appearance & Visualizer
    int themeMode_ = 0;
    bool glassEffect_ = true;
    int waveformMode_ = 1;
    int waveformDensity_ = 1;
    double waveformThickness_ = 2.0;
    bool waveformHoverTimePreview_ = true;

    // Audio Tools
    QString defaultOutputDirectory_;
    int overwritePolicy_ = 0;
    QString defaultTranscodeFormat_ = QStringLiteral("MP3 / 320kbps / 44.1kHz / Stereo");
    bool preserveMetadata_ = true;
    bool keepPitchWhileSpeedChange_ = true;
    bool vocalProtection_ = true;

    // Hotkeys
    QString hkPlayPause_ = QStringLiteral("Global + Space");
    QString hkPrevNext_ = QStringLiteral("Global + Left / Global + Right");
    QString hkVolumeUpDown_ = QStringLiteral("Global + Up / Global + Down");
    QString hkToggleMiniPlayer_ = QStringLiteral("Alt + P");
    QString hkSearch_ = QStringLiteral("Ctrl + F");
    QString hkWaveformMode_ = QStringLiteral("Tab");
    QString hkAudioTools_ = QStringLiteral("Alt + D");

    // Cache & Storage
    QString cacheDirectory_;
    bool autoCleanCache_ = true;
    bool cleanTempOnExit_ = true;
    int cacheSizeLimitMB_ = 1024;
    int currentCacheSizeMB_ = 0;
};
