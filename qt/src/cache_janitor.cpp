#include "cache_janitor.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>

#include <algorithm>
#include <vector>

namespace {

bool isSafeCachePath(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.isAbsolute()) {
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    // Refuse root/system paths (e.g. C:/, D:/).
    if (canonical.length() <= 3) {
        return false;
    }
    // Require the path to identify the application to avoid wiping arbitrary directories.
    if (!canonical.contains(QStringLiteral("AgPlayer"), Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

qint64 entryTimeMs(const QFileInfo& info)
{
    const QDateTime access = info.lastRead();
    if (access.isValid()) {
        return access.toMSecsSinceEpoch();
    }
    const QDateTime modified = info.lastModified();
    if (modified.isValid()) {
        return modified.toMSecsSinceEpoch();
    }
    return 0;
}

} // namespace

CacheJanitor::TrimReport CacheJanitor::trimToSize(const QString& dir, qint64 limitBytes)
{
    TrimReport report;
    if (limitBytes <= 0 || dir.isEmpty() || !isSafeCachePath(dir)) {
        return report;
    }

    const QDir root(dir);
    if (!root.exists()) {
        return report;
    }

    std::vector<Entry> entries;
    qint64 totalSize = 0;

    const QStringList filters{QStringLiteral("*.agwf")};
    QDirIterator it(dir, filters, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        Entry entry;
        entry.path = info.absoluteFilePath();
        entry.size = info.size();
        entry.lastAccessMs = entryTimeMs(info);
        totalSize += entry.size;
        entries.push_back(std::move(entry));
    }

    if (totalSize <= limitBytes) {
        return report;
    }

    const qint64 targetSize = static_cast<qint64>(static_cast<double>(limitBytes) * 0.9);

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  return a.lastAccessMs < b.lastAccessMs;
              });

    for (const Entry& entry : entries) {
        if (totalSize <= targetSize) {
            break;
        }
        if (QFile::remove(entry.path)) {
            totalSize -= entry.size;
            report.bytesFreed += entry.size;
            ++report.filesRemoved;
        }
    }

    return report;
}
