#include "qml_registration.hpp"

#include "audio_preview_controller.hpp"
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
#include "settings_controller.hpp"
#include "waveform_item.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <qqml.h>

void register_agplayer_qml_types(LibraryModel* library,
                                 PlaybackController* playback,
                                 ImportController* importer,
                                 WindowController* windows,
                                 AudioToolsController* audioTools,
                                 MetadataEditor* metadataEditor,
                                 FormatConverter* formatConverter,
                                 FilenameProcessor* filenameProcessor,
                                 LightEditor* lightEditor,
                                 SettingsController* settings,
                                 WaveformProvider* waveformProvider,
                                 PlaylistModel* playlistModel,
                                 EqualizerController* equalizer)
{
    static PlaylistModel fallbackPlaylistModel;
    static EqualizerController fallbackEqualizer(nullptr);
    PlaylistModel* const playlists = playlistModel != nullptr
        ? playlistModel : &fallbackPlaylistModel;
    qmlRegisterSingletonType<AudioPreviewController>(
        "AgPlayer", 1, 0, "AudioPreviewController",
        [playback](QQmlEngine*, QJSEngine*) -> QObject* {
            return new AudioPreviewController(
                AG_AUDIO_BACKEND_DEFAULT, playback);
        });
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", library);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaylistModel", playlists);
    qmlRegisterType<LibraryFilterModel>("AgPlayer", 1, 0, "LibraryFilterModel");
    qmlRegisterType<LibraryManagerController>("AgPlayer", 1, 0,
                                               "LibraryManagerController");
    qmlRegisterType<LibraryFileOperations>("AgPlayer", 1, 0,
                                           "LibraryFileOperations");
    qmlRegisterSingletonType<ReplayGainScanner>(
        "AgPlayer", 1, 0, "ReplayGainScanner",
        [library](QQmlEngine*, QJSEngine*) -> QObject* {
            return new ReplayGainScanner(library);
        });
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", playback);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", importer);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", windows);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "AudioToolsController", audioTools);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "MetadataEditor", metadataEditor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FormatConverter", formatConverter);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FilenameProcessor", filenameProcessor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LightEditor", lightEditor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SettingsController", settings);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "EqualizerController",
                                 equalizer != nullptr ? equalizer
                                                      : &fallbackEqualizer);
    if (waveformProvider != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WaveformProvider", waveformProvider);
    }
    qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
}
