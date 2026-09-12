#include "import_controller.hpp"
#include "library_model.hpp"
#include "library_store.hpp"
#include "playback_controller.hpp"
#include "runtime_log.hpp"
#include "tag_model.hpp"
#include "tag_store.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>
#include <utility>

class ShutdownTest final : public QObject {
    Q_OBJECT

private slots:
    void closeDuringWaveformWriteLeavesValidState();
    void closeIsIdempotentWhenImportStillRunning();
    void closeReleasesCoreAndFlushesLibraryEvenIfImportEmpty();
    void closeSynchronouslyFlushesImmediateTagEdit();
};

namespace {

// Harness mirrors app/main.cpp wiring in a gui-less form so the shutdown
// sequence can be exercised without a real QGuiApplication. The ImportController
// is given a probe that blocks on a semaphore until the test releases it, so
// requestClose() is guaranteed to fire while a QtConcurrent worker is mid-flight.
class Harness {
private:
    QString libraryPath_;
public:
    Harness(QTemporaryDir& scratch, QSemaphore& probeEntered, QSemaphore& probeRelease)
        : libraryPath_(scratch.filePath(QStringLiteral("library.json")))
        , store_(libraryPath_)
        , tags_(&library_, scratch.filePath(QStringLiteral("tags.json")))
        , importer_(&library_, makeBlockingProbe(probeEntered, probeRelease))
    {
        ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
        if (ag_player_create_with_config(&config, &core_) != AG_OK) {
            QFAIL("ag_player_create_with_config failed in Harness constructor");
            return;
        }
        playback_ = std::make_unique<PlaybackController>(core_, &library_);

        QObject::connect(&library_, &LibraryModel::rowsInserted, &store_,
            [this](const QModelIndex&, int, int) {
                store_.requestSave(library_.tracks());
            });
        QObject::connect(&library_, &LibraryModel::dataChanged, &store_,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                store_.requestSave(library_.tracks());
            });
        QObject::connect(&library_, &LibraryModel::modelReset, &store_,
            [this] { store_.requestSave(library_.tracks()); });
        QObject::connect(&library_, &LibraryModel::flushRequested, &store_,
            [this] {
                store_.requestSave(library_.tracks());
                store_.flush();
            });

        WindowController::ShutdownActions actions;
        actions.cancelWaveform = [this] { importer_.cancel(); };
        actions.stopPlayback = [this] {
                if (core_ != nullptr) {
                    ag_player_stop(core_);
                }
            };
        actions.flushLibrary = [this] {
            library_.flush();
            tagFlushSucceeded_ = tags_.flush();
        };
        actions.releaseCore = [this] {
            playback_->setPlayer(nullptr);
            if (core_ != nullptr) {
                ag_player_destroy(core_);
                core_ = nullptr;
            }
        };
        actions.quitApplication = [] { QCoreApplication::quit(); };
        windows_.setShutdownActions(std::move(actions));
    }

    ~Harness()
    {
        if (core_ != nullptr) {
            ag_player_destroy(core_);
            core_ = nullptr;
        }
    }

    void startLongWaveformJob(const QStringList& paths)
    {
        QList<QUrl> urls;
        urls.reserve(paths.size());
        for (const QString& path : paths) {
            urls.append(QUrl::fromLocalFile(path));
        }
        importer_.importUrls(urls);
    }

    void requestClose() { windows_.requestClose(); }

    bool importerBusy() const { return importer_.busy(); }

    QString libraryPath() const { return libraryPath_; }
    QString libraryDirectory() const { return QFileInfo(libraryPath_).absolutePath(); }
    QString tagPath() const { return QFileInfo(libraryPath_).dir().filePath(QStringLiteral("tags.json")); }
    bool tagFlushSucceeded() const { return tagFlushSucceeded_; }

    bool libraryJsonIsValid() const
    {
        QFile file(libraryPath_);
        if (!file.exists()) {
            return true;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray bytes = file.readAll();
        const QJsonDocument document = QJsonDocument::fromJson(bytes);
        return document.isArray();
    }

    int runningWorkerCount() const
    {
        return importer_.busy() ? 1 : 0;
    }

    // Public accessors for test assertions.
    LibraryModel library_;
    LibraryStore store_;
    TagModel tags_;
    ImportController importer_;

private:
    static ProbeFunction makeBlockingProbe(QSemaphore& entered, QSemaphore& release)
    {
        return [&entered, &release](const QString& requestedPath) {
            entered.release();
            // Bounded wait so cancel() cannot deadlock the test thread if the
            // worker is already inside probe() when cancel() fires. The test
            // releases the semaphore to let the probe return promptly, but the
            // timeout is the safety net.
            if (!release.tryAcquire(1, 1000)) {
                return ProbeResult{AG_CANCELLED, {}, QStringLiteral("probe timeout")};
            }
            TrackRecord track;
            track.path = requestedPath;
            track.title = QStringLiteral("Blocked-result");
            track.available = true;
            return ProbeResult{AG_OK, track, {}};
        };
    }

    ag_player* core_ = nullptr;
    std::unique_ptr<PlaybackController> playback_;
    WindowController windows_;
    bool tagFlushSucceeded_ = false;
};

QStringList makeFixtureFiles(QTemporaryDir& dir, int count)
{
    QStringList paths;
    paths.reserve(count);
    for (int index = 0; index < count; ++index) {
        const QString path = dir.filePath(QStringLiteral("track-%1.wav").arg(index));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        if (file.write("audio") != 5) {
            return {};
        }
        paths.append(path);
    }
    return paths;
}

bool hasTmpResidue(const QString& directory)
{
    const QDir dir(directory);
    const QStringList filters{QStringLiteral("*.tmp"), QStringLiteral("*.tmp.*")};
    return !dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot).isEmpty();
}

} // namespace

void ShutdownTest::closeDuringWaveformWriteLeavesValidState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSemaphore probeEntered;
    QSemaphore probeRelease;
    const QStringList paths = makeFixtureFiles(dir, 3);

    Harness app(dir, probeEntered, probeRelease);
    QSignalSpy finishedSpy(&app.importer_, &ImportController::finished);

    app.startLongWaveformJob(paths);
    QVERIFY(probeEntered.tryAcquire(1, 3000));
    QVERIFY(app.importerBusy());

    // Release the probe so cancel() inside requestClose() can complete promptly.
    probeRelease.release(paths.size());
    app.requestClose();

    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(!app.importerBusy(), 3000);
    QVERIFY(finishedSpy.count() >= 1);

    QVERIFY(!hasTmpResidue(app.libraryDirectory()));
    QVERIFY(app.libraryJsonIsValid());
    QCOMPARE(app.runningWorkerCount(), 0);
}

void ShutdownTest::closeIsIdempotentWhenImportStillRunning()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSemaphore probeEntered;
    QSemaphore probeRelease;
    const QStringList paths = makeFixtureFiles(dir, 2);

    Harness app(dir, probeEntered, probeRelease);
    app.startLongWaveformJob(paths);
    QVERIFY(probeEntered.tryAcquire(1, 3000));

    probeRelease.release(paths.size());
    app.requestClose();
    app.requestClose();
    app.requestClose();

    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(!app.importerBusy(), 3000);

    QVERIFY(app.libraryJsonIsValid());
    QVERIFY(!hasTmpResidue(app.libraryDirectory()));
    QCOMPARE(app.runningWorkerCount(), 0);
}

void ShutdownTest::closeReleasesCoreAndFlushesLibraryEvenIfImportEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSemaphore probeEntered;
    QSemaphore probeRelease;

    Harness app(dir, probeEntered, probeRelease);

    TrackRecord track;
    track.trackId = QStringLiteral("pre-existing");
    track.path = dir.filePath(QStringLiteral("existing.wav"));
    track.title = QStringLiteral("Existing");
    track.available = true;
    QVERIFY(app.library_.append(track));
    app.requestClose();

    QCoreApplication::processEvents();
    QVERIFY(!app.importerBusy());
    QVERIFY(app.libraryJsonIsValid());
    QVERIFY(!hasTmpResidue(app.libraryDirectory()));

    QFile json(app.libraryPath());
    QVERIFY(json.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(json.readAll());
    QVERIFY(document.isArray());
    QCOMPARE(document.array().size(), 1);
}

void ShutdownTest::closeSynchronouslyFlushesImmediateTagEdit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSemaphore probeEntered;
    QSemaphore probeRelease;
    Harness app(dir, probeEntered, probeRelease);

    QVERIFY(app.tags_.createTag(QStringLiteral("Immediate")));
    QVERIFY(app.tags_.dirty());
    app.requestClose();

    QVERIFY(app.tagFlushSucceeded());
    QVERIFY(!app.tags_.dirty());
    const QList<TagEntry> restored = TagStore(app.tagPath()).load();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.constFirst().key, QStringLiteral("immediate"));
}

QTEST_GUILESS_MAIN(ShutdownTest)
#include "shutdown_test.moc"
