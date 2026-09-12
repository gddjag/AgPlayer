#pragma once

#include "import_controller.hpp"

// Probes file metadata and optionally performs offline BPM analysis.
// Declared in a dedicated header so call sites (e.g. main.cpp) do not need
// to maintain a fragile forward declaration of the definition in
// import_controller.cpp.
ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm);
