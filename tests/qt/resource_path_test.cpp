#include "resource_path.hpp"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class ResourcePathTest final : public QObject {
    Q_OBJECT

private slots:
    void rootContainmentDoesNotCreateDoubleSeparators();
    void missingPathsUseCleanAbsoluteIdentity();
    void casePolicyMatchesThePlatform();
};

void ResourcePathTest::rootContainmentDoesNotCreateDoubleSeparators()
{
    const QString root = agplayer::qt::resourcePathIdentity(QDir::rootPath());
    QVERIFY(!root.isEmpty());
    QVERIFY(!root.endsWith(QStringLiteral("//")));
    QVERIFY(agplayer::qt::resourcePathIsWithin(
        QDir(root).filePath(QStringLiteral("music/track.mp3")), root));
    QVERIFY(agplayer::qt::resourcePathIsWithin(root, root));
#ifdef Q_OS_WIN
    const QString otherDrive = root.startsWith(QStringLiteral("C:"), Qt::CaseInsensitive)
        ? QStringLiteral("D:/music/track.mp3")
        : QStringLiteral("C:/music/track.mp3");
    QVERIFY(!agplayer::qt::resourcePathIsWithin(otherDrive, root));
#else
    QVERIFY(agplayer::qt::resourcePathIsWithin(
        QStringLiteral("/tmp/music/track.mp3"), QStringLiteral("/")));
#endif
}

void ResourcePathTest::missingPathsUseCleanAbsoluteIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString requested = QDir(dir.path()).filePath(
        QStringLiteral("missing/../future/song.mp3"));
    QCOMPARE(agplayer::qt::resourcePathIdentity(requested),
             QDir::fromNativeSeparators(QDir::cleanPath(
                 QFileInfo(requested).absoluteFilePath())));
}

void ResourcePathTest::casePolicyMatchesThePlatform()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString lower = QDir(dir.path()).filePath(QStringLiteral("future/song.mp3"));
    const QString upper = lower.toUpper();
#ifdef Q_OS_WIN
    QVERIFY(agplayer::qt::resourcePathsEqual(lower, upper));
#else
    QVERIFY(!agplayer::qt::resourcePathsEqual(lower, upper));
#endif
}

QTEST_GUILESS_MAIN(ResourcePathTest)
#include "resource_path_test.moc"
