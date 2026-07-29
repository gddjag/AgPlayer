#include "library_model.hpp"
#include "playback_controller.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QFile>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PlaybackControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void commandsReflectOnlyCoreSnapshots();
    void snapshotSignalsEmitOnlyForChangesAtBoundedFrequency();
    void unavailableRowsAreExcludedFromQueueIndices();
    void loadsRowWithoutStartingPlayback();
    void nullCoreReportsStableErrors();
    void survivesLibraryModelDestruction();
    void libraryRequestsShareQueueAndFavoriteState();
    void playbackControllerIsAnAgPlayerQmlSingleton();
};

void PlaybackControllerTest::commandsReflectOnlyCoreSnapshots()
{
    const QByteArray path = qgetenv("AGPLAYER_TEST_WAV");
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QSignalSpy durationChanged(&controller, &PlaybackController::durationMsChanged);
        QSignalSpy stateChanged(&controller, &PlaybackController::stateChanged);
        QSignalSpy positionChanged(&controller, &PlaybackController::positionMsChanged);

        QCOMPARE(controller.durationMs(), 0);
        QCOMPARE(ag_player_load(core, path.constData()), AG_OK);
        QTRY_COMPARE(controller.durationMs(), 2'000);
        QCOMPARE(durationChanged.count(), 1);
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QCOMPARE(snapshot.duration_ms, 2'000);
        QCOMPARE(controller.durationMs(), snapshot.duration_ms);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(durationChanged.count(), 1);

        const auto initialState = controller.state();
        controller.play();
        QCOMPARE(controller.state(), initialState);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QCOMPARE(stateChanged.count(), 1);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 1);
        controller.pause();
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(stateChanged.count(), 2);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 2);

        controller.togglePlayback();
        QCOMPARE(controller.state(), PlaybackController::Paused);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QCOMPARE(stateChanged.count(), 3);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 3);

        controller.pause();
        QCOMPARE(controller.state(), PlaybackController::Playing);
        QTRY_COMPARE(controller.state(), PlaybackController::Paused);
        QCOMPARE(stateChanged.count(), 4);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 4);

        const qint64 pausedPosition = controller.positionMs();
        const qint64 laterTarget = controller.durationMs() * 3 / 4;
        const qint64 earlierTarget = controller.durationMs() / 4;
        const qint64 seekTarget = qAbs(laterTarget - pausedPosition) >= 100
                                      ? laterTarget
                                      : earlierTarget;
        QVERIFY(seekTarget > 0);
        QVERIFY(seekTarget < controller.durationMs());
        QVERIFY(qAbs(seekTarget - pausedPosition) >= 100);
        positionChanged.clear();

        controller.seek(seekTarget);
        QCOMPARE(controller.positionMs(), pausedPosition);
        QTRY_VERIFY(qAbs(controller.positionMs() - seekTarget) <= 2);
        QCOMPARE(positionChanged.count(), 1);
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QVERIFY(qAbs(snapshot.position_ms - seekTarget) <= 2);
        QCOMPARE(controller.positionMs(), snapshot.position_ms);
        const qint64 landedPosition = controller.positionMs();
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(positionChanged.count(), 1);
        QCOMPARE(controller.positionMs(), landedPosition);
        QCOMPARE(ag_player_snapshot(core, &snapshot), AG_OK);
        QCOMPARE(snapshot.position_ms, landedPosition);

        QSignalSpy volumeChanged(&controller, &PlaybackController::volumeChanged);
        controller.setVolume(0.25F);
        QCOMPARE(controller.volume(), 1.0F);
        QTRY_COMPARE(controller.volume(), 0.25F);
        QCOMPARE(volumeChanged.count(), 1);

        QSignalSpy mutedChanged(&controller, &PlaybackController::mutedChanged);
        controller.toggleMuted();
        QCOMPARE(controller.muted(), false);
        QTRY_COMPARE(controller.muted(), true);
        QCOMPARE(mutedChanged.count(), 1);

        QSignalSpy modeChanged(&controller, &PlaybackController::modeChanged);
        controller.cycleMode();
        QCOMPARE(controller.mode(), PlaybackController::Sequential);
        QTRY_COMPARE(controller.mode(), PlaybackController::RepeatOne);
        QCOMPARE(modeChanged.count(), 1);

        controller.setMode(PlaybackController::RepeatAll);
        QTRY_COMPARE(controller.mode(), PlaybackController::RepeatAll);
        QCOMPARE(modeChanged.count(), 2);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::snapshotSignalsEmitOnlyForChangesAtBoundedFrequency()
{
    QVERIFY(PlaybackController::PollIntervalMs >= 34);

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core);
        QSignalSpy stateChanged(&controller, &PlaybackController::stateChanged);
        QSignalSpy positionChanged(&controller, &PlaybackController::positionMsChanged);
        QSignalSpy durationChanged(&controller, &PlaybackController::durationMsChanged);
        QSignalSpy volumeChanged(&controller, &PlaybackController::volumeChanged);
        QSignalSpy mutedChanged(&controller, &PlaybackController::mutedChanged);
        QSignalSpy modeChanged(&controller, &PlaybackController::modeChanged);
        QSignalSpy indexChanged(&controller, &PlaybackController::trackIndexChanged);
        QSignalSpy countChanged(&controller, &PlaybackController::trackCountChanged);
        QSignalSpy trackIdChanged(&controller, &PlaybackController::currentTrackIdChanged);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(stateChanged.count(), 0);
        QCOMPARE(positionChanged.count(), 0);
        QCOMPARE(durationChanged.count(), 0);
        QCOMPARE(volumeChanged.count(), 0);
        QCOMPARE(mutedChanged.count(), 0);
        QCOMPARE(modeChanged.count(), 0);
        QCOMPARE(indexChanged.count(), 0);
        QCOMPARE(countChanged.count(), 0);
        QCOMPARE(trackIdChanged.count(), 0);

        controller.setVolume(0.5F);
        QTRY_COMPARE(controller.volume(), 0.5F);
        QCOMPARE(volumeChanged.count(), 1);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(volumeChanged.count(), 1);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::unavailableRowsAreExcludedFromQueueIndices()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.wav"));
    const QString thirdPath = directory.filePath(QStringLiteral("third.wav"));
    QVERIFY(QFile::copy(fixture, firstPath));
    QVERIFY(QFile::copy(fixture, thirdPath));

    LibraryModel model;
    TrackRecord first;
    first.trackId = QStringLiteral("first");
    first.path = firstPath;
    first.available = true;
    QVERIFY(model.append(first));
    TrackRecord unavailable;
    unavailable.trackId = QStringLiteral("missing");
    unavailable.path = directory.filePath(QStringLiteral("missing.wav"));
    unavailable.available = false;
    QVERIFY(model.append(unavailable));
    TrackRecord third;
    third.trackId = QStringLiteral("third");
    third.path = thirdPath;
    third.available = true;
    QVERIFY(model.append(third));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        QSignalSpy indexChanged(&controller, &PlaybackController::trackIndexChanged);
        QSignalSpy countChanged(&controller, &PlaybackController::trackCountChanged);
        QSignalSpy trackIdChanged(&controller, &PlaybackController::currentTrackIdChanged);
        controller.playRow(2);
        QTRY_COMPARE(controller.trackCount(), 2);
        QTRY_COMPARE(controller.trackIndex(), 1);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("third"));
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 1);
        QCOMPARE(trackIdChanged.count(), 1);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 1);
        QCOMPARE(trackIdChanged.count(), 1);

        controller.previous();
        QCOMPARE(controller.trackIndex(), 1);
        QTRY_COMPARE(controller.trackIndex(), 0);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("first"));
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 2);
        QCOMPARE(trackIdChanged.count(), 2);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 2);
        QCOMPARE(trackIdChanged.count(), 2);

        controller.next();
        QCOMPARE(controller.trackIndex(), 0);
        QTRY_COMPARE(controller.trackIndex(), 1);
        QCOMPARE(controller.currentTrackId(), QStringLiteral("third"));
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 3);
        QCOMPARE(trackIdChanged.count(), 3);
        QTest::qWait(PlaybackController::PollIntervalMs * 3);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(indexChanged.count(), 3);
        QCOMPARE(trackIdChanged.count(), 3);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::loadsRowWithoutStartingPlayback()
{
    const QString fixture = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!fixture.isEmpty());
    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("restore-track");
    track.path = fixture;
    track.available = true;
    QVERIFY(model.append(track));

    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);
    {
        PlaybackController controller(core, &model);
        controller.loadRow(0);
        QTRY_COMPARE(controller.trackIndex(), 0);
        QTRY_COMPARE(controller.currentTrackId(), QStringLiteral("restore-track"));
        QCOMPARE(controller.state(), PlaybackController::Stopped);

        controller.seek(750);
        QTRY_VERIFY(qAbs(controller.positionMs() - 750) <= 2);
        QCOMPARE(controller.state(), PlaybackController::Stopped);

        QTRY_VERIFY(controller.durationMs() > 0);
        controller.seek(controller.durationMs() + 10'000);
        QTRY_COMPARE(controller.positionMs(), controller.durationMs());
        QCOMPARE(controller.errorMessage(), QString());
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::nullCoreReportsStableErrors()
{
    PlaybackController controller;
    QSignalSpy errorChanged(&controller, &PlaybackController::errorMessageChanged);
    controller.play();
    QCOMPARE(controller.errorMessage(), QStringLiteral("Playback core is unavailable"));
    QCOMPARE(errorChanged.count(), 1);
    controller.pause();
    controller.seek(1);
    controller.next();
    controller.previous();
    controller.setVolume(0.5F);
    controller.toggleMuted();
    controller.cycleMode();
    QCOMPARE(controller.errorMessage(), QStringLiteral("Playback core is unavailable"));
    QCOMPARE(errorChanged.count(), 1);
}

void PlaybackControllerTest::survivesLibraryModelDestruction()
{
    auto* model = new LibraryModel;
    PlaybackController controller(nullptr, model);
    delete model;
    controller.playRow(0);
    controller.toggleFavorite();
    controller.toggleFavorite(0);
    QTest::qWait(PlaybackController::PollIntervalMs * 2);
}

void PlaybackControllerTest::libraryRequestsShareQueueAndFavoriteState()
{
    const QString path = QString::fromUtf8(qgetenv("AGPLAYER_TEST_WAV"));
    QVERIFY(!path.isEmpty());
    ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
    ag_player* core = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &core), AG_OK);

    LibraryModel model;
    TrackRecord track;
    track.trackId = QStringLiteral("fixture-track");
    track.path = path;
    track.available = true;
    QVERIFY(model.append(track));

    {
        PlaybackController controller(core, &model);
        model.playRow(0);
        QTRY_COMPARE(controller.currentTrackId(), model.tracks().front().trackId);
        QTRY_COMPARE(controller.state(), PlaybackController::Playing);
        QTRY_COMPARE(model.tracks().front().playCount, 1);
        QVERIFY(model.tracks().front().lastPlayedAtMs > 0);
        controller.toggleFavorite();
        QVERIFY(model.tracks().front().favorite);
    }
    ag_player_destroy(core);
}

void PlaybackControllerTest::playbackControllerIsAnAgPlayerQmlSingleton()
{
    PlaybackController playback;
    WindowController windows;
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", &playback);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", &windows);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQml 2.15\n"
        "import AgPlayer 1.0\n"
        "QtObject {\n"
        "  property var first: PlaybackController\n"
        "  property var second: PlaybackController\n"
        "  property var firstWindow: WindowController\n"
        "  property var secondWindow: WindowController\n"
        "}",
        QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    QObject* first = object->property("first").value<QObject*>();
    QObject* second = object->property("second").value<QObject*>();
    QCOMPARE(first, &playback);
    QCOMPARE(first, second);
    QObject* firstWindow = object->property("firstWindow").value<QObject*>();
    QObject* secondWindow = object->property("secondWindow").value<QObject*>();
    QCOMPARE(firstWindow, &windows);
    QCOMPARE(firstWindow, secondWindow);
}

QTEST_GUILESS_MAIN(PlaybackControllerTest)
#include "playback_controller_test.moc"
