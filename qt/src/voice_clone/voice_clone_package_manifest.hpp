#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVector>

namespace agplayer::voice_clone {

struct VoiceClonePackageFile {
    QString relativePath;
    QUrl url;
    QByteArray sha256;
    qint64 expectedBytes = -1;
    QString sourceRepository;
    QString sourceRevision;
    QString sourcePath;
};

struct VoiceClonePackageLicense {
    QString id;
    QString name;
    QUrl url;
    QString revision;
    QString spdx;
    bool requiredAcceptance = false;
    QString useRestriction;
};

QVector<VoiceClonePackageLicense> approvedRequiredLicenses(const QString& modelId,
                                                           const QString& adapterId);

class VoiceClonePackageManifest {
public:
    static VoiceClonePackageManifest fromJson(const QJsonObject& object);

    bool isValid() const;
    QString errorString() const;
    bool licenseAcceptanceRequired() const;

    QString packageId;
    QString modelId;
    QString adapterId;
    QString version;
    QString revision;
    QString modelDisplayName;
    QString modelDescription;
    QString sourceProvider;
    QString sourceRepository;
    QUrl sourceUrl;
    QVector<VoiceClonePackageLicense> licenses;
    // Compatibility aliases for older callers. Parsed manifests derive these
    // from the first license entry; acceptance logic always uses licenses.
    QUrl licenseUrl;
    QString licenseRevision;
    bool requiresLicenseAcceptance = false;
    qint64 totalBytes = -1;
    QVector<VoiceClonePackageFile> files;

private:
    QString validationError() const;
    QString parseError_;
};

} // namespace agplayer::voice_clone
