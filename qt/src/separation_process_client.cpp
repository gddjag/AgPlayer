#include "separation_process_client.hpp"

#include "separation_protocol.hpp"

#include <QUuid>

#include <algorithm>
#include <utility>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
#endif

using namespace agplayer::separation;

#ifdef Q_OS_WIN
namespace {

QString windowsErrorMessage(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    const QString detail = length > 0
        ? QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed()
        : QString::number(error);
    if (buffer) LocalFree(buffer);
    return detail;
}

} // namespace

struct SeparationProcessClient::WindowsJob {
    ~WindowsJob()
    {
        releaseLaunchAttributes();
        if (handle) CloseHandle(handle);
    }

    bool initialize(QString* error)
    {
        handle = CreateJobObjectW(nullptr, nullptr);
        if (!handle) return fail(error, QStringLiteral("CreateJobObject"));

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(handle, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits))) {
            return fail(error, QStringLiteral("SetInformationJobObject"));
        }

        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) {
            return fail(error,
                        QStringLiteral("InitializeProcThreadAttributeList(size)"));
        }
        attributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, bytes));
        if (!attributeList) {
            if (error) *error = QStringLiteral("HeapAlloc: insufficient memory");
            return false;
        }
        if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &bytes)) {
            return fail(error, QStringLiteral("InitializeProcThreadAttributeList"));
        }
        attributeListInitialized = true;
        if (!UpdateProcThreadAttribute(attributeList, 0,
                                       PROC_THREAD_ATTRIBUTE_JOB_LIST, &handle,
                                       sizeof(handle), nullptr, nullptr)) {
            return fail(error, QStringLiteral("UpdateProcThreadAttribute(JOB_LIST)"));
        }
        return true;
    }

    void apply(QProcess::CreateProcessArguments* arguments)
    {
        startupInfo.StartupInfo = *arguments->startupInfo;
        startupInfo.StartupInfo.cb = sizeof(startupInfo);
        startupInfo.lpAttributeList = attributeList;
        arguments->startupInfo = &startupInfo.StartupInfo;
        arguments->flags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    void releaseLaunchAttributes()
    {
        if (!attributeList) return;
        if (attributeListInitialized)
            DeleteProcThreadAttributeList(attributeList);
        HeapFree(GetProcessHeap(), 0, attributeList);
        attributeList = nullptr;
        attributeListInitialized = false;
        startupInfo.lpAttributeList = nullptr;
    }

    void terminate()
    {
        if (!handle || terminationRequested) return;
        terminationRequested = true;
        TerminateJobObject(handle, ERROR_PROCESS_ABORTED);
    }

private:
    bool fail(QString* error, const QString& operation)
    {
        const DWORD code = GetLastError();
        if (error) {
            *error = QStringLiteral("%1: %2 (Windows error %3)")
                         .arg(operation, windowsErrorMessage(code))
                         .arg(code);
        }
        return false;
    }

    HANDLE handle = nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST attributeList = nullptr;
    STARTUPINFOEXW startupInfo{};
    bool attributeListInitialized = false;
    bool terminationRequested = false;
};
#endif

SeparationProcessClient::SeparationProcessClient(
    QString program, QStringList arguments, Deadlines deadlines, QObject* parent)
    : QObject(parent),
      program_(std::move(program)),
      arguments_(std::move(arguments)),
      deadlines_(deadlines),
      pendingType_(ProtocolType::Probe)
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
        if (process_.state() != QProcess::NotRunning) terminateProcess();
    });
    connect(&process_, &QProcess::started, this, [this] {
#ifdef Q_OS_UNIX
        processGroup_ = process_.processId();
#endif
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
    connect(&process_, &QProcess::stateChanged, this,
            [this](QProcess::ProcessState) {
        emit requestAvailabilityChanged();
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        process_.readAllStandardError();
    });
    connect(&process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !completing_) {
            const quint64 generation = processGeneration_;
            const QString message = tr("无法启动分离 Worker：%1")
                                        .arg(process_.errorString());
            QMetaObject::invokeMethod(this, [this, generation, message] {
                if (generation != processGeneration_ || failureEmitted_)
                    return;
                finishFailure(message);
            }, Qt::QueuedConnection);
        }
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        helloTimer_.stop();
        heartbeatTimer_.stop();
        exitTimer_.stop();
        buffer_.clear();
#ifdef Q_OS_UNIX
        // The group survives its leader. Clean descendants even if the worker
        // itself crashed or acknowledged shutdown before its FFmpeg exited.
        if (processGroup_ > 0) ::kill(-static_cast<pid_t>(processGroup_), SIGKILL);
        processGroup_ = 0;
#endif
#ifdef Q_OS_WIN
        windowsJob_.reset();
#endif
        if (!pendingFailure_.isEmpty()) {
            finishFailure(pendingFailure_);
        } else if (!timeoutFailure_.isEmpty()) {
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
        terminateProcess();
    }
#ifdef Q_OS_WIN
    windowsJob_.reset();
#endif
}

SeparationProcessClient::State SeparationProcessClient::state() const noexcept
{
    return state_;
}

bool SeparationProcessClient::isProcessRunning() const noexcept
{
    return process_.state() != QProcess::NotRunning;
}

bool SeparationProcessClient::canAcceptRequest() const noexcept
{
    return (state_ == Stopped || state_ == Error) && !isProcessRunning()
        && pendingFailure_.isEmpty();
}

QString SeparationProcessClient::activeRequestId() const
{
    return activeRequestId_;
}

bool SeparationProcessClient::startProbe(const QJsonObject& payload)
{
    return begin(ProtocolType::Probe, payload);
}

bool SeparationProcessClient::setWorker(QString program, QStringList arguments)
{
    if (!canAcceptRequest()) return false;
    program_ = std::move(program);
    arguments_ = std::move(arguments);
    return true;
}

bool SeparationProcessClient::startJob(const QJsonObject& payload)
{
    return begin(ProtocolType::Start, payload);
}

bool SeparationProcessClient::begin(ProtocolType type,
                                    const QJsonObject& payload)
{
    if (program_.isEmpty() || !canAcceptRequest()) {
        return false;
    }
    pendingType_ = type;
    ++processGeneration_;
    pendingPayload_ = payload;
    activeRequestId_ = QStringLiteral("request-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    timeoutFailure_.clear();
    userCancellation_ = false;
    completing_ = false;
    failureEmitted_ = false;
    shutdownSent_ = false;
    pendingFailure_.clear();
    buffer_.clear();
    process_.setProgram(program_);
    process_.setArguments(arguments_);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    setState(Starting);
#ifdef Q_OS_WIN
    windowsJob_ = std::make_unique<WindowsJob>();
    QString jobError;
    if (!windowsJob_->initialize(&jobError)) {
        windowsJob_.reset();
        finishFailure(tr("无法安全启动分离 Worker：%1").arg(jobError));
        return true;
    }
    process_.setCreateProcessArgumentsModifier([this](
            QProcess::CreateProcessArguments* arguments) {
        windowsJob_->apply(arguments);
    });
#endif
#ifdef Q_OS_UNIX
    processGroup_ = 0;
    process_.setChildProcessModifier([] { if (::setsid() < 0) ::_exit(127); });
#endif
    process_.start();
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier({});
    if (windowsJob_) windowsJob_->releaseLaunchAttributes();
#endif
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
    emit requestAvailabilityChanged();
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
            || message.type == ProtocolType::Error
            || message.type == ProtocolType::Probe)) {
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
    if (pendingFailure_.isEmpty()) {
        pendingFailure_ = message;
        helloTimer_.stop();
        heartbeatTimer_.stop();
        exitTimer_.stop();
        timeoutFailure_.clear();
        userCancellation_ = false;
        completing_ = false;
        setState(Error);
    }
    if (process_.state() != QProcess::NotRunning) {
        terminateProcess();
        return;
    }
#ifdef Q_OS_WIN
    windowsJob_.reset();
#endif
    failureEmitted_ = true;
    const QString failure = std::exchange(pendingFailure_, {});
    emit failed(failure, true);
    emit requestAvailabilityChanged();
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

void SeparationProcessClient::terminateProcess()
{
#ifdef Q_OS_UNIX
    const qint64 group = processGroup_ > 0 ? processGroup_ : process_.processId();
    if (group > 0) ::kill(-static_cast<pid_t>(group), SIGKILL);
#endif
#ifdef Q_OS_WIN
    if (windowsJob_) windowsJob_->terminate();
#endif
    process_.kill();
}
