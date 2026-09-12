#include "library_model.hpp"
#include "replay_gain_scanner.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTemporaryDir>

#include <cmath>

class ReplayGainScannerTest final : public QObject {
    Q_OBJECT

private slots:
    void scansWithoutChangingSourceFile();
    void storesResultOnTrack();
    void scansLibraryAndComputesAlbumGain();
};

namespace {
QByteArray fileHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

QString fixturePath()
{
    return QString::fromLocal8Bit(qgetenv("AGPLAYER_TEST_AUDIO"));
}
}

void ReplayGainScannerTest::scansWithoutChangingSourceFile()
{
    const QString path = fixturePath();
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
    const QByteArray before = fileHash(path);

    const ReplayGainAnalysis analysis = ReplayGainScanner::analyzeFile(path);

    QVERIFY2(analysis.success, qPrintable(analysis.error));
    QVERIFY(std::isfinite(analysis.loudnessLufs));
    QVERIFY(std::isfinite(analysis.gainDb));
    QVERIFY(analysis.peak > 0.0 && analysis.peak <= 1.01);
    QCOMPARE(fileHash(path), before);
}

void ReplayGainScannerTest::storesResultOnTrack()
{
    LibraryModel library;
    TrackRecord track;
    track.path = fixturePath();
    track.title = QStringLiteral("fixture");
    track.available = true;
    QVERIFY(library.append(track));

    ReplayGainScanner scanner(&library);
    const ReplayGainAnalysis analysis = ReplayGainScanner::analyzeFile(track.path);
    QVERIFY(analysis.success);
    QVERIFY(scanner.applyResult(library.tracks().front().trackId, analysis));

    const TrackRecord& stored = library.tracks().front();
    QVERIFY(stored.replayGainScanned);
    QCOMPARE(stored.replayGainTrackDb, analysis.gainDb);
    QCOMPARE(stored.replayPeak, analysis.peak);
}

void ReplayGainScannerTest::scansLibraryAndComputesAlbumGain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = dir.filePath(QStringLiteral("one.wav"));
    const QString second = dir.filePath(QStringLiteral("two.wav"));
    QVERIFY(QFile::copy(fixturePath(), first));
    QVERIFY(QFile::copy(fixturePath(), second));

    LibraryModel library;
    for (const QString& path : {first, second}) {
        TrackRecord track;
        track.path = path;
        track.title = QFileInfo(path).baseName();
        track.album = QStringLiteral("album");
        track.available = true;
        QVERIFY(library.append(track));
    }
    ReplayGainScanner scanner(&library);
    QVERIFY(scanner.scanAll());
    QTRY_VERIFY_WITH_TIMEOUT(!scanner.running(), 10'000);
    QCOMPARE(scanner.progress(), 1.0);
    QVERIFY(library.tracks().at(0).replayGainScanned);
    QVERIFY(library.tracks().at(1).replayGainScanned);
    QCOMPARE(library.tracks().at(0).replayGainAlbumDb,
             library.tracks().at(1).replayGainAlbumDb);
}

QTEST_MAIN(ReplayGainScannerTest)
#include "replay_gain_scanner_test.moc"
