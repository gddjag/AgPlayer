#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

enum class VocalModelFamily {
    Mdx,
    Demucs,
};

struct VocalDownloadFile {
    QString fileName;
    QUrl url;
    qint64 bytes = 0;
    QString sha256;
};

struct VocalModelCard {
    QString id;
    VocalModelFamily family = VocalModelFamily::Mdx;
    QList<VocalDownloadFile> files;
    QStringList stems;
    QString provenance;
    QString resourceGuidance;
    QString displayName{};
    QString useCase{};
    QString tierLabel{};
    QString badgeLabel{};
    QString provider{};
    QString repositoryUrl{};
};

struct VocalRuntimePackage {
    QString id;
    QUrl url;
    qint64 bytes = 0;
    QString sha256;
};

class VocalSeparationCatalog {
public:
    static QList<VocalModelCard> models();
    static VocalRuntimePackage directMlRuntime();
    static VocalRuntimePackage nativeRuntime();
};

struct CustomManifestValidationResult {
    bool accepted = false;
    QString error;
};

CustomManifestValidationResult validateCustomModelManifest(const QJsonObject& manifest);

// Returns a verified, free domestic mirror URL when the source has a supported
// mirror. An empty URL means the user must use the manual/community route.
QUrl vocalDomesticMirrorUrl(const QUrl& source);
