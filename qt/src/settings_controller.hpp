#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "frequency_color_waveform_settings.hpp"

#include <memory>

class FileAssociationController;
class SettingsControllerTest;
class UpdateChecker;

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
    Q_PROPERTY(QString systemIntegrationError READ systemIntegrationError
                    NOTIFY systemIntegrationErrorChanged)
    Q_PROPERTY(QString globalHotkeyError READ globalHotkeyError
                    NOTIFY globalHotkeyErrorChanged)

    // Playback & Engine
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice
                   NOTIFY outputDeviceChanged)
    Q_PROPERTY(bool exclusiveMode READ exclusiveMode WRITE setExclusiveMode
                   NOTIFY exclusiveModeChanged)
    Q_PROPERTY(bool matchTrackSampleRate READ matchTrackSampleRate
                   WRITE setMatchTrackSampleRate
                   NOTIFY matchTrackSampleRateChanged)
    Q_PROPERTY(int transitionFadeMs READ transitionFadeMs
                   WRITE setTransitionFadeMs NOTIFY transitionFadeMsChanged)
    Q_PROPERTY(int defaultPlaybackMode READ defaultPlaybackMode WRITE setDefaultPlaybackMode
                   NOTIFY defaultPlaybackModeChanged)
    Q_PROPERTY(bool autoReadBpm READ autoReadBpm WRITE setAutoReadBpm NOTIFY autoReadBpmChanged)
    Q_PROPERTY(bool autoReadRating READ autoReadRating WRITE setAutoReadRating
                   NOTIFY autoReadRatingChanged)

    // Appearance & Visualizer
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QString windowLayoutTheme READ windowLayoutTheme
                   WRITE setWindowLayoutTheme NOTIFY windowLayoutThemeChanged)
    Q_PROPERTY(int playerShellMode READ playerShellMode WRITE setPlayerShellMode
                   NOTIFY playerShellModeChanged)
    Q_PROPERTY(bool rollingBeatGridEnabled READ rollingBeatGridEnabled
                   WRITE setRollingBeatGridEnabled
                   NOTIFY rollingBeatGridEnabledChanged)
    Q_PROPERTY(int rollingBeatGridGrouping READ rollingBeatGridGrouping
                   WRITE setRollingBeatGridGrouping
                   NOTIFY rollingBeatGridGroupingChanged)
    Q_PROPERTY(int waveformMode READ waveformMode WRITE setWaveformMode NOTIFY waveformModeChanged)
    Q_PROPERTY(double waveformHeight READ waveformHeight WRITE setWaveformHeight
                   NOTIFY waveformHeightChanged)
    Q_PROPERTY(double waveformDensity READ waveformDensity WRITE setWaveformDensity
                   NOTIFY waveformDensityChanged)
    Q_PROPERTY(double waveformThickness READ waveformThickness WRITE setWaveformThickness
                   NOTIFY waveformThicknessChanged)
    Q_PROPERTY(int waveformPeakAlgorithm READ waveformPeakAlgorithm
                   WRITE setWaveformPeakAlgorithm NOTIFY waveformPeakAlgorithmChanged)
    Q_PROPERTY(QString waveformSolidBaseColor READ waveformSolidBaseColor
                   WRITE setWaveformSolidBaseColor NOTIFY waveformSolidBaseColorChanged)
    Q_PROPERTY(QString waveformSolidProgressColor READ waveformSolidProgressColor
                   WRITE setWaveformSolidProgressColor NOTIFY waveformSolidProgressColorChanged)
    Q_PROPERTY(QString waveformRgbBaseColor READ waveformRgbBaseColor
                   WRITE setWaveformRgbBaseColor NOTIFY waveformRgbBaseColorChanged)
    Q_PROPERTY(QString waveformRgbStartColor READ waveformRgbStartColor
                   WRITE setWaveformRgbStartColor NOTIFY waveformRgbStartColorChanged)
    Q_PROPERTY(QString waveformRgbMiddleColor READ waveformRgbMiddleColor
                   WRITE setWaveformRgbMiddleColor NOTIFY waveformRgbMiddleColorChanged)
    Q_PROPERTY(QString waveformRgbEndColor READ waveformRgbEndColor
                   WRITE setWaveformRgbEndColor NOTIFY waveformRgbEndColorChanged)
    Q_PROPERTY(FrequencyColorWaveformSettings* frequencyColorWaveform
                   READ frequencyColorWaveform CONSTANT)
    Q_PROPERTY(bool waveformRgbProgress READ waveformRgbProgress
                   WRITE setWaveformRgbProgress NOTIFY waveformRgbProgressChanged)
    Q_PROPERTY(bool waveformHoverTimePreview READ waveformHoverTimePreview
                   WRITE setWaveformHoverTimePreview NOTIFY waveformHoverTimePreviewChanged)
    Q_PROPERTY(bool waveformPlaybackGuide READ waveformPlaybackGuide
                   WRITE setWaveformPlaybackGuide NOTIFY waveformPlaybackGuideChanged)
    Q_PROPERTY(int waveformCanvasHeight READ waveformCanvasHeight
                   WRITE setWaveformCanvasHeight NOTIFY waveformCanvasHeightChanged)
    Q_PROPERTY(bool waveformCanvasLocked READ waveformCanvasLocked
                   WRITE setWaveformCanvasLocked NOTIFY waveformCanvasLockedChanged)
    Q_PROPERTY(bool listWaveformThumbnailEnabled
                   READ listWaveformThumbnailEnabled
                   WRITE setListWaveformThumbnailEnabled
                   NOTIFY listWaveformThumbnailEnabledChanged)
    Q_PROPERTY(QString listWaveformThumbnailMode
                   READ listWaveformThumbnailMode
                   WRITE setListWaveformThumbnailMode
                   NOTIFY listWaveformThumbnailModeChanged)
    Q_PROPERTY(double trackWaveformBrightness
                   READ trackWaveformBrightness
                   WRITE setTrackWaveformBrightness
                   NOTIFY trackWaveformBrightnessChanged)
    Q_PROPERTY(int spectrumColorMode READ spectrumColorMode WRITE setSpectrumColorMode
                   NOTIFY spectrumColorModeChanged)
    Q_PROPERTY(QString spectrumSolidColor READ spectrumSolidColor
                   WRITE setSpectrumSolidColor NOTIFY spectrumSolidColorChanged)
    Q_PROPERTY(QString spectrumRgbStartColor READ spectrumRgbStartColor
                   WRITE setSpectrumRgbStartColor NOTIFY spectrumRgbStartColorChanged)
    Q_PROPERTY(QString spectrumRgbMiddleColor READ spectrumRgbMiddleColor
                   WRITE setSpectrumRgbMiddleColor NOTIFY spectrumRgbMiddleColorChanged)
    Q_PROPERTY(QString spectrumRgbEndColor READ spectrumRgbEndColor
                   WRITE setSpectrumRgbEndColor NOTIFY spectrumRgbEndColorChanged)
    Q_PROPERTY(int replayGainMode READ replayGainMode WRITE setReplayGainMode
                   NOTIFY replayGainModeChanged)
    Q_PROPERTY(bool replayGainClipProtection READ replayGainClipProtection
                   WRITE setReplayGainClipProtection NOTIFY replayGainClipProtectionChanged)

    // Audio Tools
    Q_PROPERTY(QString defaultOutputDirectory READ defaultOutputDirectory
                   WRITE setDefaultOutputDirectory NOTIFY defaultOutputDirectoryChanged)
    Q_PROPERTY(int parallelJobs READ parallelJobs WRITE setParallelJobs
                   NOTIFY parallelJobsChanged)
    Q_PROPERTY(int overwritePolicy READ overwritePolicy WRITE setOverwritePolicy
                   NOTIFY overwritePolicyChanged)
    Q_PROPERTY(QString transcodeFormat READ transcodeFormat WRITE setTranscodeFormat
                   NOTIFY transcodeFormatChanged)
    Q_PROPERTY(int transcodeBitrateKbps READ transcodeBitrateKbps
                   WRITE setTranscodeBitrateKbps NOTIFY transcodeBitrateKbpsChanged)
    Q_PROPERTY(int transcodeSampleRateHz READ transcodeSampleRateHz
                   WRITE setTranscodeSampleRateHz NOTIFY transcodeSampleRateHzChanged)
    Q_PROPERTY(int transcodeChannels READ transcodeChannels WRITE setTranscodeChannels
                   NOTIFY transcodeChannelsChanged)
    Q_PROPERTY(bool preserveMetadata READ preserveMetadata WRITE setPreserveMetadata
                   NOTIFY preserveMetadataChanged)
    Q_PROPERTY(bool preserveCover READ preserveCover WRITE setPreserveCover
                   NOTIFY preserveCoverChanged)
    Q_PROPERTY(bool preserveDirectoryStructure READ preserveDirectoryStructure
                   WRITE setPreserveDirectoryStructure
                   NOTIFY preserveDirectoryStructureChanged)
    Q_PROPERTY(bool extractVideoAudio READ extractVideoAudio WRITE setExtractVideoAudio
                   NOTIFY extractVideoAudioChanged)
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
    Q_PROPERTY(QVariantMap rollingKeyboardShortcuts READ rollingKeyboardShortcuts
                   NOTIFY rollingKeyboardShortcutsChanged)

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
    Q_PROPERTY(QObject* updateChecker READ updateChecker CONSTANT)
    Q_PROPERTY(QString releaseDate READ releaseDate CONSTANT)

public:
    enum PlayerShellMode {
        Classic = 0,
        Integrated = 1,
        Rolling = 2,
    };
    Q_ENUM(PlayerShellMode)

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
    QString systemIntegrationError() const;
    QString globalHotkeyError() const;

    // Playback & Engine getters
    QString outputDevice() const;
    bool exclusiveMode() const noexcept;
    bool matchTrackSampleRate() const noexcept;
    int transitionFadeMs() const noexcept;
    int defaultPlaybackMode() const noexcept;
    bool autoReadBpm() const noexcept;
    bool autoReadRating() const noexcept;

    // Appearance & Visualizer getters
    int themeMode() const noexcept;
    QString windowLayoutTheme() const;
    int playerShellMode() const noexcept;
    bool rollingBeatGridEnabled() const noexcept;
    int rollingBeatGridGrouping() const noexcept;
    int waveformMode() const noexcept;
    double waveformHeight() const noexcept;
    double waveformDensity() const noexcept;
    double waveformThickness() const noexcept;
    int waveformPeakAlgorithm() const noexcept;
    QString waveformSolidBaseColor() const;
    QString waveformSolidProgressColor() const;
    QString waveformRgbBaseColor() const;
    QString waveformRgbStartColor() const;
    QString waveformRgbMiddleColor() const;
    QString waveformRgbEndColor() const;
    FrequencyColorWaveformSettings* frequencyColorWaveform() const noexcept;
    bool waveformRgbProgress() const noexcept;
    bool waveformHoverTimePreview() const noexcept;
    bool waveformPlaybackGuide() const noexcept;
    int waveformCanvasHeight() const noexcept;
    bool waveformCanvasLocked() const noexcept;
    bool listWaveformThumbnailEnabled() const noexcept;
    QString listWaveformThumbnailMode() const;
    double trackWaveformBrightness() const noexcept;
    int spectrumColorMode() const noexcept;
    QString spectrumSolidColor() const;
    QString spectrumRgbStartColor() const;
    QString spectrumRgbMiddleColor() const;
    QString spectrumRgbEndColor() const;
    int replayGainMode() const noexcept;
    bool replayGainClipProtection() const noexcept;

    // Audio Tools getters
    QString defaultOutputDirectory() const;
    int parallelJobs() const noexcept;
    int overwritePolicy() const noexcept;
    QString transcodeFormat() const;
    int transcodeBitrateKbps() const noexcept;
    int transcodeSampleRateHz() const noexcept;
    int transcodeChannels() const noexcept;
    bool preserveMetadata() const noexcept;
    bool preserveCover() const noexcept;
    bool preserveDirectoryStructure() const noexcept;
    bool extractVideoAudio() const noexcept;
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
    QVariantMap rollingKeyboardShortcuts() const;

    // Cache & Storage getters
    QString cacheDirectory() const;
    bool autoCleanCache() const noexcept;
    bool cleanTempOnExit() const noexcept;
    int cacheSizeLimitMB() const noexcept;
    int currentCacheSizeMB() const noexcept;

    // About getters
    QString version() const;
    QObject* updateChecker() const;
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
    void setSystemIntegrationError(const QString& value);
    void setGlobalHotkeyError(const QString& value);

    // Playback & Engine setters
    void setOutputDevice(const QString& value);
    void setExclusiveMode(bool value);
    void setMatchTrackSampleRate(bool value);
    void setTransitionFadeMs(int value);
    void setDefaultPlaybackMode(int value);
    void setAutoReadBpm(bool value);
    void setAutoReadRating(bool value);

    // Appearance & Visualizer setters
    void setThemeMode(int value);
    void setWindowLayoutTheme(const QString& value);
    void setPlayerShellMode(int value);
    void setRollingBeatGridEnabled(bool value);
    void setRollingBeatGridGrouping(int value);
    void setWaveformMode(int value);
    void setWaveformHeight(double value);
    void setWaveformDensity(double value);
    void setWaveformThickness(double value);
    void setWaveformPeakAlgorithm(int value);
    void setWaveformSolidBaseColor(const QString& value);
    void setWaveformSolidProgressColor(const QString& value);
    void setWaveformRgbBaseColor(const QString& value);
    void setWaveformRgbStartColor(const QString& value);
    void setWaveformRgbMiddleColor(const QString& value);
    void setWaveformRgbEndColor(const QString& value);
    void setWaveformRgbProgress(bool value);
    void setWaveformHoverTimePreview(bool value);
    void setWaveformPlaybackGuide(bool value);
    void setWaveformCanvasHeight(int value);
    void setWaveformCanvasLocked(bool value);
    void setListWaveformThumbnailEnabled(bool value);
    void setListWaveformThumbnailMode(const QString& value);
    void setTrackWaveformBrightness(double value);
    void setSpectrumColorMode(int value);
    void setSpectrumSolidColor(const QString& value);
    void setSpectrumRgbStartColor(const QString& value);
    void setSpectrumRgbMiddleColor(const QString& value);
    void setSpectrumRgbEndColor(const QString& value);
    void setReplayGainMode(int value);
    void setReplayGainClipProtection(bool value);

    // Audio Tools setters
    void setDefaultOutputDirectory(const QString& value);
    void setParallelJobs(int value);
    void setOverwritePolicy(int value);
    void setTranscodeFormat(const QString& value);
    void setTranscodeBitrateKbps(int value);
    void setTranscodeSampleRateHz(int value);
    void setTranscodeChannels(int value);
    void setPreserveMetadata(bool value);
    void setPreserveCover(bool value);
    void setPreserveDirectoryStructure(bool value);
    void setExtractVideoAudio(bool value);
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
    Q_INVOKABLE void resetWaveformDefaults();
    Q_INVOKABLE void cycleWaveformMode();
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void commitEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE bool setRollingKeyboardShortcut(const QString& action,
                                                const QString& sequence);
    Q_INVOKABLE void resetRollingKeyboardShortcuts();
    Q_INVOKABLE void rebindFileAssociations();
    Q_INVOKABLE bool openDefaultAppsSettings();
    Q_INVOKABLE void clearWaveformCache();
    Q_INVOKABLE void clearCoverCache();
    Q_INVOKABLE void clearTempFiles();
    Q_INVOKABLE void clearAllCache();
    Q_INVOKABLE void openOfficialWebsite();
    Q_INVOKABLE void trimCacheNow();

    void onWaveformCacheSaved();

signals:
    void autoStartWithWindowsChanged();
    void restoreLastPlaybackOnStartupChanged();
    void showListWindowPanelChanged();
    void windowMagneticSnapChanged();
    void listWindowPositionChanged();
    void closeBehaviorChanged();
    void languageChanged();
    void setAsDefaultPlayerChanged();
    void fileAssociationsChanged();
    void systemIntegrationErrorChanged();
    void globalHotkeyErrorChanged();

    void outputDeviceChanged();
    void exclusiveModeChanged();
    void matchTrackSampleRateChanged();
    void transitionFadeMsChanged();
    void defaultPlaybackModeChanged();
    void autoReadBpmChanged();
    void autoReadRatingChanged();

    void themeModeChanged();
    void windowLayoutThemeChanged();
    void playerShellModeChanged();
    void rollingBeatGridEnabledChanged();
    void rollingBeatGridGroupingChanged();
    void waveformModeChanged();
    void waveformHeightChanged();
    void waveformDensityChanged();
    void waveformThicknessChanged();
    void waveformPeakAlgorithmChanged();
    void waveformSolidBaseColorChanged();
    void waveformSolidProgressColorChanged();
    void waveformRgbBaseColorChanged();
    void waveformRgbStartColorChanged();
    void waveformRgbMiddleColorChanged();
    void waveformRgbEndColorChanged();
    void waveformRgbProgressChanged();
    void waveformHoverTimePreviewChanged();
    void waveformPlaybackGuideChanged();
    void waveformCanvasHeightChanged();
    void waveformCanvasLockedChanged();
    void listWaveformThumbnailEnabledChanged();
    void listWaveformThumbnailModeChanged();
    void trackWaveformBrightnessChanged();
    void spectrumColorModeChanged();
    void spectrumSolidColorChanged();
    void spectrumRgbStartColorChanged();
    void spectrumRgbMiddleColorChanged();
    void spectrumRgbEndColorChanged();
    void replayGainModeChanged();
    void replayGainClipProtectionChanged();

    void defaultOutputDirectoryChanged();
    void parallelJobsChanged();
    void overwritePolicyChanged();
    void transcodeFormatChanged();
    void transcodeBitrateKbpsChanged();
    void transcodeSampleRateHzChanged();
    void transcodeChannelsChanged();
    void preserveMetadataChanged();
    void preserveCoverChanged();
    void preserveDirectoryStructureChanged();
    void extractVideoAudioChanged();
    void keepPitchWhileSpeedChangeChanged();
    void vocalProtectionChanged();

    void hkPlayPauseChanged();
    void hkPrevNextChanged();
    void hkVolumeUpDownChanged();
    void hkToggleMiniPlayerChanged();
    void hkSearchChanged();
    void hkWaveformModeChanged();
    void hkAudioToolsChanged();
    void rollingKeyboardShortcutsChanged();

    void cacheDirectoryChanged();
    void autoCleanCacheChanged();
    void cleanTempOnExitChanged();
    void cacheSizeLimitMBChanged();
    void currentCacheSizeMBChanged();
    void cacheTrimReport(qint64 bytesFreed, int filesRemoved);

private:
    UpdateChecker* updateChecker_ = nullptr;
    friend class SettingsControllerTest;

    void load();
    void saveAll(bool includeMediaSettings = true);
    void restoreDefaults(bool includeMediaSettings = true);
    void emitAllChanged(bool includeMediaSettings = true);
    void persistValue(const QString& key, const QVariant& value);
    bool conflictsWithLegacyKeyboardShortcuts(const QString& sequence) const;
    bool conflictsWithRollingKeyboardShortcuts(const QString& value) const;
    void applyAutoStartWithWindows();
    void applyCommittedEffects();
    void recalculateCacheSize();
    void enforceCacheSizeLimit();
    static qint64 directorySizeBytes(const QString& path);
    static QString defaultMusicDirectory();
    static QString resolveTestCacheDirectory(const QString& cacheLocation,
                                             const QString& tempLocation,
                                             const QString& appDataLocation);
    static QString defaultCacheDirectory();
    static QString defaultExportDir();
    static QString validatedLanguage(const QString& value);

    QSettings settings_;
    bool editActive_ = false;
    bool loading_ = false;
    std::unique_ptr<FileAssociationController> fileAssociationController_;
    std::unique_ptr<FrequencyColorWaveformSettings> frequencyColorWaveform_;

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
    QStringList fileAssociations_;
    QString systemIntegrationError_;
    QString globalHotkeyError_;

    // Playback & Engine
    QString outputDevice_;
    bool exclusiveMode_ = false;
    bool matchTrackSampleRate_ = true;
    int transitionFadeMs_ = 200;
    int defaultPlaybackMode_ = 3;
    bool autoReadBpm_ = true;
    bool autoReadRating_ = true;

    // Appearance & Visualizer
    int themeMode_ = 0;
    QString windowLayoutTheme_ = QStringLiteral("dual-window");
    int playerShellMode_ = Classic;
    bool rollingBeatGridEnabled_ = false;
    int rollingBeatGridGrouping_ = 4;
    int waveformMode_ = 0;
    double waveformHeight_ = 0.8;
    double waveformDensity_ = 2.0;
    double waveformThickness_ = 1.0;
    int waveformPeakAlgorithm_ = 0;
    QString waveformSolidBaseColor_ = QStringLiteral("#9098a6");
    QString waveformSolidProgressColor_ = QStringLiteral("#d27722");
    QString waveformRgbBaseColor_ = QStringLiteral("#00b4a0");
    QString waveformRgbStartColor_ = QStringLiteral("#00d4ff");
    QString waveformRgbMiddleColor_ = QStringLiteral("#7b2ff7");
    QString waveformRgbEndColor_ = QStringLiteral("#e62e9b");
    bool waveformRgbProgress_ = false;
    bool waveformHoverTimePreview_ = true;
    bool waveformPlaybackGuide_ = false;
    int waveformCanvasHeight_ = 78;
    bool waveformCanvasLocked_ = true;
    bool listWaveformThumbnailEnabled_ = true;
    QString listWaveformThumbnailMode_ = QStringLiteral("Spectral");
    double trackWaveformBrightness_ = 0.50;
    int spectrumColorMode_ = 1;
    QString spectrumSolidColor_ = QStringLiteral("#7b2ff7");
    QString spectrumRgbStartColor_ = QStringLiteral("#00d4ff");
    QString spectrumRgbMiddleColor_ = QStringLiteral("#7b2ff7");
    QString spectrumRgbEndColor_ = QStringLiteral("#e62e9b");
    int replayGainMode_ = 0;
    bool replayGainClipProtection_ = true;

    // Audio Tools
    QString defaultOutputDirectory_;
    int parallelJobs_ = 5;
    int overwritePolicy_ = 0;
    QString transcodeFormat_ = QStringLiteral("MP3");
    int transcodeBitrateKbps_ = 320;
    int transcodeSampleRateHz_ = 44100;
    int transcodeChannels_ = 2;
    bool preserveMetadata_ = true;
    bool preserveCover_ = true;
    bool preserveDirectoryStructure_ = true;
    bool extractVideoAudio_ = true;
    bool keepPitchWhileSpeedChange_ = true;
    bool vocalProtection_ = true;

    // Hotkeys
    QString hkPlayPause_ = QStringLiteral("MediaPlayPause");
    QString hkPrevNext_ = QStringLiteral("MediaPrevTrack / MediaNextTrack");
    QString hkVolumeUpDown_ = QStringLiteral("VolumeUp / VolumeDown");
    QString hkToggleMiniPlayer_ = QStringLiteral("Alt + P");
    QString hkSearch_ = QStringLiteral("Ctrl + F");
    QString hkWaveformMode_ = QStringLiteral("Tab");
    QString hkAudioTools_ = QStringLiteral("Alt + D");
    QVariantMap rollingKeyboardShortcuts_;

    // Cache & Storage
    QString cacheDirectory_;
    bool autoCleanCache_ = true;
    bool cleanTempOnExit_ = true;
    int cacheSizeLimitMB_ = 10 * 1024;
    int currentCacheSizeMB_ = 0;
};
