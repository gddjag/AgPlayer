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
    void stopsMainPlayerBeforePreviewAndDoesNotResume();
    void startingMainPlayerStopsActivePreview();
    void appliesUpdatedDspParametersToActivePreview();
    void rejectsMissingFiles();
    void clearsOldStateWhenNewSourceCannotLoad();
    void stopsCurrentPreviewWhenNewSourceIsMissing();
    void switchesSourceAtTheSameAbsolutePositionAndPlaybackState();
    void preservesActiveDspWhenSwitchingSources();
};

void AudioPreviewControllerTest::startingMainPlayerStopsActivePreview()
{
    const QString fixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    ag_player* mainPlayer = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 0U};
    QCOMPARE(ag_player_create_with_config(&config, &mainPlayer), AG_OK);
    const QByteArray fixtureUtf8 = fixture.toUtf8();
    QCOMPARE(ag_player_load(mainPlayer, fixtureUtf8.constData()), AG_OK);
    PlaybackController mainPlayback(mainPlayer);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL, &mainPlayback);

    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    mainPlayback.play();
    QTRY_COMPARE_WITH_TIMEOUT(mainPlayback.state(),
                              PlaybackController::Playing, 2'000);
    QTRY_VERIFY_WITH_TIMEOUT(!preview.playing(), 2'000);
    QVERIFY(preview.hasSource());
    QVERIFY(!preview.playing());
    ag_player_destroy(mainPlayer);
}

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

void AudioPreviewControllerTest::stopsMainPlayerBeforePreviewAndDoesNotResume()
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

void AudioPreviewControllerTest::
switchesSourceAtTheSameAbsolutePositionAndPlaybackState()
{
    const QString fixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString stem = temporary.filePath(QStringLiteral("人声.wav"));
    QVERIFY(QFile::copy(fixture, stem));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    preview.seek(650);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= 600, 2'000);
    preview.pause();
    QTRY_VERIFY_WITH_TIMEOUT(!preview.playing(), 2'000);
    const qint64 before = preview.positionMs();

    QVERIFY(preview.switchSourcePreservingPosition(QUrl::fromLocalFile(stem)));
    QVERIFY(preview.isCurrentSource(QUrl::fromLocalFile(stem)));
    QVERIFY(!preview.playing());
    QVERIFY(qAbs(preview.positionMs() - before) <= 75);

    preview.resume();
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    const qint64 playingBefore = preview.positionMs();
    QVERIFY(preview.switchSourcePreservingPosition(QUrl::fromLocalFile(fixture)));
    QVERIFY(preview.playing());
    QVERIFY(qAbs(preview.positionMs() - playingBefore) <= 100);
}

void AudioPreviewControllerTest::preservesActiveDspWhenSwitchingSources()
{
    const QString fixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString stem = temporary.filePath(QStringLiteral("stem.wav"));
    QVERIFY(QFile::copy(fixture, stem));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    preview.play(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
    const qint64 originalDuration = preview.durationMs();
    preview.setDspParameters(1.5, 0, true, false, false, true);
    QTRY_VERIFY_WITH_TIMEOUT(!preview.processing(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.durationMs() < originalDuration, 10'000);
    preview.seek(650);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= 600, 2'000);
    const qint64 absoluteBeforeSwitch = preview.positionMs();

    QVERIFY(preview.switchSourcePreservingPosition(QUrl::fromLocalFile(stem)));
    QTRY_VERIFY_WITH_TIMEOUT(!preview.processing(), 10'000);
    QVERIFY(preview.isCurrentSource(QUrl::fromLocalFile(stem)));
    QVERIFY(preview.durationMs() < originalDuration);
    QVERIFY(preview.playing());
    QVERIFY(qAbs(preview.positionMs() - absoluteBeforeSwitch) <= 125);
}

QTEST_MAIN(AudioPreviewControllerTest)
#include "audio_preview_controller_test.moc"
