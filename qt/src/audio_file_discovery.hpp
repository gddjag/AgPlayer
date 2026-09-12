#pragma once

#include <QList>
#include <QFuture>
#include <QStringList>
#include <QUrl>

class QFileInfo;

namespace agplayer::qt {

QStringList supportedAudioExtensions();
bool isSupportedAudioExtension(const QString& extension);
bool isSupportedAudioFile(const QFileInfo& info);

QStringList supportedVideoExtensions();
bool isSupportedVideoExtension(const QString& extension);

QList<QUrl> expandAudioUrls(
    const QList<QUrl>& urls,
    bool includeExplicitNonAudioFile = false);
QFuture<QList<QUrl>> expandAudioUrlsAsync(
    QList<QUrl> urls,
    bool includeExplicitNonAudioFile = false);
QUrl firstAudioUrl(const QUrl& url);

} // namespace agplayer::qt
