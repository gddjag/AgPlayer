#include <QDir>
#include <QTemporaryFile>
#include <QTest>

#include <agplayer/c_api.h>

#include <cmath>

#include "bpm_fixture.hpp"

class BpmAnalyzerTest : public QObject {
    Q_OBJECT

private slots:
    void analyzeKnownBpm120()
    {
        QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_120_XXXXXX.wav")));
        QVERIFY(tempFile.open());
        tempFile.close();

        QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 120, 8));

        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(tempFile.fileName().toUtf8().constData(), &result), AG_OK);
        QVERIFY(std::abs(result.bpm - 120.0) < 1.0);
        QVERIFY(result.confidence > 80.0);
    }

    void analyzeKnownBpm90()
    {
        QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_90_XXXXXX.wav")));
        QVERIFY(tempFile.open());
        tempFile.close();

        QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 90, 10));

        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(tempFile.fileName().toUtf8().constData(), &result), AG_OK);
        QVERIFY(std::abs(result.bpm - 90.0) < 1.0);
        QVERIFY(result.confidence > 80.0);
    }

    void analyzeKnownBpm140()
    {
        QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_140_XXXXXX.wav")));
        QVERIFY(tempFile.open());
        tempFile.close();

        QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 140, 8));

        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(tempFile.fileName().toUtf8().constData(), &result), AG_OK);
        QVERIFY(std::abs(result.bpm - 140.0) < 1.0);
        QVERIFY(result.confidence > 80.0);
    }

    void invalidArgumentsReturnError()
    {
        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(nullptr, &result), AG_INVALID_ARGUMENT);
        QCOMPARE(ag_bpm_analyze("some/path.wav", nullptr), AG_INVALID_ARGUMENT);
    }

    void missingFileReturnsError()
    {
        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze("/nonexistent/file/that/does/not/exist.wav", &result),
                 AG_IO_ERROR);
        QCOMPARE(result.bpm, 0.0);
        QCOMPARE(result.confidence, 0.0);
    }
};

QTEST_MAIN(BpmAnalyzerTest)

#include "bpm_analyzer_test.moc"
