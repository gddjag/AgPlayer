#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class FileAssociationController final : public QObject {
    Q_OBJECT
public:
    explicit FileAssociationController(QObject* parent = nullptr);
    FileAssociationController(const QString& registryRootPath, QObject* parent = nullptr);

    Q_INVOKABLE bool registerForExtensions(const QStringList& extensions);
    Q_INVOKABLE bool unregisterForExtensions(const QStringList& extensions);
    Q_INVOKABLE bool unregisterAll();
    Q_INVOKABLE bool isAssociated(const QString& extension) const;
    Q_INVOKABLE QString lastError() const;
    QString registryRootPath() const;
    bool hasCustomRegistryRoot() const;

    static QStringList supportedAudioExtensions();

private:
    QString lastError_;
    QString registryRootPath_;

    bool writeProgId(const QString& appPath);
    bool removeProgId();
    bool writeExtension(const QString& extension, const QString& progId);
    bool removeExtension(const QString& extension);
    bool writeCapabilities(const QStringList& extensions, const QString& progId);
    bool removeCapabilities();
    bool registeredExtensions(QStringList* extensions);
    QString registryPath(const QString& relativePath) const;
};
