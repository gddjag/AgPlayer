#include "cancellation_token.hpp"

#include <algorithm>
#include <utility>

namespace agplayer::separation {

CancellationToken::Subscription::Subscription(
    std::shared_ptr<CallbackEntry> entry)
    : entry_(std::move(entry))
{
}

CancellationToken::Subscription::~Subscription() { reset(); }

CancellationToken::Subscription::Subscription(Subscription&& other) noexcept
    : entry_(std::move(other.entry_))
{
}

CancellationToken::Subscription& CancellationToken::Subscription::operator=(
    Subscription&& other) noexcept
{
    if (this != &other) {
        reset();
        entry_ = std::move(other.entry_);
    }
    return *this;
}

void CancellationToken::Subscription::reset()
{
    if (!entry_) return;
    auto entry = std::move(entry_);
    const std::lock_guard lock(entry->mutex);
    entry->active = false;
    entry->callback = {};
}

bool CancellationToken::cancel()
{
    {
        const std::lock_guard lock(terminalMutex_);
        if (committed_) return false;
        if (cancelled_.exchange(true)) return true;
    }
    std::vector<std::shared_ptr<CallbackEntry>> callbacks;
    {
        const std::lock_guard lock(callbacksMutex_);
        callbacks_.erase(
            std::remove_if(callbacks_.begin(), callbacks_.end(),
                           [](const auto& weak) { return weak.expired(); }),
            callbacks_.end());
        callbacks.reserve(callbacks_.size());
        for (const auto& weak : callbacks_) {
            if (auto callback = weak.lock()) callbacks.push_back(std::move(callback));
        }
    }
    for (const auto& entry : callbacks) {
        const std::lock_guard lock(entry->mutex);
        if (entry->active && entry->callback) entry->callback();
    }
    return true;
}

bool CancellationToken::isCancelled() const { return cancelled_.load(); }

bool CancellationToken::tryCommit(
    const std::function<bool()>& publish) const
{
    const std::lock_guard lock(terminalMutex_);
    if (cancelled_.load() || committed_ || !publish()) return false;
    committed_ = true;
    return true;
}

const std::atomic_bool& CancellationToken::atomicFlag() const
{
    return cancelled_;
}

CancellationToken::Subscription CancellationToken::notifyOnCancel(
    std::function<void()> callback) const
{
    auto entry = std::make_shared<CallbackEntry>();
    entry->callback = std::move(callback);
    bool notifyImmediately = false;
    {
        const std::lock_guard lock(callbacksMutex_);
        callbacks_.erase(
            std::remove_if(callbacks_.begin(), callbacks_.end(),
                           [](const auto& weak) { return weak.expired(); }),
            callbacks_.end());
        if (cancelled_.load()) notifyImmediately = true;
        else callbacks_.push_back(entry);
    }
    if (notifyImmediately && entry->callback) entry->callback();
    return Subscription(std::move(entry));
}

} // namespace agplayer::separation
