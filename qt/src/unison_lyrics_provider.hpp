#pragma once

#include "lyrics_provider.hpp"

#include <QHash>

class QNetworkAccessManager;
class QNetworkReply;

class UnisonLyricsProvider final : public LyricsProvider {
    Q_OBJECT

public:
    explicit UnisonLyricsProvider(QNetworkAccessManager* manager, QObject* parent = nullptr,
                                  int requestTimeoutMs = 15000);
    void requestExact(quint64 requestId, const Track& track) override;
    void requestSearch(quint64 requestId, const Track& track) override;
    void cancel(quint64 requestId) override;

private:
    void request(quint64 requestId, const Track& track, bool exact);
    void handleReply(QNetworkReply* reply, bool exact);

    QNetworkAccessManager* manager_ = nullptr;
    QHash<quint64, QNetworkReply*> replies_;
    int requestTimeoutMs_ = 15000;
};
