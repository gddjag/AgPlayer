#include "resource_path.hpp"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#endif

namespace {

#ifdef Q_OS_WIN
QString finalWindowsPath(const QString& absolutePath)
{
    const std::wstring nativePath =
        QDir::toNativeSeparators(absolutePath).toStdWString();
    const HANDLE handle = CreateFileW(
        nativePath.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    QString result;
    const DWORD length = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (length > 0) {
        std::wstring buffer(static_cast<std::size_t>(length), L'\0');
        const DWORD written = GetFinalPathNameByHandleW(
            handle, buffer.data(), length, FILE_NAME_NORMALIZED);
        if (written > 0 && written < length) {
            result = QString::fromWCharArray(
                buffer.data(), static_cast<qsizetype>(written));
            if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) {
                result = QStringLiteral("\\\\") + result.sliced(8);
            } else if (result.startsWith(QStringLiteral("\\\\?\\"))) {
                result = result.sliced(4);
            }
        }
    }
    CloseHandle(handle);
    return result;
}
#endif

QString cleanPath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

} // namespace

Qt::CaseSensitivity agplayer::qt::resourcePathCaseSensitivity() noexcept
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString agplayer::qt::resourcePathIdentity(const QString& path)
{
    if (path.trimmed().isEmpty()) return {};
    const QFileInfo info(path);
    QString identity;
    if (info.exists()) {
#ifdef Q_OS_WIN
        identity = finalWindowsPath(info.absoluteFilePath());
#endif
        if (identity.isEmpty()) identity = info.canonicalFilePath();
    }
    if (identity.isEmpty()) identity = info.absoluteFilePath();
    return cleanPath(identity);
}

bool agplayer::qt::resourcePathsEqual(const QString& left, const QString& right)
{
    const QString leftIdentity = resourcePathIdentity(left);
    const QString rightIdentity = resourcePathIdentity(right);
    return !leftIdentity.isEmpty() && !rightIdentity.isEmpty()
        && leftIdentity.compare(rightIdentity, resourcePathCaseSensitivity()) == 0;
}

bool agplayer::qt::resourcePathIsWithin(const QString& candidate,
                                        const QString& root)
{
    const QString candidateIdentity = resourcePathIdentity(candidate);
    const QString rootIdentity = resourcePathIdentity(root);
    return resourcePathIdentityIsWithin(candidateIdentity, rootIdentity);
}

bool agplayer::qt::resourcePathIdentityIsWithin(
    const QString& candidateIdentity, const QString& requestedRootIdentity)
{
    QString rootIdentity = requestedRootIdentity;
    if (candidateIdentity.isEmpty() || rootIdentity.isEmpty()) return false;
    const Qt::CaseSensitivity sensitivity = resourcePathCaseSensitivity();
    if (candidateIdentity.compare(rootIdentity, sensitivity) == 0) return true;
    if (!rootIdentity.endsWith(QLatin1Char('/'))) rootIdentity += QLatin1Char('/');
    return candidateIdentity.startsWith(rootIdentity, sensitivity);
}
