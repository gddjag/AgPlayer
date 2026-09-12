#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace agplayer::separation {

class CancellationToken final {
private:
    struct CallbackEntry;

public:
    class Subscription final {
    public:
        Subscription() = default;
        ~Subscription();

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;

    private:
        friend class CancellationToken;
        explicit Subscription(std::shared_ptr<CallbackEntry> entry);
        void reset();

        std::shared_ptr<CallbackEntry> entry_;
    };

    CancellationToken() = default;
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    bool cancel();
    [[nodiscard]] bool isCancelled() const;
    [[nodiscard]] bool tryCommit(const std::function<bool()>& publish) const;
    [[nodiscard]] const std::atomic_bool& atomicFlag() const;
    [[nodiscard]] Subscription notifyOnCancel(std::function<void()> callback) const;

private:
    struct CallbackEntry {
        std::mutex mutex;
        bool active = true;
        std::function<void()> callback;
    };

    std::atomic_bool cancelled_{false};
    mutable std::mutex terminalMutex_;
    mutable bool committed_ = false;
    mutable std::mutex callbacksMutex_;
    mutable std::vector<std::weak_ptr<CallbackEntry>> callbacks_;
};

} // namespace agplayer::separation
