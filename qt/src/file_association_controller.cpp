#include "file_association_controller.hpp"
#include "audio_file_discovery.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#ifdef Q_OS_MACOS
#include "macos_system_integration.hpp"
#endif

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

#ifdef Q_OS_WIN
constexpr char kProgId[] = "AgPlayerAudioFile";
constexpr char kProgIdDisplayName[] = "AgPlayer Audio File";
#endif

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
    : FileAssociationController(QStringLiteral("Software"), parent)
{
}

FileAssociationController::FileAssociationController(const QString& registryRootPath,
                                                       QObject* parent)
    : QObject(parent),
      registryRootPath_(registryRootPath.trimmed())
{
    while (registryRootPath_.endsWith(QLatin1Char('\\'))) {
        registryRootPath_.chop(1);
    }
    if (registryRootPath_.isEmpty()) {
        registryRootPath_ = QStringLiteral("Software");
    }
}

QStringList FileAssociationController::supportedAudioExtensions()
{
    return agplayer::qt::supportedAudioExtensions();
}

QString FileAssociationController::lastError() const
{
    return lastError_;
}

QString FileAssociationController::registryRootPath() const
{
    return registryRootPath_;
}

bool FileAssociationController::hasCustomRegistryRoot() const
{
    return registryRootPath_ != QStringLiteral("Software");
}

QString FileAssociationController::registryPath(const QString& relativePath) const
{
    return registryRootPath_ + QLatin1Char('\\') + relativePath;
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
#elif defined(Q_OS_MACOS)
    QStringList registered;
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (normalized.isEmpty()) continue;
        QString error;
        if (!agplayer::qt::macos::setFileAssociation(normalized, &error)) {
            for (const QString& rollbackExtension : registered) {
                QString ignored;
                agplayer::qt::macos::restoreFileAssociation(rollbackExtension, &ignored);
            }
            lastError_ = error;
            return false;
        }
        registered.append(normalized);
    }
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
#elif defined(Q_OS_MACOS)
    bool allOk = true;
    QStringList errors;
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (normalized.isEmpty()) continue;
        QString error;
        if (!agplayer::qt::macos::restoreFileAssociation(normalized, &error)) {
            allOk = false;
            errors.append(error);
        }
    }
    lastError_ = errors.join(QStringLiteral("; "));
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
    QStringList extensionsToRemove;
    QStringList errors;
    if (!registeredExtensions(&extensionsToRemove)) {
        errors.append(lastError_);
    }

    // Always cover the built-ins as well in case capabilities registration was
    // interrupted. Do not enumerate shared extension keys.
    QStringList extensions = extensionsToRemove + supportedAudioExtensions()
        + agplayer::qt::supportedVideoExtensions();
    extensions.removeDuplicates();
    for (const QString& ext : extensions) {
        if (!removeExtension(ext)) {
            errors.append(lastError_);
        }
    }
    if (!removeCapabilities()) {
        errors.append(lastError_);
    }
    if (!removeProgId()) {
        errors.append(lastError_);
    }
    if (!errors.isEmpty()) {
        lastError_ = errors.join(QStringLiteral("; "));
        return false;
    }
    lastError_.clear();
    return true;
#elif defined(Q_OS_MACOS)
    return unregisterForExtensions(agplayer::qt::macos::managedFileAssociations());
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
    const QString keyPath = registryPath(
        QStringLiteral("Classes\\.%1\\OpenWithProgids").arg(normalized));
    const std::wstring keyPathW = keyPath.toStdWString();

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPathW.c_str(), 0, KEY_READ, &key)
        != ERROR_SUCCESS) {
        return false;
    }

    DWORD valueType = 0;
    const LSTATUS status = RegQueryValueExW(
        key,
        QString::fromLatin1(kProgId).toStdWString().c_str(),
        nullptr,
        &valueType,
        nullptr,
        nullptr);
    RegCloseKey(key);

    return status == ERROR_SUCCESS && valueType == REG_SZ;
#elif defined(Q_OS_MACOS)
    return agplayer::qt::macos::isFileAssociationActive(normalized);
#else
    return false;
#endif
}

#ifdef Q_OS_WIN

bool FileAssociationController::writeProgId(const QString& appPath)
{
    const QString rootPath = registryPath(
        QStringLiteral("Classes\\%1").arg(QString::fromLatin1(kProgId)));
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
    const QString rootPath = registryPath(
        QStringLiteral("Classes\\%1").arg(QString::fromLatin1(kProgId)));
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
    // Extension roots are shared registry keys. Register only our OpenWith
    // value; never replace another application's default, PerceivedType, or
    // shell entries.
    const QString keyPath = registryPath(
        QStringLiteral("Classes\\.%1\\OpenWithProgids").arg(extension));
    const std::wstring keyPathW = keyPath.toStdWString();
    const std::wstring progIdW = progId.toStdWString();
    if (!writeRegistryString(HKEY_CURRENT_USER, keyPathW.c_str(), progIdW.c_str(),
                             QString())) {
        lastError_ = tr("Failed to register OpenWith ProgID for .%1").arg(extension);
        return false;
    }
    return true;
}

bool FileAssociationController::removeExtension(const QString& extension)
{
    const QString openWithPath = registryPath(
        QStringLiteral("Classes\\.%1\\OpenWithProgids").arg(extension));
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
    const QString capabilitiesPath = registryPath(QStringLiteral("AgPlayer\\Capabilities"));
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
    const std::wstring registeredApplicationsPathW = registryPath(
        QStringLiteral("RegisteredApplications")).toStdWString();
    ok &= writeRegistryString(HKEY_CURRENT_USER, registeredApplicationsPathW.c_str(),
                              L"AgPlayer", capabilitiesPath);
    if (!ok) lastError_ = tr("Failed to register default-app capabilities");
    return ok;
}

bool FileAssociationController::removeCapabilities()
{
    const QString registeredApplicationsPath = registryPath(
        QStringLiteral("RegisteredApplications"));
    const std::wstring registeredApplicationsPathW = registeredApplicationsPath.toStdWString();
    const LSTATUS registeredStatus = RegDeleteKeyValueW(
        HKEY_CURRENT_USER, registeredApplicationsPathW.c_str(), L"AgPlayer");
    const bool registeredOk = registeredStatus == ERROR_SUCCESS
        || registeredStatus == ERROR_FILE_NOT_FOUND || registeredStatus == ERROR_PATH_NOT_FOUND;

    // Capabilities are our dedicated subtree. Never delete Software\AgPlayer,
    // which also contains unrelated application settings.
    const QString capabilitiesPath = registryPath(QStringLiteral("AgPlayer\\Capabilities"));
    const std::wstring capabilitiesPathW = capabilitiesPath.toStdWString();
    const LSTATUS capabilityStatus = RegDeleteTreeW(
        HKEY_CURRENT_USER, capabilitiesPathW.c_str());
    const bool capabilitiesOk = capabilityStatus == ERROR_SUCCESS
        || capabilityStatus == ERROR_FILE_NOT_FOUND || capabilityStatus == ERROR_PATH_NOT_FOUND;
    if (registeredOk && capabilitiesOk) {
        return true;
    }
    if (!registeredOk && !capabilitiesOk) {
        lastError_ = tr("Failed to remove RegisteredApplications entry and default-app capabilities");
    } else if (!registeredOk) {
        lastError_ = tr("Failed to remove RegisteredApplications entry");
    } else {
        lastError_ = tr("Failed to remove default-app capabilities");
    }
    return false;
}

bool FileAssociationController::registeredExtensions(QStringList* extensions)
{
    const QString associationsPath = registryPath(
        QStringLiteral("AgPlayer\\Capabilities\\FileAssociations"));
    const std::wstring associationsPathW = associationsPath.toStdWString();
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(HKEY_CURRENT_USER, associationsPathW.c_str(), 0,
                                             KEY_READ, &key);
    if (openStatus == ERROR_FILE_NOT_FOUND || openStatus == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (openStatus != ERROR_SUCCESS) {
        lastError_ = tr("Failed to read default-app file associations");
        return false;
    }

    DWORD index = 0;
    while (true) {
        wchar_t valueName[256] = {};
        DWORD valueNameSize = 256;
        DWORD valueType = 0;
        const LSTATUS status = RegEnumValueW(key, index, valueName, &valueNameSize,
                                              nullptr, &valueType, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            RegCloseKey(key);
            return true;
        }
        if (status != ERROR_SUCCESS) {
            RegCloseKey(key);
            lastError_ = tr("Failed to enumerate default-app file associations");
            return false;
        }
        if (valueType == REG_SZ && valueNameSize > 1 && valueName[0] == L'.') {
            extensions->append(QString::fromWCharArray(valueName + 1,
                                                        static_cast<int>(valueNameSize - 1)));
        }
        ++index;
    }
}

#endif
