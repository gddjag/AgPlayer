#include "import_controller.hpp"
#include "library_model.hpp"
#include "metadata_text.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <memory>

#include "bpm_analyzer.hpp"
#include "bpm_fixture.hpp"

class ImportControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void deduplicatesCanonicalPathsAndContinuesAfterFailure();
    void productionProbeImportsMetadataAndUsesBrandFallback();
    void productionProbePreservesAudioAndVideoKinds();
    void modelCanBeDestroyedWhileProbeIsBlocked();
    void controllerCanBeDestroyedWhileProbeIsBlocked();
    void rejectsModelWithDifferentThreadAffinity();
    void writesBpmWhenAutoReadEnabled();
    void productionProbePrefersValidEmbeddedBpm();
    void leavesBpmZeroWhenAutoReadDisabled();
    void probeObservesDynamicAnalyzeBpmFlag();
    void importsSupportedAudioRecursivelyFromFolder();
    void droppedFolderAndChinesePathAreExpanded();
    void folderDiscoveryRunsOffModelThreadAndReturnsImmediately();
    void importsEightyThreeTracksProgressivelyWithinBudget();
    void probesFilesWithBoundedParallelism();
    void batchesModelNotificationsForLargeImports();
    void errorsCanBeDismissedWithoutStartingAnotherImport();
    void queuesDropsReceivedWhileAnImportIsBusy();
    void importsTenThousandLightweightRecordsWithinBudget();
    void performanceRealImportResponsiveness();
    void alreadyImportedTracksAreSkippedWithoutFalseSuccess();
    void cachedDuplicateRemovedDuringDiscoveryIsNotReinserted();
    void importedTracksAppearFirstInDiscoveryOrder();
    void metadataDecoderPreservesUtf8AndUsesCp936Fallback();
    void metadataDecoderRejectsAmbiguousMojibakeRepair();
};

namespace {
void createFile(const QString& path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("audio"), 5);
}
}

void ImportControllerTest::metadataDecoderPreservesUtf8AndUsesCp936Fallback()
{
    const QByteArray utf8 = QStringLiteral("Björk 音乐").toUtf8();
    QCOMPARE(agplayer::qt::decodeMetadataText(utf8.constData()),
             QStringLiteral("Björk 音乐"));
    const QByteArray cp936("\xD6\xD0\xCE\xC4", 4);
    QCOMPARE(agplayer::qt::decodeMetadataText(cp936.constData()),
             QStringLiteral("中文"));
}

void ImportControllerTest::metadataDecoderRejectsAmbiguousMojibakeRepair()
{
    // These bytes are valid UTF-8 and can be deliberate literal text.  The
    // decoder must not guess that they were intended to mean a different word.
    const QByteArray literal = QString::fromUtf8("Ã©").toUtf8();
    QCOMPARE(agplayer::qt::decodeMetadataText(literal.constData()),
             QString::fromUtf8("Ã©"));
}

void ImportControllerTest::deduplicatesCanonicalPathsAndContinuesAfterFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));
    const QString goodPath = dir.filePath(QStringLiteral("good.wav"));
    const QString duplicatePath = dir.filePath(QStringLiteral("sub/../good.wav"));
    const QString badPath = dir.filePath(QStringLiteral("bad.wav"));
    createFile(goodPath);
    createFile(badPath);

    std::atomic_bool probeRanOffModelThread{false};
    std::atomic_int probeCalls{0};
    LibraryModel model;
    const ProbeFunction probe = [&probeRanOffModelThread, &probeCalls, &model](const QString& path) {
        probeRanOffModelThread.store(QThread::currentThread() != model.thread());
        ++probeCalls;
        if (path.endsWith(QStringLiteral("good.wav"))) {
            TrackRecord track;
            track.path = path;
            track.title = QStringLiteral("Good");
            track.available = true;
            return ProbeResult{AG_OK, track, {}};
        }
        return ProbeResult{AG_DECODE_ERROR, {}, QStringLiteral("decode failed")};
    };
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(goodPath),
                         QUrl::fromLocalFile(duplicatePath),
                         QUrl::fromLocalFile(badPath)});

    QVERIFY(finished.wait(3000));
    QVERIFY(probeRanOffModelThread.load());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.tracks().front().title, QStringLiteral("Good"));
    QVERIFY(!model.tracks().front().trackId.isEmpty());
    QCOMPARE(importer.errors().size(), 1);
    QVERIFY(importer.errors().front().contains(QStringLiteral("decode failed")));
    QCOMPARE(importer.progress(), 1.0);
    QVERIFY(!importer.busy());
    QCOMPARE(importer.importedTrackIds().size(), 1);
    QCOMPARE(importer.importedTrackIds().front(),
             model.tracks().front().trackId);

    finished.clear();
    importer.importPaths({goodPath});
    QVERIFY(finished.wait(3000));
    QCOMPARE(probeCalls.load(), 2); // Existing canonical track reuses its metadata.
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(importer.importedTrackIds().size(), 1);
    QCOMPARE(importer.importedTrackIds().front(),
             model.tracks().front().trackId);
}

void ImportControllerTest::importsSupportedAudioRecursivelyFromFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("nested"))));

    const QString rootTrack = dir.filePath(QStringLiteral("root.MP3"));
    const QString nestedTrack = dir.filePath(QStringLiteral("nested/child.flac"));
    const QString ignoredFile = dir.filePath(QStringLiteral("notes.txt"));
    const QString ignoredVideo = dir.filePath(QStringLiteral("clip.avi"));
    createFile(rootTrack);
    createFile(nestedTrack);
    createFile(ignoredFile);
    createFile(ignoredVideo);

    QStringList probedPaths;
    LibraryModel model;
    ImportController importer(&model, [&probedPaths](const QString& path) {
        probedPaths.append(path);
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    });
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importFolder(QUrl::fromLocalFile(dir.path()));

    QVERIFY(finished.wait(3000));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(probedPaths.size(), 2);
    QVERIFY(probedPaths.contains(canonicalLibraryPath(rootTrack)));
    QVERIFY(probedPaths.contains(canonicalLibraryPath(nestedTrack)));
    QVERIFY(!probedPaths.contains(canonicalLibraryPath(ignoredFile)));
    QVERIFY(!probedPaths.contains(canonicalLibraryPath(ignoredVideo)));
}

void ImportControllerTest::droppedFolderAndChinesePathAreExpanded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath(QStringLiteral("中文歌单"));
    QVERIFY(QDir().mkpath(folder));
    const QString trackPath = QDir(folder).filePath(QStringLiteral("测试歌曲.wav"));
    createFile(trackPath);

    LibraryModel model;
    ImportController importer(&model, [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    });
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(folder)});

    QVERIFY(finished.wait(3000));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.tracks().front().path, canonicalLibraryPath(trackPath));
}

void ImportControllerTest::folderDiscoveryRunsOffModelThreadAndReturnsImmediately()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString trackPath = dir.filePath(QStringLiteral("background.wav"));
    createFile(trackPath);

    QSemaphore discoveryEntered;
    QSemaphore releaseDiscovery;
    std::atomic_bool discoveryRanOffModelThread{false};
    LibraryModel model;
    const DiscoveryFunction discovery =
        [&discoveryEntered, &releaseDiscovery, &discoveryRanOffModelThread,
         &model, trackPath](const QList<QUrl>&) {
            discoveryRanOffModelThread.store(
                QThread::currentThread() != model.thread());
            discoveryEntered.release();
            releaseDiscovery.acquire();
            return QStringList{trackPath};
        };
    const ProbeFunction probe = [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.title = QStringLiteral("Background");
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(&model, probe, discovery);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(dir.path())});

    QVERIFY2(discoveryEntered.tryAcquire(1, 1000),
             "importUrls must return while discovery continues off-thread");
    QVERIFY(importer.busy());
    QVERIFY(discoveryRanOffModelThread.load());
    releaseDiscovery.release();
    QVERIFY(finished.wait(3000));
    QCOMPARE(model.rowCount(), 1);
}

void ImportControllerTest::importsEightyThreeTracksProgressivelyWithinBudget()
{
    const QString fixture = QString::fromLocal8Bit(qgetenv("AGPLAYER_TEST_AUDIO"));
    QVERIFY2(!fixture.isEmpty(), "AGPLAYER_TEST_AUDIO must name the generated WAV fixture");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int index = 0; index < 83; ++index) {
        const QString path = dir.filePath(QStringLiteral("track-%1.wav").arg(index, 2, 10,
                                                                              QLatin1Char('0')));
        QVERIFY2(QFile::copy(fixture, path), qPrintable(path));
    }

    LibraryModel model;
    ImportController importer(&model);
    QSignalSpy finished(&importer, &ImportController::finished);
    QElapsedTimer timer;
    timer.start();

    importer.importFolder(QUrl::fromLocalFile(dir.path()));

    QTRY_VERIFY_WITH_TIMEOUT(model.rowCount() > 0, 1000);
    QVERIFY2(timer.elapsed() <= 1000, "the first imported track must be visible within one second");
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(model.rowCount(), 83);
    QVERIFY2(timer.elapsed() <= 5000, "83-track import must finish within five seconds");
}

void ImportControllerTest::probesFilesWithBoundedParallelism()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int index = 0; index < 12; ++index) {
        createFile(dir.filePath(QStringLiteral("parallel-%1.wav").arg(index)));
    }

    std::atomic_int active{0};
    std::atomic_int maximum{0};
    const ProbeFunction probe = [&active, &maximum](const QString& path) {
        const int now = active.fetch_add(1) + 1;
        int observed = maximum.load();
        while (observed < now
               && !maximum.compare_exchange_weak(observed, now)) {
        }
        QThread::msleep(80);
        active.fetch_sub(1);

        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };

    LibraryModel model;
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);
    importer.importFolder(QUrl::fromLocalFile(dir.path()));

    QVERIFY(finished.wait(5000));
    QCOMPARE(model.rowCount(), 12);
    QVERIFY2(maximum.load() >= 2, "metadata probes must overlap");
    QVERIFY2(maximum.load() <= 4, "metadata probing must be capped at four workers");
}

void ImportControllerTest::batchesModelNotificationsForLargeImports()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList paths;
    for (int index = 0; index < 240; ++index) {
        const QString path = dir.filePath(QStringLiteral("batch-%1.wav").arg(index));
        createFile(path);
        paths.append(path);
    }

    const ProbeFunction probe = [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, std::move(track), {}};
    };
    LibraryModel model;
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);
    QSignalSpy rowsInserted(&model, &QAbstractItemModel::rowsInserted);

    importer.importPaths(paths);

    QVERIFY(finished.wait(5000));
    QCOMPARE(model.rowCount(), 240);
    QVERIFY2(rowsInserted.count() <= 8,
             "large imports must update the model in bounded batches");
}

void ImportControllerTest::errorsCanBeDismissedWithoutStartingAnotherImport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.wav"));
    createFile(path);

    LibraryModel model;
    ImportController importer(
        &model, [](const QString&) {
            return ProbeResult{AG_DECODE_ERROR, {}, QStringLiteral("decode failed")};
        });
    QSignalSpy finished(&importer, &ImportController::finished);
    QSignalSpy errorsChanged(&importer, &ImportController::errorsChanged);
    importer.importPaths({path});
    QVERIFY(finished.wait(3000));
    QVERIFY(!importer.errors().isEmpty());

    importer.clearErrors();

    QVERIFY(importer.errors().isEmpty());
    QVERIFY(errorsChanged.count() >= 2);
}

void ImportControllerTest::productionProbeImportsMetadataAndUsesBrandFallback()
{
    const QString fixture = QString::fromLocal8Bit(qgetenv("AGPLAYER_TEST_AUDIO"));
    QVERIFY2(!fixture.isEmpty(), "AGPLAYER_TEST_AUDIO must name the generated WAV fixture");
    LibraryModel model;
    ImportController importer(&model);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(fixture)});

    QVERIFY(finished.wait(3000));
    QCOMPARE(importer.errors().size(), 0);
    QCOMPARE(model.rowCount(), 1);
    const TrackRecord& track = model.tracks().front();
    QCOMPARE(track.format, QStringLiteral("wav"));
    QVERIFY(track.sampleRate > 0);
    QVERIFY(track.bitDepth > 0);
    QVERIFY(track.channels > 0);
    QVERIFY(track.bitRate > 0);
    QVERIFY(track.metadataProbeAttempted);
    QVERIFY(track.durationMs > 0);
    QVERIFY(track.fileSize > 0);
    QCOMPARE(track.coverUrl,
             QUrl(QStringLiteral("qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png")));
}

void ImportControllerTest::productionProbePreservesAudioAndVideoKinds()
{
    const QString audioFixture = QString::fromUtf8(AGPLAYER_TEST_AUDIO);
    const QString videoOnlyFixture = QString::fromUtf8(AGPLAYER_TEST_VIDEO_ONLY);
    const QString attachedPictureFixture =
        QString::fromUtf8(AGPLAYER_TEST_AUDIO_WITH_ATTACHED_PICTURE);
    QVERIFY(!audioFixture.isEmpty());
    QVERIFY(!videoOnlyFixture.isEmpty());
    QVERIFY(!attachedPictureFixture.isEmpty());

    LibraryModel model;
    ImportController importer(&model);
    QSignalSpy finished(&importer, &ImportController::finished);
    importer.importUrls({QUrl::fromLocalFile(audioFixture),
                         QUrl::fromLocalFile(videoOnlyFixture),
                         QUrl::fromLocalFile(attachedPictureFixture)});

    QVERIFY(finished.wait(5000));
    QCOMPARE(importer.errors().size(), 0);
    QCOMPARE(model.rowCount(), 3);

    const TrackRecord* const audio = model.recordForId(trackIdForPath(audioFixture));
    const TrackRecord* const videoOnly =
        model.recordForId(trackIdForPath(videoOnlyFixture));
    const TrackRecord* const attachedPicture =
        model.recordForId(trackIdForPath(attachedPictureFixture));
    QVERIFY(audio != nullptr);
    QVERIFY(videoOnly != nullptr);
    QVERIFY(attachedPicture != nullptr);
    QVERIFY(audio->hasAudio);
    QVERIFY(!audio->hasVideo);
    QVERIFY(!videoOnly->hasAudio);
    QVERIFY(videoOnly->hasVideo);
    QVERIFY(attachedPicture->hasAudio);
    QVERIFY(!attachedPicture->hasVideo);
}

void ImportControllerTest::modelCanBeDestroyedWhileProbeIsBlocked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("blocked.wav"));
    createFile(path);
    QSemaphore entered;
    QSemaphore release;
    auto model = std::make_unique<LibraryModel>();
    const ProbeFunction probe = [&entered, &release](const QString& requestedPath) {
        entered.release();
        release.acquire();
        TrackRecord track;
        track.path = requestedPath;
        track.title = QStringLiteral("Late result");
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(model.get(), probe);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(path)});
    QVERIFY(entered.tryAcquire(1, 3000));
    model.reset();
    release.release();

    QVERIFY(finished.wait(3000));
    QVERIFY(!importer.busy());
    QCOMPARE(importer.progress(), 1.0);
}

void ImportControllerTest::controllerCanBeDestroyedWhileProbeIsBlocked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("blocked.wav"));
    createFile(path);
    QSemaphore entered;
    QSemaphore release;
    QSemaphore exited;
    LibraryModel model;
    const ProbeFunction probe = [&entered, &release, &exited](const QString& requestedPath) {
        entered.release();
        release.acquire();
        exited.release();
        TrackRecord track;
        track.path = requestedPath;
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };
    auto importer = std::make_unique<ImportController>(&model, probe);

    importer->importUrls({QUrl::fromLocalFile(path)});
    QVERIFY(entered.tryAcquire(1, 3000));
    importer.reset();
    release.release();

    QVERIFY(exited.tryAcquire(1, 3000));
    QCoreApplication::processEvents();
    QCOMPARE(model.rowCount(), 0);
}

void ImportControllerTest::rejectsModelWithDifferentThreadAffinity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("thread.wav"));
    createFile(path);
    QThread modelThread;
    LibraryModel model;
    model.moveToThread(&modelThread);
    modelThread.start();
    std::atomic_int probeCalls{0};
    const ProbeFunction probe = [&probeCalls](const QString& requestedPath) {
        ++probeCalls;
        TrackRecord track;
        track.path = requestedPath;
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(path)});
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);

    QThread* const testThread = QThread::currentThread();
    QVERIFY(QMetaObject::invokeMethod(
        &model,
        [&model, testThread] { model.moveToThread(testThread); },
        Qt::BlockingQueuedConnection));
    modelThread.quit();
    QVERIFY(modelThread.wait(3000));
    QCOMPARE(probeCalls.load(), 0);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(importer.errors().size(), 1);
    QVERIFY(importer.errors().front().contains(QStringLiteral("thread"), Qt::CaseInsensitive));
    QVERIFY(!importer.busy());
}

void ImportControllerTest::writesBpmWhenAutoReadEnabled()
{
    QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_click_XXXXXX.wav")));
    QVERIFY(tempFile.open());
    tempFile.close();

    QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 120, 8));

    LibraryModel model;
    const ProbeFunction probe = [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.available = true;
        const BpmAnalyzeResult bpm = analyze_bpm(path);
        track.bpm = bpm.bpm;
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(tempFile.fileName())});

    QVERIFY(finished.wait(5000));
    QCOMPARE(importer.errors().size(), 0);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(std::abs(model.tracks().front().bpm - 120.0) < 1.0);
}

void ImportControllerTest::leavesBpmZeroWhenAutoReadDisabled()
{
    QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_click_off_XXXXXX.wav")));
    QVERIFY(tempFile.open());
    tempFile.close();

    QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 120, 8));

    LibraryModel model;
    ImportController importer(&model);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(tempFile.fileName())});

    QVERIFY(finished.wait(5000));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.tracks().front().bpm, 0.0);
}

void ImportControllerTest::probeObservesDynamicAnalyzeBpmFlag()
{
    QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_dynamic_XXXXXX.wav")));
    QVERIFY(tempFile.open());
    tempFile.close();

    QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 120, 8));

    LibraryModel model;
    auto analyzeFlag = std::make_shared<std::atomic_bool>(false);
    const ProbeFunction probe = [analyzeFlag](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.available = true;
        if (analyzeFlag->load(std::memory_order_relaxed)) {
            const BpmAnalyzeResult bpm = analyze_bpm(path);
            track.bpm = bpm.bpm;
        }
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(&model, probe);

    {
        QSignalSpy finished(&importer, &ImportController::finished);
        importer.importUrls({QUrl::fromLocalFile(tempFile.fileName())});
        QVERIFY(finished.wait(5000));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.tracks().front().bpm, 0.0);
    }

    analyzeFlag->store(true, std::memory_order_relaxed);
    model.replaceAll({});

    {
        QSignalSpy finished(&importer, &ImportController::finished);
        importer.importUrls({QUrl::fromLocalFile(tempFile.fileName())});
        QVERIFY(finished.wait(5000));
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(std::abs(model.tracks().front().bpm - 120.0) < 1.0);
    }
}

void ImportControllerTest::productionProbePrefersValidEmbeddedBpm()
{
    const QString fixture = QString::fromLocal8Bit(qgetenv("AGPLAYER_TEST_AUDIO"));
    QVERIFY2(!fixture.isEmpty(), "AGPLAYER_TEST_AUDIO must name the generated WAV fixture");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tagged = dir.filePath(QStringLiteral("tagged-bpm.mp3"));
    QCOMPARE(ag_transcode(fixture.toUtf8().constData(), tagged.toUtf8().constData(),
                         "libmp3lame", 192000, 44100, 2,
                         nullptr, nullptr, nullptr), AG_OK);
    QCOMPARE(ag_metadata_write_extended(
                 tagged.toUtf8().constData(), nullptr, nullptr, nullptr,
                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                 nullptr, "127.50", nullptr, nullptr, nullptr,
                 nullptr, 0U, nullptr), AG_OK);

    LibraryModel model;
    ImportController importer(&model);
    QSignalSpy finished(&importer, &ImportController::finished);
    importer.importUrls({QUrl::fromLocalFile(tagged)});
    QVERIFY(finished.wait(5000));
    QCOMPARE(importer.errors().size(), 0);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(std::abs(model.tracks().front().bpm - 127.5) < 0.01);
}

void ImportControllerTest::queuesDropsReceivedWhileAnImportIsBusy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("first.wav"));
    const QString secondPath = dir.filePath(QStringLiteral("second.wav"));
    createFile(firstPath);
    createFile(secondPath);

    QSemaphore firstEntered;
    QSemaphore releaseFirst;
    const ProbeFunction probe = [&](const QString& path) {
        if (path == QDir::cleanPath(firstPath)) {
            firstEntered.release();
            releaseFirst.acquire();
        }
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).baseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };

    LibraryModel model;
    ImportController importer(&model, probe);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importPaths({firstPath});
    QVERIFY(firstEntered.tryAcquire(1, 3000));
    importer.importPaths({secondPath});
    releaseFirst.release();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.indexForLocalFile(firstPath) >= 0);
    QVERIFY(model.indexForLocalFile(secondPath) >= 0);
}

void ImportControllerTest::importsTenThousandLightweightRecordsWithinBudget()
{
    QStringList paths;
    paths.reserve(10000);
    for (int index = 0; index < 10000; ++index) {
        paths.append(QDir::temp().filePath(
            QStringLiteral("agplayer-index-%1.mp3").arg(index)));
    }
    const DiscoveryFunction discovery = [paths](const QList<QUrl>&) {
        return paths;
    };
    const ProbeFunction probe = [](const QString& path) {
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).baseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };

    LibraryModel model;
    ImportController importer(&model, probe, discovery);
    QSignalSpy finished(&importer, &ImportController::finished);
    QElapsedTimer timer;
    timer.start();
    importer.importUrls({QUrl::fromLocalFile(QDir::tempPath())});

    QVERIFY(finished.wait(30000));
    QCOMPARE(model.rowCount(), 10000);
    QVERIFY2(timer.elapsed() <= 30000,
             qPrintable(QStringLiteral("10K indexing took %1 ms")
                            .arg(timer.elapsed())));
}

void ImportControllerTest::performanceRealImportResponsiveness()
{
    if (!qEnvironmentVariableIsSet("AGPLAYER_RUN_PERFORMANCE"))
        QSKIP("Opt-in real-file performance measurement");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(source, 120, 1));
    QList<QUrl> urls;
    for (int i = 0; i < 2000; ++i) {
        const QString path = dir.filePath(QStringLiteral("track-%1.wav").arg(i));
        QVERIFY(QFile::copy(source, path));
        urls.append(QUrl::fromLocalFile(path));
    }
    LibraryModel model;
    ImportController importer(&model);
    QSignalSpy finished(&importer, &ImportController::finished);
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 previousTick = 0;
    qint64 maxGap = 0;
    int ticks = 0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    connect(&heartbeat, &QTimer::timeout, this, [&] {
        const qint64 now = elapsed.elapsed();
        maxGap = qMax(maxGap, now - previousTick);
        previousTick = now;
        ++ticks;
    });
    heartbeat.start();
    importer.importUrls(urls);
    const qint64 dispatchMs = elapsed.elapsed();
    QVERIFY(finished.wait(60000));
    maxGap = qMax(maxGap, elapsed.elapsed() - previousTick);
    qInfo("PERF real_import files=2000 pcm=16000Hz/mono/16bit/1s elapsed_ms=%lld dispatch_ms=%lld heartbeat_max_gap_ms=%lld ticks=%d",
          elapsed.elapsed(), dispatchMs, maxGap, ticks);
    QCOMPARE(model.rowCount(), 2000);
    QCOMPARE(importer.importedTrackIds().size(), 2000);
    QVERIFY(importer.errors().isEmpty());
    QVERIFY(ticks > 0);
    for (const TrackRecord& track : model.tracks()) {
        QCOMPARE(track.durationMs, 1000);
        QCOMPARE(track.sampleRate, 16000);
    }
}

void ImportControllerTest::alreadyImportedTracksAreSkippedWithoutFalseSuccess()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("already-there.wav"));
    createFile(path);

    TrackRecord existing;
    existing.path = path;
    existing.title = QStringLiteral("Existing");
    existing.available = true;
    LibraryModel model;
    QVERIFY(model.append(existing));

    std::atomic_int probeCalls{0};
    ImportController importer(&model, [&probeCalls](const QString& candidate) {
        ++probeCalls;
        TrackRecord track;
        track.path = candidate;
        track.title = QStringLiteral("Duplicate probe");
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    });
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importPaths({path});
    QVERIFY(finished.wait(3000));
    QCOMPARE(probeCalls.load(), 0);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(importer.errors().isEmpty());
    QCOMPARE(importer.property("skippedCount").toInt(), 1);
    QCOMPARE(importer.importedTrackIds().size(), 1);
    QCOMPARE(importer.importedTrackIds().front(), model.tracks().front().trackId);
}

void ImportControllerTest::cachedDuplicateRemovedDuringDiscoveryIsNotReinserted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("removed-during-import.wav"));
    createFile(path);

    TrackRecord existing;
    existing.path = path;
    existing.title = QStringLiteral("Existing");
    existing.available = true;
    LibraryModel model;
    QVERIFY(model.append(existing));
    const QString trackId = model.tracks().front().trackId;

    QSemaphore discoveryEntered;
    QSemaphore releaseDiscovery;
    const DiscoveryFunction discovery =
        [&discoveryEntered, &releaseDiscovery, path](const QList<QUrl>&) {
            discoveryEntered.release();
            releaseDiscovery.acquire();
            return QStringList{path};
        };
    std::atomic_int probeCalls{0};
    const ProbeFunction probe = [&probeCalls](const QString& candidate) {
        ++probeCalls;
        TrackRecord track;
        track.path = candidate;
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(&model, probe, discovery);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(dir.path())});
    QVERIFY(discoveryEntered.tryAcquire(1, 3000));
    QVERIFY(model.removeTrack(trackId));
    releaseDiscovery.release();

    QVERIFY(finished.wait(3000));
    QCOMPARE(probeCalls.load(), 0);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(importer.importedTrackIds().isEmpty());
}

void ImportControllerTest::importedTracksAppearFirstInDiscoveryOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("01-first.wav"));
    const QString secondPath = dir.filePath(QStringLiteral("02-second.wav"));
    createFile(firstPath);
    createFile(secondPath);

    TrackRecord oldTrack;
    oldTrack.trackId = QStringLiteral("old");
    oldTrack.path = dir.filePath(QStringLiteral("old.wav"));
    oldTrack.title = QStringLiteral("Old");
    oldTrack.available = true;
    createFile(oldTrack.path);
    LibraryModel model;
    QVERIFY(model.append(oldTrack));

    const DiscoveryFunction discovery = [firstPath, secondPath](const QList<QUrl>&) {
        return QStringList{firstPath, secondPath};
    };
    const ProbeFunction probe = [firstPath](const QString& path) {
        if (path == canonicalLibraryPath(firstPath)) QThread::msleep(80);
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).completeBaseName();
        track.available = true;
        return ProbeResult{AG_OK, track, {}};
    };
    ImportController importer(&model, probe, discovery);
    QSignalSpy finished(&importer, &ImportController::finished);

    importer.importUrls({QUrl::fromLocalFile(dir.path())});

    QVERIFY(finished.wait(3000));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.tracks().at(0).path, canonicalLibraryPath(firstPath));
    QCOMPARE(model.tracks().at(1).path, canonicalLibraryPath(secondPath));
    QCOMPARE(model.tracks().at(2).trackId, QStringLiteral("old"));
}

QTEST_GUILESS_MAIN(ImportControllerTest)
#include "import_controller_test.moc"
