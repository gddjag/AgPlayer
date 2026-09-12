#include "lyrics_provider_chain.hpp"

#include <QDateTime>

#include <algorithm>
#include <utility>

namespace {

constexpr qint64 kCircuitDurationMs = 10 * 60 * 1000;

} // namespace

LyricsProviderChain::LyricsProviderChain(QList<Route> routes, QObject* parent, Clock clock,
                                         SearchResultMatcher searchResultMatcher)
    : LyricsProvider(parent)
    , routes_(std::move(routes))
    , clock_(std::move(clock))
    , searchResultMatcher_(std::move(searchResultMatcher))
{
    if (!clock_) clock_ = [] { return QDateTime::currentMSecsSinceEpoch(); };
    for (const Route& route : routes_) {
        if (route.provider == nullptr) continue;
        connect(route.provider, &LyricsProvider::finished, this,
                [this](const quint64 internalId, const Result& result) {
            handleProviderFinished(internalId, result);
        });
    }
}

void LyricsProviderChain::requestExact(const quint64 requestId, const Track& track)
{
    request(requestId, track, true);
}

void LyricsProviderChain::requestSearch(const quint64 requestId, const Track& track)
{
    request(requestId, track, false);
}

void LyricsProviderChain::cancel(const quint64 requestId)
{
    const auto iterator = pending_.find(requestId);
    if (iterator == pending_.end()) return;

    const Pending pending = *iterator;
    pending_.erase(iterator);
    if (pending.activeInternalId == 0) return;

    internalToExternal_.remove(pending.activeInternalId);
    if (pending.activeRouteIndex >= 0 && pending.activeRouteIndex < routes_.size()) {
        RouteHealth& health = health_[pending.activeRouteIndex];
        health.probeInFlight = false;
        if (LyricsProvider* provider = routes_.at(pending.activeRouteIndex).provider) {
            provider->cancel(pending.activeInternalId);
        }
    }
}

QStringList LyricsProviderChain::routeIds() const
{
    QStringList result;
    result.reserve(routes_.size());
    for (const Route& route : routes_) result.append(route.id);
    return result;
}

void LyricsProviderChain::request(const quint64 requestId, const Track& track, const bool exact)
{
    cancel(requestId);
    pending_.insert(requestId, {track, exact});
    advance(requestId);
}

void LyricsProviderChain::advance(const quint64 requestId)
{
    while (pending_.contains(requestId)) {
        Pending& pending = pending_[requestId];
        if (pending.nextRouteIndex >= routes_.size()) {
            Result result = pending.encounteredUnavailableRoute && !pending.encounteredNoMatch
                ? Result::technicalError(0, false, QStringLiteral("all-routes-failed"))
                : Result::notFound();
            finish(requestId, std::move(result));
            return;
        }

        const int routeIndex = pending.nextRouteIndex++;
        const Route& route = routes_.at(routeIndex);
        if (route.provider == nullptr) {
            appendAttempt(pending, route, QStringLiteral("provider-unavailable"));
            pending.encounteredUnavailableRoute = true;
            emit routeFailed(requestId, pending.attempts.constLast());
            if (!pending_.contains(requestId)) return;
            continue;
        }

        RouteHealth& health = health_[routeIndex];
        const qint64 now = clockMs();
        if (health.blockedUntilMs > now) {
            appendAttempt(pending, route, QStringLiteral("circuit-open"), 0,
                          health.blockedUntilMs - now);
            pending.encounteredUnavailableRoute = true;
            continue;
        }
        if (health.blockedUntilMs > 0 && health.probeInFlight) {
            appendAttempt(pending, route, QStringLiteral("circuit-open"));
            pending.encounteredUnavailableRoute = true;
            continue;
        }
        if (health.blockedUntilMs > 0) health.probeInFlight = true;
        dispatch(requestId, routeIndex);
        return;
    }
}

void LyricsProviderChain::dispatch(const quint64 requestId, const int routeIndex)
{
    auto iterator = pending_.find(requestId);
    if (iterator == pending_.end()) return;
    Pending& pending = *iterator;
    quint64 internalId = nextInternalId_++;
    if (internalId == 0) internalId = nextInternalId_++;
    pending.activeRouteIndex = routeIndex;
    pending.activeInternalId = internalId;
    internalToExternal_.insert(internalId, requestId);

    LyricsProvider* const provider = routes_.at(routeIndex).provider;
    if (pending.exact) provider->requestExact(internalId, pending.track);
    else provider->requestSearch(internalId, pending.track);
}

void LyricsProviderChain::handleProviderFinished(const quint64 internalId, const Result& result)
{
    const auto external = internalToExternal_.find(internalId);
    if (external == internalToExternal_.end()) return;
    const quint64 requestId = *external;
    const auto pendingIterator = pending_.find(requestId);
    if (pendingIterator == pending_.end() || pendingIterator->activeInternalId != internalId) {
        internalToExternal_.remove(internalId);
        return;
    }

    Pending& pending = *pendingIterator;
    const int routeIndex = pending.activeRouteIndex;
    if (routeIndex < 0 || routeIndex >= routes_.size()) return;
    const Route& route = routes_.at(routeIndex);
    RouteHealth& health = health_[routeIndex];
    internalToExternal_.remove(internalId);
    pending.activeInternalId = 0;
    pending.activeRouteIndex = -1;
    health.probeInFlight = false;

    const auto appendProviderAttempts = [&pending, &result] {
        pending.attempts.append(result.attempts);
    };
    const auto resetHealth = [&health] {
        health.consecutiveTechnicalFailures = 0;
        health.blockedUntilMs = 0;
    };

    if (result.kind == Result::Found) {
        if (searchResultMatcher_
            && !searchResultMatcher_(pending.track, {result.candidate})) {
            resetHealth();
            pending.encounteredNoMatch = true;
            appendAttempt(pending, route, QStringLiteral("no-acceptable-match"));
            appendProviderAttempts();
            advance(requestId);
            return;
        }
        resetHealth();
        Result completed = result;
        finish(requestId, std::move(completed));
        return;
    }
    if (result.kind == Result::SearchResults) {
        if (result.candidates.isEmpty()) {
            resetHealth();
            pending.encounteredNoMatch = true;
            appendAttempt(pending, route, QStringLiteral("empty-search"));
            appendProviderAttempts();
            advance(requestId);
            return;
        }
        if (searchResultMatcher_ && !searchResultMatcher_(pending.track, result.candidates)) {
            resetHealth();
            pending.encounteredNoMatch = true;
            appendAttempt(pending, route, QStringLiteral("no-acceptable-match"));
            appendProviderAttempts();
            advance(requestId);
            return;
        }
        resetHealth();
        Result completed = result;
        finish(requestId, std::move(completed));
        return;
    }
    if (result.kind == Result::NotFound) {
        resetHealth();
        pending.encounteredNoMatch = true;
        appendAttempt(pending, route, QStringLiteral("not-found"));
        appendProviderAttempts();
        advance(requestId);
        return;
    }
    if (result.kind == Result::RateLimited) {
        appendAttempt(pending, route, QStringLiteral("rate-limited"), 429, result.retryAfterMs);
        const RouteAttempt failedAttempt = pending.attempts.constLast();
        appendProviderAttempts();
        pending.encounteredUnavailableRoute = true;
        health.blockedUntilMs = std::max(health.blockedUntilMs,
                                         clockMs() + std::max<qint64>(0, result.retryAfterMs));
        emit routeFailed(requestId, failedAttempt);
        if (pending_.contains(requestId)) advance(requestId);
        return;
    }

    appendAttempt(pending, route, result.diagnostic, result.httpStatus, result.retryAfterMs,
                  result.offline);
    const RouteAttempt failedAttempt = pending.attempts.constLast();
    appendProviderAttempts();
    pending.encounteredUnavailableRoute = true;
    ++health.consecutiveTechnicalFailures;
    if (health.consecutiveTechnicalFailures >= 3) health.blockedUntilMs = clockMs() + kCircuitDurationMs;
    emit routeFailed(requestId, failedAttempt);
    if (pending_.contains(requestId)) advance(requestId);
}

void LyricsProviderChain::finish(const quint64 requestId, Result result)
{
    const auto iterator = pending_.find(requestId);
    if (iterator == pending_.end()) return;
    result.attempts = iterator->attempts + result.attempts;
    pending_.erase(iterator);
    complete(requestId, result);
}

void LyricsProviderChain::appendAttempt(Pending& pending, const Route& route, QString diagnostic,
                                        const int httpStatus, const qint64 retryAfterMs,
                                        const bool offline)
{
    pending.attempts.append({route.id, route.name, std::move(diagnostic), httpStatus,
                             retryAfterMs, offline});
}

qint64 LyricsProviderChain::clockMs() const
{
    return clock_();
}
