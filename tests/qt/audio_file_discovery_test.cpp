#include "audio_file_discovery.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class AudioFileDiscoveryTest final : public QObject {
    Q_OBJECT

private slots:
    void videoExtensionsAreDistinctAndCaseInsensitive();
    void directoryExpansionRemainsAudioOnly();
};

void AudioFileDiscoveryTest::videoExtensionsAreDistinctAndCaseInsensitive()
{
    // Catches video formats being folded into the audio discovery list, or a
    // direct-path video suffix being rejected solely because of casing.
    const QStringList expected{
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v")};
    QCOMPARE(agplayer::qt::supportedVideoExtensions(), expected);
    for (const QString& extension : expected) {
        QVERIFY(!agplayer::qt::supportedAudioExtensions().contains(extension));
        QVERIFY(agplayer::qt::isSupportedVideoExtension(
            QStringLiteral(".") + extension.toUpper()));
    }
}

void AudioFileDiscoveryTest::directoryExpansionRemainsAudioOnly()
{
    // Catches a directory scan accidentally importing hidden video formats.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString& fileName : {
             QStringLiteral("track.MP3"), QStringLiteral("movie.MP4"),
             QStringLiteral("clip.mKv")}) {
        QFile file(directory.filePath(fileName));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    const QList<QUrl> expanded = agplayer::qt::expandAudioUrls(
        {QUrl::fromLocalFile(directory.path())});
    QCOMPARE(expanded.size(), 1);
    QCOMPARE(expanded.constFirst().fileName(), QStringLiteral("track.MP3"));
}

QTEST_MAIN(AudioFileDiscoveryTest)

#include "audio_file_discovery_test.moc"
