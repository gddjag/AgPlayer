#pragma once

class ImportController;
class LibraryModel;
class PlaybackController;
class WindowController;

// Registers all AgPlayer QML singletons and the WaveformItem type into the
// "AgPlayer" QML module (URI "AgPlayer", version 1.0).  Call this once from
// every entry point that loads QML from the AgPlayer module (main.cpp and
// QML test harnesses) so the registration stays in sync.
void register_agplayer_qml_types(LibraryModel* library,
                                 PlaybackController* playback,
                                 ImportController* importer,
                                 WindowController* windows);
