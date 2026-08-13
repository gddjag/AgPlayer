#include "voice_clone_package_manager.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDirIterator>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <utility>
#include <limits>

namespace agplayer::voice_clone {
namespace {

QString normalizedAbsolute(const QString& path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool pathIsWithin(const QString& root, const QString& candidate)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return candidate.compare(root, sensitivity) == 0
           || candidate.startsWith(root + QLatin1Char('/'), sensitivity);
}

bool isReparsePoint(const QFileInfo& info)
{
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return info.isSymLink();
#endif
}

bool pathTraversesReparsePoint(const QString& path)
{
    QString current = normalizedAbsolute(path);
    while (!current.isEmpty()) {
        const QFileInfo info(current);
        if ((info.exists() || info.isSymLink()) && isReparsePoint(info)) return true;
        const QString parent = QDir::fromNativeSeparators(info.absolutePath());
        if (parent == current) break;
        current = parent;
    }
    return false;
}

bool prepareInstallRoot(const QString& path, QString* error)
{
    const QString absolute = normalizedAbsolute(path);
    if (pathTraversesReparsePoint(absolute)) {
        *error = QStringLiteral("Install root ancestor contains a link or reparse point");
        return false;
    }
    QString existingPath = absolute;
    while (!QFileInfo::exists(existingPath)) {
        const QString parent = normalizedAbsolute(QFileInfo(existingPath).absolutePath());
        if (parent == existingPath) break;
        existingPath = parent;
    }
    const QFileInfo existingInfo(existingPath);
    const QString canonicalExisting = QDir::fromNativeSeparators(
        existingInfo.canonicalFilePath());
    if (!existingInfo.isDir() || canonicalExisting.isEmpty()
        || pathTraversesReparsePoint(existingPath)) {
        *error = QStringLiteral("Install root has no safe canonical ancestor");
        return false;
    }
    if (!QDir().mkpath(absolute) || pathTraversesReparsePoint(absolute)) {
        *error = QStringLiteral("Cannot create safe install root");
        return false;
    }
    const QString canonicalRoot = QDir::fromNativeSeparators(
        QFileInfo(absolute).canonicalFilePath());
    if (canonicalRoot.isEmpty() || !pathIsWithin(canonicalExisting, canonicalRoot)) {
        *error = QStringLiteral("Install root canonical path escapes its safe ancestor");
        return false;
    }
    return true;
}

bool removeDirectoryTree(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) return true;
    if (!info.isDir() || pathTraversesReparsePoint(path)) return false;
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
                                    | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        if (isReparsePoint(iterator.fileInfo())) return false;
    }
    return QDir(path).removeRecursively();
}

bool directoryTreeContainsReparsePoint(const QString& path)
{
    const QFileInfo rootInfo(path);
    if (pathTraversesReparsePoint(path) || isReparsePoint(rootInfo)) return true;
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
                                    | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        if (isReparsePoint(iterator.fileInfo())) return true;
    }
    return false;
}

bool hashMatches(const QString& path, const QByteArray& expected)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return false;
    return hash.result().toHex().compare(expected, Qt::CaseInsensitive) == 0;
}

QString normalizedRelativePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString unknownField(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) return it.key();
    }
    return {};
}

QString readLicenseRecords(const QString& path, QJsonArray* records)
{
    *records = {};
    const QFileInfo info(path);
    if (!info.exists()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("cannot read license acceptance store");
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QStringLiteral("corrupt license acceptance JSON");
    }
    const QJsonObject root = document.object();
    if (!unknownField(root,
                      {QStringLiteral("schemaVersion"), QStringLiteral("records")})
             .isEmpty()
        || !root.value(QStringLiteral("schemaVersion")).isDouble()
        || root.value(QStringLiteral("schemaVersion")).toDouble() != 1.0
        || !root.value(QStringLiteral("records")).isArray()) {
        return QStringLiteral("invalid license acceptance store schema");
    }
    const QSet<QString> recordFields{
        QStringLiteral("modelId"), QStringLiteral("adapterId"),
        QStringLiteral("licenseUrl"), QStringLiteral("revision"),
        QStringLiteral("acceptedAt")};
    for (const QJsonValue& value : root.value(QStringLiteral("records")).toArray()) {
        if (!value.isObject()) return QStringLiteral("invalid license acceptance record");
        const QJsonObject record = value.toObject();
        if (!unknownField(record, recordFields).isEmpty()
            || record.size() != recordFields.size()) {
            return QStringLiteral("invalid license acceptance record fields");
        }
        for (const QString& field : recordFields) {
            if (!record.value(field).isString() || record.value(field).toString().isEmpty()) {
                return QStringLiteral("invalid license acceptance record type");
            }
        }
        const QUrl licenseUrl(record.value(QStringLiteral("licenseUrl")).toString());
        if (!licenseUrl.isValid()
            || licenseUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
            || licenseUrl.host().isEmpty()
            || !QDateTime::fromString(record.value(QStringLiteral("acceptedAt")).toString(),
                                      Qt::ISODateWithMs)
                    .isValid()) {
            return QStringLiteral("invalid license acceptance record value");
        }
    }
    *records = root.value(QStringLiteral("records")).toArray();
    return {};
}

} // namespace

VoiceClonePackageManager::VoiceClonePackageManager(QString installRoot, QObject* parent)
    : VoiceClonePackageManager(std::move(installRoot), nullptr, {}, parent)
{
}

VoiceClonePackageManager::VoiceClonePackageManager(
    QString installRoot,
    QNetworkAccessManager* networkAccessManager,
    QObject* parent)
    : VoiceClonePackageManager(std::move(installRoot), networkAccessManager, {}, parent)
{
}

VoiceClonePackageManager::VoiceClonePackageManager(
    QString installRoot,
    QNetworkAccessManager* networkAccessManager,
    DeploymentOperations deploymentOperations,
    QObject* parent)
    : QObject(parent),
      installRoot_(normalizedAbsolute(std::move(installRoot))),
      ownedNetwork_(networkAccessManager == nullptr
                        ? std::make_unique<QNetworkAccessManager>()
                        : nullptr),
      network_(networkAccessManager != nullptr ? networkAccessManager
                                               : ownedNetwork_.get()),
      deploymentOperations_(std::move(deploymentOperations))
{
}

VoiceClonePackageManager::~VoiceClonePackageManager()
{
    pauseRequested_ = true;
    ++operationGeneration_;
    if (reply_ != nullptr) reply_->abort();
    partialFile_.close();
}

VoiceClonePackageManager::State VoiceClonePackageManager::state() const { return state_; }
QString VoiceClonePackageManager::errorString() const { return error_; }
qint64 VoiceClonePackageManager::transferredBytes() const { return transferredBytes_; }
qint64 VoiceClonePackageManager::totalBytes() const { return totalBytes_; }
bool VoiceClonePackageManager::isProgressIndeterminate() const { return totalBytes_ < 0; }

int VoiceClonePackageManager::progressPercent() const
{
    if (totalBytes_ < 0) return -1;
    if (totalBytes_ == 0) return state_ == Completed ? 100 : 0;
    return static_cast<int>(qMin<qint64>(100, transferredBytes_ * 100 / totalBytes_));
}

VoiceClonePackageManifest VoiceClonePackageManager::currentManifest() const { return manifest_; }
QString VoiceClonePackageManager::stagingDirectory() const { return stagingDirectory_; }
QString VoiceClonePackageManager::partialFilePath() const { return partialFilePath_; }
QString VoiceClonePackageManager::resumeMetadataPath() const { return resumeMetadataPath_; }

QString VoiceClonePackageManager::licenseAcceptancePath() const
{
    return QDir(installRoot_).filePath(QStringLiteral("license-acceptances.json"));
}

void VoiceClonePackageManager::setState(const State state, const QString& error)
{
    state_ = state;
    error_ = error;
    emit stateChanged();
}

bool VoiceClonePackageManager::preparePaths(QString* error)
{
    if (!prepareInstallRoot(installRoot_, error)) return false;
    const QString stagingRoot = QDir(installRoot_).filePath(QStringLiteral(".staging"));
    stagingDirectory_ = QDir(stagingRoot).filePath(manifest_.packageId);
    targetDirectory_ = QDir(installRoot_).filePath(manifest_.packageId);
    if (!pathIsWithin(installRoot_, normalizedAbsolute(stagingDirectory_))
        || !pathIsWithin(installRoot_, normalizedAbsolute(targetDirectory_))) {
        *error = QStringLiteral("Package path escapes install root");
        return false;
    }
    if (pathTraversesReparsePoint(targetDirectory_)
        || pathTraversesReparsePoint(stagingDirectory_)) {
        *error = QStringLiteral("Package staging or target contains a link or reparse point");
        return false;
    }
    if (!QDir().mkpath(stagingDirectory_)
        || pathTraversesReparsePoint(stagingDirectory_)) {
        *error = QStringLiteral("Cannot create safe package staging directory");
        return false;
    }
    QSet<QString> allowed;
    for (const VoiceClonePackageFile& file : std::as_const(manifest_.files)) {
        const QString base = normalizedRelativePath(file.relativePath);
        allowed.insert(base);
        allowed.insert(base + QStringLiteral(".part"));
        allowed.insert(base + QStringLiteral(".resume.json"));
    }
    QVector<QString> directories;
    QDirIterator iterator(stagingDirectory_, QDir::AllEntries | QDir::Hidden | QDir::System
                                                 | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (isReparsePoint(info)) {
            *error = QStringLiteral("Package staging contains a link or reparse point");
            return false;
        }
        if (info.isDir()) {
            directories.prepend(absolute);
            continue;
        }
        const QString relative = QDir(stagingDirectory_).relativeFilePath(absolute);
        if (!allowed.contains(QDir::fromNativeSeparators(relative)) && !QFile::remove(absolute)) {
            *error = QStringLiteral("Cannot remove unlisted staging content");
            return false;
        }
    }
    for (const QString& directory : std::as_const(directories)) QDir().rmdir(directory);
    return true;
}

bool VoiceClonePackageManager::validateStaging(QString* error) const
{
    QHash<QString, QByteArray> expected;
    for (const VoiceClonePackageFile& file : std::as_const(manifest_.files)) {
        expected.insert(normalizedRelativePath(file.relativePath), file.sha256);
    }
    QSet<QString> observed;
    QDirIterator iterator(stagingDirectory_, QDir::AllEntries | QDir::Hidden | QDir::System
                                                 | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (isReparsePoint(info)) {
            *error = QStringLiteral("Package staging contains a link or reparse point");
            return false;
        }
        if (info.isDir()) continue;
        const QString relative = QDir::fromNativeSeparators(
            QDir(stagingDirectory_).relativeFilePath(absolute));
        if (!expected.contains(relative) || observed.contains(relative)
            || !hashMatches(absolute, expected.value(relative))) {
            *error = QStringLiteral("Package staging contains unlisted or invalid content");
            return false;
        }
        observed.insert(relative);
    }
    if (observed.size() != expected.size()) {
        *error = QStringLiteral("Package staging is incomplete");
        return false;
    }
    return true;
}

void VoiceClonePackageManager::start(const VoiceClonePackageManifest& manifest)
{
    if (state_ == Resolving || state_ == Downloading || state_ == Verifying
        || state_ == Committing) {
        return;
    }
    manifest_ = manifest;
    ++operationGeneration_;
    manifest_.requiresLicenseAcceptance = manifest_.licenseAcceptanceRequired();
    error_.clear();
    pauseRequested_ = false;
    cancelRequested_ = false;
    resolvedSizes_.clear();
    currentIndex_ = 0;
    completedBytes_ = 0;
    currentResumeOffset_ = 0;
    transferredBytes_ = 0;
    totalBytes_ = -1;
    partialFilePath_.clear();
    resumeMetadataPath_.clear();

    if (!manifest_.isValid()) {
        setState(Failed, manifest_.errorString());
        return;
    }
    QString pathError;
    if (!preparePaths(&pathError)) {
        setState(Failed, pathError);
        return;
    }
    QString licenseError;
    if (manifest_.licenseAcceptanceRequired()
        && !hasAcceptedCurrentLicense(&licenseError)) {
        setState(LicenseRequired,
                 licenseError.isEmpty()
                     ? QStringLiteral("License acceptance is required")
                     : QStringLiteral("Invalid license acceptance store: %1")
                           .arg(licenseError));
        return;
    }
    setState(Resolving);
    resolveNextFile();
}

void VoiceClonePackageManager::resolveNextFile()
{
    if (pauseRequested_ || cancelRequested_) return;
    if (currentIndex_ >= manifest_.files.size()) {
        beginDownloads();
        return;
    }
    QNetworkRequest request(manifest_.files.at(currentIndex_).url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_->head(request);
    QNetworkReply* const currentReply = reply_;
    const quint64 generation = operationGeneration_;
    connect(currentReply, &QNetworkReply::finished, this, [this, currentReply, generation] {
        if (reply_ != currentReply || operationGeneration_ != generation) {
            currentReply->deleteLater();
            return;
        }
        reply_ = nullptr;
        currentReply->deleteLater();
        if (pauseRequested_ || cancelRequested_) return;
        if (currentReply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Package metadata request failed: %1")
                     .arg(currentReply->errorString()));
            return;
        }
        const QVariant lengthHeader = currentReply->header(QNetworkRequest::ContentLengthHeader);
        bool ok = false;
        const qint64 length = lengthHeader.toLongLong(&ok);
        resolvedSizes_.append(ok && length >= 0 ? length : -1);
        ++currentIndex_;
        resolveNextFile();
    });
}

void VoiceClonePackageManager::beginDownloads()
{
    bool allKnown = true;
    qint64 requiredBytes = 0;
    for (const qint64 size : std::as_const(resolvedSizes_)) {
        if (size < 0) {
            allKnown = false;
            continue;
        }
        if (requiredBytes > (std::numeric_limits<qint64>::max)() - size) {
            fail(QStringLiteral("Package size exceeds supported range"));
            return;
        }
        requiredBytes += size;
    }
    totalBytes_ = allKnown ? requiredBytes : -1;
    const QStorageInfo storage(installRoot_);
    if (storage.isValid() && storage.isReady()
        && requiredBytes > static_cast<qint64>(storage.bytesAvailable())) {
        fail(QStringLiteral("Insufficient disk space for package"));
        return;
    }
    currentIndex_ = 0;
    completedBytes_ = 0;
    transferredBytes_ = 0;
    emit progressChanged();
    downloadNextFile();
}

bool VoiceClonePackageManager::resumeMetadataMatches(
    const VoiceClonePackageFile& file) const
{
    QFile metadata(resumeMetadataPath_);
    if (!metadata.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
    if (!document.isObject()) return false;
    const QJsonObject object = document.object();
    return object.value(QStringLiteral("url")).toString() == file.url.toString()
           && object.value(QStringLiteral("sha256")).toString().toLatin1()
                  .compare(file.sha256, Qt::CaseInsensitive) == 0
           && object.value(QStringLiteral("revision")).toString() == manifest_.revision;
}

bool VoiceClonePackageManager::writeResumeMetadata(const VoiceClonePackageFile& file,
                                                   const qint64 total)
{
    QSaveFile metadata(resumeMetadataPath_);
    if (!metadata.open(QIODevice::WriteOnly)) return false;
    const QJsonObject object{{QStringLiteral("url"), file.url.toString()},
                             {QStringLiteral("sha256"), QString::fromLatin1(file.sha256)},
                             {QStringLiteral("revision"), manifest_.revision},
                             {QStringLiteral("totalBytes"), total}};
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return metadata.write(bytes) == bytes.size() && metadata.commit();
}

void VoiceClonePackageManager::downloadNextFile()
{
    if (pauseRequested_ || cancelRequested_) return;
    if (currentIndex_ >= manifest_.files.size()) {
        setState(Committing);
        QString commitError;
        if (!validateStaging(&commitError)) {
            fail(commitError);
            return;
        }
        if (!commitStagingDirectory(stagingDirectory_, targetDirectory_, &commitError,
                                    deploymentOperations_)) {
            fail(commitError);
            return;
        }
        transferredBytes_ = totalBytes_ >= 0 ? totalBytes_ : completedBytes_;
        emit progressChanged();
        setState(Completed, commitError);
        return;
    }

    const VoiceClonePackageFile& entry = manifest_.files.at(currentIndex_);
    const QString finalPath = QDir(stagingDirectory_)
                                  .filePath(normalizedRelativePath(entry.relativePath));
    if (!pathIsWithin(normalizedAbsolute(stagingDirectory_), normalizedAbsolute(finalPath))) {
        fail(QStringLiteral("Package target escapes staging directory"));
        return;
    }
    if (QFileInfo::exists(finalPath) && hashMatches(finalPath, entry.sha256)) {
        completedBytes_ += QFileInfo(finalPath).size();
        updateProgress(0);
        ++currentIndex_;
        downloadNextFile();
        return;
    }
    if (QFileInfo::exists(finalPath) && !QFile::remove(finalPath)) {
        fail(QStringLiteral("Cannot replace invalid staged package file"));
        return;
    }
    if (!QDir().mkpath(QFileInfo(finalPath).absolutePath())
        || pathTraversesReparsePoint(QFileInfo(finalPath).absolutePath())) {
        fail(QStringLiteral("Package file parent contains a link or reparse point"));
        return;
    }

    partialFilePath_ = finalPath + QStringLiteral(".part");
    resumeMetadataPath_ = finalPath + QStringLiteral(".resume.json");
    if (pathTraversesReparsePoint(partialFilePath_)
        || pathTraversesReparsePoint(resumeMetadataPath_)) {
        fail(QStringLiteral("Partial package path contains a link or reparse point"));
        return;
    }
    if (!resumeMetadataMatches(entry)) removeCurrentPartial();
    currentResumeOffset_ = QFileInfo(partialFilePath_).size();
    const qint64 expectedSize = resolvedSizes_.value(currentIndex_, -1);
    if (expectedSize < 0 && currentResumeOffset_ > 0) {
        removeCurrentPartial();
        currentResumeOffset_ = 0;
    }
    if (expectedSize >= 0 && currentResumeOffset_ > expectedSize) {
        removeCurrentPartial();
        currentResumeOffset_ = 0;
    }
    if (!writeResumeMetadata(entry, expectedSize)) {
        fail(QStringLiteral("Cannot persist package resume metadata"));
        return;
    }
    if (expectedSize >= 0 && currentResumeOffset_ == expectedSize) {
        updateProgress(currentResumeOffset_);
        finishCurrentDownload();
        return;
    }

    partialFile_.setFileName(partialFilePath_);
    const QIODevice::OpenMode mode = QIODevice::WriteOnly
                                     | (currentResumeOffset_ > 0 ? QIODevice::Append
                                                                 : QIODevice::Truncate);
    if (!partialFile_.open(mode)) {
        fail(QStringLiteral("Cannot open partial package file"));
        return;
    }
    QNetworkRequest request(entry.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (currentResumeOffset_ > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(currentResumeOffset_) + '-');
    }
    setState(Downloading);
    updateProgress(currentResumeOffset_);
    reply_ = network_->get(request);
    QNetworkReply* const currentReply = reply_;
    const quint64 generation = operationGeneration_;
    connect(currentReply, &QIODevice::readyRead, this, [this, currentReply, generation] {
        if (reply_ != currentReply || operationGeneration_ != generation) return;
        if (!pauseRequested_ && !cancelRequested_) consumeReplyData();
    });
    connect(currentReply, &QNetworkReply::finished, this, [this, currentReply, generation] {
        if (reply_ != currentReply || operationGeneration_ != generation) {
            currentReply->deleteLater();
            return;
        }
        if (!pauseRequested_ && !cancelRequested_) consumeReplyData();
        reply_ = nullptr;
        partialFile_.flush();
        partialFile_.close();
        currentReply->deleteLater();
        if (pauseRequested_ || cancelRequested_) return;
        if (currentReply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Package download failed: %1")
                     .arg(currentReply->errorString()));
            return;
        }
        if (currentResumeOffset_ > 0
            && currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 206) {
            removeCurrentPartial();
            downloadNextFile();
            return;
        }
        finishCurrentDownload();
    });
}

void VoiceClonePackageManager::consumeReplyData()
{
    if (reply_ == nullptr || !partialFile_.isOpen()) return;
    const QByteArray bytes = reply_->readAll();
    if (bytes.isEmpty()) return;
    if (partialFile_.write(bytes) != bytes.size()) {
        reply_->abort();
        fail(QStringLiteral("Cannot write partial package file"));
        return;
    }
    updateProgress(partialFile_.size());
}

void VoiceClonePackageManager::finishCurrentDownload()
{
    setState(Verifying);
    const qint64 expectedSize = resolvedSizes_.value(currentIndex_, -1);
    const QFileInfo partialInfo(partialFilePath_);
    if (expectedSize >= 0 && partialInfo.size() != expectedSize) {
        fail(QStringLiteral("Downloaded package size does not match metadata"));
        return;
    }
    const VoiceClonePackageFile& entry = manifest_.files.at(currentIndex_);
    if (!hashMatches(partialFilePath_, entry.sha256)) {
        removeCurrentPartial();
        fail(QStringLiteral("Downloaded package SHA-256 mismatch"));
        return;
    }
    const QString finalPath = partialFilePath_.left(
        partialFilePath_.size() - QStringLiteral(".part").size());
    if (QFileInfo::exists(finalPath) && !QFile::remove(finalPath)) {
        fail(QStringLiteral("Cannot replace staged package file"));
        return;
    }
    if (!QFile::rename(partialFilePath_, finalPath)) {
        fail(QStringLiteral("Cannot finalize staged package file"));
        return;
    }
    QFile::remove(resumeMetadataPath_);
    completedBytes_ += QFileInfo(finalPath).size();
    updateProgress(0);
    ++currentIndex_;
    downloadNextFile();
}

void VoiceClonePackageManager::updateProgress(const qint64 currentFileBytes)
{
    transferredBytes_ = completedBytes_ + currentFileBytes;
    emit progressChanged();
}

void VoiceClonePackageManager::fail(const QString& error)
{
    if (partialFile_.isOpen()) partialFile_.flush();
    partialFile_.close();
    setState(Failed, error);
}

bool VoiceClonePackageManager::pause()
{
    if (state_ != Resolving && state_ != Downloading) return false;
    pauseRequested_ = true;
    if (state_ == Downloading) consumeReplyData();
    ++operationGeneration_;
    QNetworkReply* const activeReply = std::exchange(reply_, nullptr);
    if (activeReply != nullptr) activeReply->abort();
    partialFile_.flush();
    partialFile_.close();
    setState(Paused);
    return true;
}

void VoiceClonePackageManager::cancel()
{
    if (state_ == Completed || state_ == Canceled || state_ == Idle) return;
    cancelRequested_ = true;
    if (state_ == Downloading) consumeReplyData();
    ++operationGeneration_;
    QNetworkReply* const activeReply = std::exchange(reply_, nullptr);
    if (activeReply != nullptr) activeReply->abort();
    partialFile_.close();
    removeStagingSafely();
    partialFilePath_.clear();
    resumeMetadataPath_.clear();
    setState(Canceled);
}

bool VoiceClonePackageManager::retry()
{
    if (state_ != Failed && state_ != Paused && state_ != LicenseRequired
        && state_ != Canceled) {
        return false;
    }
    if (state_ == LicenseRequired && !hasAcceptedCurrentLicense()) return false;
    const VoiceClonePackageManifest retryManifest = manifest_;
    start(retryManifest);
    return state_ == Resolving || state_ == Downloading || state_ == Completed;
}

void VoiceClonePackageManager::removeCurrentPartial()
{
    partialFile_.close();
    QFile::remove(partialFilePath_);
    QFile::remove(resumeMetadataPath_);
}

void VoiceClonePackageManager::removeStagingSafely()
{
    if (stagingDirectory_.isEmpty()) return;
    const QString stagingRoot = normalizedAbsolute(
        QDir(installRoot_).filePath(QStringLiteral(".staging")));
    const QString staging = normalizedAbsolute(stagingDirectory_);
    if (pathIsWithin(stagingRoot, staging) && !pathTraversesReparsePoint(staging)) {
        removeDirectoryTree(staging);
    }
}

bool VoiceClonePackageManager::hasAcceptedCurrentLicense(QString* error) const
{
    QJsonArray records;
    const QString readError = readLicenseRecords(licenseAcceptancePath(), &records);
    if (error != nullptr) *error = readError;
    if (!readError.isEmpty()) return false;
    return hasLicenseAcceptance(manifest_.modelId, manifest_.adapterId, manifest_.licenseUrl,
                                manifest_.licenseRevision);
}

bool VoiceClonePackageManager::hasLicenseAcceptance(const QString& modelId,
                                                    const QString& adapterId,
                                                    const QUrl& licenseUrl,
                                                    const QString& revision) const
{
    if (pathTraversesReparsePoint(licenseAcceptancePath())) return false;
    QJsonArray records;
    if (!readLicenseRecords(licenseAcceptancePath(), &records).isEmpty()) return false;
    for (const QJsonValue& value : std::as_const(records)) {
        const QJsonObject acceptance = value.toObject();
        if (acceptance.value(QStringLiteral("modelId")).toString() == modelId
            && acceptance.value(QStringLiteral("adapterId")).toString() == adapterId
            && acceptance.value(QStringLiteral("licenseUrl")).toString()
                   == licenseUrl.toString()
            && acceptance.value(QStringLiteral("revision")).toString()
                   == revision
            && QDateTime::fromString(
                   acceptance.value(QStringLiteral("acceptedAt")).toString(),
                   Qt::ISODateWithMs).isValid()) {
            return true;
        }
    }
    return false;
}

bool VoiceClonePackageManager::acceptLicense(const QUrl& licenseUrl,
                                             const QString& revision)
{
    if (!manifest_.licenseAcceptanceRequired() || licenseUrl != manifest_.licenseUrl
        || revision != manifest_.licenseRevision) {
        return false;
    }
    return acceptLicenseIdentity(manifest_.modelId, manifest_.adapterId,
                                 manifest_.licenseUrl, manifest_.licenseRevision);
}

bool VoiceClonePackageManager::acceptLicenseIdentity(const QString& modelId,
                                                     const QString& adapterId,
                                                     const QUrl& licenseUrl,
                                                     const QString& revision)
{
    if (modelId.trimmed().isEmpty() || adapterId.trimmed().isEmpty()
        || revision.trimmed().isEmpty() || !licenseUrl.isValid()
        || licenseUrl.scheme() != QStringLiteral("https")) {
        return false;
    }
    QString pathError;
    if (!prepareInstallRoot(installRoot_, &pathError)) {
        error_ = pathError;
        emit stateChanged();
        return false;
    }
    if (pathTraversesReparsePoint(licenseAcceptancePath())) return false;
    QJsonArray records;
    const QString readError = readLicenseRecords(licenseAcceptancePath(), &records);
    if (!readError.isEmpty()) {
        error_ = QStringLiteral("Invalid license acceptance store: %1").arg(readError);
        emit stateChanged();
        return false;
    }
    QJsonArray retained;
    for (const QJsonValue& value : std::as_const(records)) {
        const QJsonObject object = value.toObject();
        const bool sameKey = object.value(QStringLiteral("modelId")).toString()
                                 == modelId
                             && object.value(QStringLiteral("adapterId")).toString()
                                 == adapterId
                             && object.value(QStringLiteral("licenseUrl")).toString()
                                 == licenseUrl.toString()
                             && object.value(QStringLiteral("revision")).toString()
                                 == revision;
        if (!sameKey) {
            retained.append(object);
        }
    }
    retained.append(QJsonObject{
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("licenseUrl"), licenseUrl.toString()},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("acceptedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    QSaveFile file(licenseAcceptancePath());
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(
        QJsonObject{{QStringLiteral("schemaVersion"), 1},
                    {QStringLiteral("records"), retained}})
                                 .toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}

bool VoiceClonePackageManager::commitStagingDirectory(const QString& stagingDirectory,
                                                      const QString& targetDirectory,
                                                      QString* error,
                                                      const DeploymentOperations& operations)
{
    if (error != nullptr) error->clear();
    auto reject = [error](const QString& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    const QString staging = normalizedAbsolute(stagingDirectory);
    const QString target = normalizedAbsolute(targetDirectory);
    const QString parent = normalizedAbsolute(QFileInfo(target).absolutePath());
    if (staging == parent || !pathIsWithin(parent, staging)) {
        return reject(QStringLiteral("Staging and target must share one install root"));
    }
    const QString backup = target + QStringLiteral(".rollback");
    const auto renameDirectory = [&operations](const QString& source,
                                               const QString& destination) {
        return operations.renameDirectory
                   ? operations.renameDirectory(source, destination)
                   : QDir().rename(source, destination);
    };
    const auto removeDirectory = [&operations](const QString& path) {
        return operations.removeDirectory ? operations.removeDirectory(path)
                                          : removeDirectoryTree(path);
    };
    if (QFileInfo::exists(backup)) {
        if (!QFileInfo::exists(target) && QFileInfo(backup).isDir()
            && !directoryTreeContainsReparsePoint(backup)
            && renameDirectory(backup, target)) {
            return reject(QStringLiteral("Interrupted package commit recovered previous version"));
        }
        return reject(QStringLiteral("Rollback directory already exists"));
    }
    const QFileInfo stagingInfo(staging);
    if (!stagingInfo.exists() || !stagingInfo.isDir()
        || directoryTreeContainsReparsePoint(staging)) {
        return reject(QStringLiteral("Staging directory is missing or contains a reparse point"));
    }
    const QFileInfo targetInfo(target);
    if (targetInfo.exists() && !targetInfo.isDir()) {
        return reject(QStringLiteral("Target package path is not a directory"));
    }
    if (targetInfo.exists() && directoryTreeContainsReparsePoint(target)) {
        return reject(QStringLiteral("Target directory contains a reparse point"));
    }
    const bool hadTarget = QFileInfo::exists(target);
    if (hadTarget && !renameDirectory(target, backup)) {
        return reject(QStringLiteral("Cannot preserve installed package for rollback"));
    }
    if (!renameDirectory(staging, target)) {
        if (hadTarget && !renameDirectory(backup, target)) {
            return reject(QStringLiteral("Package commit failed and rollback failed"));
        }
        return reject(QStringLiteral("Package commit failed; previous version restored"));
    }
    if (hadTarget && !removeDirectory(backup)) {
        if (error != nullptr) {
            *error = QStringLiteral("Package committed; backup cleanup deferred");
        }
        return true;
    }
    return true;
}

} // namespace agplayer::voice_clone
