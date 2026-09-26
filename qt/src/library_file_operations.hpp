#pragma once

#include "library_model.hpp"

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

class LibraryFileOperations : public QObject {
    Q_OBJECT
    Q_PROPERTY(LibraryModel* libraryModel READ libraryModel WRITE setLibraryModel NOTIFY libraryModelChanged)

public:
    enum ConflictMode { Skip, AutoRename, Overwrite };
    Q_ENUM(ConflictMode)

    explicit LibraryFileOperations(QObject* parent = nullptr);
    LibraryModel* libraryModel() const noexcept;
    void setLibraryModel(LibraryModel* model);

    Q_INVOKABLE bool showInFolder(const QString& trackId) const;
    Q_INVOKABLE bool copyPath(const QString& trackId) const;
    Q_INVOKABLE QUrl fileUrl(const QString& trackId) const;
    Q_INVOKABLE bool renameTrack(const QString& trackId, const QString& newBaseName);
    Q_INVOKABLE int moveTracks(const QStringList& trackIds, const QString& destinationFolder,
                               ConflictMode conflictMode = Skip);
    Q_INVOKABLE int copyTracks(const QStringList& trackIds, const QString& destinationFolder,
                               ConflictMode conflictMode = Skip) const;
    Q_INVOKABLE int moveTracksToUrl(const QStringList& trackIds, const QUrl& destinationFolder,
                                    ConflictMode conflictMode = Skip);
    Q_INVOKABLE int copyTracksToUrl(const QStringList& trackIds, const QUrl& destinationFolder,
                                    ConflictMode conflictMode = Skip) const;
    Q_INVOKABLE QVariantMap trashTracks(const QStringList& trackIds);
    Q_INVOKABLE bool relocateTrack(const QString& trackId, const QString& newPath);
    Q_INVOKABLE bool relocateTrackToUrl(const QString& trackId, const QUrl& newFile);
    Q_INVOKABLE QVariantMap trackDetails(const QString& trackId) const;

signals:
    void libraryModelChanged();
    void operationFailed(const QString& message);

private:
    QString resolvedDestination(const QString& sourcePath, const QString& folder,
                                ConflictMode mode) const;
    QPointer<LibraryModel> library_;
};
