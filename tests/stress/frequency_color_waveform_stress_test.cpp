#include "waveform_item.hpp"
#include "waveform_layer_material.hpp"

#include "frequency_color_waveform_analyzer.hpp"
#include "frequency_color_waveform_cache.hpp"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTest>
#include <QVariantMap>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

class StressWaveformItem final : public WaveformItem {
public:
    using WaveformItem::updatePaintNode;
};

namespace {

QVariantList makeBand(const int count, const double phase)
{
    QVariantList result;
    result.reserve(count);
    for (int index = 0; index < count; ++index) {
        const double ramp = static_cast<double>((index + static_cast<int>(phase)) % 97)
            / 96.0;
        result.append(0.1 + 0.8 * ramp);
    }
    return result;
}

QVariantMap makeFourLayers()
{
    QVariantMap layers;
    layers.insert(QStringLiteral("mix"), makeBand(2'000, 0.0));
    layers.insert(QStringLiteral("bass"), makeBand(2'000, 13.0));
    layers.insert(QStringLiteral("mid"), makeBand(2'000, 29.0));
    layers.insert(QStringLiteral("high"), makeBand(2'000, 47.0));
    layers.insert(QStringLiteral("totalSamples"), 48'000 * 600);
    layers.insert(QStringLiteral("sampleRate"), 48'000);
    layers.insert(QStringLiteral("peakCount"), 2'000);
    return layers;
}

std::size_t residentBytes()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) == FALSE) {
        return 0U;
    }
    return static_cast<std::size_t>(counters.WorkingSetSize);
#else
    return 0U;
#endif
}

std::uint64_t processCpuMilliseconds()
{
#if defined(Q_OS_WIN)
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)
        == FALSE) {
        return 0U;
    }
    const auto toTicks = [](const FILETIME& value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U)
            | static_cast<std::uint64_t>(value.dwLowDateTime);
    };
    return (toTicks(kernel) + toTicks(user)) / 10'000U;
#else
    return 0U;
#endif
}

class MeasurementTimerResolution final {
public:
    MeasurementTimerResolution()
    {
#if defined(Q_OS_WIN)
        module_ = LoadLibraryW(L"winmm.dll");
        if (module_ == nullptr) return;
        begin_ = reinterpret_cast<PeriodFunction>(
            GetProcAddress(module_, "timeBeginPeriod"));
        end_ = reinterpret_cast<PeriodFunction>(
            GetProcAddress(module_, "timeEndPeriod"));
        active_ = begin_ != nullptr && end_ != nullptr && begin_(1U) == 0U;
#endif
    }

    ~MeasurementTimerResolution()
    {
#if defined(Q_OS_WIN)
        if (active_) end_(1U);
        if (module_ != nullptr) FreeLibrary(module_);
#endif
    }

    MeasurementTimerResolution(const MeasurementTimerResolution&) = delete;
    MeasurementTimerResolution& operator=(const MeasurementTimerResolution&) = delete;

private:
#if defined(Q_OS_WIN)
    using PeriodFunction = unsigned int (WINAPI*)(unsigned int);
    HMODULE module_ = nullptr;
    PeriodFunction begin_ = nullptr;
    PeriodFunction end_ = nullptr;
    bool active_ = false;
#endif
};

std::filesystem::path fileSystemPath(const QString& path)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

agplayer::FrequencyColorCacheData cacheDataFrom(
    const agplayer::FrequencyColorWaveformData& waveform)
{
    agplayer::FrequencyColorCacheData cache;
    const auto quantize = [](const std::vector<float>& values) {
        std::vector<std::uint8_t> result;
        result.reserve(values.size());
        for (const float value : values) {
            result.push_back(agplayer::quantize_frequency_color_peak(value));
        }
        return result;
    };
    cache.low = quantize(waveform.low);
    cache.mid = quantize(waveform.mid);
    cache.high = quantize(waveform.high);
    cache.point_count = static_cast<std::uint32_t>(cache.low.size());
    cache.sample_rate = waveform.sample_rate;
    cache.timeline_frames = waveform.timeline_frames;
    cache.algorithm_version =
        agplayer::FrequencyColorWaveformAnalyzer::kAlgorithmVersion;
    return cache;
}

int runMeasurement(const QString& fixtureArgument, const QString& outputArgument,
                   const int progressDurationMs)
{
    if (progressDurationMs <= 0) {
        qCritical() << "frequency measurement duration must be positive";
        return 2;
    }
#if defined(Q_OS_WIN)
    if (SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) == FALSE
        || GetPriorityClass(GetCurrentProcess()) != HIGH_PRIORITY_CLASS) {
        qCritical() << "frequency measurement could not acquire High priority";
        return 2;
    }
#endif
    const std::filesystem::path fixture = fileSystemPath(fixtureArgument);
    const std::filesystem::path output = fileSystemPath(outputArgument);
    if (!std::filesystem::is_regular_file(fixture)
        || std::filesystem::exists(output)) {
        qCritical() << "frequency measurement input/output path is invalid";
        return 2;
    }
    std::error_code error;
    std::filesystem::create_directories(output.parent_path(), error);
    if (error) {
        qCritical() << "frequency measurement output directory cannot be created";
        return 2;
    }

    QJsonArray coldMilliseconds;
    QJsonArray cacheHitMilliseconds;
    QJsonArray decoderOpenCounts;
    QJsonArray cacheBytes;
    std::array<std::filesystem::path, 5> caches{};
    for (int run = 0; run < 5; ++run) {
        agplayer::FrequencyColorWaveformData waveform;
        agplayer::FrequencyColorAnalysisDiagnostics diagnostics;
        const auto started = std::chrono::steady_clock::now();
        const ag_result result = agplayer::FrequencyColorWaveformAnalyzer::analyze(
            fixture.string(), agplayer::FrequencyColorWaveformAnalyzer::kDefaultPointCount,
            nullptr, nullptr, nullptr, waveform, &diagnostics);
        const auto finished = std::chrono::steady_clock::now();
        if (result != AG_OK || diagnostics.decoder_open_count != 1U) {
            qCritical() << "frequency cold analysis did not use exactly one decoder open";
            return 3;
        }
        caches[static_cast<std::size_t>(run)] = output.parent_path()
            / ("frequency-cold-" + std::to_string(run) + ".fcw");
        if (!agplayer::FrequencyColorWaveformCache::save_atomic(
                caches[static_cast<std::size_t>(run)], fixture,
                cacheDataFrom(waveform))) {
            qCritical() << "frequency cache save failed";
            return 3;
        }
        coldMilliseconds.append(static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                finished - started).count()));
        decoderOpenCounts.append(static_cast<int>(diagnostics.decoder_open_count));
        const std::uintmax_t cacheSize = std::filesystem::file_size(
            caches[static_cast<std::size_t>(run)], error);
        cacheBytes.append(static_cast<qint64>(cacheSize));
        if (error || cacheSize >= 8192U) {
            qCritical() << "frequency cache exceeds the FCW size gate";
            return 3;
        }
    }
    for (const std::filesystem::path& cache : caches) {
        agplayer::FrequencyColorCacheData loaded;
        const auto started = std::chrono::steady_clock::now();
        const bool loadedCache = agplayer::FrequencyColorWaveformCache::load(
            cache, fixture,
            agplayer::FrequencyColorWaveformAnalyzer::kAlgorithmVersion, loaded);
        const auto finished = std::chrono::steady_clock::now();
        if (!loadedCache || loaded.point_count != 2'000U) {
            qCritical() << "frequency cache hit failed";
            return 3;
        }
        cacheHitMilliseconds.append(static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                finished - started).count()));
    }

    QQuickWindow window;
    StressWaveformItem item;
    item.setParentItem(window.contentItem());
    item.setWidth(3'840);
    item.setHeight(160);
    item.setDensity(2.0);
    item.setDuration(600'000);
    item.setVisualMode(3);
    item.setLayers(makeFourLayers());
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    if (node == nullptr) {
        qCritical() << "frequency progress measurement did not create geometry";
        return 4;
    }
    const auto* root = static_cast<const FrequencyWaveformRootNode*>(node);
    if (window.effectiveDevicePixelRatio() != 2.0 || !root->usesLineFallback()) {
        qCritical() << "frequency progress measurement did not use the DPR 2 bounded fallback";
        delete node;
        return 4;
    }
    const std::uint64_t geometryBefore = item.frequencyGeometryRevision();
    const std::size_t residentAfterWarmup = residentBytes();
    std::size_t peakResident = residentAfterWarmup;
    const MeasurementTimerResolution timerResolution;
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t requestedUpdates =
        (static_cast<std::uint64_t>(progressDurationMs) * 60U + 999U) / 1'000U;
    const std::uint64_t cpuBefore = processCpuMilliseconds();
    for (std::uint64_t updates = 0U; updates < requestedUpdates; ++updates) {
        item.setPosition(static_cast<qint64>(updates % 600'000U));
        node = item.updatePaintNode(node, nullptr);
        if (((updates + 1U) % 1'024U) == 0U) {
            peakResident = (std::max)(peakResident, residentBytes());
        }
        const std::uint64_t elapsedMicroseconds =
            (static_cast<std::uint64_t>(progressDurationMs) * 1'000U
             * (updates + 1U)) / requestedUpdates;
        std::this_thread::sleep_until(started + std::chrono::microseconds{
            static_cast<std::int64_t>(elapsedMicroseconds)});
    }
    peakResident = (std::max)(peakResident, residentBytes());
    const auto finished = std::chrono::steady_clock::now();
    const std::uint64_t cpuAfter = processCpuMilliseconds();
    const std::size_t residentAfterProgress = residentBytes();
    delete node;
    if (item.frequencyGeometryRevision() != geometryBefore) {
        qCritical() << "progress measurement rebuilt frequency geometry";
        return 4;
    }

    QJsonObject metrics;
    metrics.insert(QStringLiteral("coldAnalysisMilliseconds"), coldMilliseconds);
    metrics.insert(QStringLiteral("cacheHitMilliseconds"), cacheHitMilliseconds);
    metrics.insert(QStringLiteral("decoderOpenCounts"), decoderOpenCounts);
    metrics.insert(QStringLiteral("cacheBytes"), cacheBytes);
    metrics.insert(QStringLiteral("progressDurationMilliseconds"), static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(finished - started)
            .count()));
    metrics.insert(QStringLiteral("progressCadenceHz"), 60);
#if defined(Q_OS_WIN)
    metrics.insert(QStringLiteral("measurementPriorityClass"),
                   QStringLiteral("High"));
#endif
    metrics.insert(QStringLiteral("progressRequestedUpdates"),
                   static_cast<qint64>(requestedUpdates));
    metrics.insert(QStringLiteral("progressUpdates"),
                   static_cast<qint64>(requestedUpdates));
    metrics.insert(QStringLiteral("progressProcessCpuMilliseconds"),
                   static_cast<qint64>(cpuAfter - cpuBefore));
    metrics.insert(QStringLiteral("progressGeometryRevision"),
                   static_cast<qint64>(item.frequencyGeometryRevision()));
    metrics.insert(QStringLiteral("progressMaterialRevision"),
                   static_cast<qint64>(item.frequencyMaterialRevision()));
    metrics.insert(QStringLiteral("residentAfterWarmupBytes"),
                   static_cast<qint64>(residentAfterWarmup));
    metrics.insert(QStringLiteral("residentPeakBytes"),
                   static_cast<qint64>(peakResident));
    metrics.insert(QStringLiteral("residentAfterProgressBytes"),
                   static_cast<qint64>(residentAfterProgress));
    QFile metricsFile(outputArgument);
    if (!metricsFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || metricsFile.write(QJsonDocument(metrics).toJson(QJsonDocument::Compact))
            < 0) {
        qCritical() << "frequency measurement output write failed";
        return 5;
    }
    return 0;
}

} // namespace

class FrequencyColorWaveformStressTest final : public QObject {
    Q_OBJECT

private slots:
    void slowWindowDemotesExactlyOnce();
    void mixedIntervalsDoNotOscillate();
    void stableRunPromotesExactlyOnce();
    void progressKeepsFourLayerGeometryStatic();
    void resizeZoomStressSettlesResidentMemory();
};

void FrequencyColorWaveformStressTest::slowWindowDemotesExactlyOnce()
{
    FrequencyFramePressure pressure;
    for (int sample = 0; sample < 44; ++sample) {
        pressure.record(std::chrono::milliseconds{25});
    }
    QCOMPARE(pressure.quality_penalty, 0);
    pressure.record(std::chrono::milliseconds{25});
    QCOMPARE(pressure.quality_penalty, 1);
    for (int sample = 45; sample < 60; ++sample) {
        pressure.record(std::chrono::milliseconds{25});
    }
    QCOMPARE(pressure.quality_penalty, 1);
}

void FrequencyColorWaveformStressTest::mixedIntervalsDoNotOscillate()
{
    FrequencyFramePressure pressure;
    for (int cycle = 0; cycle < 8; ++cycle) {
        for (int sample = 0; sample < 5; ++sample) {
            pressure.record(std::chrono::milliseconds{25});
        }
        for (int sample = 0; sample < 4; ++sample) {
            pressure.record(std::chrono::milliseconds{16});
        }
    }
    QCOMPARE(pressure.quality_penalty, 0);
}

void FrequencyColorWaveformStressTest::stableRunPromotesExactlyOnce()
{
    FrequencyFramePressure pressure;
    for (int sample = 0; sample < 45; ++sample) {
        pressure.record(std::chrono::milliseconds{25});
    }
    QCOMPARE(pressure.quality_penalty, 1);
    for (int sample = 0; sample < 239; ++sample) {
        pressure.record(std::chrono::milliseconds{16});
    }
    QCOMPARE(pressure.quality_penalty, 1);
    pressure.record(std::chrono::milliseconds{16});
    QCOMPARE(pressure.quality_penalty, 0);
    for (int sample = 0; sample < 240; ++sample) {
        pressure.record(std::chrono::milliseconds{16});
    }
    QCOMPARE(pressure.quality_penalty, 0);
}

void FrequencyColorWaveformStressTest::progressKeepsFourLayerGeometryStatic()
{
    StressWaveformItem item;
    item.setWidth(3'840);
    item.setHeight(160);
    // This deterministic no-window fixture selects the triangle-material path.
    // Its density is doubled so it exercises the same 3,840 requested points
    // as a 3,840 logical-pixel item at DPR 2 and density 1.
    item.setDensity(2.0);
    item.setDuration(600'000);
    item.setVisualMode(3);
    item.setLayers(makeFourLayers());

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* root = static_cast<FrequencyWaveformRootNode*>(node);
    std::array<const void*, 4> vertexPointers{};
    for (int role = 0; role < 4; ++role) {
        const auto* layer = root->layer(
            static_cast<FrequencyWaveformNodeRole>(role))->geometry();
        QCOMPARE(layer->drawingMode(), QSGGeometry::DrawTriangles);
        QCOMPARE(layer->vertexCount(), 46'068);
        QCOMPARE(layer->indexCount(), 69'102);
        vertexPointers[static_cast<std::size_t>(role)] = layer->vertexData();
    }
    const std::uint64_t geometryRevision = item.frequencyGeometryRevision();
    const std::uint64_t materialRevision = item.frequencyMaterialRevision();

    for (int update = 1; update <= 10'000; ++update) {
        item.setPosition(update % 600'000);
        node = item.updatePaintNode(node, nullptr);
    }

    QCOMPARE(item.frequencyGeometryRevision(), geometryRevision);
    QVERIFY(item.frequencyMaterialRevision() > materialRevision);
    root = static_cast<FrequencyWaveformRootNode*>(node);
    for (int role = 0; role < 4; ++role) {
        const auto* geometry = root->layer(
            static_cast<FrequencyWaveformNodeRole>(role))->geometry();
        QCOMPARE(geometry->vertexData(), vertexPointers[static_cast<std::size_t>(role)]);
    }
    delete node;
}

void FrequencyColorWaveformStressTest::resizeZoomStressSettlesResidentMemory()
{
    QQuickWindow window;
    QCOMPARE(window.effectiveDevicePixelRatio(), 2.0);
    StressWaveformItem item;
    item.setParentItem(window.contentItem());
    item.setWidth(3'840);
    item.setHeight(160);
    item.setDuration(600'000);
    item.setVisualMode(3);
    item.setLayers(makeFourLayers());

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* root = static_cast<FrequencyWaveformRootNode*>(node);
    QVERIFY(root->usesLineFallback());
    for (int role = 0; role < 4; ++role) {
        const auto* geometry = root->layer(
            static_cast<FrequencyWaveformNodeRole>(role))->geometry();
        QCOMPARE(geometry->drawingMode(), QSGGeometry::DrawLines);
        QCOMPARE(geometry->vertexCount(), 1'024);
    }
    for (int warmup = 0; warmup < 200; ++warmup) {
        item.setWidth(3'840 - (warmup & 1));
        item.setVisibleRange(warmup % 500, 600'000 - warmup % 500);
        node = item.updatePaintNode(node, nullptr);
    }
    const std::size_t residentAfterWarmup = residentBytes();
    QVERIFY(residentAfterWarmup > 0U);

    for (int update = 0; update < 1'000; ++update) {
        item.setWidth(3'840 - (update & 1));
        const qint64 inset = update % 10'000;
        item.setVisibleRange(inset, 600'000 - inset);
        node = item.updatePaintNode(node, nullptr);
    }
    const std::size_t residentAfterStress = residentBytes();
    const std::size_t growth = residentAfterStress > residentAfterWarmup
        ? residentAfterStress - residentAfterWarmup : 0U;
    qInfo() << "frequency-stress-resident-growth-bytes" << growth;
    QVERIFY2(growth <= 8U * 1024U * 1024U,
             qPrintable(QStringLiteral("resident growth %1 exceeds 8 MiB")
                            .arg(growth)));
    delete node;
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (argc == 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--measure")) {
        bool validDuration = false;
        const int duration = QString::fromLocal8Bit(argv[4]).toInt(&validDuration);
        return validDuration ? runMeasurement(QString::fromLocal8Bit(argv[2]),
                                               QString::fromLocal8Bit(argv[3]),
                                               duration)
                             : 2;
    }
    FrequencyColorWaveformStressTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "frequency_color_waveform_stress_test.moc"
