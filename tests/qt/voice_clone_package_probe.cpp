#include "voice_clone/voice_clone_adapter_manifest.hpp"
#include "voice_clone/voice_clone_host_controller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <iostream>

using namespace agplayer::voice_clone;

namespace {

int fail(const QString& message)
{
    std::cerr << message.toStdString() << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return fail(QStringLiteral("usage: voice_clone_package_probe <plugin-root>"));

    const QString root = QDir::fromNativeSeparators(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
    if (!QFileInfo(root).isDir()) return fail(QStringLiteral("plugin root is missing"));

    qputenv("AGPLAYER_VOICE_CLONE_ROOT", root.toUtf8());
    VoiceCloneHostController host;
    host.refresh();
    if (host.state() != VoiceCloneHostController::Compatible)
        return fail(QStringLiteral("host rejected package: %1").arg(host.errorString()));
    if (!host.openPlugin())
        return fail(QStringLiteral("QPluginLoader rejected package: %1").arg(host.errorString()));
    if (host.pluginController() == nullptr)
        return fail(QStringLiteral("plugin controller is missing"));

    const QUrl mainQml = host.mainQmlUrl();
    if (!mainQml.isValid() || mainQml.scheme() != QStringLiteral("qrc"))
        return fail(QStringLiteral("plugin mainQml URL is invalid"));
    QFile workspace(QStringLiteral(":") + mainQml.path());
    if (!workspace.open(QIODevice::ReadOnly)
        || !workspace.readAll().contains("VoiceCloneModelBar"))
        return fail(QStringLiteral("plugin mainQml resource is unreadable"));

    const QStringList adapters{QStringLiteral("qwen"),
                               QStringLiteral("indextts25"),
                               QStringLiteral("cosyvoice3")};
    for (const QString& adapterId : adapters) {
        const QString adapterRoot = QDir(root).filePath(
            QStringLiteral("adapters/%1/1.0.0").arg(adapterId));
        QFile manifestFile(QDir(adapterRoot).filePath(QStringLiteral("adapter.json")));
        if (!manifestFile.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("adapter manifest is missing: %1").arg(adapterId));
        const AdapterManifestParseResult parsed = parseAdapterManifest(manifestFile.readAll());
        if (!parsed.isValid() || parsed.manifest.adapterId != adapterId)
            return fail(QStringLiteral("adapter manifest is invalid: %1").arg(adapterId));
        const AdapterLauncherResolution launcher = resolveAdapterLauncher(
            parsed.manifest, parsed.manifest.defaultLauncherId, adapterRoot);
        if (!launcher.isValid() || !QFileInfo(launcher.absolutePath).isFile())
            return fail(QStringLiteral("adapter launcher is missing: %1").arg(adapterId));
    }

    host.closePlugin();
    if (host.pluginLoaded()) return fail(QStringLiteral("plugin did not unload"));
    qunsetenv("AGPLAYER_VOICE_CLONE_ROOT");
    return 0;
}
