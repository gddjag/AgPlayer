#include "track_waveform_thumbnail_provider.hpp"
#include "waveform_cache.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class TrackWaveformThumbnailStressTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsTenThousandLogicalRowsBoundedToVisibleRequests();
};

namespace {

std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

QString cachePath(const QString& directory, const QString& sourcePath)
{
    const std::string key =
        agplayer::WaveformCache::key_for(filesystemPath(sourcePath));
    return QDir(directory).filePath(
        QString::fromStdString(key) + QStringLiteral("-average.agwf"));
}

} // namespace

void TrackWaveformThumbnailStressTest::keepsTenThousandLogicalRowsBoundedToVisibleRequests()
{
    constexpr int logicalRowCount = 10'000;
    constexpr int visibleRowCount = 24;
    constexpr int scrollStep = 97;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("shared.wav"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("source-audio", 12), 12);
    source.close();

    const QString cacheDirectory = directory.filePath(QStringLiteral("cache"));
    QVERIFY(QDir().mkpath(cacheDirectory));
    agplayer::WaveformCacheData data;
    data.mix.assign(256U, 0.5F);
    data.bass.assign(256U, 0.25F);
    data.mid.assign(256U, 0.40F);
    data.high.assign(256U, 0.10F);
    const QString existingCache = cachePath(cacheDirectory, sourcePath);
    QVERIFY(agplayer::WaveformCache::save_v4(
        filesystemPath(existingCache), filesystemPath(sourcePath), data));
    QFile cacheFile(existingCache);
    QVERIFY(cacheFile.open(QIODevice::ReadOnly));
    const QByteArray cacheBefore = cacheFile.readAll();
    cacheFile.close();
    const QStringList filesBefore = QDir(cacheDirectory).entryList(QDir::Files);

    TrackWaveformThumbnailProvider provider(cacheDirectory);
    std::vector<std::pair<QString, quint64>> visible;
    for (int firstRow = 0; firstRow < logicalRowCount;
         firstRow += scrollStep) {
        for (const auto& previous : visible) {
            provider.cancel(previous.first, previous.second);
        }
        visible.clear();

        const int lastRow = std::min(firstRow + visibleRowCount,
                                     logicalRowCount);
        for (int row = firstRow; row < lastRow; ++row) {
            const QString trackId = QStringLiteral("logical-track-%1").arg(row);
            const quint64 generation = static_cast<quint64>(row + 1);
            provider.request(trackId, sourcePath, generation);
            visible.emplace_back(trackId, generation);
        }
    }

    const QVariantMap rapidScroll = provider.diagnostics();
    const int maxInFlight =
        rapidScroll.value(QStringLiteral("maxInFlightTracks"), -1).toInt();
    QVERIFY(maxInFlight >= 1);
    // Both bounded workers may still be finishing canceled filesystem reads
    // while the next viewport's visible rows are queued.
    QVERIFY(maxInFlight <= visibleRowCount + 2);
    QVERIFY(rapidScroll.value(QStringLiteral("queuedJobs")).toInt()
            <= visibleRowCount);
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.diagnostics().value(QStringLiteral("inFlightTracks")).toInt(),
        0, 10'000);

    std::vector<std::pair<QString, quint64>> burst;
    burst.reserve(logicalRowCount);
    for (int row = 0; row < logicalRowCount; ++row) {
        const QString trackId = QStringLiteral("burst-track-%1").arg(row);
        const quint64 generation = static_cast<quint64>(10'000 + row);
        provider.request(trackId, sourcePath, generation);
        burst.emplace_back(trackId, generation);
    }
    const QVariantMap burstState = provider.diagnostics();
    QVERIFY(burstState.value(QStringLiteral("queuedJobs")).toInt() <= 256);
    QVERIFY(burstState.value(QStringLiteral("inFlightTracks")).toInt() <= 257);
    QVERIFY(burstState.value(QStringLiteral("maxInFlightTracks")).toInt()
            <= 257);
    for (const auto& request : burst) {
        provider.cancel(request.first, request.second);
    }
    QTRY_COMPARE_WITH_TIMEOUT(
        provider.diagnostics().value(QStringLiteral("inFlightTracks")).toInt(),
        0, 10'000);

    QSignalSpy ready(&provider,
                     &TrackWaveformThumbnailProvider::thumbnailReady);
    for (int track = 0; track < 300; ++track) {
        provider.request(QStringLiteral("lru-track-%1").arg(track), sourcePath,
                         static_cast<quint64>(20'000 + track));
        if (ready.isEmpty()) {
            QVERIFY2(ready.wait(5000), "LRU stress request did not complete");
        }
        ready.takeFirst();
    }

    const QVariantMap completed = provider.diagnostics();
    QCOMPARE(completed.value(QStringLiteral("maxActiveWorkers")).toInt(), 2);
    QCOMPARE(completed.value(QStringLiteral("activeWorkers")).toInt(), 0);
    QVERIFY(completed.value(QStringLiteral("cacheEntries")).toInt() <= 256);
    QCOMPARE(completed.value(QStringLiteral("cacheEntries")).toInt(), 256);
    QCOMPARE(completed.value(QStringLiteral("inFlightTracks")).toInt(), 0);
    QCOMPARE(QDir(cacheDirectory).entryList(QDir::Files), filesBefore);
    QVERIFY(cacheFile.open(QIODevice::ReadOnly));
    QCOMPARE(cacheFile.readAll(), cacheBefore);
}

QTEST_GUILESS_MAIN(TrackWaveformThumbnailStressTest)

#include "track_waveform_thumbnail_stress_test.moc"
