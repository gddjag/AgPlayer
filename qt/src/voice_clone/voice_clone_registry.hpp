#pragma once

#include "voice_clone_manifest.hpp"

#include <QStringList>

namespace agplayer::voice_clone {

struct VoiceCloneDiagnostic {
    QString manifestPath;
    QString message;
};

struct VoiceCloneDiscovery {
    QVector<VoiceCloneModel> models;
    QVector<VoiceCloneDiagnostic> diagnostics;

    bool isValid() const { return diagnostics.isEmpty(); }
    QString errorString() const;
};

class VoiceCloneRegistry {
public:
    static VoiceCloneRegistry loadBuiltIn(const QString& registryPath);
    static VoiceCloneDiscovery discoverUserModels(const QString& portableRoot,
                                                  const QStringList& trustedAdapterIds);

    bool isValid() const { return error_.isEmpty(); }
    QString errorString() const { return error_; }
    QStringList modelIds() const;
    VoiceCloneModel model(const QString& stableId) const;

private:
    QVector<VoiceCloneModel> models_;
    QString error_;
};

} // namespace agplayer::voice_clone
