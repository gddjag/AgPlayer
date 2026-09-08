#include "library_file_operations.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>
#include <QUuid>

#include <utility>

namespace {

bool samePath(const QString& left, const QString& right)
{
    const QString normalizedLeft = QDir::cleanPath(
        QFileInfo(left).absoluteFilePath());
    const QString normalizedRight = QDir::cleanPath(
        QFileInfo(right).absoluteFilePath());
#ifdef Q_OS_WIN
    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
#else
    return normalizedLeft == normalizedRight;
#endif
}

class ExistingTargetBackup final {
public:
    explicit ExistingTargetBackup(QString target)
        : target_(std::move(target))
    {}

    bool preserve(bool overwrite)
    {
        if (!QFileInfo::exists(target_)) return true;
        if (!overwrite) return false;
        const QFileInfo targetInfo(target_);
        backup_ = targetInfo.dir().filePath(
            QStringLiteral(".agplayer-file-operation-%1.bak")
                .arg(QUuid::createUuid().toString(QUuid::Id128)));
        if (QFileInfo::exists(backup_) || !QFile::rename(target_, backup_)) {
            backup_.clear();
            return false;
        }
        return true;
    }

    bool restore()
    {
        if (backup_.isEmpty()) return true;
        // A target appearing after preserve() belongs to another actor. Never
        // delete an unknown file merely to put our backup back in place.
        if (QFileInfo::exists(target_)) return false;
        if (!QFile::rename(backup_, target_)) return false;
        backup_.clear();
        return true;
    }

    bool discard()
    {
        if (backup_.isEmpty()) return true;
        if (QFileInfo::exists(backup_) && !QFile::remove(backup_)) return false;
        backup_.clear();
        return true;
    }

    const QString& recoveryPath() const noexcept { return backup_; }

private:
    QString target_;
    QString backup_;
};

QString stagePath(const QString& path, const QString& operation)
{
    const QFileInfo info(path);
    return info.dir().filePath(QStringLiteral(".agplayer-%1-%2.tmp")
        .arg(operation, QUuid::createUuid().toString(QUuid::Id128)));
}

bool moveFile(const QString& source, const QString& target)
{
    if (QFile::rename(source, target)) return true;
    if (!QFile::copy(source, target)) return false;
    if (QFile::remove(source)) return true;
    QFile::remove(target);
    return false;
}

QString withRecoveryPath(const QString& message, const QString& path)
{
    return path.isEmpty()
        ? message
        : QCoreApplication::translate(
              "LibraryFileOperations", "%1；原目标保留在 %2")
              .arg(message, QDir::toNativeSeparators(path));
}

QString withSourceRecoveryPath(const QString& message, const QString& path)
{
    return QCoreApplication::translate(
        "LibraryFileOperations", "%1；源文件保留在 %2")
        .arg(message, QDir::toNativeSeparators(path));
}

QString recoveryMessage(const QString& message,
                        const ExistingTargetBackup& backup)
{
    return withRecoveryPath(message, backup.recoveryPath());
}

} // namespace

LibraryFileOperations::LibraryFileOperations(QObject* parent)
    : QObject(parent)
{}

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
    if (library_->updateTrackPath(trackId, target)) return true;
    if (!QFile::rename(target, sourcePath)) {
        emit operationFailed(withSourceRecoveryPath(
            tr("曲库更新失败，且文件名自动回滚未完成"), target));
    }
    return false;
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
        if (target.isEmpty() || samePath(source, target)) continue;
        ExistingTargetBackup backup(target);
        if (!backup.preserve(conflictMode == Overwrite)) continue;
        const QString stage = stagePath(source, QStringLiteral("move-stage"));
        if (QFileInfo::exists(stage) || !QFile::rename(source, stage)) {
            if (!backup.restore()) {
                emit operationFailed(recoveryMessage(
                    tr("文件移动暂存失败，且覆盖目标自动恢复未完成"), backup));
            }
            continue;
        }
        if (!moveFile(stage, target)) {
            const bool sourceRestored = !QFileInfo::exists(source)
                && QFile::rename(stage, source);
            const bool targetRestored = backup.restore();
            if (!sourceRestored || !targetRestored) {
                QString message = tr("文件移动提交失败，且自动回滚未完整完成");
                if (!sourceRestored) {
                    message = withSourceRecoveryPath(message, stage);
                }
                emit operationFailed(recoveryMessage(message, backup));
            }
            continue;
        }
        if (!library_->updateTrackPath(trackId, target)) {
            if (!moveFile(target, source)) {
                emit operationFailed(recoveryMessage(withSourceRecoveryPath(
                    tr("曲库更新失败，且文件移动自动回滚未完成"),
                    target), backup));
                continue;
            }
            if (!backup.restore()) {
                emit operationFailed(recoveryMessage(
                    tr("曲库更新失败，且覆盖目标自动恢复未完成"), backup));
            }
            continue;
        }
        if (!backup.discard()) {
            emit operationFailed(recoveryMessage(
                tr("文件已移动，但覆盖备份清理失败"), backup));
        }
        ++moved;
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
        if (target.isEmpty() || samePath(source, target)) continue;
        ExistingTargetBackup backup(target);
        if (!backup.preserve(conflictMode == Overwrite)) continue;
        const QString stage = stagePath(target, QStringLiteral("copy-stage"));
        if (QFileInfo::exists(stage) || !QFile::copy(source, stage)) {
            if (!backup.restore()) {
                emit const_cast<LibraryFileOperations*>(this)->operationFailed(
                    recoveryMessage(
                        tr("文件复制暂存失败，且覆盖目标自动恢复未完成"),
                        backup));
            }
            continue;
        }
        if (!QFile::rename(stage, target)) {
            QFile::remove(stage);
            if (!backup.restore()) {
                emit const_cast<LibraryFileOperations*>(this)->operationFailed(
                    recoveryMessage(
                        tr("文件复制提交失败，且覆盖目标自动恢复未完成"),
                        backup));
            }
            continue;
        }
        if (!backup.discard()) {
            emit const_cast<LibraryFileOperations*>(this)->operationFailed(
                recoveryMessage(tr("文件已复制，但覆盖备份清理失败"),
                                backup));
        }
        ++copied;
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

QVariantMap LibraryFileOperations::trashTracks(const QStringList& trackIds)
{
    QVariantMap result{{QStringLiteral("successCount"), 0},
                       {QStringLiteral("failureCount"), 0},
                       {QStringLiteral("failures"), QVariantList{}}};
    if (library_ == nullptr) return result;
    int removed = 0;
    QVariantList failures;
    for (const QString& trackId : trackIds) {
        const QString path = library_->trackForId(trackId).value(QStringLiteral("path")).toString();
        QString reason;
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            reason = tr("文件不存在或路径无效");
        } else if (!QFile::moveToTrash(path)) {
            reason = tr("系统未能把文件移入回收站");
        } else if (!library_->removeTrack(trackId)) {
            reason = tr("文件已移入回收站，但曲库记录移除失败");
        } else {
            ++removed;
        }
        if (!reason.isEmpty()) {
            failures.append(QVariantMap{{QStringLiteral("trackId"), trackId},
                                        {QStringLiteral("path"), path},
                                        {QStringLiteral("reason"), reason}});
        }
    }
    result.insert(QStringLiteral("successCount"), removed);
    result.insert(QStringLiteral("failureCount"), failures.size());
    result.insert(QStringLiteral("failures"), failures);
    return result;
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
    if (details.value(QStringLiteral("format")).toString().trimmed().isEmpty()) {
        details.insert(QStringLiteral("format"), info.suffix().toUpper());
    }
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
