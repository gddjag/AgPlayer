#pragma once

class AudioToolsController;
class EqualizerController;
class FormatConverter;
class FilenameProcessor;
class ImportController;
class LibraryFilterModel;
class LibraryModel;
class LightEditor;
class MetadataEditor;
class PlaybackController;
class PlaylistModel;
class SettingsController;
class WaveformProvider;
class WindowController;

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
                                 LightEditor* lightEditor,
                                 SettingsController* settings,
                                 WaveformProvider* waveformProvider = nullptr,
                                 PlaylistModel* playlistModel = nullptr,
                                 EqualizerController* equalizer = nullptr);
