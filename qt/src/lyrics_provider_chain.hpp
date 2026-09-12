#pragma once

#include "lyrics_provider.hpp"

#include <QHash>

#include <functional>

class LyricsProviderChain final : public LyricsProvider {
    Q_OBJECT

public:
    using Clock = std::function<qint64()>;
    using SearchResultMatcher = std::function<bool(const Track&, const QList<Candidate>&)>;
    struct Route final {
        QString id;
        QString name;
        LyricsProvider* provider = nullptr;
    };

    explicit LyricsProviderChain(QList<Route> routes, QObject* parent = nullptr,
                                 Clock clock = {}, SearchResultMatcher searchResultMatcher = {});
    void requestExact(quint64 requestId, const Track& track) override;
    void requestSearch(quint64 requestId, const Track& track) override;
    void cancel(quint64 requestId) override;
    [[nodiscard]] QStringList routeIds() const;

signals:
    void routeFailed(quint64 requestId, const LyricsProvider::RouteAttempt& attempt);

private:
    struct RouteHealth final {
        int consecutiveTechnicalFailures = 0;
        qint64 blockedUntilMs = 0;
        bool probeInFlight = false;
    };
    struct Pending final {
        Track track;
        bool exact = true;
        int nextRouteIndex = 0;
        int activeRouteIndex = -1;
        quint64 activeInternalId = 0;
        QList<RouteAttempt> attempts;
        bool encounteredNoMatch = false;
        bool encounteredUnavailableRoute = false;
    };

    void request(quint64 requestId, const Track& track, bool exact);
    void advance(quint64 requestId);
    void dispatch(quint64 requestId, int routeIndex);
    void handleProviderFinished(quint64 internalId, const Result& result);
    void finish(quint64 requestId, Result result);
    void appendAttempt(Pending& pending, const Route& route, QString diagnostic,
                       int httpStatus = 0, qint64 retryAfterMs = 0, bool offline = false);
    [[nodiscard]] qint64 clockMs() const;

    QList<Route> routes_;
    Clock clock_;
    SearchResultMatcher searchResultMatcher_;
    QHash<int, RouteHealth> health_;
    QHash<quint64, Pending> pending_;
    QHash<quint64, quint64> internalToExternal_;
    quint64 nextInternalId_ = 1;
};
