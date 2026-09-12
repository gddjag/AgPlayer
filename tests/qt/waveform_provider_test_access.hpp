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

    static void publishPendingPreview(WaveformProvider& provider)
    {
        provider.onProgressTimer();
    }

    static void failAfterPreview(WaveformProvider& provider)
    {
        provider.currentPath_ = QStringLiteral("broken.wav");
        provider.currentTrackId_ = QStringLiteral("broken");
        provider.currentLayers_ = {{QStringLiteral("_complete"), false},
            {QStringLiteral("mix"), QVariantList{0.5}}};
        WaveformProvider::Job job;
        job.path = provider.currentPath_;
        job.trackId = provider.currentTrackId_;
        job.generation = provider.activeGeneration_;
        job.aggregation = provider.currentAggregation_;
        job.result = AG_DECODE_ERROR;
        QFutureInterface<WaveformProvider::Job> future;
        future.reportStarted();
        future.reportResult(job);
        future.reportFinished();
        provider.watcher_ = new QFutureWatcher<WaveformProvider::Job>(&provider);
        provider.watcher_->setFuture(future.future());
        provider.onAnalysisFinished();
    }
};
