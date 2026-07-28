#include "qml_registration.hpp"

#include "audio_preview_controller.hpp"
#include "audio_tools_controller.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_filter_model.hpp"
#include "library_model.hpp"
#include "light_editor_controller.hpp"
#include "metadata_editor.hpp"
#include "pitch_shifter.hpp"
#include "playback_controller.hpp"
#include "playlist_model.hpp"
#include "settings_controller.hpp"
#include "speed_adjuster.hpp"
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
                                 PitchShifter* pitchShifter,
                                 SpeedAdjuster* speedAdjuster,
                                 LightEditor* lightEditor,
                                 SettingsController* settings,
                                 WaveformProvider* waveformProvider,
                                 PlaylistModel* playlistModel)
{
    static PlaylistModel fallbackPlaylistModel;
    PlaylistModel* const playlists = playlistModel != nullptr
        ? playlistModel : &fallbackPlaylistModel;
    qmlRegisterSingletonType<AudioPreviewController>(
        "AgPlayer", 1, 0, "AudioPreviewController",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return new AudioPreviewController;
        });
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", library);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaylistModel", playlists);
    qmlRegisterType<LibraryFilterModel>("AgPlayer", 1, 0, "LibraryFilterModel");
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", playback);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", importer);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", windows);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "AudioToolsController", audioTools);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "MetadataEditor", metadataEditor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "FormatConverter", formatConverter);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PitchShifter", pitchShifter);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SpeedAdjuster", speedAdjuster);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LightEditor", lightEditor);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SettingsController", settings);
    if (waveformProvider != nullptr) {
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WaveformProvider", waveformProvider);
    }
    qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
}
