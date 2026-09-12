#pragma once

#include <QString>

#include <cstddef>

class CacheJanitor {
public:
    struct TrimReport {
        qint64 bytesFreed = 0;
        int filesRemoved = 0;
    };

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
