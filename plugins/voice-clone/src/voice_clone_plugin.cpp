#include "voice_clone_plugin.hpp"

#include "voice_clone_controller.hpp"
#include "voice_clone_package_manager.hpp"

#include <QCoreApplication>
#include <QDir>

namespace agplayer::voice_clone {

VoiceClonePlugin::VoiceClonePlugin()
{
    QString root = qEnvironmentVariable("AGPLAYER_VOICE_CLONE_ROOT");
    if (root.isEmpty())
        root = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins/voice-clone"));
    root = QDir::fromNativeSeparators(QDir(root).absolutePath());
    const QString modelsRoot = QDir(root).filePath(QStringLiteral("models/voice-clone"));
    QDir().mkpath(modelsRoot);
    packageManager_ = std::make_unique<VoiceClonePackageManager>(
        QDir(root).filePath(QStringLiteral("packages")));
    controller_ = std::make_unique<VoiceCloneController>(
        root, modelsRoot,
        packageManager_.get());
}

VoiceClonePlugin::~VoiceClonePlugin()
{
    shutdown();
}

AgPlayerVoiceClonePluginMetadata VoiceClonePlugin::metadata() const
{
    return {QStringLiteral("agplayer.voice-clone"),
            QStringLiteral("Voice Clone"),
            QStringLiteral("1.0.0")};
}

QObject* VoiceClonePlugin::controller()
{
    return controller_.get();
}

QUrl VoiceClonePlugin::mainQmlUrl() const
{
    return QUrl(QStringLiteral("qrc:/AgPlayer/VoiceClone/VoiceClonePage.qml"));
}

int VoiceClonePlugin::protocolVersion() const
{
    return 1;
}

void VoiceClonePlugin::shutdown()
{
    if (controller_) controller_->shutdown();
}

} // namespace agplayer::voice_clone
