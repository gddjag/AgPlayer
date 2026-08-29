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
    QString displayName;
    QString useCase;
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
};

struct CustomManifestValidationResult {
    bool accepted = false;
    QString error;
};

CustomManifestValidationResult validateCustomModelManifest(const QJsonObject& manifest);
