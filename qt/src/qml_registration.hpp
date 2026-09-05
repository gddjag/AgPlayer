#pragma once

class AudioToolsController;
class AudioVisualFeatureController;
class AudioEditorController;
class EqualizerController;
class FormatConverter;
class FilenameProcessor;
class ImportController;
class LibraryFilterModel;
class ResourceFolderController;
class LibraryNavigationModel;
class LibraryModel;
class LyricsService;
class MetadataEditor;
class AudioPreviewController;
class PlaybackController;
class PlayerExperienceController;
class PlaybackClipDragAdapter;
class PlaylistModel;
class SettingsController;
class TagModel;
class TrackWaveformThumbnailProvider;
class WaveformProvider;
class VocalSeparationController;
class VideoPlaybackController;
class WindowController;
class LosslessAnalysisController;

struct AgPlayerQmlRuntimeModels final {
    TagModel* tagModel = nullptr;
    LibraryNavigationModel* libraryNavigationModel = nullptr;
    ResourceFolderController* resourceFolderController = nullptr;
    TrackWaveformThumbnailProvider* trackWaveformThumbnailProvider = nullptr;
    PlaybackClipDragAdapter* playbackClipDragAdapter = nullptr;
    VideoPlaybackController* videoPlaybackController = nullptr;
    LosslessAnalysisController* losslessAnalysisController = nullptr;
};

// Registers all AgPlayer QML singletons and the WaveformItem type into the
// "AgPlayer" QML module (URI "AgPlayer", version 1.0).  Call this once from
// every entry point that loads QML from the AgPlayer module (main.cpp and
// QML test harnesses) so the registration stays in sync.
void register_agplayer_qml_types(LibraryModel* library,
                                 PlaybackController* playback,
                                 ImportController* importer,
                                 WindowController* windows,
                                 AudioToolsController* audioTools,
                                 MetadataEditor* metadataEditor,
                                 FormatConverter* formatConverter,
                                 FilenameProcessor* filenameProcessor,
                                 SettingsController* settings,
                                 WaveformProvider* waveformProvider = nullptr,
                                 PlaylistModel* playlistModel = nullptr,
                                 EqualizerController* equalizer = nullptr,
                                 AudioEditorController* audioEditor = nullptr,
                                 const AgPlayerQmlRuntimeModels& runtime = {},
                                 PlayerExperienceController* experience = nullptr,
                                 AudioVisualFeatureController* audioFeatures = nullptr,
                                 LyricsService* lyricsService = nullptr,
                                 AudioPreviewController* audioPreview = nullptr,
                                 VocalSeparationController* vocalSeparation = nullptr);
