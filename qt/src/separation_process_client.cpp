#include "separation_process_client.hpp"

#include "separation_protocol.hpp"

#include <QUuid>

#include <algorithm>

using namespace agplayer::separation;

SeparationProcessClient::SeparationProcessClient(
    QString program, QStringList arguments, Deadlines deadlines, QObject* parent)
    : QObject(parent),
      program_(std::move(program)),
      arguments_(std::move(arguments)),
      deadlines_(deadlines),
      pendingType_(ProtocolType::Probe),
      lastType_(ProtocolType::Probe)
{
    for (QTimer* timer : {&helloTimer_, &heartbeatTimer_, &exitTimer_}) {
        timer->setSingleShot(true);
    }
    connect(&helloTimer_, &QTimer::timeout, this, [this] {
        finishFailure(tr("分离 Worker 启动握手超时"));
    });
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this] {
        beginTimeoutFailure(tr("分离 Worker 响应超时"));
    });
    connect(&exitTimer_, &QTimer::timeout, this, [this] {
        if (process_.state() != QProcess::NotRunning) process_.kill();
    });
    connect(&process_, &QProcess::started, this, [this] {
        if (userCancellation_) {
            shutdownSent_ = true;
            send(ProtocolType::Shutdown, activeRequestId_);
            exitTimer_.start(std::max(1, deadlines_.cancelGraceMs));
            return;
        }
        helloRequestId_ = QStringLiteral("hello-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        send(ProtocolType::Hello, helloRequestId_);
        helloTimer_.start(std::max(1, deadlines_.helloMs));
    });
    connect(&process_, &QProcess::readyReadStandardOutput,
            this, &SeparationProcessClient::readStandardOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        process_.readAllStandardError();
    });
    connect(&process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !completing_) {
            finishFailure(tr("无法启动分离 Worker：%1").arg(process_.errorString()));
        }
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        helloTimer_.stop();
        heartbeatTimer_.stop();
        exitTimer_.stop();
        buffer_.clear();
        if (!timeoutFailure_.isEmpty()) {
            const QString message = std::exchange(timeoutFailure_, {});
            finishFailure(message);
        } else if (userCancellation_) {
            finishCancellation();
        } else if (completing_) {
            completing_ = false;
            setState(Stopped);
        } else if (state_ != Stopped && state_ != Error) {
            finishFailure(tr("分离 Worker 意外退出"));
        }
    });
}

SeparationProcessClient::~SeparationProcessClient()
{
    process_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}

SeparationProcessClient::State SeparationProcessClient::state() const noexcept
{
    return state_;
}

bool SeparationProcessClient::isProcessRunning() const noexcept
{
    return process_.state() != QProcess::NotRunning;
}

QString SeparationProcessClient::activeRequestId() const
{
    return activeRequestId_;
}

bool SeparationProcessClient::startProbe(const QJsonObject& payload)
{
    return begin(ProtocolType::Probe, payload, true);
}

bool SeparationProcessClient::startJob(const QJsonObject& payload)
{
    return begin(ProtocolType::Start, payload, true);
}

bool SeparationProcessClient::retryLast()
{
    return hasLastRequest_ && begin(lastType_, lastPayload_, false);
}

bool SeparationProcessClient::begin(ProtocolType type,
                                    const QJsonObject& payload, bool remember)
{
    if (program_.isEmpty() || (state_ != Stopped && state_ != Error)
        || process_.state() != QProcess::NotRunning) {
        return false;
    }
    pendingType_ = type;
    pendingPayload_ = payload;
    if (remember) {
        lastType_ = type;
        lastPayload_ = payload;
        hasLastRequest_ = true;
    }
    activeRequestId_ = QStringLiteral("request-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    timeoutFailure_.clear();
    userCancellation_ = false;
    completing_ = false;
    failureEmitted_ = false;
    shutdownSent_ = false;
    buffer_.clear();
    process_.setProgram(program_);
    process_.setArguments(arguments_);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    setState(Starting);
    process_.start();
    return true;
}

void SeparationProcessClient::cancel()
{
    if (state_ == Cancelling || (state_ != Starting && state_ != Ready
                                 && state_ != Busy)) return;
    const State previous = state_;
    userCancellation_ = true;
    setState(Cancelling);
    helloTimer_.stop();
    heartbeatTimer_.stop();
    if (process_.state() == QProcess::Running) {
        if (previous == Busy) {
            send(ProtocolType::Cancel, activeRequestId_);
        } else {
            shutdownSent_ = true;
            send(ProtocolType::Shutdown, activeRequestId_);
        }
    }
    exitTimer_.start(std::max(1, deadlines_.cancelGraceMs));
}

void SeparationProcessClient::setState(State state)
{
    if (state_ == state) return;
    state_ = state;
    emit stateChanged();
}

void SeparationProcessClient::send(ProtocolType type, const QString& requestId,
                                   const QJsonObject& payload)
{
    if (process_.state() == QProcess::Running) {
        process_.write(encodeProtocolMessage(type, requestId, payload));
    }
}

void SeparationProcessClient::readStandardOutput()
{
    buffer_.append(process_.readAllStandardOutput());
    if (buffer_.size() > kMaximumProtocolLineBytes
        && !buffer_.contains('\n')) {
        finishFailure(tr("分离 Worker 消息超过大小限制"));
        return;
    }
    qsizetype newline = -1;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        const QByteArray line = buffer_.left(newline);
        buffer_.remove(0, newline + 1);
        if (line.size() > kMaximumProtocolLineBytes) {
            finishFailure(tr("分离 Worker 消息超过大小限制"));
            return;
        }
        handleLine(line);
        if (state_ == Error) return;
    }
    if (buffer_.size() > kMaximumProtocolLineBytes) {
        finishFailure(tr("分离 Worker 消息超过大小限制"));
    }
}

void SeparationProcessClient::handleLine(const QByteArray& line)
{
    const ProtocolParseResult parsed = parseProtocolMessage(line);
    if (!parsed.ok) {
        finishFailure(tr("分离 Worker 协议错误：%1").arg(parsed.error.message));
        return;
    }
    const ProtocolMessage& message = parsed.message;
    if (state_ == Starting) {
        if (message.requestId != helloRequestId_) {
            emit staleMessageIgnored(message.requestId);
            return;
        }
        if (message.type != ProtocolType::Hello
            || message.payload.value(QStringLiteral("protocol")).toInt(-1)
                   != kSeparationProtocolVersion) {
            finishFailure(tr("分离 Worker 握手无效"));
            return;
        }
        helloTimer_.stop();
        setState(Ready);
        send(pendingType_, activeRequestId_, pendingPayload_);
        setState(Busy);
        heartbeatTimer_.start(std::max(1, deadlines_.heartbeatMs));
        return;
    }
    if (message.requestId != activeRequestId_) {
        emit staleMessageIgnored(message.requestId);
        return;
    }
    if (state_ == Cancelling
        && (message.type == ProtocolType::Progress
            || message.type == ProtocolType::Result
            || message.type == ProtocolType::Error)) {
        return;
    }
    if (message.type == ProtocolType::Progress && state_ == Busy
        && pendingType_ == ProtocolType::Start) {
        heartbeatTimer_.start(std::max(1, deadlines_.heartbeatMs));
        emit progressReceived(
            std::clamp(message.payload.value(QStringLiteral("fraction")).toDouble(),
                       0.0, 1.0),
            message.payload.value(QStringLiteral("stage")).toString());
    } else if (message.type == ProtocolType::Probe && state_ == Busy
               && pendingType_ == ProtocolType::Probe) {
        heartbeatTimer_.stop();
        emit probeReceived(message.payload);
        requestShutdown();
    } else if (message.type == ProtocolType::Result && state_ == Busy
               && pendingType_ == ProtocolType::Start) {
        heartbeatTimer_.stop();
        emit resultReceived(message.payload);
        requestShutdown();
    } else if (message.type == ProtocolType::Error && state_ == Busy) {
        const QString detail = message.payload.value(QStringLiteral("message")).toString();
        finishFailure(detail.isEmpty() ? tr("分离 Worker 报告错误") : detail);
    } else if (message.type == ProtocolType::Cancel && state_ == Cancelling) {
        shutdownSent_ = true;
        send(ProtocolType::Shutdown, activeRequestId_);
    } else if (message.type != ProtocolType::Shutdown || !shutdownSent_) {
        finishFailure(tr("分离 Worker 返回了方向无效的消息"));
    }
}

void SeparationProcessClient::beginTimeoutFailure(const QString& message)
{
    if (state_ != Busy) return;
    timeoutFailure_ = message;
    setState(Cancelling);
    send(ProtocolType::Cancel, activeRequestId_);
    exitTimer_.start(std::max(1, deadlines_.cancelGraceMs));
}

void SeparationProcessClient::finishFailure(const QString& message)
{
    if (failureEmitted_) return;
    failureEmitted_ = true;
    helloTimer_.stop();
    heartbeatTimer_.stop();
    exitTimer_.stop();
    timeoutFailure_.clear();
    userCancellation_ = false;
    completing_ = false;
    setState(Error);
    if (process_.state() != QProcess::NotRunning) process_.kill();
    emit failed(message, true);
}

void SeparationProcessClient::finishCancellation()
{
    if (!userCancellation_) return;
    userCancellation_ = false;
    completing_ = false;
    setState(Stopped);
    emit cancelled();
}

void SeparationProcessClient::requestShutdown()
{
    completing_ = true;
    shutdownSent_ = true;
    setState(Ready);
    send(ProtocolType::Shutdown, activeRequestId_);
    exitTimer_.start(std::max(1, deadlines_.cancelGraceMs));
}
