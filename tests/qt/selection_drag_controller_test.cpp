#include "audio_editor/selection_drag_controller.hpp"
#include "audio_editor/playback_clip_drag_adapter.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/audio_document.hpp"
#include "library_model.hpp"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <filesystem>

using agplayer::editor::AudioDocument;
using agplayer::editor::AudioFileAnalyzer;
using agplayer::editor::FadeCurve;
using agplayer::editor::Selection;

class SelectionDragControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void thresholdDefersRenderAndLaunchesExactlyOnce()
    {
        std::atomic_int prepares{0};
        std::atomic_int drags{0};
        SelectionDragController controller(
            [&prepares](const HandoffRequest&, const std::atomic_bool*) {
                ++prepares;
                return HandoffAssetResult{
                    true, QStringLiteral("C:/handoff.wav"),
                    QUrl::fromLocalFile(QStringLiteral("C:/handoff.wav")), {}};
            },
            [&drags](const QUrl& url) {
                QVERIFY(url.isLocalFile());
                ++drags;
            });
        HandoffRequest request;
        request.snapshot.revision = 1;
        request.selection = Selection{10, 20};
        request.sourceIdentity = QStringLiteral("fixture:1");
        request.timelineRevision = 1;
        request.renderState = {48'000, 1, 1.0F, false};

        const int threshold = QApplication::startDragDistance();
        controller.begin(QPointF(10, 10), request);
        controller.update(QPointF(10 + std::max(0, threshold - 1), 10));
        QTest::qWait(20);
        QCOMPARE(prepares.load(), 0);
        QCOMPARE(drags.load(), 0);

        controller.update(QPointF(10 + threshold, 10));
        QTRY_COMPARE_WITH_TIMEOUT(prepares.load(), 1, 3'000);
        QTRY_COMPARE_WITH_TIMEOUT(drags.load(), 1, 3'000);
        controller.update(QPointF(10 + threshold * 2, 10));
        QTest::qWait(20);
        QCOMPARE(prepares.load(), 1);
        QCOMPARE(drags.load(), 1);
    }

    void assetManagerCachesIdentityAndProducesVerifiedWavUri()
    {
        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        const auto analysis = AudioFileAnalyzer::analyze(
            std::filesystem::u8path(fixture.constData()), 256);
        QVERIFY2(analysis.success, analysis.message.c_str());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        HandoffAssetManager manager(directory.path());
        HandoffRequest request;
        request.snapshot = AudioDocument::fromSource(analysis.source)
            .timelineSnapshot();
        request.selection = Selection{0, std::min<qint64>(
            analysis.source.total_frames, 2'048)};
        request.sourceIdentity = QString::fromUtf8(fixture)
            + QStringLiteral(":")
            + QString::number(QFileInfo(QString::fromUtf8(fixture)).size());
        request.timelineRevision = request.snapshot.revision;
        request.renderState = {
            static_cast<int>(analysis.source.sample_rate),
            static_cast<int>(analysis.source.channels), 1.0F, false};

        const auto first = manager.prepare(request);
        QVERIFY2(first.success, qPrintable(first.error));
        QVERIFY(first.url.isValid());
        QVERIFY(first.url.isLocalFile());
        QCOMPARE(first.url.toLocalFile(), first.path);
        QFile output(first.path);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.read(4), QByteArray("RIFF", 4));

        const auto cached = manager.prepare(request);
        QVERIFY(cached.success);
        QCOMPARE(cached.path, first.path);
        ++request.timelineRevision;
        const auto revised = manager.prepare(request);
        QVERIFY2(revised.success, qPrintable(revised.error));
        QVERIFY(revised.path != first.path);
        QVERIFY(QFileInfo(revised.path).isFile());
    }

    void assetManagerUsesRequestedReadableStemAndShortHash()
    {
        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        const auto analysis = AudioFileAnalyzer::analyze(
            std::filesystem::u8path(fixture.constData()), 256);
        QVERIFY2(analysis.success, analysis.message.c_str());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        HandoffAssetManager manager(directory.path());
        HandoffRequest request;
        request.snapshot = AudioDocument::fromSource(analysis.source)
            .timelineSnapshot();
        request.selection = Selection{0, std::min<qint64>(
            analysis.source.total_frames, 2'048)};
        request.sourceIdentity = QString::fromUtf8(fixture);
        request.timelineRevision = request.snapshot.revision;
        request.renderState = {
            static_cast<int>(analysis.source.sample_rate),
            static_cast<int>(analysis.source.channels), 1.0F, false};
        request.outputFileStem = QStringLiteral("歌曲名_01m22.350-01m38.710");

        const auto result = manager.prepare(request);
        QVERIFY2(result.success, qPrintable(result.error));
        const QString name = QFileInfo(result.path).fileName();
        QVERIFY(name.startsWith(
            QStringLiteral("歌曲名_01m22.350-01m38.710_")));
        QVERIFY(QRegularExpression(
            QStringLiteral("_[0-9a-f]{8}\\.wav$"))
                    .match(name).hasMatch());
    }

    void playbackAdapterDefersProbeAndFileUntilThresholdThenExportsOneClip()
    {
        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        const QString sourcePath = QString::fromUtf8(fixture);
        const auto source = AudioFileAnalyzer::analyze(
            std::filesystem::u8path(fixture.constData()), 64);
        QVERIFY2(source.success, source.message.c_str());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        LibraryModel library;
        TrackRecord track;
        track.trackId = QStringLiteral("clip-track");
        track.path = sourcePath;
        track.title = QStringLiteral("歌/曲:名");
        track.sampleRate = static_cast<int>(source.source.sample_rate);
        track.durationMs = static_cast<qint64>(
            source.source.total_frames * 1000 / source.source.sample_rate);
        track.available = true;
        QVERIFY(library.append(track));

        QList<QUrl> drags;
        PlaybackClipDragAdapter adapter(
            &library, directory.path(),
            [&drags](const QUrl& url) { drags.append(url); });
        QSignalSpy ready(&adapter,
                         &PlaybackClipDragAdapter::handoffReady);
        const int threshold = QApplication::startDragDistance();
        QVERIFY(adapter.begin(10, 10, track.trackId, 100, 350));
        adapter.update(10 + std::max(0, threshold - 1), 10);
        QTest::qWait(30);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 0);
        QCOMPARE(drags.size(), 0);

        adapter.update(10 + threshold, 10);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10'000);
        QTRY_COMPARE_WITH_TIMEOUT(drags.size(), 1, 10'000);
        QVERIFY(drags.front().isLocalFile());
        QVERIFY(QFileInfo(drags.front().toLocalFile()).completeBaseName().startsWith(
            QStringLiteral("歌_曲_名_片段_00m00.100-00m00.350_")));
        QCOMPARE(QDir(directory.path()).entryList(
                     {QStringLiteral("*.wav")}, QDir::Files).size(), 1);

        const auto clip = AudioFileAnalyzer::analyze(
            std::filesystem::path(drags.front().toLocalFile().toStdWString()),
            64);
        QVERIFY2(clip.success, clip.message.c_str());
        QCOMPARE(clip.source.sample_rate, source.source.sample_rate);
        QCOMPARE(clip.source.channels, source.source.channels);
        const qint64 expectedFrames = static_cast<qint64>(
            source.source.sample_rate) * 250 / 1000;
        QVERIFY(std::abs(clip.source.total_frames - expectedFrames) <= 1);

        adapter.update(10 + threshold * 2, 10);
        QTest::qWait(30);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(drags.size(), 1);
    }

    void assetManagerUsesTimePitchPipelineAndKeysEveryRenderParameter()
    {
        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        const auto analysis = AudioFileAnalyzer::analyze(
            std::filesystem::u8path(fixture.constData()), 256);
        QVERIFY2(analysis.success, analysis.message.c_str());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        HandoffAssetManager manager(directory.path());
        HandoffRequest request;
        request.snapshot = AudioDocument::fromSource(analysis.source)
            .timelineSnapshot();
        request.selection = Selection{0, std::min<qint64>(
            analysis.source.total_frames, analysis.source.sample_rate)};
        request.sourceIdentity = QString::fromUtf8(fixture)
            + QStringLiteral(":")
            + QString::number(QFileInfo(QString::fromUtf8(fixture)).size());
        request.timelineRevision = request.snapshot.revision;
        request.renderState = {
            static_cast<int>(analysis.source.sample_rate),
            static_cast<int>(analysis.source.channels), 1.0F, false};

        const auto normal = manager.prepare(request);
        QVERIFY2(normal.success, qPrintable(normal.error));
        const auto normalAnalysis = AudioFileAnalyzer::analyze(
            std::filesystem::path(normal.path.toStdWString()), 64);
        QVERIFY2(normalAnalysis.success, normalAnalysis.message.c_str());

        request.renderState.speedPercent = 200.0;
        const auto fast = manager.prepare(request);
        QVERIFY2(fast.success, qPrintable(fast.error));
        QVERIFY(fast.path != normal.path);
        const auto fastAnalysis = AudioFileAnalyzer::analyze(
            std::filesystem::path(fast.path.toStdWString()), 64);
        QVERIFY2(fastAnalysis.success, fastAnalysis.message.c_str());
        QVERIFY2(fastAnalysis.source.total_frames
                     < normalAnalysis.source.total_frames * 3 / 4,
                 "handoff speed must affect rendered duration");

        request.renderState.pitchCents = 100;
        const auto pitched = manager.prepare(request);
        QVERIFY2(pitched.success, qPrintable(pitched.error));
        QVERIFY(pitched.path != fast.path);

        request.renderState.keepPitch = false;
        const auto coupled = manager.prepare(request);
        QVERIFY2(coupled.success, qPrintable(coupled.error));
        QVERIFY(coupled.path != pitched.path);

        request.renderState.formantPreservation = true;
        const auto formant = manager.prepare(request);
        QVERIFY2(formant.success, qPrintable(formant.error));
        QVERIFY(formant.path != coupled.path);
    }

    void cacheKeyUsesStableDocumentContentAcrossReopenedEdits()
    {
        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        const auto analysis = AudioFileAnalyzer::analyze(
            std::filesystem::u8path(fixture.constData()), 256);
        QVERIFY2(analysis.success, analysis.message.c_str());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        AudioDocument gainDocument = AudioDocument::fromSource(analysis.source);
        AudioDocument fadeDocument = AudioDocument::fromSource(analysis.source);
        const auto gainId = gainDocument.timelineSnapshot().events.front().id;
        const auto fadeId = fadeDocument.timelineSnapshot().events.front().id;
        QVERIFY(gainDocument.setEventGain(gainId, 0.25F));
        QVERIFY(fadeDocument.setEventFadeOut(fadeId, 16));
        const auto gainSnapshot = gainDocument.timelineSnapshot();
        const auto fadeSnapshot = fadeDocument.timelineSnapshot();
        QCOMPARE(gainSnapshot.revision, fadeSnapshot.revision);

        HandoffRequest request;
        request.selection = Selection{0, std::min<qint64>(
            analysis.source.total_frames, 2'048)};
        request.sourceIdentity = QString::fromUtf8(fixture)
            + QStringLiteral(":")
            + QString::number(QFileInfo(QString::fromUtf8(fixture)).size());
        request.timelineRevision = gainSnapshot.revision;
        request.renderState = {
            static_cast<int>(analysis.source.sample_rate),
            static_cast<int>(analysis.source.channels), 1.0F, false};
        HandoffAssetManager manager(directory.path());

        request.snapshot = gainSnapshot;
        const auto gain = manager.prepare(request);
        QVERIFY2(gain.success, qPrintable(gain.error));
        request.snapshot = fadeSnapshot;
        const auto fade = manager.prepare(request);
        QVERIFY2(fade.success, qPrintable(fade.error));
        QVERIFY2(gain.path != fade.path,
                 "same-revision reopened edits collided in the handoff cache");
        QFile gainFile(gain.path);
        QFile fadeFile(fade.path);
        QVERIFY(gainFile.open(QIODevice::ReadOnly));
        QVERIFY(fadeFile.open(QIODevice::ReadOnly));
        QVERIFY(gainFile.readAll() != fadeFile.readAll());
    }

    void cacheKeyIncludesFadeInAndFadeOutCurves()
    {
        const QByteArray fixture = qgetenv("AGPLAYER_EDITOR_FIXTURE");
        QVERIFY2(!fixture.isEmpty(), "AGPLAYER_EDITOR_FIXTURE is required");
        const auto analysis = AudioFileAnalyzer::analyze(
            std::filesystem::u8path(fixture.constData()), 256);
        QVERIFY2(analysis.success, analysis.message.c_str());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        HandoffRequest request;
        request.snapshot = AudioDocument::fromSource(analysis.source)
            .timelineSnapshot();
        auto& event = request.snapshot.events.front();
        const qint64 frames = std::min<qint64>(
            analysis.source.total_frames, 4'096);
        event.sourceEnd = event.sourceStart + frames;
        event.fadeIn = frames / 4;
        event.fadeOut = frames / 4;
        event.fadeInCurve = FadeCurve::Linear;
        event.fadeOutCurve = FadeCurve::Linear;
        request.snapshot.totalFrames = frames;
        request.selection = Selection{0, frames};
        request.sourceIdentity = QString::fromUtf8(fixture)
            + QStringLiteral(":")
            + QString::number(QFileInfo(QString::fromUtf8(fixture)).size());
        request.timelineRevision = request.snapshot.revision;
        request.renderState = {
            static_cast<int>(analysis.source.sample_rate),
            static_cast<int>(analysis.source.channels), 1.0F, false};
        HandoffAssetManager manager(directory.path());

        const auto linear = manager.prepare(request);
        QVERIFY2(linear.success, qPrintable(linear.error));

        request.snapshot.events.front().fadeInCurve = FadeCurve::Exponential;
        const auto exponentialIn = manager.prepare(request);
        QVERIFY2(exponentialIn.success, qPrintable(exponentialIn.error));
        QVERIFY2(exponentialIn.path != linear.path,
                 "fade-in curve collided in the handoff cache");

        request.snapshot.events.front().fadeInCurve = FadeCurve::Linear;
        request.snapshot.events.front().fadeOutCurve = FadeCurve::Exponential;
        const auto exponentialOut = manager.prepare(request);
        QVERIFY2(exponentialOut.success, qPrintable(exponentialOut.error));
        QVERIFY2(exponentialOut.path != linear.path,
                 "fade-out curve collided in the handoff cache");

        QFile linearFile(linear.path);
        QFile exponentialInFile(exponentialIn.path);
        QFile exponentialOutFile(exponentialOut.path);
        QVERIFY(linearFile.open(QIODevice::ReadOnly));
        QVERIFY(exponentialInFile.open(QIODevice::ReadOnly));
        QVERIFY(exponentialOutFile.open(QIODevice::ReadOnly));
        const QByteArray linearBytes = linearFile.readAll();
        QVERIFY(linearBytes != exponentialInFile.readAll());
        QVERIFY(linearBytes != exponentialOutFile.readAll());
    }

    void rebeginWhileOldPrepareFinishesKeepsNewestGesture()
    {
        QSemaphore firstStarted;
        QSemaphore releaseFirst;
        QSemaphore secondStarted;
        QSemaphore releaseSecond;
        std::atomic_int prepares{0};
        QList<QUrl> drags;
        SelectionDragController controller(
            [&](const HandoffRequest& request, const std::atomic_bool*) {
                const int prepare = ++prepares;
                if (prepare == 1) {
                    firstStarted.release();
                    releaseFirst.acquire();
                } else {
                    secondStarted.release();
                    releaseSecond.acquire();
                }
                const QString path = QStringLiteral("C:/handoff-%1.wav")
                    .arg(request.selection.start);
                return HandoffAssetResult{
                    true, path, QUrl::fromLocalFile(path), {}};
            },
            [&](const QUrl& url) { drags.append(url); });
        const auto cleanup = qScopeGuard([&] {
            releaseFirst.release();
            releaseSecond.release();
            controller.cancel();
        });
        HandoffRequest first;
        first.selection = Selection{10, 20};
        HandoffRequest second = first;
        second.selection = Selection{200, 220};
        const int threshold = QApplication::startDragDistance();

        controller.begin(QPointF(0, 0), first);
        controller.update(QPointF(threshold, 0));
        QVERIFY(firstStarted.tryAcquire(1, 3'000));
        controller.begin(QPointF(0, 0), second);
        controller.update(QPointF(threshold, 0));
        QCOMPARE(prepares.load(), 1);

        releaseFirst.release();
        QTRY_COMPARE_WITH_TIMEOUT(prepares.load(), 2, 3'000);
        QVERIFY(secondStarted.tryAcquire(1));
        QCOMPARE(drags.size(), 0);
        releaseSecond.release();
        QTRY_COMPARE_WITH_TIMEOUT(drags.size(), 1, 3'000);
        QCOMPARE(drags.front(),
                 QUrl::fromLocalFile(QStringLiteral("C:/handoff-200.wav")));
    }

    void pointerReleaseCancelsPendingPrepareBeforeLateDrag()
    {
        QSemaphore started;
        QSemaphore release;
        std::atomic_int drags{0};
        SelectionDragController controller(
            [&](const HandoffRequest&, const std::atomic_bool*) {
                started.release();
                release.acquire();
                return HandoffAssetResult{
                    true, QStringLiteral("C:/late.wav"),
                    QUrl::fromLocalFile(QStringLiteral("C:/late.wav")), {}};
            },
            [&](const QUrl&) { ++drags; });
        HandoffRequest request;
        request.selection = Selection{10, 20};
        controller.begin(QPointF(0, 0), request);
        controller.update(QPointF(QApplication::startDragDistance(), 0));
        QVERIFY(started.tryAcquire(1, 3'000));

        controller.release();
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.preparing(), 3'000);
        QCOMPARE(drags.load(), 0);
    }
};

QTEST_MAIN(SelectionDragControllerTest)
#include "selection_drag_controller_test.moc"
