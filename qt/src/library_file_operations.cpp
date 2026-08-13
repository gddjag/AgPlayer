#include "library_file_operations.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>

LibraryFileOperations::LibraryFileOperations(QObject* parent) : QObject(parent) {}

LibraryModel* LibraryFileOperations::libraryModel() const noexcept { return library_; }

void LibraryFileOperations::setLibraryModel(LibraryModel* model)
{
    if (library_ == model) return;
    library_ = model;
    emit libraryModelChanged();
}

bool LibraryFileOperations::showInFolder(const QString& trackId) const
{
    if (library_ == nullptr) return false;
    const QString path = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
    if (path.isEmpty()) return false;
#ifdef Q_OS_WIN
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool LibraryFileOperations::copyPath(const QString& trackId) const
{
    if (library_ == nullptr || QGuiApplication::clipboard() == nullptr) return false;
    const QString path = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
    if (path.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    return true;
}

QUrl LibraryFileOperations::fileUrl(const QString& trackId) const
{
    if (library_ == nullptr) return {};
    const QString path = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
    return QFileInfo(path).isFile() ? QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()) : QUrl{};
}

bool LibraryFileOperations::renameTrack(const QString& trackId, const QString& newBaseName)
{
    if (library_ == nullptr || newBaseName.trimmed().isEmpty()) return false;
    const QString sourcePath = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
    const QFileInfo source(sourcePath);
    QString safeName = newBaseName.trimmed();
    for (const QChar invalid : QStringLiteral("<>:\"/\\|?*")) safeName.remove(invalid);
    if (safeName.isEmpty()) return false;
    const QString target = source.dir().filePath(
        safeName + (source.suffix().isEmpty() ? QString() : QStringLiteral(".") + source.suffix()));
    if (QFileInfo::exists(target) || !QFile::rename(sourcePath, target)) return false;
    return library_->updateTrackPath(trackId, target);
}

int LibraryFileOperations::moveTracks(const QStringList& trackIds,
                                      const QString& destinationFolder,
                                      ConflictMode conflictMode)
{
    if (library_ == nullptr || !QFileInfo(destinationFolder).isDir()) return 0;
    int moved = 0;
    for (const QString& trackId : trackIds) {
        const QString source = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
        const QString target = resolvedDestination(source, destinationFolder, conflictMode);
        if (target.isEmpty()) continue;
        if (conflictMode == Overwrite && QFileInfo::exists(target) && !QFile::remove(target)) continue;
        bool success = QFile::rename(source, target);
        if (!success && QFile::copy(source, target)) success = QFile::remove(source);
        if (success && library_->updateTrackPath(trackId, target)) ++moved;
    }
    return moved;
}

int LibraryFileOperations::copyTracks(const QStringList& trackIds,
                                      const QString& destinationFolder,
                                      ConflictMode conflictMode) const
{
    if (library_ == nullptr || !QFileInfo(destinationFolder).isDir()) return 0;
    int copied = 0;
    for (const QString& trackId : trackIds) {
        const QString source = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
        const QString target = resolvedDestination(source, destinationFolder, conflictMode);
        if (target.isEmpty()) continue;
        if (conflictMode == Overwrite && QFileInfo::exists(target) && !QFile::remove(target)) continue;
        if (QFile::copy(source, target)) ++copied;
    }
    return copied;
}

int LibraryFileOperations::moveTracksToUrl(const QStringList& trackIds,
                                            const QUrl& destinationFolder,
                                            ConflictMode conflictMode)
{
    return destinationFolder.isLocalFile()
               ? moveTracks(trackIds, destinationFolder.toLocalFile(), conflictMode)
               : 0;
}

int LibraryFileOperations::copyTracksToUrl(const QStringList& trackIds,
                                            const QUrl& destinationFolder,
                                            ConflictMode conflictMode) const
{
    return destinationFolder.isLocalFile()
               ? copyTracks(trackIds, destinationFolder.toLocalFile(), conflictMode)
               : 0;
}

int LibraryFileOperations::trashTracks(const QStringList& trackIds)
{
    if (library_ == nullptr) return 0;
    int removed = 0;
    for (const QString& trackId : trackIds) {
        const QString path = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && QFile::moveToTrash(path) && library_->removeTrack(trackId)) ++removed;
    }
    return removed;
}

bool LibraryFileOperations::relocateTrack(const QString& trackId, const QString& newPath)
{
    return library_ != nullptr && QFileInfo(newPath).isFile()
           && library_->updateTrackPath(trackId, newPath);
}

bool LibraryFileOperations::relocateTrackToUrl(const QString& trackId, const QUrl& newFile)
{
    return newFile.isLocalFile() && relocateTrack(trackId, newFile.toLocalFile());
}

QVariantMap LibraryFileOperations::trackDetails(const QString& trackId) const
{
    if (library_ == nullptr) return {};
    QVariantMap details = library_->trackForId(trackId);
    const QFileInfo info(details.value(QStringLiteral("path")).toString());
    details.insert(QStringLiteral("directory"), info.absolutePath());
    details.insert(QStringLiteral("modifiedAt"), info.lastModified());
    details.insert(QStringLiteral("fileName"), info.fileName());
    return details;
}

QString LibraryFileOperations::resolvedDestination(const QString& sourcePath,
                                                    const QString& folder,
                                                    ConflictMode mode) const
{
    if (!QFileInfo(sourcePath).isFile()) return {};
    const QFileInfo source(sourcePath);
    QString target = QDir(folder).filePath(source.fileName());
    if (!QFileInfo::exists(target)) return target;
    if (mode == Skip) return {};
    if (mode == Overwrite) return target;
    const QString stem = source.completeBaseName();
    const QString suffix = source.suffix();
    for (int copy = 2; copy < 10000; ++copy) {
        target = QDir(folder).filePath(stem + QStringLiteral(" (%1)").arg(copy)
            + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));
        if (!QFileInfo::exists(target)) return target;
    }
    return {};
}
