#include "file_association_controller.hpp"
#include "audio_file_discovery.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr char kProgId[] = "AgPlayerAudioFile";
constexpr char kProgIdDisplayName[] = "AgPlayer Audio File";

QString normalizeExtension(const QString& ext)
{
    QString result = ext.trimmed().toLower();
    while (!result.isEmpty() && result.startsWith('.')) {
        result.remove(0, 1);
    }
    return result;
}

#ifdef Q_OS_WIN

bool writeRegistryString(HKEY key, const wchar_t* subKey, const wchar_t* valueName,
                         const QString& value)
{
    const std::wstring valueW = value.toStdWString();
    const LSTATUS status = RegSetKeyValueW(
        key,
        subKey,
        valueName,
        REG_SZ,
        valueW.c_str(),
        static_cast<DWORD>((valueW.size() + 1) * sizeof(wchar_t)));
    return status == ERROR_SUCCESS;
}

#endif

} // namespace

FileAssociationController::FileAssociationController(QObject* parent)
    : QObject(parent)
{
}

QStringList FileAssociationController::supportedAudioExtensions()
{
    return agplayer::qt::supportedAudioExtensions();
}

QString FileAssociationController::lastError() const
{
    return lastError_;
}

bool FileAssociationController::registerForExtensions(const QStringList& extensions)
{
    lastError_.clear();

#ifdef Q_OS_WIN
    const QString appPath = QCoreApplication::applicationFilePath();
    if (appPath.isEmpty()) {
        lastError_ = tr("Cannot determine application path");
        return false;
    }

    if (!writeProgId(appPath)) {
        return false;
    }

    const QString progId = QString::fromLatin1(kProgId);
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (normalized.isEmpty()) {
            continue;
        }
        if (!writeExtension(normalized, progId)) {
            // Rollback: remove the ProgID we just wrote.
            removeProgId();
            return false;
        }
    }
    if (!writeCapabilities(extensions, progId)) return false;
    return true;
#else
    lastError_ = tr("not supported on this platform");
    return false;
#endif
}

bool FileAssociationController::unregisterForExtensions(const QStringList& extensions)
{
    lastError_.clear();

#ifdef Q_OS_WIN
    bool allOk = true;
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (normalized.isEmpty()) {
            continue;
        }
        if (!removeExtension(normalized)) {
            allOk = false;
        }
    }
    return allOk;
#else
    lastError_ = tr("not supported on this platform");
    return false;
#endif
}

bool FileAssociationController::unregisterAll()
{
    lastError_.clear();

#ifdef Q_OS_WIN
    // Remove every extension currently associated with our ProgID, not just
    // the built-in supported audio extensions.
    const QString classesPath = QStringLiteral("Software\\Classes");
    const std::wstring classesPathW = classesPath.toStdWString();
    HKEY classesKey = nullptr;
    QStringList extensionsToRemove;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, classesPathW.c_str(), 0, KEY_READ, &classesKey)
        == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t subKeyName[256] = {};
        DWORD subKeyNameSize = 256;
        while (RegEnumKeyExW(classesKey, index, subKeyName, &subKeyNameSize,
                             nullptr, nullptr, nullptr, nullptr)
               == ERROR_SUCCESS) {
            const QString subKey = QString::fromWCharArray(subKeyName, static_cast<int>(subKeyNameSize));
            if (subKey.startsWith('.')) {
                extensionsToRemove.append(subKey.mid(1));
            }
            subKeyNameSize = 256;
            ++index;
        }
        RegCloseKey(classesKey);
    }

    // Always cover the supported built-ins as well in case enumeration missed any.
    const QStringList extensions = extensionsToRemove
        + supportedAudioExtensions() + agplayer::qt::supportedVideoExtensions();
    for (const QString& ext : extensions) {
        removeExtension(ext);
    }
    const bool capabilitiesOk = removeCapabilities();
    return removeProgId() && capabilitiesOk;
#else
    lastError_ = tr("not supported on this platform");
    return false;
#endif
}

bool FileAssociationController::isAssociated(const QString& extension) const
{
    const QString normalized = normalizeExtension(extension);
    if (normalized.isEmpty()) {
        return false;
    }

#ifdef Q_OS_WIN
    const QString keyPath = QStringLiteral("Software\\Classes\\.%1").arg(normalized);
    const std::wstring keyPathW = keyPath.toStdWString();

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPathW.c_str(), 0, KEY_READ, &key)
        != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[256] = {};
    DWORD valueSize = sizeof(value);
    DWORD valueType = 0;
    const LSTATUS status = RegQueryValueExW(
        key,
        nullptr,
        nullptr,
        &valueType,
        reinterpret_cast<LPBYTE>(value),
        &valueSize);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || valueType != REG_SZ) {
        return false;
    }
    return QString::fromWCharArray(value) == QString::fromLatin1(kProgId);
#else
    return false;
#endif
}

#ifdef Q_OS_WIN

bool FileAssociationController::writeProgId(const QString& appPath)
{
    const QString rootPath = QStringLiteral("Software\\Classes\\%1").arg(QString::fromLatin1(kProgId));
    const std::wstring rootPathW = rootPath.toStdWString();

    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        rootPathW.c_str(),
        0,
        nullptr,
        0,
        KEY_WRITE,
        nullptr,
        &key,
        nullptr);
    if (status != ERROR_SUCCESS) {
        lastError_ = tr("Failed to create ProgID registry key");
        return false;
    }

    bool ok = true;
    ok &= writeRegistryString(key, nullptr, nullptr, QString::fromLatin1(kProgIdDisplayName));
    ok &= writeRegistryString(
        key,
        L"DefaultIcon",
        nullptr,
        QStringLiteral("%1,0").arg(appPath));
    ok &= writeRegistryString(
        key,
        L"shell\\open\\command",
        nullptr,
        QStringLiteral("\"%1\" \"%2\"")
            .arg(appPath, QStringLiteral("%1")));

    RegCloseKey(key);

    if (!ok) {
        lastError_ = tr("Failed to write ProgID registry values");
        removeProgId();
        return false;
    }
    return true;
}

bool FileAssociationController::removeProgId()
{
    const QString rootPath = QStringLiteral("Software\\Classes\\%1").arg(QString::fromLatin1(kProgId));
    const std::wstring rootPathW = rootPath.toStdWString();
    const LSTATUS status =
        RegDeleteTreeW(HKEY_CURRENT_USER, rootPathW.c_str());
    if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND
        || status == ERROR_PATH_NOT_FOUND) {
        lastError_.clear();
        return true;
    }
    lastError_ = tr("Failed to remove ProgID registry key");
    return false;
}

bool FileAssociationController::writeExtension(const QString& extension,
                                               const QString& progId)
{
    const QString keyPath = QStringLiteral("Software\\Classes\\.%1").arg(extension);
    const std::wstring keyPathW = keyPath.toStdWString();

    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        keyPathW.c_str(),
        0,
        nullptr,
        0,
        KEY_WRITE,
        nullptr,
        &key,
        nullptr);
    if (status != ERROR_SUCCESS) {
        lastError_ = tr("Failed to create extension registry key for .%1").arg(extension);
        return false;
    }

    bool ok = true;
    ok &= writeRegistryString(key, nullptr, nullptr, progId);
    ok &= writeRegistryString(
        key,
        nullptr,
        L"PerceivedType",
        agplayer::qt::isSupportedVideoExtension(extension)
            ? QStringLiteral("video")
            : QStringLiteral("audio"));
    ok &= writeRegistryString(key, L"OpenWithProgids", progId.toStdWString().c_str(),
                              QString());

    RegCloseKey(key);

    if (!ok) {
        lastError_ = tr("Failed to write extension registry values for .%1").arg(extension);
        removeExtension(extension);
        return false;
    }
    return true;
}

bool FileAssociationController::removeExtension(const QString& extension)
{
    const QString keyPath = QStringLiteral("Software\\Classes\\.%1").arg(extension);
    const std::wstring keyPathW = keyPath.toStdWString();

    // Only remove if this extension points to our ProgID; do not destroy other
    // applications' associations.
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPathW.c_str(), 0, KEY_READ, &key)
        == ERROR_SUCCESS) {
        wchar_t value[256] = {};
        DWORD valueSize = sizeof(value);
        DWORD valueType = 0;
        const bool isOurs =
            RegQueryValueExW(key, nullptr, nullptr, &valueType,
                             reinterpret_cast<LPBYTE>(value), &valueSize)
                == ERROR_SUCCESS
            && valueType == REG_SZ
            && QString::fromWCharArray(value) == QString::fromLatin1(kProgId);
        RegCloseKey(key);

        if (isOurs) {
            if (RegDeleteTreeW(HKEY_CURRENT_USER, keyPathW.c_str()) == ERROR_SUCCESS) {
                return true;
            }
            lastError_ = tr("Failed to remove extension registry key for .%1").arg(extension);
            return false;
        }
    }

    // The user may have selected another default application after AgPlayer
    // registered itself. Preserve that association while removing our stale
    // OpenWith entry so the deleted ProgID is never left behind.
    const QString openWithPath = keyPath + QStringLiteral("\\OpenWithProgids");
    const std::wstring openWithPathW = openWithPath.toStdWString();
    const std::wstring progIdW = QString::fromLatin1(kProgId).toStdWString();
    const LSTATUS status = RegDeleteKeyValueW(
        HKEY_CURRENT_USER, openWithPathW.c_str(), progIdW.c_str());
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND
        && status != ERROR_PATH_NOT_FOUND) {
        lastError_ = tr("Failed to remove stale ProgID for .%1").arg(extension);
        return false;
    }
    return true;
}

bool FileAssociationController::writeCapabilities(const QStringList& extensions,
                                                   const QString& progId)
{
    const QString capabilitiesPath = QStringLiteral("Software\\AgPlayer\\Capabilities");
    const std::wstring capabilitiesPathW = capabilitiesPath.toStdWString();
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, capabilitiesPathW.c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        lastError_ = tr("Failed to create default-app capabilities");
        return false;
    }
    bool ok = writeRegistryString(key, nullptr, L"ApplicationName", QStringLiteral("AgPlayer"));
    ok &= writeRegistryString(key, nullptr, L"ApplicationDescription",
                              tr("AgPlayer audio player"));
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (!normalized.isEmpty()) {
            ok &= writeRegistryString(key, L"FileAssociations",
                                      QStringLiteral(".%1").arg(normalized).toStdWString().c_str(),
                                      progId);
        }
    }
    RegCloseKey(key);
    ok &= writeRegistryString(HKEY_CURRENT_USER, L"Software\\RegisteredApplications",
                              L"AgPlayer", capabilitiesPath);
    if (!ok) lastError_ = tr("Failed to register default-app capabilities");
    return ok;
}

bool FileAssociationController::removeCapabilities()
{
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"AgPlayer");
    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AgPlayer");
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND
        || status == ERROR_PATH_NOT_FOUND;
}

#endif
