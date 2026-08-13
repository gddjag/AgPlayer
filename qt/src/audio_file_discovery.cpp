#include "audio_file_discovery.hpp"
#include "file_association_controller.hpp"

#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QtConcurrent>

#include <utility>

namespace {

bool isSupportedAudioFile(const QFileInfo& info)
{
    static const QSet<QString> suffixes = [] {
        const QStringList extensions =
            FileAssociationController::supportedAudioExtensions();
        return QSet<QString>(extensions.cbegin(), extensions.cend());
    }();
    return info.isFile()
        && suffixes.contains(info.suffix().toLower());
}

QString identityFor(const QFileInfo& info)
{
    QString identity = info.canonicalFilePath();
    if (identity.isEmpty()) {
        identity = info.absoluteFilePath();
    }
#ifdef Q_OS_WIN
    identity = identity.toLower();
#endif
    return identity;
}

} // namespace

QList<QUrl> agplayer::qt::expandAudioUrls(
    const QList<QUrl>& urls,
    const bool includeExplicitNonAudioFile)
{
    QList<QUrl> result;
    QSet<QString> seen;
    const auto appendFile =
        [&result, &seen](const QFileInfo& info, const bool requireAudio) {
        if (!info.isFile()
            || (requireAudio && !isSupportedAudioFile(info))) {
            return;
        }
        const QString identity = identityFor(info);
        if (identity.isEmpty() || seen.contains(identity)) {
            return;
        }
        seen.insert(identity);
        result.append(QUrl::fromLocalFile(info.absoluteFilePath()));
    };

    for (const QUrl& url : urls) {
        const QFileInfo info(url.toLocalFile());
        if (info.isDir()) {
            QDirIterator it(
                info.absoluteFilePath(), QDir::Files | QDir::Readable,
                QDirIterator::Subdirectories);
            while (it.hasNext()) {
                appendFile(QFileInfo(it.next()), true);
            }
        } else {
            appendFile(info, !includeExplicitNonAudioFile);
        }
    }
    return result;
}

QFuture<QList<QUrl>> agplayer::qt::expandAudioUrlsAsync(
    QList<QUrl> urls,
    const bool includeExplicitNonAudioFile)
{
    return QtConcurrent::run(
        [urls = std::move(urls), includeExplicitNonAudioFile] {
        return expandAudioUrls(urls, includeExplicitNonAudioFile);
    });
}

QUrl agplayer::qt::firstAudioUrl(const QUrl& url)
{
    const QList<QUrl> expanded = expandAudioUrls({url});
    return expanded.isEmpty() ? QUrl{} : expanded.first();
}
