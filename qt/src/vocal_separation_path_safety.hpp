#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#endif

namespace vocal_separation_paths {

enum class SafePathKind { Missing, RegularFile, Directory, Unsafe };

inline QString absoluteCleanPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

#ifdef Q_OS_WIN
inline QString extendedNativePath(const QString& path)
{
    QString native = QDir::toNativeSeparators(absoluteCleanPath(path));
    if (native.startsWith(QStringLiteral("\\\\?\\"))) return native;
    if (native.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
}

inline SafePathKind safePathKind(const QString& path)
{
    const QString native = extendedNativePath(path);
    const DWORD attributes = GetFileAttributesW(
        reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
            ? SafePathKind::Missing : SafePathKind::Unsafe;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return SafePathKind::Unsafe;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        ? SafePathKind::Directory : SafePathKind::RegularFile;
}
#else
inline SafePathKind safePathKind(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymbolicLink()) return SafePathKind::Unsafe;
    if (!info.exists()) return SafePathKind::Missing;
    if (info.isFile()) return SafePathKind::RegularFile;
    if (info.isDir()) return SafePathKind::Directory;
    return SafePathKind::Unsafe;
}
#endif

inline bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return absoluteCleanPath(left).compare(absoluteCleanPath(right),
                                           Qt::CaseInsensitive) == 0;
#else
    return absoluteCleanPath(left) == absoluteCleanPath(right);
#endif
}

inline bool lexicallyWithin(const QString& path, const QString& directory)
{
    QString root = QDir::fromNativeSeparators(absoluteCleanPath(directory));
    const QString candidate = QDir::fromNativeSeparators(absoluteCleanPath(path));
    if (!root.endsWith(QLatin1Char('/'))) root += QLatin1Char('/');
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

inline bool safeExistingPathWithin(const QString& path, const QString& directory,
                                   SafePathKind finalKind)
{
    if (safePathKind(directory) != SafePathKind::Directory
        || !lexicallyWithin(path, directory)) return false;
    const QString relative = QDir(directory).relativeFilePath(path);
    const QStringList parts = QDir::fromNativeSeparators(relative).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.contains(QStringLiteral(".."))) return false;
    QString current = absoluteCleanPath(directory);
    for (qsizetype index = 0; index < parts.size(); ++index) {
        current = QDir(current).filePath(parts.at(index));
        const SafePathKind expected = index + 1 == parts.size()
            ? finalKind : SafePathKind::Directory;
        if (safePathKind(current) != expected) return false;
    }
    QString root = QDir(directory).canonicalPath();
    QString candidate = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || candidate.isEmpty()) return false;
    root = QDir::fromNativeSeparators(root);
    candidate = QDir::fromNativeSeparators(candidate);
    if (!root.endsWith(QLatin1Char('/'))) root += QLatin1Char('/');
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

inline bool safeExistingFileWithin(const QString& path, const QString& directory)
{
    return safeExistingPathWithin(path, directory, SafePathKind::RegularFile);
}

inline bool safeExistingFile(const QString& path)
{
    if (safePathKind(path) != SafePathKind::RegularFile) return false;
    QString parent = QFileInfo(path).absolutePath();
    while (!parent.isEmpty()) {
        if (safePathKind(parent) != SafePathKind::Directory) return false;
        const QString next = QFileInfo(parent).absolutePath();
        if (samePath(next, parent)) break;
        parent = next;
    }
    return !QFileInfo(path).canonicalFilePath().isEmpty();
}

inline bool safeExistingDirectory(const QString& path)
{
    if (safePathKind(path) != SafePathKind::Directory) return false;
    QString current = absoluteCleanPath(path);
    while (!current.isEmpty()) {
        if (safePathKind(current) != SafePathKind::Directory) return false;
        const QString next = QFileInfo(current).absolutePath();
        if (samePath(next, current)) break;
        current = next;
    }
    return !QDir(path).canonicalPath().isEmpty();
}

inline bool openFileHandleIsRegular(QFile* file)
{
    if (file == nullptr || !file->isOpen()) return false;
#ifdef Q_OS_WIN
    BY_HANDLE_FILE_INFORMATION information{};
    const intptr_t operatingSystemHandle = _get_osfhandle(file->handle());
    if (operatingSystemHandle == -1) return false;
    const HANDLE handle = reinterpret_cast<HANDLE>(operatingSystemHandle);
    return handle != INVALID_HANDLE_VALUE
        && GetFileInformationByHandle(handle, &information)
        && (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY
                                             | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
#else
    return true;
#endif
}

inline bool openRegularFileForRead(QFile* file)
{
    if (file == nullptr || !safeExistingFile(file->fileName())) return false;
    const QString before = QFileInfo(file->fileName()).canonicalFilePath();
    if (before.isEmpty() || !file->open(QIODevice::ReadOnly)) return false;
    const QString after = QFileInfo(file->fileName()).canonicalFilePath();
    if (!safeExistingFile(file->fileName()) || after.isEmpty()
        || !samePath(before, after)) {
        file->close();
        return false;
    }
    if (!openFileHandleIsRegular(file)) {
        file->close();
        return false;
    }
    return true;
}

inline bool openRegularFileForAppend(QFile* file)
{
    if (file == nullptr) return false;
    const SafePathKind kind = safePathKind(file->fileName());
    QString before;
    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Append;
    if (kind == SafePathKind::RegularFile) {
        if (!safeExistingFile(file->fileName())) return false;
        before = QFileInfo(file->fileName()).canonicalFilePath();
        mode |= QIODevice::ExistingOnly;
    } else if (kind == SafePathKind::Missing) {
        mode |= QIODevice::NewOnly;
    } else {
        return false;
    }
    if (!file->open(mode)) return false;
    const QString after = QFileInfo(file->fileName()).canonicalFilePath();
    if (!safeExistingFile(file->fileName()) || after.isEmpty()
        || (!before.isEmpty() && !samePath(before, after))
        || !openFileHandleIsRegular(file)) {
        file->close();
        return false;
    }
    return true;
}

inline bool openRegularFileForReadWithin(QFile* file, const QString& directory)
{
    return file != nullptr
        && safeExistingFileWithin(file->fileName(), directory)
        && openRegularFileForRead(file)
        && safeExistingFileWithin(file->fileName(), directory);
}

inline bool validateFlatDirectory(const QString& directory,
                                  const QSet<QString>& allowedNames)
{
    if (safePathKind(directory) != SafePathKind::Directory) return false;
    const QFileInfoList entries = QDir(directory).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (!allowedNames.contains(entry.fileName())
            || safePathKind(entry.absoluteFilePath()) != SafePathKind::RegularFile)
            return false;
    }
    return true;
}

inline bool removeKnownFlatDirectory(const QString& directory,
                                     const QSet<QString>& allowedNames)
{
    const SafePathKind kind = safePathKind(directory);
    if (kind == SafePathKind::Missing) return true;
    if (kind != SafePathKind::Directory
        || !validateFlatDirectory(directory, allowedNames)) return false;
    const QFileInfoList entries = QDir(directory).entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (!allowedNames.contains(entry.fileName())
            || safePathKind(entry.absoluteFilePath()) != SafePathKind::RegularFile
            || !QFile::remove(entry.absoluteFilePath())) return false;
    }
    return QDir().rmdir(directory);
}

} // namespace vocal_separation_paths
