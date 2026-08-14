#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>

class QObject;

struct AgPlayerVoiceClonePluginMetadata {
    QString id;
    QString displayName;
    QString version;
};

class AgPlayerVoiceClonePluginInterface {
public:
    virtual ~AgPlayerVoiceClonePluginInterface() = default;

    virtual AgPlayerVoiceClonePluginMetadata metadata() const = 0;
    virtual QObject* controller() = 0;
    virtual QUrl mainQmlUrl() const = 0;
    virtual int protocolVersion() const = 0;
    virtual void shutdown() = 0;
};

#define AgPlayerVoiceClonePluginInterface_iid \
    "com.agplayer.VoiceClonePluginInterface/1.0"
Q_DECLARE_INTERFACE(AgPlayerVoiceClonePluginInterface,
                    AgPlayerVoiceClonePluginInterface_iid)
