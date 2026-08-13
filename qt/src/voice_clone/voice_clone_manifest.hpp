#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace agplayer::voice_clone {

struct VoiceCloneSource {
    QString provider;
    QString url;
};

struct VoiceCloneLicense {
    QString name;
    QString url;
};

struct VoiceCloneRequiredFile {
    QString relativePath;
    QString sha256;
};

struct VoiceCloneReferenceAudioRules {
    double minimumSeconds = 0.0;
    double maximumSeconds = 0.0;
    QStringList extensions;
};

struct VoiceCloneModel {
    QString stableId;
    QString displayName;
    QString description;
    QString adapterId;
    QString runtimeId;
    QString revision;
    QString installState;
    QString officialProjectUrl;
    QString huggingFaceUrl;
    QString modelScopeUrl;
    QStringList capabilityPreview;
    VoiceCloneSource source;
    VoiceCloneLicense license;
    QVector<VoiceCloneRequiredFile> files;
    VoiceCloneReferenceAudioRules referenceAudio;
    QJsonObject capabilitySchema;
    bool stable = false;
    bool requiresLicenseAcceptance = false;
};

struct ManifestParseResult {
    VoiceCloneModel model;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

ManifestParseResult parseLocalModelManifest(const QByteArray& json);

} // namespace agplayer::voice_clone
