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
};

class VoiceClonePackageManifest {
public:
    static VoiceClonePackageManifest fromJson(const QJsonObject& object);

    bool isValid() const;
    QString errorString() const;

    QString packageId;
    QString version;
    QString revision;
    QUrl licenseUrl;
    QString licenseRevision;
    bool requiresLicenseAcceptance = false;
    QVector<VoiceClonePackageFile> files;

private:
    QString validationError() const;
    QString parseError_;
};

} // namespace agplayer::voice_clone
