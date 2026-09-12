#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class LyricsProvider : public QObject {
    Q_OBJECT

public:
    struct Source final {
        QString providerId;
        QString providerName;
        QUrl sourceUrl;
        QString attribution;
        bool supportsSyncedLyrics = false;
    };
    struct RouteAttempt final {
        QString providerId;
        QString providerName;
        QString diagnostic;
        int httpStatus = 0;
        qint64 retryAfterMs = 0;
        bool offline = false;
    };
    struct Track final {
        QString title;
        QString artist;
        QString album{};
        qint64 durationMs = 0;
        bool lowPriority = false;
    };
    struct Candidate final {
        Source source;
        QString title;
        QString artist;
        QString album;
        qint64 durationSeconds = 0;
        QString syncedLyrics;
        QString plainLyrics;
        bool instrumental = false;
    };
    struct Result final {
        enum Kind { Found, NotFound, TechnicalError, RateLimited, SearchResults } kind = NotFound;
        Candidate candidate;
        QList<Candidate> candidates;
        QList<RouteAttempt> attempts;
        int httpStatus = 0;
        qint64 retryAfterMs = 0;
        bool offline = false;
        QString diagnostic;
        [[nodiscard]] static Result found(Candidate candidate);
        [[nodiscard]] static Result notFound();
        [[nodiscard]] static Result search(QList<Candidate> candidates);
        [[nodiscard]] static Result technicalError(int httpStatus = 0, bool offline = false,
                                                   QString diagnostic = {});
        [[nodiscard]] static Result rateLimited(qint64 retryAfterMs);
    };

    using QObject::QObject;
    ~LyricsProvider() override = default;
    virtual void requestExact(quint64 requestId, const Track& track) = 0;
    virtual void requestSearch(quint64 requestId, const Track& track) = 0;
    virtual void cancel(quint64 requestId) = 0;

    void complete(quint64 requestId, const Result& result) { emit finished(requestId, result); }

signals:
    void finished(quint64 requestId, const LyricsProvider::Result& result);
};

class LrclibProvider final : public LyricsProvider {
    Q_OBJECT

public:
    explicit LrclibProvider(QNetworkAccessManager* manager, QObject* parent = nullptr,
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

Q_DECLARE_METATYPE(LyricsProvider::Result)
