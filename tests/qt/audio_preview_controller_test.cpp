#include "audio_preview_controller.hpp"

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
    void playsWithoutTouchingTheMainPlayer();
    void rejectsMissingFiles();
    void clearsOldStateWhenNewSourceCannotLoad();
    void stopsCurrentPreviewWhenNewSourceIsMissing();
};

void AudioPreviewControllerTest::playsWithoutTouchingTheMainPlayer()
{
    const QString fixture =
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/fixtures/sine-440hz.wav");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    preview.setVolume(0.25);
    QCOMPARE(preview.volume(), 0.25);

    preview.toggle(QUrl::fromLocalFile(fixture));
    QTRY_VERIFY_WITH_TIMEOUT(preview.hasSource(), 2'000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 2'000);
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

QTEST_MAIN(AudioPreviewControllerTest)
#include "audio_preview_controller_test.moc"
