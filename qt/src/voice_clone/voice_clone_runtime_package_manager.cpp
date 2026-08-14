#include "voice_clone_runtime_package_manager.hpp"

#include "voice_clone_package_manager.hpp"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>

#include <limits>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace agplayer::voice_clone {
namespace {

QString absoluteNormalized(const QString& path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool isWithin(const QString& root, const QString& path)
{
    const QString normalizedRoot = QDir::cleanPath(absoluteNormalized(root));
    const QString normalizedPath = QDir::cleanPath(absoluteNormalized(path));
    return normalizedPath == normalizedRoot
           || normalizedPath.startsWith(normalizedRoot + QLatin1Char('/'), Qt::CaseInsensitive);
}

bool isReparse(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymLink()) return true;
#ifdef Q_OS_WIN
    const DWORD attributes = GetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

bool hasReparseAncestor(QString path)
{
    path = absoluteNormalized(path);
    while (!path.isEmpty()) {
        if (QFileInfo::exists(path) && isReparse(path)) return true;
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) break;
        path = parent;
    }
    return false;
}

bool collectRegularTree(const QString& root, QVector<QString>* files = nullptr)
{
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || isReparse(root)) return false;
    QStringList pending{absoluteNormalized(root)};
    while (!pending.isEmpty()) {
        const QString directory = pending.takeLast();
        const QFileInfoList entries = QDir(directory).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            const QString path = entry.absoluteFilePath();
            if (isReparse(path)) return false;
            if (entry.isDir()) pending.append(path);
            else if (entry.isFile()) {
                if (files != nullptr) files->append(path);
            } else {
                return false;
            }
        }
    }
    return true;
}

bool hasReparseDescendant(const QString& root)
{
    return !collectRegularTree(root);
}

bool removeTree(const QString& path)
{
    if (!QFileInfo::exists(path)) return true;
    if (!QFileInfo(path).isDir() || isReparse(path)) return false;
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QString child = entry.absoluteFilePath();
        if (isReparse(child)) return false;
        if (entry.isDir()) {
            if (!removeTree(child)) return false;
        } else if (!entry.isFile() || !QFile::remove(child)) {
            return false;
        }
    }
    return QDir().rmdir(path);
}

bool safeArchiveEntry(const QString& path)
{
    QString normalized = path;
    if (normalized.endsWith(QLatin1Char('/'))) normalized.chop(1);
    if (normalized.isEmpty() || normalized.startsWith(QLatin1Char('/')) || normalized.startsWith(QLatin1Char('\\'))
        || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':'))
        || path.contains(QChar::Null) || path.contains(QLatin1Char('\r'))
        || path.contains(QLatin1Char('\n'))) return false;
    const QStringList components = normalized.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (components.isEmpty()) return false;
    static const QRegularExpression safeComponent(
        QStringLiteral("^[^<>:\"/\\\\|?*\\x{0000}-\\x{001F}]+$"));
    static const QRegularExpression device(QStringLiteral("^(COM|LPT)[1-9]$"));
    for (const QString& component : components) {
        const QString base = component.section(QLatin1Char('.'), 0, 0).toUpper();
        if (component == QStringLiteral(".") || component == QStringLiteral("..")
            || component.isEmpty() || component.startsWith(QLatin1Char(' '))
            || component.endsWith(QLatin1Char('.'))
            || component.endsWith(QLatin1Char(' ')) || !safeComponent.match(component).hasMatch()
            || base == QStringLiteral("CON") || base == QStringLiteral("PRN")
            || base == QStringLiteral("AUX") || base == QStringLiteral("NUL")
            || device.match(base).hasMatch()) return false;
    }
    return true;
}

QString systemTar()
{
#ifdef Q_OS_WIN
    wchar_t buffer[MAX_PATH + 1]{};
    const UINT length = GetWindowsDirectoryW(buffer, MAX_PATH + 1);
    if (length == 0 || length > MAX_PATH) return {};
    const QString windows = QString::fromWCharArray(buffer, int(length));
    const QString candidate = QDir(windows).filePath(QStringLiteral("System32/tar.exe"));
    if (QFileInfo(candidate).isFile() && !hasReparseAncestor(candidate)
        && isWithin(windows, candidate)) return candidate;
#endif
    return {};
}

QString hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

quint16 little16(const QByteArray& bytes, const qsizetype offset)
{
    return quint16(quint8(bytes.at(offset)))
           | (quint16(quint8(bytes.at(offset + 1))) << 8U);
}

quint32 little32(const QByteArray& bytes, const qsizetype offset)
{
    return quint32(little16(bytes, offset))
           | (quint32(little16(bytes, offset + 2)) << 16U);
}

bool readAt(QFile* file, const qint64 offset, const qint64 count, QByteArray* output)
{
    if (offset < 0 || count < 0 || !file->seek(offset)) return false;
    *output = file->read(count);
    return output->size() == count;
}

bool containsZip64Extra(const QByteArray& extra, bool* malformed)
{
    qsizetype offset = 0;
    while (offset < extra.size()) {
        if (extra.size() - offset < 4) { *malformed = true; return false; }
        const quint16 id = little16(extra, offset);
        const quint16 size = little16(extra, offset + 2);
        offset += 4;
        if (size > extra.size() - offset) { *malformed = true; return false; }
        if (id == 0x0001U) return true;
        offset += size;
    }
    return false;
}

bool inspectZipCentralDirectory(const QString& archivePath,
                                const VoiceCloneRuntimePackageManifest& manifest,
                                QString* error)
{
    constexpr quint64 maximumEntryBytes = 16ULL * 1024 * 1024 * 1024;
    constexpr quint64 maximumInstalledBytes = 64ULL * 1024 * 1024 * 1024;
    constexpr quint64 maximumCompressionRatio = 200;
    constexpr quint64 ratioThreshold = 1024 * 1024;
    constexpr int maximumEntries = 200000;
    QFile archive(archivePath);
    if (!archive.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Runtime ZIP central directory could not be opened");
        return false;
    }
    const qint64 archiveSize = archive.size();
    if (archiveSize < 22) {
        *error = QStringLiteral("Runtime ZIP central directory is truncated"); return false;
    }
    const qint64 tailSize = qMin<qint64>(archiveSize, 22 + 65535);
    QByteArray tail;
    if (!readAt(&archive, archiveSize - tailSize, tailSize, &tail)) {
        *error = QStringLiteral("Runtime ZIP central directory could not be read"); return false;
    }
    const QByteArray endSignature = QByteArray::fromHex("504b0506");
    qsizetype endOffset = -1;
    for (qsizetype candidate = tail.lastIndexOf(endSignature); candidate >= 0;
         candidate = tail.lastIndexOf(endSignature, candidate - 1)) {
        if (candidate + 22 <= tail.size()
            && candidate + 22 + little16(tail, candidate + 20) == tail.size()) {
            endOffset = candidate;
            break;
        }
        if (candidate == 0) break;
    }
    if (endOffset < 0) {
        *error = QStringLiteral("Runtime ZIP central directory end record is missing or truncated");
        return false;
    }
    const quint16 disk = little16(tail, endOffset + 4);
    const quint16 centralDisk = little16(tail, endOffset + 6);
    const quint16 diskEntries = little16(tail, endOffset + 8);
    const quint16 entryCount = little16(tail, endOffset + 10);
    const quint32 centralBytes = little32(tail, endOffset + 12);
    const quint32 centralOffset = little32(tail, endOffset + 16);
    const qint64 absoluteEndOffset = archiveSize - tailSize + endOffset;
    if (disk != 0 || centralDisk != 0 || diskEntries != entryCount
        || entryCount == 0xffffU || centralBytes == 0xffffffffU
        || centralOffset == 0xffffffffU) {
        *error = QStringLiteral("Runtime ZIP64 or multi-disk archives are unsupported"); return false;
    }
    if (entryCount == 0 || entryCount > maximumEntries
        || entryCount != manifest.entryCount) {
        *error = QStringLiteral("Runtime ZIP entry count differs from its manifest"); return false;
    }
    if (quint64(centralOffset) + quint64(centralBytes) != quint64(absoluteEndOffset)) {
        *error = QStringLiteral("Runtime ZIP central directory is truncated or misplaced");
        return false;
    }

    QHash<QString, VoiceCloneRuntimeFile> expected;
    for (const auto& file : manifest.files) expected.insert(file.relativePath.toLower(), file);
    QSet<QString> listed;
    struct LocalRange { quint64 begin; quint64 end; };
    QVector<LocalRange> ranges;
    quint64 totalInstalled = 0;
    qint64 centralCursor = centralOffset;
    for (quint16 index = 0; index < entryCount; ++index) {
        QByteArray header;
        if (!readAt(&archive, centralCursor, 46, &header)
            || little32(header, 0) != 0x02014b50U) {
            *error = QStringLiteral("Runtime ZIP central directory entry is truncated"); return false;
        }
        const quint16 versionMade = little16(header, 4);
        const quint16 versionNeeded = little16(header, 6);
        const quint16 flags = little16(header, 8);
        const quint16 method = little16(header, 10);
        const quint32 crc = little32(header, 16);
        const quint32 compressedBytes = little32(header, 20);
        const quint32 installedBytes = little32(header, 24);
        const quint16 nameBytes = little16(header, 28);
        const quint16 extraBytes = little16(header, 30);
        const quint16 commentBytes = little16(header, 32);
        const quint16 startDisk = little16(header, 34);
        const quint32 externalAttributes = little32(header, 38);
        const quint32 localOffset = little32(header, 42);
        if (versionNeeded >= 45 || compressedBytes == 0xffffffffU
            || installedBytes == 0xffffffffU || localOffset == 0xffffffffU
            || startDisk != 0) {
            *error = QStringLiteral("Runtime ZIP64 entries are unsupported"); return false;
        }
        if ((flags & 0x0001U) != 0) {
            *error = QStringLiteral("Runtime ZIP contains an encrypted entry"); return false;
        }
        if ((flags & ~quint16(0x0800U)) != 0) {
            *error = QStringLiteral("Runtime ZIP contains unsupported entry flags"); return false;
        }
        if (method != 0 && method != 8) {
            *error = QStringLiteral("Runtime ZIP compression method is unsupported"); return false;
        }
        const quint16 creator = versionMade >> 8U;
        const quint32 unixType = (externalAttributes >> 16U) & 0xf000U;
        if ((creator == 3 && unixType != 0 && unixType != 0x8000U)
            || (externalAttributes & 0x10U) != 0) {
            *error = QStringLiteral("Runtime ZIP contains an unsafe ZIP entry type"); return false;
        }
        if (nameBytes == 0 || nameBytes > 4096) {
            *error = QStringLiteral("Runtime ZIP contains an unsafe path"); return false;
        }
        QByteArray variable;
        const qint64 variableBytes = qint64(nameBytes) + extraBytes + commentBytes;
        if (!readAt(&archive, centralCursor + 46, variableBytes, &variable)) {
            *error = QStringLiteral("Runtime ZIP central directory entry is truncated"); return false;
        }
        const QByteArray encodedName = variable.left(nameBytes);
        const QByteArray extra = variable.mid(nameBytes, extraBytes);
        bool malformedExtra = false;
        if (containsZip64Extra(extra, &malformedExtra)) {
            *error = QStringLiteral("Runtime ZIP64 entries are unsupported"); return false;
        }
        if (malformedExtra) {
            *error = QStringLiteral("Runtime ZIP central directory extra data is truncated");
            return false;
        }
        const QString path = QString::fromUtf8(encodedName);
        if (path.toUtf8() != encodedName || path.endsWith(QLatin1Char('/'))
            || !safeArchiveEntry(path)) {
            *error = QStringLiteral("Runtime ZIP contains an unsafe path"); return false;
        }
        const QString normalized = path.toLower();
        if (listed.contains(normalized) || !expected.contains(normalized)) {
            *error = QStringLiteral("Runtime ZIP contains a duplicate or unlisted path"); return false;
        }
        listed.insert(normalized);
        if (expected.value(normalized).bytes != installedBytes) {
            *error = QStringLiteral("Runtime ZIP installed size differs from its manifest");
            return false;
        }
        if (installedBytes > maximumEntryBytes
            || totalInstalled > maximumInstalledBytes - installedBytes) {
            *error = QStringLiteral("Runtime ZIP installed size exceeds supported limits");
            return false;
        }
        totalInstalled += installedBytes;
        if (method == 0 && compressedBytes != installedBytes) {
            *error = QStringLiteral("Runtime ZIP stored entry size is inconsistent"); return false;
        }
        if (installedBytes > ratioThreshold
            && (compressedBytes == 0
                || quint64(installedBytes) / quint64(compressedBytes)
                       > maximumCompressionRatio)) {
            *error = QStringLiteral("Runtime ZIP compression ratio exceeds the safe limit");
            return false;
        }

        QByteArray localHeader;
        if (localOffset >= centralOffset || !readAt(&archive, localOffset, 30, &localHeader)
            || little32(localHeader, 0) != 0x04034b50U) {
            *error = QStringLiteral("Runtime ZIP local entry header is truncated"); return false;
        }
        const quint16 localFlags = little16(localHeader, 6);
        const quint16 localMethod = little16(localHeader, 8);
        const quint32 localCrc = little32(localHeader, 14);
        const quint32 localCompressed = little32(localHeader, 18);
        const quint32 localInstalled = little32(localHeader, 22);
        const quint16 localNameBytes = little16(localHeader, 26);
        const quint16 localExtraBytes = little16(localHeader, 28);
        const quint64 localVariableBytes = quint64(localNameBytes) + localExtraBytes;
        const quint64 dataBegin = quint64(localOffset) + 30 + localVariableBytes;
        const quint64 dataEnd = dataBegin + compressedBytes;
        if (localFlags != flags || localMethod != method || localCrc != crc
            || localCompressed != compressedBytes || localInstalled != installedBytes
            || localNameBytes != nameBytes || dataEnd > centralOffset) {
            *error = QStringLiteral("Runtime ZIP local and central entry metadata differ");
            return false;
        }
        QByteArray localVariable;
        if (!readAt(&archive, qint64(localOffset) + 30, qint64(localVariableBytes),
                    &localVariable)
            || localVariable.left(localNameBytes) != encodedName) {
            *error = QStringLiteral("Runtime ZIP local entry path is truncated or inconsistent");
            return false;
        }
        bool malformedLocalExtra = false;
        if (containsZip64Extra(localVariable.mid(localNameBytes), &malformedLocalExtra)) {
            *error = QStringLiteral("Runtime ZIP64 entries are unsupported"); return false;
        }
        if (malformedLocalExtra) {
            *error = QStringLiteral("Runtime ZIP local entry extra data is truncated"); return false;
        }
        ranges.append({localOffset, dataEnd});
        centralCursor += 46 + variableBytes;
    }
    if (centralCursor != qint64(centralOffset) + centralBytes) {
        *error = QStringLiteral("Runtime ZIP central directory size is inconsistent"); return false;
    }
    if (listed.size() != expected.size() || totalInstalled != quint64(manifest.installedBytes)) {
        *error = QStringLiteral("Runtime ZIP file list or installed size differs from its manifest");
        return false;
    }
    std::sort(ranges.begin(), ranges.end(), [](const LocalRange& left, const LocalRange& right) {
        return left.begin < right.begin;
    });
    if (ranges.isEmpty() || ranges.constFirst().begin != 0) {
        *error = QStringLiteral("Runtime ZIP local records are misplaced"); return false;
    }
    for (qsizetype index = 1; index < ranges.size(); ++index) {
        if (ranges.at(index).begin < ranges.at(index - 1).end) {
            *error = QStringLiteral("Runtime ZIP local records overlap"); return false;
        }
    }
    return true;
}

} // namespace

VoiceCloneRuntimePackageManager::VoiceCloneRuntimePackageManager(
    QString runtimeRoot, const VoiceCloneRuntimeValidationPolicy policy, QObject* parent)
    : VoiceCloneRuntimePackageManager(std::move(runtimeRoot), nullptr, {}, policy, parent)
{
}

VoiceCloneRuntimePackageManager::VoiceCloneRuntimePackageManager(
    QString runtimeRoot, QNetworkAccessManager* network,
    DeploymentOperations deploymentOperations,
    const VoiceCloneRuntimeValidationPolicy policy, QObject* parent)
    : QObject(parent), runtimeRoot_(absoluteNormalized(runtimeRoot)), policy_(policy),
      network_(network), deploymentOperations_(std::move(deploymentOperations))
{
    if (network_ == nullptr) {
        ownedNetwork_ = std::make_unique<QNetworkAccessManager>();
        network_ = ownedNetwork_.get();
    }
}

VoiceCloneRuntimePackageManager::~VoiceCloneRuntimePackageManager()
{
    ++generation_;
    closeReply();
}

QString VoiceCloneRuntimePackageManager::phase() const
{
    switch (state_) {
    case Idle: return QStringLiteral("idle");
    case Resolving: return QStringLiteral("runtime-resolving");
    case Downloading: return QStringLiteral("runtime-downloading");
    case Paused: return QStringLiteral("runtime-paused");
    case Verifying: return QStringLiteral("runtime-verifying");
    case Extracting: return QStringLiteral("runtime-installing");
    case Committing: return QStringLiteral("runtime-committing");
    case Completed: return QStringLiteral("ready");
    case Canceled: return QStringLiteral("canceled");
    case Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

int VoiceCloneRuntimePackageManager::progressPercent() const
{
    if (totalBytes_ <= 0 || transferredBytes_ < 0) return -1;
    return int(qBound<qint64>(qint64(0), transferredBytes_ * 100 / totalBytes_, qint64(100)));
}

QString VoiceCloneRuntimePackageManager::partialArchivePath(
    const VoiceCloneRuntimePackageManifest& manifest) const
{
    return QDir(runtimeRoot_).filePath(
        QStringLiteral(".downloads/%1-%2.zip.part").arg(manifest.packageId, manifest.version));
}

QString VoiceCloneRuntimePackageManager::resumeMetadataPath(
    const VoiceCloneRuntimePackageManifest& manifest) const
{
    return partialArchivePath(manifest) + QStringLiteral(".json");
}

void VoiceCloneRuntimePackageManager::setState(const State state, const QString& error)
{
    state_ = state;
    error_ = error;
    emit stateChanged();
}

bool VoiceCloneRuntimePackageManager::prepareRoot(QString* error)
{
    if (runtimeRoot_.isEmpty() || hasReparseAncestor(runtimeRoot_)) {
        *error = QStringLiteral("Runtime root traverses a link or reparse point"); return false;
    }
    if (!QDir().mkpath(runtimeRoot_) || hasReparseAncestor(runtimeRoot_)) {
        *error = QStringLiteral("Runtime root could not be created safely"); return false;
    }
    qint64 requiredBytes = manifest_.packageBytes;
    constexpr qint64 reserveBytes = 64LL * 1024 * 1024;
    for (const auto& file : manifest_.files) {
        if (file.bytes > (std::numeric_limits<qint64>::max)() - requiredBytes) {
            *error = QStringLiteral("Runtime install size exceeds supported limits"); return false;
        }
        requiredBytes += file.bytes;
    }
    if (requiredBytes > (std::numeric_limits<qint64>::max)() - reserveBytes) {
        *error = QStringLiteral("Runtime install size exceeds supported limits"); return false;
    }
    requiredBytes += reserveBytes;
    const QStorageInfo storage(runtimeRoot_);
    if (!storage.isValid() || !storage.isReady()) {
        *error = QStringLiteral("Runtime disk space could not be verified"); return false;
    }
    if (storage.bytesAvailable() < requiredBytes) {
        *error = QStringLiteral("Runtime Pack requires more verified disk space than is available");
        return false;
    }
    const QString canonical = QFileInfo(runtimeRoot_).canonicalFilePath();
    if (canonical.isEmpty() || absoluteNormalized(canonical) != runtimeRoot_) {
        *error = QStringLiteral("Runtime root is not canonical"); return false;
    }
    archivePath_ = partialArchivePath(manifest_);
    resumePath_ = resumeMetadataPath(manifest_);
    const QString versionParent = QDir(runtimeRoot_).filePath(manifest_.runtimeId);
    if (!QDir().mkpath(versionParent) || hasReparseAncestor(versionParent)) {
        *error = QStringLiteral("Runtime version root could not be created safely"); return false;
    }
    stagingRoot_ = QDir(versionParent).filePath(
        QStringLiteral(".%1-%2.staging").arg(manifest_.packageId, manifest_.version));
    targetRoot_ = QDir(versionParent).filePath(manifest_.version);
    for (const QString& path : {archivePath_, resumePath_, stagingRoot_, targetRoot_}) {
        if (!isWithin(runtimeRoot_, path) || hasReparseAncestor(path)) {
            *error = QStringLiteral("Runtime installation path is unsafe"); return false;
        }
    }
    if (!QDir().mkpath(QFileInfo(archivePath_).absolutePath())) {
        *error = QStringLiteral("Runtime download directory could not be created"); return false;
    }
    return true;
}

void VoiceCloneRuntimePackageManager::start(const VoiceCloneRuntimePackageManifest& manifest)
{
    ++generation_;
    closeReply();
    manifest_ = manifest;
    transferredBytes_ = 0;
    totalBytes_ = manifest.packageBytes;
    error_.clear();
    if (!manifest_.isValid(policy_)) { fail(manifest_.errorString(policy_)); return; }
    QString error;
    if (!prepareRoot(&error)) { fail(error); return; }
    beginHead();
}

void VoiceCloneRuntimePackageManager::beginHead()
{
    setState(Resolving);
    QNetworkRequest request(manifest_.packageUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_->head(request);
    const quint64 generation = generation_;
    QNetworkReply* expected = reply_;
    connect(reply_, &QNetworkReply::finished, this, [this, generation, expected] {
        if (generation != generation_ || reply_ != expected) return;
        const int status = expected->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const qint64 length = expected->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const QString networkError = expected->error() == QNetworkReply::NoError
                                         ? QString{} : expected->errorString();
        closeReply();
        if (!networkError.isEmpty() || status < 200 || status >= 300 || length != manifest_.packageBytes) {
            fail(QStringLiteral("Runtime package size/HEAD verification failed")); return;
        }
        beginGet();
    });
}

bool VoiceCloneRuntimePackageManager::resumeMetadataMatches(const qint64 bytes) const
{
    QFile file(resumePath_);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    return object.value(QStringLiteral("schemaVersion")).toInt() == 1
           && object.value(QStringLiteral("packageId")).toString() == manifest_.packageId
           && object.value(QStringLiteral("version")).toString() == manifest_.version
           && object.value(QStringLiteral("sha256")).toString() == manifest_.packageSha256
           && object.value(QStringLiteral("bytes")).toVariant().toLongLong() == bytes;
}

bool VoiceCloneRuntimePackageManager::writeResumeMetadata(const qint64 bytes)
{
    QSaveFile file(resumePath_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("packageId"), manifest_.packageId},
                             {QStringLiteral("version"), manifest_.version},
                             {QStringLiteral("sha256"), manifest_.packageSha256},
                             {QStringLiteral("bytes"), bytes}};
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0 && file.commit();
}

void VoiceCloneRuntimePackageManager::beginGet()
{
    resumeOffset_ = QFileInfo(archivePath_).size();
    if (resumeOffset_ < 0 || resumeOffset_ >= manifest_.packageBytes
        || !resumeMetadataMatches(resumeOffset_)) {
        QFile::remove(archivePath_);
        QFile::remove(resumePath_);
        resumeOffset_ = 0;
    }
    archive_.setFileName(archivePath_);
    if (!archive_.open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(QStringLiteral("Runtime partial archive could not be opened")); return;
    }
    if (!writeResumeMetadata(resumeOffset_)) {
        archive_.close(); fail(QStringLiteral("Runtime resume metadata could not be written")); return;
    }
    QNetworkRequest request(manifest_.packageUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resumeOffset_ > 0)
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(resumeOffset_) + '-');
    reply_ = network_->get(request);
    const quint64 generation = generation_;
    QNetworkReply* expected = reply_;
    connect(reply_, &QNetworkReply::readyRead, this, [this, generation, expected] {
        if (generation == generation_ && reply_ == expected) consumeData();
    });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this, generation, expected](const qint64 received, qint64) {
        if (generation != generation_ || reply_ != expected) return;
        transferredBytes_ = resumeOffset_ + received;
        emit progressChanged();
    });
    connect(reply_, &QNetworkReply::finished, this, [this, generation, expected] {
        if (generation == generation_ && reply_ == expected) finishDownload();
    });
    setState(Downloading);
}

void VoiceCloneRuntimePackageManager::consumeData()
{
    const QByteArray bytes = reply_->readAll();
    if (bytes.isEmpty()) return;
    if (archive_.write(bytes) != bytes.size()) {
        fail(QStringLiteral("Runtime partial archive write failed"));
        return;
    }
    transferredBytes_ = archive_.size();
    emit progressChanged();
}

void VoiceCloneRuntimePackageManager::finishDownload()
{
    consumeData();
    if (state_ == Failed) return;
    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply_->error();
    archive_.flush();
    archive_.close();
    closeReply();
    if (networkError != QNetworkReply::NoError
        || (resumeOffset_ > 0 ? status != 206 : (status < 200 || status >= 300))) {
        fail(QStringLiteral("Runtime package download failed")); return;
    }
    if (QFileInfo(archivePath_).size() != manifest_.packageBytes
        || hashFile(archivePath_) != manifest_.packageSha256) {
        fail(QStringLiteral("Runtime package size or SHA-256 mismatch")); return;
    }
    setState(Verifying);
    QString error;
    if (!inspectAndExtract(&error)) { fail(error); return; }
    setState(Committing);
    if (!commit(&error)) { fail(error); return; }
    QFile::remove(archivePath_);
    QFile::remove(resumePath_);
    transferredBytes_ = manifest_.packageBytes;
    emit progressChanged();
    setState(Completed);
}

bool VoiceCloneRuntimePackageManager::inspectAndExtract(QString* error)
{
    if (!inspectZipCentralDirectory(archivePath_, manifest_, error)) return false;
    const QString tar = systemTar();
    if (tar.isEmpty()) { *error = QStringLiteral("Trusted Windows ZIP extractor is unavailable"); return false; }
    if (QFileInfo::exists(stagingRoot_) && !removeTree(stagingRoot_)) {
        *error = QStringLiteral("Runtime staging directory is unsafe or could not be cleaned"); return false;
    }
    if (!QDir().mkpath(stagingRoot_) || hasReparseAncestor(stagingRoot_)) {
        *error = QStringLiteral("Runtime staging directory could not be created safely"); return false;
    }
    setState(Extracting);
    QProcess extraction;
    extraction.setProgram(tar);
    extraction.setArguments({QStringLiteral("-xf"), archivePath_, QStringLiteral("-C"), stagingRoot_});
    extraction.start();
    if (!extraction.waitForFinished(120000) || extraction.exitStatus() != QProcess::NormalExit
        || extraction.exitCode() != 0) {
        *error = QStringLiteral("Runtime ZIP extraction failed"); return false;
    }
    if (!validateExtracted(error) || !writeInstalledMarker(error)) return false;
    return true;
}

bool VoiceCloneRuntimePackageManager::validateExtracted(QString* error) const
{
    QVector<QString> extractedFiles;
    if (!collectRegularTree(stagingRoot_, &extractedFiles)) {
        *error = QStringLiteral("Runtime staging contains a link or reparse point"); return false;
    }
    QSet<QString> actual;
    for (const QString& path : extractedFiles) {
        const QString relative = QDir(stagingRoot_).relativeFilePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
        actual.insert(relative.toLower());
    }
    QSet<QString> expected;
    for (const auto& file : manifest_.files) {
        expected.insert(file.relativePath.toLower());
        const QString path = QDir(stagingRoot_).filePath(file.relativePath);
        if (!isWithin(stagingRoot_, path) || QFileInfo(path).size() != file.bytes
            || hashFile(path) != file.sha256) {
            *error = QStringLiteral("Extracted Runtime payload failed size/SHA verification"); return false;
        }
    }
    if (actual != expected) { *error = QStringLiteral("Extracted Runtime contains unlisted files"); return false; }
    return true;
}

bool VoiceCloneRuntimePackageManager::writeInstalledMarker(QString* error) const
{
    QSaveFile file(QDir(stagingRoot_).filePath(QStringLiteral("agplayer-runtime.json")));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(manifest_.toJson()).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        *error = QStringLiteral("Installed Runtime marker could not be written"); return false;
    }
    return true;
}

bool VoiceCloneRuntimePackageManager::commit(QString* error)
{
    VoiceClonePackageManager::DeploymentOperations operations;
    operations.renameDirectory = deploymentOperations_.renameDirectory;
    operations.removeDirectory = deploymentOperations_.removeDirectory;
    return VoiceClonePackageManager::commitStagingDirectory(stagingRoot_, targetRoot_, error, operations);
}

VoiceCloneRuntimeResolution VoiceCloneRuntimePackageManager::resolveInstalled(
    const QString& runtimeId, const QString& adapterId, const QString& adapterVersion,
    const int protocolVersion) const
{
    VoiceCloneRuntimeResolution result;
    const QString versionsRoot = QDir(runtimeRoot_).filePath(runtimeId);
    if (!isWithin(runtimeRoot_, versionsRoot) || hasReparseAncestor(versionsRoot)) {
        result.error = QStringLiteral("Installed Runtime root is unsafe"); return result;
    }
    const QFileInfoList versions = QDir(versionsRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo& version : versions) {
        QVector<QString> installedFiles;
        if (!collectRegularTree(version.absoluteFilePath(), &installedFiles)) continue;
        QFile marker(QDir(version.absoluteFilePath()).filePath(QStringLiteral("agplayer-runtime.json")));
        if (!marker.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(marker.readAll());
        if (!document.isObject()) continue;
        auto manifest = VoiceCloneRuntimePackageManifest::fromJson(document.object(), policy_);
        if (!manifest.isValid(policy_) || manifest.runtimeId != runtimeId
            || manifest.version != version.fileName()
            || !manifest.supports(adapterId, adapterVersion, protocolVersion)) continue;
        QSet<QString> actual;
        for (const QString& path : installedFiles) {
            actual.insert(QDir(version.absoluteFilePath()).relativeFilePath(path)
                              .replace(QLatin1Char('\\'), QLatin1Char('/')).toLower());
        }
        QSet<QString> expected{QStringLiteral("agplayer-runtime.json")};
        bool valid = true;
        for (const auto& file : manifest.files) {
            expected.insert(file.relativePath.toLower());
            const QString path = QDir(version.absoluteFilePath()).filePath(file.relativePath);
            if (!isWithin(version.absoluteFilePath(), path) || QFileInfo(path).size() != file.bytes
                || hashFile(path) != file.sha256) { valid = false; break; }
        }
        if (!valid || actual != expected) continue;
        result.root = absoluteNormalized(version.absoluteFilePath());
        result.manifest = manifest;
        return result;
    }
    result.error = QStringLiteral("No verified compatible Runtime Pack is installed");
    return result;
}

bool VoiceCloneRuntimePackageManager::pause()
{
    if (state_ != Downloading) return false;
    ++generation_;
    if (reply_ != nullptr) reply_->abort();
    closeReply();
    archive_.flush(); archive_.close();
    writeResumeMetadata(QFileInfo(archivePath_).size());
    setState(Paused);
    return true;
}

void VoiceCloneRuntimePackageManager::cancel()
{
    ++generation_;
    if (reply_ != nullptr) reply_->abort();
    closeReply();
    archive_.close();
    setState(Canceled);
}

bool VoiceCloneRuntimePackageManager::retry()
{
    if (state_ != Paused && state_ != Failed && state_ != Canceled) return false;
    start(manifest_);
    return state_ != Failed;
}

void VoiceCloneRuntimePackageManager::closeReply()
{
    if (reply_ != nullptr) {
        reply_->disconnect(this);
        reply_->deleteLater();
        reply_ = nullptr;
    }
}

void VoiceCloneRuntimePackageManager::fail(const QString& error)
{
    ++generation_;
    if (reply_ != nullptr) reply_->abort();
    closeReply();
    archive_.close();
    setState(Failed, error);
}

} // namespace agplayer::voice_clone
