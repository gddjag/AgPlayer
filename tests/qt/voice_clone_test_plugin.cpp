#include "plugins/voice_clone_plugin_interface.hpp"

#include <QFile>
#include <QObject>

namespace {

void writeMarker(const char* environmentName)
{
    const QString path = qEnvironmentVariable(environmentName);
    if (path.isEmpty()) return;
    QFile marker(path);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write("1");
    }
}

class VoiceCloneTestPlugin final : public QObject,
                                   public AgPlayerVoiceClonePluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AgPlayerVoiceClonePluginInterface_iid)
    Q_INTERFACES(AgPlayerVoiceClonePluginInterface)

public:
    VoiceCloneTestPlugin() { writeMarker("AGPLAYER_VOICE_CLONE_TEST_LOAD_MARKER"); }

    AgPlayerVoiceClonePluginMetadata metadata() const override
    {
        return {QStringLiteral("agplayer.voice-clone"),
                QStringLiteral("Voice Clone Test Plugin"),
                QStringLiteral("1.0.0")};
    }

    QObject* controller() override { return &controller_; }
    QUrl mainQmlUrl() const override
    {
        if (qEnvironmentVariableIsSet("AGPLAYER_VOICE_CLONE_TEST_EMPTY_QML")) {
            return {};
        }
        return QUrl(QStringLiteral("qrc:/AgPlayer/VoiceClonePage.qml"));
    }
    int protocolVersion() const override { return 1; }
    void shutdown() override
    {
        writeMarker("AGPLAYER_VOICE_CLONE_TEST_SHUTDOWN_MARKER");
    }

private:
    QObject controller_;
};

} // namespace

#include "voice_clone_test_plugin.moc"
