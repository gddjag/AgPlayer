#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace agplayer::qt {
QUrl cacheEmbeddedCover(const QByteArray& bytes, const QString& mimeType);
}
