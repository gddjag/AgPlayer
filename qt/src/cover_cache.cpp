#include "cover_cache.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

namespace agplayer::qt {
namespace {
QString coverSuffix(const QString& mimeType)
{
    if (mimeType.compare(QStringLiteral("image/jpeg"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".jpg");
    if (mimeType.compare(QStringLiteral("image/png"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".png");
    if (mimeType.compare(QStringLiteral("image/webp"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".webp");
    if (mimeType.compare(QStringLiteral("image/bmp"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(".bmp");
    return QStringLiteral(".bin");
}
}

QUrl cacheEmbeddedCover(const QByteArray& bytes, const QString& mimeType)
{
    if (bytes.isEmpty()) return {};
    static QMutex writeMutex;
    QMutexLocker lock(&writeMutex);
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QStringList roots{
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("AgPlayer"))};
    for (const QString& root : roots) {
        if (root.isEmpty()) continue;
        const QString directory = QDir(root).filePath(QStringLiteral("covers"));
        if (!QDir().mkpath(directory)) continue;
        const QString path = QDir(directory).filePath(digest + coverSuffix(mimeType));
        if (QFileInfo::exists(path)) return QUrl::fromLocalFile(path);

        QSaveFile atomic(path);
        if (atomic.open(QIODevice::WriteOnly)
            && atomic.write(bytes) == bytes.size() && atomic.commit()) {
            return QUrl::fromLocalFile(path);
        }
        atomic.cancelWriting();

        // Some Windows encrypted cache directories reject QSaveFile's rename.
        // A new, content-addressed file can still be written directly there.
        QFile direct(path);
        if (direct.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            const bool written = direct.write(bytes) == bytes.size();
            direct.close();
            if (written && direct.error() == QFileDevice::NoError)
                return QUrl::fromLocalFile(path);
            direct.remove();
        }
    }
    return {};
}
}
