#include "audio_preview_controller.hpp"
#include "library_model.hpp"
#include "playback_controller.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class AudioPreviewControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void pausesMainPlayerBeforePreviewAndDoesNotResume();
    void appliesUpdatedDspParametersToActivePreview();
    void rejectsMissingFiles();
    void clearsOldStateWhenNewSourceCannotLoad();
    void stopsCurrentPreviewWhenNewSourceIsMissing();
    void previewsTimelineWithoutTemporaryRender();
};

void AudioPreviewControllerTest::
appliesUpdatedDspParametersToActivePreview()
{
    const QString fixture =
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    const qint64 originalDuration = preview.durationMs();
    QVERIFY(originalDuration > 0);

    QSignalSpy processing(
        &preview, &AudioPreviewController::processingChanged);
    preview.setDspParameters(
        1.5, 0, true, false, false, true);
    QCOMPARE(preview.speedRatio(), 1.5);
    QTRY_VERIFY_WITH_TIMEOUT(processing.count() >= 2, 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(!preview.processing(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(
        preview.durationMs() > 0
            && preview.durationMs() < originalDuration,
        10'000);
    QVERIFY(preview.isCurrentSource(QUrl::fromLocalFile(fixture)));

    preview.setDspParameters(
        1.5, 250, true, true, true, true);
    QCOMPARE(preview.pitchCents(), 250);
    QTRY_VERIFY_WITH_TIMEOUT(!preview.processing(), 10'000);
    QVERIFY(preview.isCurrentSource(QUrl::fromLocalFile(fixture)));
}

void AudioPreviewControllerTest::pausesMainPlayerBeforePreviewAndDoesNotResume()
{
    const QString fixture =
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    ag_player* mainPlayer = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 0U};
    QCOMPARE(ag_player_create_with_config(&config, &mainPlayer), AG_OK);
    QVERIFY(mainPlayer != nullptr);
    const QByteArray fixtureUtf8 = fixture.toUtf8();
    QCOMPARE(ag_player_load(mainPlayer, fixtureUtf8.constData()), AG_OK);
    QCOMPARE(ag_player_play(mainPlayer), AG_OK);

    LibraryModel library;
    PlaybackController mainPlayback(mainPlayer, &library);
    QTRY_COMPARE_WITH_TIMEOUT(
        mainPlayback.state(), PlaybackController::Playing, 2'000);

    AudioPreviewController preview(
        AG_AUDIO_BACKEND_NULL, &mainPlayback);
    preview.setVolume(0.25);
    QCOMPARE(preview.volume(), 0.25);

    preview.toggle(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.hasSource(), 2'000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    QTRY_COMPARE_WITH_TIMEOUT(
        mainPlayback.state(), PlaybackController::Paused, 2'000);
    QVERIFY(preview.durationMs() > 0);
    QVERIFY(preview.isCurrentSource(QUrl::fromLocalFile(fixture)));
    QCOMPARE(QFileInfo(preview.sourcePath()).canonicalFilePath(),
             QFileInfo(fixture).canonicalFilePath());

    preview.seek(250);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= 200, 2'000);
    preview.pause();
    QTRY_VERIFY_WITH_TIMEOUT(!preview.playing(), 2'000);

    preview.toggle(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    preview.stop();
    QVERIFY(!preview.playing());
    QVERIFY(!preview.hasSource());
    QCOMPARE(mainPlayback.state(), PlaybackController::Paused);
    ag_player_destroy(mainPlayer);
}

void AudioPreviewControllerTest::rejectsMissingFiles()
{
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    QSignalSpy errors(&preview, &AudioPreviewController::errorOccurred);
    preview.play(QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/missing.wav")));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!preview.hasSource());
}

void AudioPreviewControllerTest::clearsOldStateWhenNewSourceCannotLoad()
{
    const QString fixture =
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    QVERIFY(preview.hasSource());

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString corruptPath = temp.filePath(QStringLiteral("corrupt.wav"));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write("not audio"), 9);
    corrupt.close();

    QSignalSpy errors(&preview, &AudioPreviewController::errorOccurred);
    preview.play(QUrl::fromLocalFile(corruptPath));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!preview.hasSource());
    QVERIFY(!preview.isCurrentSource(QUrl::fromLocalFile(corruptPath)));
    QVERIFY(!preview.playing());
    QCOMPARE(preview.positionMs(), 0);
    QCOMPARE(preview.durationMs(), 0);
}

void AudioPreviewControllerTest::stopsCurrentPreviewWhenNewSourceIsMissing()
{
    const QString fixture =
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    QVERIFY(preview.pollTimer_.isActive());

    preview.play(QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/missing.wav")));

    ag_playback_snapshot snapshot{};
    QCOMPARE(ag_player_snapshot(preview.player_, &snapshot), AG_OK);
    QCOMPARE(snapshot.state, AG_STOPPED);
    QVERIFY(!preview.pollTimer_.isActive());
    QVERIFY(!preview.hasSource());
}

void AudioPreviewControllerTest::previewsTimelineWithoutTemporaryRender()
{
    const QString fixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    QVariantMap clip;
    clip.insert(QStringLiteral("path"), fixture);
    clip.insert(QStringLiteral("timelineStartMs"), 0);
    clip.insert(QStringLiteral("inMs"), 0);
    clip.insert(QStringLiteral("outMs"), 500);
    clip.insert(QStringLiteral("timelineDurationMs"), 500);
    clip.insert(QStringLiteral("fadeInMs"), 0);
    clip.insert(QStringLiteral("fadeOutMs"), 0);
    clip.insert(QStringLiteral("gain"), 1.0);
    clip.insert(QStringLiteral("pan"), 0.0);
    clip.insert(QStringLiteral("loopMode"), QStringLiteral("Off"));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    QVERIFY(preview.playTimeline({clip}));
    QVERIFY(preview.isTimelinePreview());
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    QCOMPARE(preview.sourcePath(), QStringLiteral("agplayer://timeline-preview"));
    QCOMPARE(preview.playbackPath_, QString());
    QVERIFY(preview.durationMs() >= 450);

    preview.seek(250);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= 200, 2'000);
    preview.pause();
    QTRY_VERIFY_WITH_TIMEOUT(!preview.playing(), 2'000);
    preview.resume();
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    preview.stop();
    QVERIFY(!preview.hasSource());
    QVERIFY(!preview.isTimelinePreview());

    // The editor hands the loop range to the audio engine.  The controller
    // must remain in timeline-preview mode after more than one loop duration;
    // QML must not seek it back after a delayed state notification.
    clip.insert(QStringLiteral("outMs"), 1500);
    clip.insert(QStringLiteral("timelineDurationMs"), 1500);
    QVERIFY(preview.playTimeline({clip}, 500, 500, 1000));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    QTest::qWait(1'700);
    QVERIFY(preview.playing());
    QVERIFY(preview.positionMs() >= 500 && preview.positionMs() < 1000);
    preview.stop();

    clip.remove(QStringLiteral("loopMode"));
    clip.insert(QStringLiteral("outMs"), 500);
    clip.insert(QStringLiteral("timelineDurationMs"), 500);
    clip.insert(QStringLiteral("speedRatio"), 2.0);
    clip.insert(QStringLiteral("keepPitch"), true);
    clip.insert(QStringLiteral("loopMode"), QStringLiteral("OneShot"));
    QVERIFY(preview.playTimeline({clip}));
    QTRY_VERIFY_WITH_TIMEOUT(preview.durationMs() > 0, 2'000);
    QVERIFY(preview.durationMs() <= 300);
    preview.stop();
}

QTEST_MAIN(AudioPreviewControllerTest)
#include "audio_preview_controller_test.moc"
