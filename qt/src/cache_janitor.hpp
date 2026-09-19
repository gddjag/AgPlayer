#pragma once

#include <QString>

#include <cstddef>
#include <atomic>

class CacheJanitor {
public:
    struct TrimReport {
        qint64 bytesFreed = 0;
        int filesRemoved = 0;
        qint64 bytesRemaining = 0;
        int failures = 0;
    };

    // The selected parent and unmarked legacy contents are never deletion targets.
    static QString storageDirectory(const QString& parent, bool create = false);
    static TrimReport maintain(const QString& parent, qint64 limitBytes,
                               const std::atomic_bool* cancelled = nullptr);
    static TrimReport clearWaveforms(const QString& parent);
    // Only callers that own the fixed Qt app-cache directory may use this;
    // never pass the user-selected cacheDirectory setting here.
    static TrimReport maintainCovers(const QString& appCache, bool clear,
                                    const std::atomic_bool* cancelled = nullptr);

    /* Delete oldest .agwf files in `dir` until total size <= limitBytes * 0.9.
     * Files are ordered by last access time (falling back to last modified time).
     * Returns the number of files removed and bytes freed. */
    static TrimReport trimToSize(const QString& dir, qint64 limitBytes);

private:
    struct Entry {
        QString path;
        qint64 size = 0;
        qint64 lastAccessMs = 0;
    };
};
