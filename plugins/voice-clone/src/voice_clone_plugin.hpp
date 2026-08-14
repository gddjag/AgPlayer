#pragma once

#include "plugins/voice_clone_plugin_interface.hpp"

#include <QObject>

#include <memory>

namespace agplayer::voice_clone {

class VoiceCloneController;
class VoiceClonePackageManager;
class VoiceCloneRuntimePackageManager;

class VoiceClonePlugin final : public QObject, public AgPlayerVoiceClonePluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AgPlayerVoiceClonePluginInterface_iid)
    Q_INTERFACES(AgPlayerVoiceClonePluginInterface)

public:
    VoiceClonePlugin();
    ~VoiceClonePlugin() override;

    AgPlayerVoiceClonePluginMetadata metadata() const override;
    QObject* controller() override;
    QUrl mainQmlUrl() const override;
    int protocolVersion() const override;
    void shutdown() override;

private:
    std::unique_ptr<VoiceClonePackageManager> packageManager_;
    std::unique_ptr<VoiceCloneRuntimePackageManager> runtimeManager_;
    std::unique_ptr<VoiceCloneController> controller_;
};

} // namespace agplayer::voice_clone
