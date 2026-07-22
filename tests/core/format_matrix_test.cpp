// format_matrix_test: validates that every required Phase 1 audio format opens,
// decodes, seeks, and progresses through the public C ABI with the null backend.
//
// Fixtures live under tests/fixtures/generated/ (gitignored) and are generated
// by the FFmpeg CLI. The fixture directory is communicated through the
// AGPLAYER_FIXTURES_DIR preprocessor define. When the directory is absent the
// test is skipped via QSKIP so CI without fixtures does not fail.

#include <agplayer/c_api.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTest>

#include <array>
#include <chrono>
#include <thread>

class FormatMatrixTest final : public QObject {
    Q_OBJECT

private slots:
    void openDecodeSeek_data();
    void openDecodeSeek();
};

namespace {

struct FormatEntry {
    const char* extension;
    const char* fileName;
};

constexpr std::array<FormatEntry, 8> kFormats{{
    {"wav",  "source.wav"},
    {"mp3",  "sample.mp3"},
    {"flac", "sample.flac"},
    {"aac",  "sample.aac"},
    {"m4a",  "sample.m4a"},
    {"ogg",  "sample.ogg"},
    {"opus", "sample.opus"},
    {"wma",  "sample.wma"},
}};

QString resolveFixturesDir()
{
#ifdef AGPLAYER_FIXTURES_DIR
    return QString::fromUtf8(AGPLAYER_FIXTURES_DIR);
#else
    return QString();
#endif
}

} // namespace

void FormatMatrixTest::openDecodeSeek_data()
{
    QTest::addColumn<QString>("extension");
    QTest::addColumn<QString>("filePath");

    const QString fixturesDir = resolveFixturesDir();
    if (fixturesDir.isEmpty() || !QDir(fixturesDir).exists()) {
        return;
    }

    for (const auto& entry : kFormats) {
        const QString path = QDir(fixturesDir).filePath(
            QString::fromLatin1(entry.fileName));
        QTest::newRow(entry.extension) << QString::fromLatin1(entry.extension)
                                       << path;
    }
}

void FormatMatrixTest::openDecodeSeek()
{
    const QString fixturesDir = resolveFixturesDir();
    if (fixturesDir.isEmpty() || !QDir(fixturesDir).exists()) {
        QSKIP("Fixture directory not found; generate fixtures with FFmpeg first.");
    }

    QFETCH(QString, extension);
    QFETCH(QString, filePath);

    if (!QFileInfo::exists(filePath)) {
        QFAIL(qPrintable(QStringLiteral("Fixture file missing for '%1': %2")
                             .arg(extension, filePath)));
    }

    ag_player_config config{};
    config.backend = AG_AUDIO_BACKEND_NULL;
    config.buffer_frames = 4'096U;

    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    QVERIFY(player != nullptr);

    const QByteArray utf8Path = filePath.toUtf8();
    const ag_result loadResult = ag_player_load(player, utf8Path.constData());
    if (loadResult != AG_OK) {
        ag_player_destroy(player);
        QFAIL(qPrintable(QStringLiteral("ag_player_load failed (%1) for %2: %3")
                             .arg(static_cast<int>(loadResult))
                             .arg(extension)
                             .arg(filePath)));
    }

    const ag_result playResult = ag_player_play(player);
    if (playResult != AG_OK) {
        ag_player_destroy(player);
        QFAIL(qPrintable(QStringLiteral("ag_player_play failed (%1) for %2")
                             .arg(static_cast<int>(playResult))
                             .arg(extension)));
    }

    // Wait until the position advances at least 500ms (timeout 5s).
    QElapsedTimer elapsed;
    elapsed.start();
    bool progressed = false;
    while (!elapsed.hasExpired(5'000)) {
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(player, &snapshot), AG_OK);
        if (snapshot.state == AG_ERROR) {
            ag_player_destroy(player);
            QFAIL(qPrintable(QStringLiteral(
                "Playback entered AG_ERROR for %1").arg(extension)));
        }
        if (snapshot.position_ms >= 500) {
            progressed = true;
            break;
        }
        QTest::qSleep(20);
    }
    if (!progressed) {
        ag_player_destroy(player);
        QFAIL(qPrintable(QStringLiteral(
            "Position did not advance >=500ms within 5s for %1").arg(extension)));
    }

    // Seek to 2000ms and poll until position is near 2000ms (+/-500ms).
    const ag_result seekResult = ag_player_seek(player, 2000);
    if (seekResult != AG_OK) {
        ag_player_destroy(player);
        QFAIL(qPrintable(QStringLiteral("ag_player_seek(2000) failed (%1) for %2")
                             .arg(static_cast<int>(seekResult))
                             .arg(extension)));
    }

    bool seeked = false;
    elapsed.restart();
    while (!elapsed.hasExpired(5'000)) {
        ag_playback_snapshot snapshot{};
        QCOMPARE(ag_player_snapshot(player, &snapshot), AG_OK);
        if (snapshot.state == AG_ERROR) {
            ag_player_destroy(player);
            QFAIL(qPrintable(QStringLiteral(
                "Playback entered AG_ERROR after seek for %1").arg(extension)));
        }
        const long long delta = snapshot.position_ms - 2000LL;
        if (delta < 0 ? -delta <= 500LL : delta <= 500LL) {
            seeked = true;
            break;
        }
        QTest::qSleep(20);
    }
    if (!seeked) {
        ag_player_destroy(player);
        QFAIL(qPrintable(QStringLiteral(
            "Position did not reach 2000ms (+/-500ms) within 5s for %1")
                .arg(extension)));
    }

    QCOMPARE(ag_player_stop(player), AG_OK);
    ag_player_destroy(player);
}

QTEST_GUILESS_MAIN(FormatMatrixTest)

#include "format_matrix_test.moc"
