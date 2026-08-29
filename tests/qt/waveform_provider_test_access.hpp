#pragma once

#include "waveform_provider.hpp"

#include <memory>

struct WaveformProviderTestAccess {
    static std::weak_ptr<void> activeResources(const WaveformProvider& provider)
    {
        return provider.activeResources_;
    }

    static void waitForAnalysis(WaveformProvider& provider)
    {
        provider.currentAnalysisPool_.waitForDone();
    }
};
