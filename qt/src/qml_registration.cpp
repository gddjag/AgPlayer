#include "qml_registration.hpp"

#include "audio_preview_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_editor_waveform_item.hpp"
#include "audio_editor/playback_clip_drag_adapter.hpp"
#include "equalizer_controller.hpp"
#include "audio_tools_controller.hpp"
#include "audio_visual_feature_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_filter_model.hpp"
#include "library_file_operations.hpp"
#include "library_model.hpp"
#include "lyrics_service.hpp"
#include "resource_folder_controller.hpp"
#include "library_navigation_model.hpp"
#include "metadata_editor.hpp"
#include "playback_controller.hpp"
#include "player_experience_controller.hpp"
#include "replay_gain_scanner.hpp"
#include "playlist_model.hpp"
#include "settings_controller.hpp"
#include "rolling_keyboard_handler.hpp"
#include "tag_model.hpp"
#include "tag_filter_model.hpp"
#include "terrain_reactor_item.hpp"
#include "track_waveform_thumbnail_item.hpp"
#include "track_waveform_thumbnail_provider.hpp"
#include "waveform_item.hpp"
#include "waveform_provider.hpp"
#include "vocal_separation_controller.hpp"
#include "video_playback_controller.hpp"
#include "video_frame_item.hpp"
#include "window_controller.hpp"
#include "lossless_analysis_controller.hpp"
#include "lossless_evidence_item.hpp"

#include <qqml.h>

void register_agplayer_qml_types(LibraryModel* library,
                                 PlaybackController* playback,
                                 ImportController* importer,
                                 WindowController* windows,
                                 AudioToolsController* audioTools,
                                 MetadataEditor* metadataEditor,
                                 FormatConverter* formatConverter,
                                 FilenameProcessor* filenameProcessor,
                                 SettingsController* settings,
                                 WaveformProvider* waveformProvider,
                                 PlaylistModel* playlistModel,
                                 EqualizerController* equalizer,
                                 AudioEditorController* audioEditor,
                                 const AgPlayerQmlRuntimeModels& runtime,
                                 PlayerExperienceController* experience,
                                 AudioVisualFeatureController* audioFeatures,
                                 LyricsService* lyricsService,
                                 AudioPreviewController* audioPreview,
                                 VocalSeparationController* vocalSeparation)
{
    static PlaylistModel fallbackPlaylistModel;
    qmlRegisterType<RollingKeyboardHandler>("AgPlayer", 1, 0, "RollingKeyboardHandler");
    static EqualizerController fallbackEqualizer(nullptr);
    PlaylistModel* const playlists = playlistModel != nullptr
        ? playlistModel : &fallbackPlaylistModel;
    if (runtime.losslessAnalysisController != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LosslessAnalysisController",
                                     runtime.losslessAnalysisController);
    } else {
        qmlRegisterSingletonType<LosslessAnalysisController>(
            "AgPlayer", 1, 0, "LosslessAnalysisController",
            [](QQmlEngine*, QJSEngine*) -> QObject* { return new LosslessAnalysisController(); });
    }
    qmlRegisterType<LosslessEvidenceItem>("AgPlayer", 1, 0, "LosslessEvidenceItem");
    if (audioPreview != nullptr) {
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "AudioPreviewController", audioPreview);
    } else {
        qmlRegisterSingletonType<AudioPreviewController>(
            "AgPlayer", 1, 0, "AudioPreviewController",
            [playback](QQmlEngine*, QJSEngine*) -> QObject* {
                return new AudioPreviewController(
                    AG_AUDIO_BACKEND_DEFAULT, playback);
            });
    }
    if (vocalSeparation != nullptr) {
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "VocalSeparationController", vocalSeparation);
    }
    if (audioEditor != nullptr) {
        if (playback != nullptr) audioEditor->setPlaybackController(playback);
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "AudioEditorController", audioEditor);
    } else {
        qmlRegisterSingletonType<AudioEditorController>(
            "AgPlayer", 1, 0, "AudioEditorController",
            [playback](QQmlEngine*, QJSEngine*) -> QObject* {
                auto* controller = new AudioEditorController();
                if (playback != nullptr) {
                    controller->setPlaybackController(playback);
                }
                return controller;
            });
    }
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", library);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaylistModel", playlists);
    qmlRegisterType<LibraryFilterModel>("AgPlayer", 1, 0, "LibraryFilterModel");
    qmlRegisterType<TagFilterModel>("AgPlayer", 1, 0, "TagFilterModel");
    qmlRegisterType<LibraryFileOperations>("AgPlayer", 1, 0,
                                           "LibraryFileOperations");
    qmlRegisterSingletonType<ReplayGainScanner>(
        "AgPlayer", 1, 0, "ReplayGainScanner",
        [library](QQmlEngine*, QJSEngine*) -> QObject* {
            return new ReplayGainScanner(library);
        });
    if (runtime.tagModel != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "TagModel",
                                     runtime.tagModel);
    }
    if (runtime.libraryNavigationModel != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "LibraryNavigationModel",
                                     runtime.libraryNavigationModel);
    }
    if (runtime.resourceFolderController != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "ResourceFolderController",
                                     runtime.resourceFolderController);
    }
    if (runtime.trackWaveformThumbnailProvider != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "TrackWaveformThumbnailProvider",
                                     runtime.trackWaveformThumbnailProvider);
    }
    if (runtime.playbackClipDragAdapter != nullptr) {
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "PlaybackClipDragAdapter",
            runtime.playbackClipDragAdapter);
    } else {
        qmlRegisterSingletonType<PlaybackClipDragAdapter>(
            "AgPlayer", 1, 0, "PlaybackClipDragAdapter",
            [library](QQmlEngine*, QJSEngine*) -> QObject* {
                return new PlaybackClipDragAdapter(library);
            });
    }
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", playback);
    if (runtime.videoPlaybackController != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "VideoPlaybackController",
                                     runtime.videoPlaybackController);
    }
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", importer);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", windows);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "AudioToolsController", audioTools);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "MetadataEditor", metadataEditor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FormatConverter", formatConverter);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FilenameProcessor", filenameProcessor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SettingsController", settings);
    if (lyricsService != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LyricsService", lyricsService);
    } else {
        qmlRegisterSingletonType<LyricsService>(
            "AgPlayer", 1, 0, "LyricsService",
            [library, playback, settings](QQmlEngine*, QJSEngine*) -> QObject* {
                return new LyricsService(library, playback, settings);
            });
    }
    if (experience != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "PlayerExperienceController", experience);
    } else {
        qmlRegisterSingletonType<PlayerExperienceController>(
            "AgPlayer", 1, 0, "PlayerExperienceController",
            [settings](QQmlEngine*, QJSEngine*) -> QObject* {
                return new PlayerExperienceController(settings);
            });
    }
    if (audioFeatures != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "AudioVisualFeatureController", audioFeatures);
    } else {
        qmlRegisterSingletonType<AudioVisualFeatureController>(
            "AgPlayer", 1, 0, "AudioVisualFeatureController",
            [playback](QQmlEngine*, QJSEngine*) -> QObject* {
                return new AudioVisualFeatureController(playback);
            });
    }
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "EqualizerController",
                                 equalizer != nullptr ? equalizer
                                                      : &fallbackEqualizer);
    if (waveformProvider != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WaveformProvider", waveformProvider);
    }
    qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
    qmlRegisterType<VideoFrameItem>("AgPlayer", 1, 0, "VideoFrameItem");
    qmlRegisterType<TrackWaveformThumbnailItem>(
        "AgPlayer", 1, 0, "TrackWaveformThumbnailItem");
    qmlRegisterType<TerrainReactorItem>("AgPlayer", 1, 0,
                                        "TerrainReactorItem");
    qmlRegisterType<AudioEditorWaveformItem>(
        "AgPlayer", 1, 0, "AudioEditorWaveformItem");
}
