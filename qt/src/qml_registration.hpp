#pragma once

class PlaybackController;
class WindowController;

void registerAgPlayerQmlTypes();
void registerAgPlayerQmlTypes(PlaybackController& playbackController,
                              WindowController& windowController);
