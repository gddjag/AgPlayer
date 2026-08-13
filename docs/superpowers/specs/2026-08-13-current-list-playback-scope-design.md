# Current-list Playback Scope Design

## Goal

Starting playback from a visible song list keeps automatic and manual navigation inside that list in Sequential, RepeatOne, Shuffle, and RepeatAll modes. Lists with fewer than five playable songs may continue into the rest of the library only after every song in the starting list has played.

## Design

- `TrackList` reads the visible track IDs from whichever model backs the list and starts playback with those IDs plus the selected track ID.
- `PlaybackController` rotates the visible list so the selected track is first, filters unavailable tracks, and appends the remaining library only when the visible list contains fewer than five playable tracks.
- `PlaybackSession` stores a queue-scope boundary. Navigation wraps inside scopes of five or more songs. A smaller scope unlocks its appended fallback only after the scope is exhausted; Shuffle tracks visited scope entries so it cannot escape early.
- RepeatOne never leaves the current song. Previous navigation from inside the scope also stays inside the scope.

## Verification

- Core tests cover all four playback modes at the 4/5-song boundary.
- Qt tests cover visible-list ordering, unavailable-track filtering, and fallback placement.
- Existing playback, library-filter, C API, and QML tests remain green.
