#include "worker_engine.hpp"

#include "separation_protocol.hpp"

#include <QPointer>

#include <algorithm>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace agplayer::separation {
namespace {

QJsonObject errorPayload(const QString& code, const QString& message,
                         QJsonObject diagnostics)
{
    diagnostics.insert(QStringLiteral("code"), code);
    diagnostics.insert(QStringLiteral("message"), message);
    return diagnostics;
}

} // namespace

WorkerEngine::WorkerEngine(std::shared_ptr<WorkerBackend> backend,
                           QObject* parent)
    : QObject(parent), backend_(std::move(backend))
{
    threadPool_.setMaxThreadCount(1);
    threadPool_.setExpiryTimeout(-1);
    heartbeatTimer_.setInterval(2000);
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this] {
        if (!activeJob_ || shuttingDown_) {
            heartbeatTimer_.stop();
            return;
        }
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))
            && memory.PrivateUsage > 12ULL * 1024 * 1024 * 1024) {
            const auto excessive = activeJob_;
            if (excessive->cancelled->cancel()) {
                activeJob_.reset(); heartbeatTimer_.stop(); ++generation_;
                sendError(excessive->requestId, QStringLiteral("resource_limit"),
                    QStringLiteral("分离进程超过 12 GB 内存保护上限，已取消任务")); return;
            }
        }
#endif
        // Session loading and a Demucs chunk can legitimately take longer
        // than the client's 30-second transport watchdog. Report liveness
        // without inventing progress; keep a separate bounded stage deadline.
        if (activeJob_->lastActivity.elapsed() >= 5 * 60 * 1000) {
            const auto expired = activeJob_;
            if (expired->cancelled->cancel()) {
                activeJob_.reset();
                heartbeatTimer_.stop();
                ++generation_;
                sendError(expired->requestId, QStringLiteral("stage_timeout"),
                          QStringLiteral("Model stage exceeded five minutes: %1")
                              .arg(expired->lastStage),
                          {{QStringLiteral("stage"), expired->lastStage}});
                return;
            }
        }
        emit messageReady(encodeProtocolMessage(
            ProtocolType::Progress, activeJob_->requestId,
            {{QStringLiteral("fraction"), activeJob_->lastProgress},
             {QStringLiteral("stage"), activeJob_->lastStage}}));
    });
}

WorkerEngine::~WorkerEngine()
{
    shuttingDown_ = true;
    if (activeJob_) activeJob_->cancelled->cancel();
    threadPool_.waitForDone();
}

void WorkerEngine::sendError(const QString& requestId, const QString& code,
                             const QString& message,
                             const QJsonObject& diagnostics)
{
    emit messageReady(encodeProtocolMessage(
        ProtocolType::Error,
        requestId.isEmpty() ? QStringLiteral("invalid-request") : requestId,
        errorPayload(code, message, diagnostics)));
}

void WorkerEngine::acceptLine(const QByteArray& line)
{
    const ProtocolParseResult parsed = parseProtocolMessage(line);
    if (!parsed.ok) {
        sendError(parsed.error.requestId, parsed.error.code, parsed.error.message);
        return;
    }
    const ProtocolMessage& message = parsed.message;
    if (message.type != ProtocolType::Cancel) {
        forgetCancelledRequest(message.requestId);
    }
    if (shuttingDown_ && message.type != ProtocolType::Shutdown) {
        sendError(message.requestId, QStringLiteral("worker_shutting_down"),
                  QStringLiteral("Worker is shutting down"));
        return;
    }

    switch (message.type) {
    case ProtocolType::Hello:
        emit messageReady(encodeProtocolMessage(
            ProtocolType::Hello, message.requestId,
            {{QStringLiteral("protocol"), kSeparationProtocolVersion},
             {QStringLiteral("worker"), QStringLiteral("AgSeparationWorker")},
             {QStringLiteral("boundedConcurrency"), 1}}));
        break;
    case ProtocolType::Probe:
        if (activeJob_) {
            sendError(message.requestId, activeJob_->lastStage == QStringLiteral("provider_probe")
                          ? QStringLiteral("worker_queue_full") : QStringLiteral("worker_busy"),
                      QStringLiteral("A separation request is already active"));
        } else if (pendingTasks_ >= 2) {
            sendError(message.requestId, QStringLiteral("worker_queue_full"),
                      QStringLiteral("The bounded worker queue is full"));
        } else {
            startProbe(message.requestId, message.payload);
        }
        break;
    case ProtocolType::Start:
        if (activeJob_) {
            sendError(message.requestId, QStringLiteral("worker_busy"),
                      QStringLiteral("A separation request is already active"));
        } else if (pendingTasks_ >= 2) {
            sendError(message.requestId, QStringLiteral("worker_queue_full"),
                      QStringLiteral("The bounded worker queue is full"));
        } else {
            startJob(message.requestId, message.payload);
        }
        break;
    case ProtocolType::Cancel: {
        bool accepted = cancelledRequests_.contains(message.requestId);
        if (activeJob_ && activeJob_->requestId == message.requestId) {
            accepted = activeJob_->cancelled->cancel();
            if (accepted) {
                rememberCancelledRequest(message.requestId);
                activeJob_.reset();
                heartbeatTimer_.stop();
                ++generation_;
            }
        }
        emit messageReady(encodeProtocolMessage(
            ProtocolType::Cancel, message.requestId,
            {{QStringLiteral("accepted"), accepted}}));
        break;
    }
    case ProtocolType::Shutdown:
        if (!shuttingDown_) {
            shuttingDown_ = true;
            shutdownRequestId_ = message.requestId;
            if (activeJob_) {
                activeJob_->cancelled->cancel();
                activeJob_.reset();
                heartbeatTimer_.stop();
                ++generation_;
            }
            emit messageReady(encodeProtocolMessage(
                ProtocolType::Shutdown, message.requestId,
                {{QStringLiteral("accepted"), true}}));
            if (pendingTasks_ == 0) emit shutdownReady();
        } else {
            emit messageReady(encodeProtocolMessage(
                ProtocolType::Shutdown, message.requestId,
                {{QStringLiteral("accepted"), true}}));
        }
        break;
    case ProtocolType::Progress:
    case ProtocolType::Result:
    case ProtocolType::Error:
        sendError(message.requestId, QStringLiteral("unsupported_direction"),
                  QStringLiteral("This message type is worker-to-client only"));
        break;
    }
}

void WorkerEngine::startProbe(const QString& requestId,
                              const QJsonObject& payload)
{
    auto context = std::make_shared<JobContext>();
    context->requestId = requestId; context->generation = ++generation_;
    context->cancelled = std::make_shared<CancellationToken>();
    context->lastStage = QStringLiteral("provider_probe"); context->lastActivity.start();
    activeJob_ = context; heartbeatTimer_.start();
    ++pendingTasks_;
    const QPointer<WorkerEngine> self(this);
    const std::shared_ptr<WorkerBackend> backend = backend_;
    threadPool_.start([self, backend, requestId, payload, context] {
        const BackendResult result = backend->probeCancellable(payload, *context->cancelled);
        if (self) {
            QMetaObject::invokeMethod(self, [self, requestId, generation = context->generation, result] {
                if (self) self->finishProbe(requestId, generation, result);
            });
        }
    });
}

void WorkerEngine::startJob(const QString& requestId,
                            const QJsonObject& payload)
{
    auto context = std::make_shared<JobContext>();
    context->requestId = requestId;
    context->generation = ++generation_;
    context->cancelled = std::make_shared<CancellationToken>();
    context->lastActivity.start();
    activeJob_ = context;
    heartbeatTimer_.start();
    forgetCancelledRequest(requestId);
    ++pendingTasks_;

    const QPointer<WorkerEngine> self(this);
    const std::shared_ptr<WorkerBackend> backend = backend_;
    const ProgressCallback progress = [self, requestId,
                                       generation = context->generation](
                                          double fraction,
                                          const QString& stage) {
        if (self) {
            QMetaObject::invokeMethod(self, [self, requestId, generation,
                                             fraction, stage] {
                if (self) self->deliverProgress(requestId, generation,
                                                fraction, stage);
            });
        }
    };
    threadPool_.start(
        [self, backend, context, payload, progress] {
            const BackendResult result = backend->separate(
                payload, *context->cancelled, progress);
            if (self) {
                QMetaObject::invokeMethod(
                    self, [self, requestId = context->requestId,
                           generation = context->generation, result] {
                        if (self) self->finishJob(requestId, generation, result);
                    });
            }
        });
}

void WorkerEngine::deliverProgress(const QString& requestId,
                                   quint64 generation, double fraction,
                                   const QString& stage)
{
    if (shuttingDown_ || !activeJob_
        || activeJob_->requestId != requestId
        || activeJob_->generation != generation) {
        return;
    }
    const double monotonicFraction = std::max(
        activeJob_->lastProgress, std::clamp(fraction, 0.0, 1.0));
    activeJob_->lastProgress = monotonicFraction;
    activeJob_->lastStage = stage;
    activeJob_->lastActivity.restart();
    emit messageReady(encodeProtocolMessage(
        ProtocolType::Progress, requestId,
        {{QStringLiteral("fraction"), monotonicFraction},
         {QStringLiteral("stage"), stage}}));
}

void WorkerEngine::finishProbe(const QString& requestId, quint64 generation,
                               const BackendResult& result)
{
    if (!shuttingDown_ && activeJob_ && activeJob_->requestId == requestId
        && activeJob_->generation == generation) {
        activeJob_.reset(); heartbeatTimer_.stop();
        if (result.ok) {
            emit messageReady(encodeProtocolMessage(ProtocolType::Probe,
                                                    requestId, result.payload));
        } else {
            sendError(requestId, result.code, result.message, result.payload);
        }
    }
    taskFinished();
}

void WorkerEngine::finishJob(const QString& requestId, quint64 generation,
                             const BackendResult& result)
{
    const bool current = !shuttingDown_ && activeJob_
        && activeJob_->requestId == requestId
        && activeJob_->generation == generation;
    if (current) {
        activeJob_.reset();
        heartbeatTimer_.stop();
        if (result.ok) {
            emit messageReady(encodeProtocolMessage(ProtocolType::Result,
                                                    requestId, result.payload));
        } else {
            sendError(requestId, result.code, result.message, result.payload);
        }
    }
    taskFinished();
}

void WorkerEngine::taskFinished()
{
    --pendingTasks_;
    if (shuttingDown_ && pendingTasks_ == 0) emit shutdownReady();
}

void WorkerEngine::rememberCancelledRequest(const QString& requestId)
{
    constexpr qsizetype kMaximumRememberedCancellations = 16;
    if (cancelledRequests_.contains(requestId)) return;
    cancelledRequests_.insert(requestId);
    cancelledRequestOrder_.enqueue(requestId);
    while (cancelledRequestOrder_.size() > kMaximumRememberedCancellations) {
        cancelledRequests_.remove(cancelledRequestOrder_.dequeue());
    }
}

void WorkerEngine::forgetCancelledRequest(const QString& requestId)
{
    cancelledRequests_.remove(requestId);
    cancelledRequestOrder_.removeAll(requestId);
}

} // namespace agplayer::separation
