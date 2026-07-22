#include "import_controller.hpp"
#include "library_model.hpp"

#include <QDir>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>

class ImportControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void deduplicatesCanonicalPathsAndContinuesAfterFailure();
    void productionProbeImportsMetadataAndUsesBrandFallback();
    void modelCanBeDestroyedWhileProbeIsBlocked();
    void controllerCanBeDestroyedWhileProbeIsBlocked();
    void rejectsModelWithDifferentThreadAffinity();
};

namespace {
void createFile(const QString& path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("audio"), 5);
}
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

    finished.clear();
    importer.importUrls({QUrl::fromLocalFile(goodPath)});
    QVERIFY(finished.wait(3000));
    QCOMPARE(probeCalls.load(), 2);
    QCOMPARE(model.rowCount(), 1);
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
    QVERIFY(track.durationMs > 0);
    QVERIFY(track.fileSize > 0);
    QCOMPARE(track.coverUrl,
             QUrl(QStringLiteral("qrc:/AgPlayer/assets/brand/logo-mark.png")));
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

QTEST_GUILESS_MAIN(ImportControllerTest)
#include "import_controller_test.moc"
