#include "qml_registration.hpp"

#include "equalizer_controller.hpp"
#include "audio_preview_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_editor_waveform_item.hpp"
#include "equalizer_controller.hpp"
#include "audio_tools_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_filter_model.hpp"
#include "library_file_operations.hpp"
#include "library_model.hpp"
#include "library_manager_controller.hpp"
#include "light_editor_controller.hpp"
#include "metadata_editor.hpp"
#include "playback_controller.hpp"
#include "replay_gain_scanner.hpp"
#include "playlist_model.hpp"
#include "plugin_install_manager.hpp"
#include "settings_controller.hpp"
#include "waveform_item.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"
#include "voice_clone/voice_clone_host_controller.hpp"

#include <QCoreApplication>
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
                                 EqualizerController* equalizer)
{
    static PlaylistModel fallbackPlaylistModel;
    static EqualizerController fallbackEqualizer(nullptr);
    static VoiceCloneHostController* voiceCloneHost =
        new VoiceCloneHostController(QCoreApplication::instance());
    PlaylistModel* const playlists = playlistModel != nullptr
        ? playlistModel : &fallbackPlaylistModel;
    qmlRegisterSingletonType<AudioPreviewController>(
        "AgPlayer", 1, 0, "AudioPreviewController",
        [playback](QQmlEngine*, QJSEngine*) -> QObject* {
            return new AudioPreviewController(
                AG_AUDIO_BACKEND_DEFAULT, playback);
        });
    if (audioEditor != nullptr) {
        qmlRegisterSingletonInstance(
            "AgPlayer", 1, 0, "AudioEditorController", audioEditor);
    } else {
        qmlRegisterSingletonType<AudioEditorController>(
            "AgPlayer", 1, 0, "AudioEditorController",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new AudioEditorController();
            });
    }
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", library);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaylistModel", playlists);
    qmlRegisterType<LibraryFilterModel>("AgPlayer", 1, 0, "LibraryFilterModel");
    qmlRegisterType<LibraryManagerController>("AgPlayer", 1, 0,
                                               "LibraryManagerController");
    qmlRegisterType<LibraryFileOperations>("AgPlayer", 1, 0,
                                           "LibraryFileOperations");
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", playback);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", importer);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", windows);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "AudioToolsController", audioTools);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "MetadataEditor", metadataEditor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FormatConverter", formatConverter);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FilenameProcessor", filenameProcessor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SettingsController", settings);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "EqualizerController",
                                 equalizer != nullptr ? equalizer
                                                      : &fallbackEqualizer);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                 "VoiceCloneHostController", voiceCloneHost);
    if (waveformProvider != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WaveformProvider", waveformProvider);
    }
    qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
    qmlRegisterType<AudioEditorWaveformItem>(
        "AgPlayer", 1, 0, "AudioEditorWaveformItem");
    if (pluginInstallManager != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PluginInstallManager",
                                     pluginInstallManager);
    } else {
        qmlRegisterSingletonType<PluginInstallManager>(
            "AgPlayer", 1, 0, "PluginInstallManager",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new PluginInstallManager();
            });
    }
}
