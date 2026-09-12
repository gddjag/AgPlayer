#include "audio_file_discovery.hpp"
#include "resource_path.hpp"

#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QtConcurrent>

#include <utility>

namespace {

const QStringList& supportedExtensionList()
{
    static const QStringList extensions{
        QStringLiteral("mp3"), QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("aac"),
        QStringLiteral("m4a"), QStringLiteral("ogg"),
        QStringLiteral("wma"), QStringLiteral("ape"),
        QStringLiteral("opus"), QStringLiteral("aif"),
        QStringLiteral("aiff")};
    return extensions;
}

const QSet<QString>& supportedExtensionSet()
{
    static const QSet<QString> extensions(
        supportedExtensionList().cbegin(), supportedExtensionList().cend());
    return extensions;
}

const QStringList& supportedVideoExtensionList()
{
    static const QStringList extensions{
        QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("webm"), QStringLiteral("mov"),
        QStringLiteral("avi"), QStringLiteral("m4v")};
    return extensions;
}

const QSet<QString>& supportedVideoExtensionSet()
{
    static const QSet<QString> extensions(
        supportedVideoExtensionList().cbegin(), supportedVideoExtensionList().cend());
    return extensions;
}

} // namespace

QStringList agplayer::qt::supportedAudioExtensions()
{
    return supportedExtensionList();
}

bool agplayer::qt::isSupportedAudioExtension(const QString& extension)
{
    QString normalized = extension.trimmed().toCaseFolded();
    while (normalized.startsWith(QLatin1Char('.'))) normalized.remove(0, 1);
    return supportedExtensionSet().contains(normalized);
}

QStringList agplayer::qt::supportedVideoExtensions()
{
    return supportedVideoExtensionList();
}

bool agplayer::qt::isSupportedVideoExtension(const QString& extension)
{
    QString normalized = extension.trimmed().toCaseFolded();
    while (normalized.startsWith(QLatin1Char('.'))) normalized.remove(0, 1);
    return supportedVideoExtensionSet().contains(normalized);
}

bool agplayer::qt::isSupportedAudioFile(const QFileInfo& info)
{
    return info.isFile() && isSupportedAudioExtension(info.suffix());
}

QList<QUrl> agplayer::qt::expandAudioUrls(
    const QList<QUrl>& urls,
    const bool includeExplicitNonAudioFile)
{
    QList<QUrl> result;
    QSet<QString> seen;
    const auto appendFile =
        [&result, &seen](const QFileInfo& info, const bool requireAudio) {
        if (!info.isFile()
            || (requireAudio && !agplayer::qt::isSupportedAudioFile(info))) {
            return;
        }
        const QString identity = agplayer::qt::resourcePathIdentity(
            info.absoluteFilePath());
        const QString seenKey = agplayer::qt::resourcePathCaseSensitivity()
                == Qt::CaseInsensitive ? identity.toCaseFolded() : identity;
        if (identity.isEmpty() || seen.contains(seenKey)) {
            return;
        }
        seen.insert(seenKey);
        result.append(QUrl::fromLocalFile(identity));
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
