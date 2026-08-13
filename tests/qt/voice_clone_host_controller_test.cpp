#include "voice_clone_host_controller.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

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
    QVERIFY(QFile::rename(temporary.filePath(library),
                          temporary.filePath(library + QStringLiteral(".unloaded"))));
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

QTEST_MAIN(VoiceCloneHostControllerTest)

#include "voice_clone_host_controller_test.moc"
