#include "voice_clone_host_controller.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

namespace {

constexpr auto kPluginFile = AGPLAYER_VOICE_CLONE_TEST_PLUGIN;

QJsonObject validManifest(const QString& libraryName)
{
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("pluginId"), QStringLiteral("agplayer.voice-clone")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("availableVersion"), QStringLiteral("1.0.0")},
            {QStringLiteral("platform"), QStringLiteral("windows")},
            {QStringLiteral("architecture"), QStringLiteral("x86_64")},
            {QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.0.0")},
            {QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("library"), libraryName}};
}

bool writeManifest(const QString& root, const QJsonObject& manifest)
{
    QFile file(QDir(root).filePath(QStringLiteral("agplayer-voice-clone.json")));
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact)) > 0;
}

QString copyTestPlugin(const QString& root)
{
    const QFileInfo source(QString::fromUtf8(kPluginFile));
    const QString target = QDir(root).filePath(source.fileName());
    return QFile::copy(source.absoluteFilePath(), target) ? source.fileName() : QString();
}

bool createWindowsLink(const QString& link, const QString& target, const bool directory)
{
#ifdef Q_OS_WIN
    QStringList arguments{QStringLiteral("/d"),
                          QStringLiteral("/c"),
                          QStringLiteral("mklink")};
    if (directory) arguments.append(QStringLiteral("/J"));
    arguments.append(QDir::toNativeSeparators(link));
    arguments.append(QDir::toNativeSeparators(target));
    return QProcess::execute(QStringLiteral("cmd.exe"), arguments) == 0;
#else
    Q_UNUSED(link)
    Q_UNUSED(target)
    Q_UNUSED(directory)
    return false;
#endif
}

class EnvironmentGuard {
public:
    EnvironmentGuard(const char* name, const QByteArray& value)
        : name_(name), old_(qgetenv(name)), hadValue_(!old_.isNull())
    {
        qputenv(name_, value);
    }
    ~EnvironmentGuard()
    {
        if (hadValue_) qputenv(name_, old_);
        else qunsetenv(name_);
    }

private:
    const char* name_;
    QByteArray old_;
    bool hadValue_;
};

class ApplicationVersionGuard {
public:
    explicit ApplicationVersionGuard(const QString& version)
        : old_(QCoreApplication::applicationVersion())
    {
        QCoreApplication::setApplicationVersion(version);
    }
    ~ApplicationVersionGuard() { QCoreApplication::setApplicationVersion(old_); }

private:
    QString old_;
};

class WindowsLinkGuard {
public:
    WindowsLinkGuard(QString path, const bool directory)
        : path_(std::move(path)), directory_(directory)
    {
    }
    ~WindowsLinkGuard()
    {
        if (path_.isEmpty()) return;
        if (directory_) QDir().rmdir(path_);
        else QFile::remove(path_);
    }

    bool remove()
    {
        const bool removed = directory_ ? QDir().rmdir(path_)
                                        : QFile::remove(path_);
        if (removed) path_.clear();
        return removed;
    }

private:
    QString path_;
    bool directory_;
};

} // namespace

class VoiceCloneHostControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void startsAbsentWithoutDiscoverySideEffects();
    void rejectsInvalidOrIncompatibleManifest_data();
    void rejectsInvalidOrIncompatibleManifest();
    void discoversCompatiblePluginWithoutLoadingIt();
    void loadsAndUnloadsCompatiblePlugin();
    void reportsUpdateAvailableWithoutLoadingPlugin();
    void reportsPluginLoadFailure();
    void rejectsPluginWithoutQmlEntryPoint();
    void rejectsFractionalManifestProtocolVersions_data();
    void rejectsFractionalManifestProtocolVersions();
    void acceptsMissingOrEmptyAvailableVersion_data();
    void acceptsMissingOrEmptyAvailableVersion();
    void usesApplicationVersionForCompatibility();
    void rejectsPluginRootWithReparseAncestor();
    void rejectsLinkedManifest();
    void rejectsLinkedPluginLibrary();
};

void VoiceCloneHostControllerTest::startsAbsentWithoutDiscoverySideEffects()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QDir().mkpath(temporary.filePath(QStringLiteral("registry/models"))));
    QVERIFY(QDir().mkpath(temporary.filePath(QStringLiteral("models/voice-clone/bait"))));
    QVERIFY(QDir().mkpath(temporary.filePath(QStringLiteral("runtime/python"))));
    QFile bait(temporary.filePath(QStringLiteral("registry/models/broken.json")));
    QVERIFY(bait.open(QIODevice::WriteOnly));
    QCOMPARE(bait.write("not-json"), 8);
    bait.close();
    const QString copiedPlugin = copyTestPlugin(temporary.path());
    QVERIFY(!copiedPlugin.isEmpty());

    const QString loadMarker = temporary.filePath(QStringLiteral("loaded.marker"));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());
    EnvironmentGuard marker("AGPLAYER_VOICE_CLONE_TEST_LOAD_MARKER", loadMarker.toUtf8());

    VoiceCloneHostController host;
    QCOMPARE(host.state(), VoiceCloneHostController::Absent);
    QVERIFY(!QFileInfo::exists(loadMarker));
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Absent);
    QVERIFY(!QFileInfo::exists(loadMarker));
    QVERIFY(QFileInfo::exists(bait.fileName()));
}

void VoiceCloneHostControllerTest::rejectsInvalidOrIncompatibleManifest_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<QString>("errorPart");
    QTest::newRow("schema") << QStringLiteral("schemaVersion") << QVariant(2)
                              << QStringLiteral("schema");
    QTest::newRow("platform") << QStringLiteral("platform") << QVariant(QStringLiteral("linux"))
                                << QStringLiteral("platform");
    QTest::newRow("architecture") << QStringLiteral("architecture") << QVariant(QStringLiteral("arm64"))
                                    << QStringLiteral("architecture");
    QTest::newRow("minimum player") << QStringLiteral("minimumPlayerVersion") << QVariant(QStringLiteral("9.0.0"))
                                      << QStringLiteral("player");
    QTest::newRow("protocol") << QStringLiteral("protocolVersion") << QVariant(2)
                                << QStringLiteral("protocol");
    QTest::newRow("available version type")
        << QStringLiteral("availableVersion") << QVariant(1)
        << QStringLiteral("version");
    QTest::newRow("library traversal") << QStringLiteral("library") << QVariant(QStringLiteral("../plugin.dll"))
                                         << QStringLiteral("library");
}

void VoiceCloneHostControllerTest::rejectsInvalidOrIncompatibleManifest()
{
    QFETCH(QString, field);
    QFETCH(QVariant, value);
    QFETCH(QString, errorPart);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QJsonObject manifest = validManifest(QStringLiteral("plugin.dll"));
    manifest.insert(field, QJsonValue::fromVariant(value));
    QVERIFY(writeManifest(temporary.path(), manifest));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Invalid);
    QVERIFY2(host.errorString().contains(errorPart, Qt::CaseInsensitive),
             qPrintable(host.errorString()));
}

void VoiceCloneHostControllerTest::discoversCompatiblePluginWithoutLoadingIt()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QVERIFY(writeManifest(temporary.path(), validManifest(library)));
    const QString loadMarker = temporary.filePath(QStringLiteral("loaded.marker"));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());
    EnvironmentGuard marker("AGPLAYER_VOICE_CLONE_TEST_LOAD_MARKER", loadMarker.toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    QCOMPARE(host.pluginVersion(), QStringLiteral("1.0.0"));
    QVERIFY(!host.pluginLoaded());
    QVERIFY(!QFileInfo::exists(loadMarker));
}

void VoiceCloneHostControllerTest::loadsAndUnloadsCompatiblePlugin()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QVERIFY(writeManifest(temporary.path(), validManifest(library)));
    const QString loadMarker = temporary.filePath(QStringLiteral("loaded.marker"));
    const QString shutdownMarker = temporary.filePath(QStringLiteral("shutdown.marker"));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());
    EnvironmentGuard load("AGPLAYER_VOICE_CLONE_TEST_LOAD_MARKER", loadMarker.toUtf8());
    EnvironmentGuard shutdown("AGPLAYER_VOICE_CLONE_TEST_SHUTDOWN_MARKER", shutdownMarker.toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QVERIFY2(host.openPlugin(), qPrintable(host.errorString()));
    QCOMPARE(host.state(), VoiceCloneHostController::Loaded);
    QVERIFY(host.pluginLoaded());
    QVERIFY(host.pluginController() != nullptr);
    QCOMPARE(host.mainQmlUrl(), QUrl(QStringLiteral("qrc:/AgPlayer/VoiceClonePage.qml")));
    QVERIFY(QFileInfo::exists(loadMarker));

    host.closePlugin();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    QVERIFY(!host.pluginLoaded());
    QVERIFY(host.pluginController() == nullptr);
    QVERIFY(QFileInfo::exists(shutdownMarker));
    const QString unloadedLibrary =
        temporary.filePath(library + QStringLiteral(".unloaded"));
    QVERIFY(QFile::rename(temporary.filePath(library), unloadedLibrary));
    QVERIFY(QFile::remove(unloadedLibrary));
}

void VoiceCloneHostControllerTest::reportsUpdateAvailableWithoutLoadingPlugin()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QJsonObject manifest = validManifest(library);
    manifest.insert(QStringLiteral("availableVersion"), QStringLiteral("1.1.0"));
    QVERIFY(writeManifest(temporary.path(), manifest));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::UpdateAvailable);
    QCOMPARE(host.availableVersion(), QStringLiteral("1.1.0"));
    QVERIFY(!host.pluginLoaded());
}

void VoiceCloneHostControllerTest::reportsPluginLoadFailure()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = QStringLiteral("broken.dll");
    QFile broken(temporary.filePath(library));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    QCOMPARE(broken.write("not-a-library"), 13);
    broken.close();
    QVERIFY(writeManifest(temporary.path(), validManifest(library)));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    QVERIFY(!host.openPlugin());
    QCOMPARE(host.state(), VoiceCloneHostController::Failed);
    QVERIFY(!host.errorString().isEmpty());
    QVERIFY(!host.pluginLoaded());
}

void VoiceCloneHostControllerTest::rejectsPluginWithoutQmlEntryPoint()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QVERIFY(writeManifest(temporary.path(), validManifest(library)));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());
    EnvironmentGuard emptyQml("AGPLAYER_VOICE_CLONE_TEST_EMPTY_QML", QByteArray("1"));

    VoiceCloneHostController host;
    host.refresh();
    QVERIFY(!host.openPlugin());
    QCOMPARE(host.state(), VoiceCloneHostController::Failed);
    QVERIFY(host.errorString().contains(QStringLiteral("QML"), Qt::CaseInsensitive));
}

void VoiceCloneHostControllerTest::rejectsFractionalManifestProtocolVersions_data()
{
    QTest::addColumn<QString>("field");
    QTest::newRow("schema") << QStringLiteral("schemaVersion");
    QTest::newRow("protocol") << QStringLiteral("protocolVersion");
}

void VoiceCloneHostControllerTest::rejectsFractionalManifestProtocolVersions()
{
    QFETCH(QString, field);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QJsonObject manifest = validManifest(library);
    manifest.insert(field, 1.5);
    QVERIFY(writeManifest(temporary.path(), manifest));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Invalid);
    QVERIFY(host.errorString().contains(
        field.startsWith(QStringLiteral("schema")) ? QStringLiteral("schema")
                                                    : QStringLiteral("protocol"),
        Qt::CaseInsensitive));
}

void VoiceCloneHostControllerTest::acceptsMissingOrEmptyAvailableVersion_data()
{
    QTest::addColumn<bool>("removeField");
    QTest::newRow("missing") << true;
    QTest::newRow("empty") << false;
}

void VoiceCloneHostControllerTest::acceptsMissingOrEmptyAvailableVersion()
{
    QFETCH(bool, removeField);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QJsonObject manifest = validManifest(library);
    if (removeField) manifest.remove(QStringLiteral("availableVersion"));
    else manifest.insert(QStringLiteral("availableVersion"), QString());
    QVERIFY(writeManifest(temporary.path(), manifest));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    QCOMPARE(host.availableVersion(), host.pluginVersion());
}

void VoiceCloneHostControllerTest::usesApplicationVersionForCompatibility()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = copyTestPlugin(temporary.path());
    QVERIFY(!library.isEmpty());
    QJsonObject manifest = validManifest(library);
    manifest.insert(QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.1.0"));
    QVERIFY(writeManifest(temporary.path(), manifest));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", temporary.path().toUtf8());
    ApplicationVersionGuard version(QStringLiteral("1.2.0"));

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
}

void VoiceCloneHostControllerTest::rejectsPluginRootWithReparseAncestor()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reparse-point contract");
#else
    QTemporaryDir target;
    QTemporaryDir container;
    QVERIFY(target.isValid());
    QVERIFY(container.isValid());
    const QString realRoot = target.filePath(QStringLiteral("plugin"));
    QVERIFY(QDir().mkpath(realRoot));
    const QString library = copyTestPlugin(realRoot);
    QVERIFY(!library.isEmpty());
    QVERIFY(writeManifest(realRoot, validManifest(library)));
    const QString junction = container.filePath(QStringLiteral("linked-parent"));
    QVERIFY(createWindowsLink(junction, target.path(), true));
    WindowsLinkGuard junctionGuard(junction, true);
    const QString linkedRoot = QDir(junction).filePath(QStringLiteral("plugin"));
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", linkedRoot.toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Invalid);
    QVERIFY(host.errorString().contains(QStringLiteral("link"), Qt::CaseInsensitive)
            || host.errorString().contains(QStringLiteral("reparse"), Qt::CaseInsensitive));
    QVERIFY(junctionGuard.remove());
#endif
}

void VoiceCloneHostControllerTest::rejectsLinkedManifest()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reparse-point contract");
#else
    QTemporaryDir rootDirectory;
    QTemporaryDir outside;
    QVERIFY(rootDirectory.isValid());
    QVERIFY(outside.isValid());
    const QString library = copyTestPlugin(rootDirectory.path());
    QVERIFY(!library.isEmpty());
    QVERIFY(writeManifest(outside.path(), validManifest(library)));
    const QString link = rootDirectory.filePath(QStringLiteral("agplayer-voice-clone.json"));
    const QString target = outside.filePath(QStringLiteral("agplayer-voice-clone.json"));
    if (!createWindowsLink(link, target, false)) {
        QSKIP("File symbolic links are unavailable");
    }
    WindowsLinkGuard linkGuard(link, false);
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", rootDirectory.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Invalid);
    QVERIFY(host.errorString().contains(QStringLiteral("manifest"), Qt::CaseInsensitive)
            && (host.errorString().contains(QStringLiteral("link"), Qt::CaseInsensitive)
                || host.errorString().contains(QStringLiteral("reparse"), Qt::CaseInsensitive)));
    QVERIFY(linkGuard.remove());
#endif
}

void VoiceCloneHostControllerTest::rejectsLinkedPluginLibrary()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reparse-point contract");
#else
    QTemporaryDir rootDirectory;
    QTemporaryDir outside;
    QVERIFY(rootDirectory.isValid());
    QVERIFY(outside.isValid());
    const QString library = copyTestPlugin(outside.path());
    QVERIFY(!library.isEmpty());
    QVERIFY(writeManifest(rootDirectory.path(), validManifest(library)));
    const QString link = rootDirectory.filePath(library);
    const QString target = outside.filePath(library);
    if (!createWindowsLink(link, target, false)) {
        QSKIP("File symbolic links are unavailable");
    }
    WindowsLinkGuard linkGuard(link, false);
    EnvironmentGuard root("AGPLAYER_VOICE_CLONE_ROOT", rootDirectory.path().toUtf8());

    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Invalid);
    QVERIFY(host.errorString().contains(QStringLiteral("library"), Qt::CaseInsensitive)
            && (host.errorString().contains(QStringLiteral("link"), Qt::CaseInsensitive)
                || host.errorString().contains(QStringLiteral("reparse"), Qt::CaseInsensitive)));
    QVERIFY(linkGuard.remove());
#endif
}

QTEST_MAIN(VoiceCloneHostControllerTest)

#include "voice_clone_host_controller_test.moc"
