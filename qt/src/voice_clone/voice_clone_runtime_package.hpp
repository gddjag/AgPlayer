#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVector>

namespace agplayer::voice_clone {

enum class VoiceCloneRuntimeValidationPolicy {
    OfficialSignedOnly,
    AllowLoopbackUnsignedTest,
};

struct VoiceCloneRuntimeAdapterCompatibility {
    QString adapterId;
    QString adapterVersion;
};

struct VoiceCloneRuntimeFile {
    QString relativePath;
    qint64 bytes = -1;
    QString sha256;
};

struct VoiceCloneRuntimeLicenseNotice {
    QString name;
    QString spdx;
    QUrl url;
};

struct VoiceCloneRuntimePackageManifest {
    int schemaVersion = 0;
    QString packageId;
    QString runtimeId;
    QString version;
    QVector<VoiceCloneRuntimeAdapterCompatibility> compatibleAdapters;
    int protocolVersion = 0;
    QString platform;
    QString architecture;
    QString minimumPlayerVersion;
    QString archiveFormat;
    QString installRoot;
    QString publisherName;
    QUrl publisherUrl;
    QVector<VoiceCloneRuntimeLicenseNotice> licenseNotices;
    QString signatureStatus;
    QString signatureAlgorithm;
    QString signatureKeyId;
    QUrl packageUrl;
    qint64 packageBytes = -1;
    QString packageSha256;
    qint64 installedBytes = -1;
    int entryCount = 0;
    QVector<VoiceCloneRuntimeFile> files;
    QString parseError;

    static VoiceCloneRuntimePackageManifest fromJson(
        const QJsonObject& object,
        VoiceCloneRuntimeValidationPolicy policy =
            VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly);
    bool isValid(VoiceCloneRuntimeValidationPolicy policy =
                     VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly) const;
    QString errorString(VoiceCloneRuntimeValidationPolicy policy =
                            VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly) const;
    QJsonObject toJson() const;
    bool supports(const QString& adapterId, const QString& adapterVersion,
                  int protocol) const;
};

struct VoiceCloneRuntimeFeed {
    bool enabled = false;
    QString signatureStatus;
    QVector<VoiceCloneRuntimePackageManifest> runtimes;
    QString error;

    static VoiceCloneRuntimeFeed fromJson(
        const QByteArray& json,
        VoiceCloneRuntimeValidationPolicy policy =
            VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly);
    bool isValid() const { return error.isEmpty(); }
};

} // namespace agplayer::voice_clone
