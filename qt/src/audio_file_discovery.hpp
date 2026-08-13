#pragma once

#include <QList>
#include <QFuture>
#include <QUrl>

namespace agplayer::qt {

QList<QUrl> expandAudioUrls(
    const QList<QUrl>& urls,
    bool includeExplicitNonAudioFile = false);
QFuture<QList<QUrl>> expandAudioUrlsAsync(
    QList<QUrl> urls,
    bool includeExplicitNonAudioFile = false);
QUrl firstAudioUrl(const QUrl& url);

} // namespace agplayer::qt
