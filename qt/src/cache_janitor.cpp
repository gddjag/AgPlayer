#include "cache_janitor.hpp"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace {
constexpr auto kDirectory = ".agplayer-cache-v1";
constexpr auto kMarker = ".owner";
const QByteArray kOwnership("AgPlayer disposable waveform cache v1\n");

bool isLink(const QFileInfo& info)
{
    return info.isSymLink() || info.isJunction();
}

bool isCacheFile(const QFileInfo& info, const QString& storage)
{
    static const QRegularExpression name(
        QStringLiteral("^[0-9a-f]{16}(?:-average|-rms)?\\.agwf$"));
    return info.isFile() && !isLink(info)
        && info.canonicalPath() == storage
        && name.match(info.fileName()).hasMatch();
}

qint64 entryTimeMs(const QFileInfo& info)
{
    return (info.lastRead().isValid() ? info.lastRead() : info.lastModified()).toMSecsSinceEpoch();
}
} // namespace

QString CacheJanitor::storageDirectory(const QString& parent, bool create)
{
    if (parent.isEmpty() || !QFileInfo(parent).isAbsolute() || QDir(parent).isRoot()) return {};
    static QMutex creationMutex;
    QMutexLocker lock(&creationMutex);
    if (create && !QDir().mkpath(parent)) return {};
    const QString root = QFileInfo(parent).canonicalFilePath();
    if (root.isEmpty() || QDir(root).isRoot()) return {};
    const QString path = QDir(root).filePath(QString::fromLatin1(kDirectory));
    if (isLink(QFileInfo(path))) return {};
    if (!QFileInfo::exists(path)) {
        if (!create || !QDir(root).mkdir(QString::fromLatin1(kDirectory))) return {};
    }
    const QFileInfo directory(path);
    if (!directory.isDir() || isLink(directory) || directory.canonicalFilePath() != path) return {};
    const QString markerPath = QDir(path).filePath(QString::fromLatin1(kMarker));
    if (isLink(QFileInfo(markerPath))) return {};
    QFile marker(markerPath);
    if (!marker.exists() && create) {
        // Never adopt a directory that contains unowned user data.
        if (!QDir(path).isEmpty()) return {};
        if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return {};
        const bool written = marker.write(kOwnership) == kOwnership.size();
        marker.close();
        if (!written) return {};
    }
    if (!marker.open(QIODevice::ReadOnly) || marker.read(128) != kOwnership) return {};
    return path;
}

CacheJanitor::TrimReport CacheJanitor::maintain(
    const QString& parent, qint64 limitBytes, const std::atomic_bool* cancelled)
{
    TrimReport report;
    const QString storage = storageDirectory(parent);
    if (storage.isEmpty()) {
        const QFileInfo root(parent);
        const QFileInfo candidate(QDir(parent).filePath(QString::fromLatin1(kDirectory)));
        // An absent cache is empty; an existing but unverifiable namespace is
        // protected, and a manual clear must not report it as a success.
        if (limitBytes < 0 && (parent.isEmpty() || !root.isAbsolute() || QDir(parent).isRoot()
            || (root.exists() && !root.isDir()) || candidate.exists() || isLink(candidate)))
            ++report.failures;
        return report;
    }
    if (!QFileInfo(storage).isReadable()) {
        ++report.failures;
        return report;
    }
    const auto stopped = [cancelled] { return cancelled && cancelled->load(std::memory_order_relaxed); };
    std::vector<Entry> entries;
    for (const QFileInfo& info : QDir(storage).entryInfoList(
             {QStringLiteral("*.agwf")}, QDir::Files | QDir::NoSymLinks, QDir::NoSort)) {
        if (stopped()) return report;
        if (!isCacheFile(info, storage)) continue;
        report.bytesRemaining += info.size();
        if (limitBytes != 0) entries.push_back({info.absoluteFilePath(), info.size(), entryTimeMs(info)});
    }
    if (limitBytes == 0 || (limitBytes > 0 && report.bytesRemaining <= limitBytes)) return report;
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.lastAccessMs < b.lastAccessMs;
    });
    const qint64 target = limitBytes < 0 ? -1 : limitBytes * 9 / 10;
    for (const Entry& entry : entries) {
        if (stopped() || report.bytesRemaining <= target) break;
        // Recheck ownership/containment before removal; never recurse into directories.
        if (storageDirectory(parent) != storage) { ++report.failures; break; }
        if (!isCacheFile(QFileInfo(entry.path), storage)) continue;
        if (QFile::remove(entry.path)) {
            report.bytesRemaining -= entry.size;
            report.bytesFreed += entry.size;
            ++report.filesRemoved;
        } else if (QFileInfo::exists(entry.path)) {
            ++report.failures;
        }
    }
    return report;
}

CacheJanitor::TrimReport CacheJanitor::clearWaveforms(const QString& parent)
{
    return maintain(parent, -1);
}

CacheJanitor::TrimReport CacheJanitor::trimToSize(const QString& dir, qint64 limitBytes)
{
    return maintain(dir, std::max(qint64(0), limitBytes));
}

CacheJanitor::TrimReport CacheJanitor::maintainCovers(
    const QString& appCache, bool clear, const std::atomic_bool* cancelled)
{
    TrimReport report;
    const QFileInfo root(appCache);
    if (!root.isAbsolute() || !root.isDir() || isLink(root) || QDir(appCache).isRoot()) {
        if (clear && (root.exists() || isLink(root) || appCache.isEmpty() || !root.isAbsolute()))
            ++report.failures;
        return report;
    }
    const QString path = QDir(root.canonicalFilePath()).filePath(QStringLiteral("covers"));
    const QFileInfo folder(path);
    if (!folder.isDir() || isLink(folder) || folder.canonicalFilePath() != path || !folder.isReadable()) {
        if (clear && (folder.exists() || isLink(folder))) ++report.failures;
        return report;
    }
    static const QRegularExpression name(QStringLiteral("^([0-9a-f]{64})\\.(?:jpg|png|webp|bmp|bin)$"));
    for (const QFileInfo& fileInfo : QDir(path).entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::NoSort)) {
        if (cancelled && cancelled->load()) break;
        const auto match = name.match(fileInfo.fileName());
        if (!match.hasMatch() || isLink(fileInfo) || fileInfo.canonicalPath() != path) continue;
        report.bytesRemaining += fileInfo.size();
        if (!clear) continue;
        // Both cover writers use SHA-256(content) as the filename. A copied
        // personal file with a cache-looking name is not disposable unless it matches.
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) { ++report.failures; continue; }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        bool readOk = true;
        while (!file.atEnd()) {
            if (cancelled && cancelled->load()) return report;
            const QByteArray bytes = file.read(16384);
            if (bytes.isEmpty() && file.error() != QFileDevice::NoError) { readOk = false; break; }
            hash.addData(bytes);
        }
        file.close();
        if (!readOk) { ++report.failures; continue; }
        if (QString::fromLatin1(hash.result().toHex()) != match.captured(1)) continue;
        if (isLink(QFileInfo(path)) || QFileInfo(path).canonicalFilePath() != path) {
            ++report.failures;
            break;
        }
        const QFileInfo current(fileInfo.absoluteFilePath());
        if (isLink(current) || current.canonicalPath() != path
            || current.size() != fileInfo.size() || current.lastModified() != fileInfo.lastModified()) continue;
        if (file.remove()) {
            report.bytesFreed += fileInfo.size();
            report.bytesRemaining -= fileInfo.size();
            ++report.filesRemoved;
        } else if (QFileInfo::exists(fileInfo.absoluteFilePath())) {
            ++report.failures;
        }
    }
    return report;
}
