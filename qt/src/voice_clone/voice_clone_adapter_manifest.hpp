#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace agplayer::voice_clone {

struct AdapterRuntime {
    QString id;
    QString root;
    bool shared = false;
};

struct AdapterLauncher {
    QString id;
    QString kind;
    QString relativePath;
    bool shared = false;
};

struct VoiceCloneAdapterManifest {
    QString adapterId;
    QString adapterVersion;
    int protocolVersion = 0;
    AdapterRuntime runtime;
    QString defaultLauncherId;
    QVector<AdapterLauncher> launchers;
};

struct AdapterManifestParseResult {
    VoiceCloneAdapterManifest manifest;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

struct AdapterLauncherResolution {
    AdapterLauncher launcher;
    QString absolutePath;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

AdapterManifestParseResult parseAdapterManifest(const QByteArray& json);
AdapterLauncherResolution resolveAdapterLauncher(const VoiceCloneAdapterManifest& manifest,
                                                 const QString& launcherId,
                                                 const QString& adapterPackRoot);

} // namespace agplayer::voice_clone
