#include "voice_clone_worker_client.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace agplayer::voice_clone {
namespace {

constexpr qsizetype kMaximumFrameBytes = 1024 * 1024;
constexpr int kMinimumHandshakeTimeoutMs = 5000;

bool isReparsePoint(const QFileInfo& info)
{
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return info.isSymLink();
#endif
}

bool hasReparseAncestor(const QString& path)
{
    QString current = QFileInfo(path).absoluteFilePath();
    while (!current.isEmpty()) {
        const QFileInfo info(current);
        if ((info.exists() || info.isSymLink()) && isReparsePoint(info)) return true;
        const QString parent = info.dir().absolutePath();
        if (parent == current) break;
        current = parent;
    }
    return false;
}

} // namespace

VoiceCloneWorkerClient::VoiceCloneWorkerClient(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<VoiceCloneWorkerMessage>();
    connect(&process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (shuttingDown_) return;
                failWorker(QStringLiteral("worker-crashed"),
                           QStringLiteral("Worker exited (%1, %2)")
                               .arg(exitCode)
                               .arg(status == QProcess::CrashExit ? QStringLiteral("crash")
                                                                 : QStringLiteral("exit")));
            });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!shuttingDown_) failWorker(QStringLiteral("worker-process-error"), process_.errorString());
    });
}

VoiceCloneWorkerClient::~VoiceCloneWorkerClient()
{
    shutdown();
}

bool VoiceCloneWorkerClient::start(const VoiceCloneAdapterManifest& manifest,
                                   const AdapterLauncherResolution& launcher,
                                   const QString& adapterPackRoot,
                                   const QString& modelRoot,
                                   const QString& outputRoot,
                                   int requestTimeoutMs)
{
    return start(manifest, launcher, adapterPackRoot, {}, modelRoot, outputRoot,
                 requestTimeoutMs);
}

bool VoiceCloneWorkerClient::start(const VoiceCloneAdapterManifest& manifest,
                                   const AdapterLauncherResolution& launcher,
                                   const QString& adapterPackRoot,
                                   const QString& runtimeRoot,
                                   const QString& modelRoot,
                                   const QString& outputRoot,
                                   int requestTimeoutMs)
{
    shutdown();
    shuttingDown_ = false;
    error_.clear();
    const QFileInfo launcherInfo(launcher.absolutePath);
    const QFileInfo modelInfo(modelRoot);
    const QFileInfo outputInfo(outputRoot);
    if (!launcher.isValid()
        || (launcher.launcher.kind != QStringLiteral("executable")
            && launcher.launcher.kind != QStringLiteral("pythonModule"))
        || !launcherInfo.exists() || !launcherInfo.isFile()
        || !modelInfo.exists() || !modelInfo.isDir()
        || !outputInfo.exists() || !outputInfo.isDir()
        || hasReparseAncestor(modelInfo.absoluteFilePath())
        || hasReparseAncestor(outputInfo.absoluteFilePath())
        || manifest.adapterId.isEmpty() || manifest.adapterVersion.isEmpty()
        || manifest.protocolVersion != 1 || requestTimeoutMs <= 0) {
        error_ = QStringLiteral("Worker launch configuration is invalid");
        return false;
    }
    manifest_ = manifest;
    launcher_ = launcher;
    adapterPackRoot_ = adapterPackRoot;
    runtimeRoot_.clear();
    modelRoot_ = QDir::fromNativeSeparators(modelInfo.canonicalFilePath());
    outputRoot_ = QDir::fromNativeSeparators(outputInfo.canonicalFilePath());
    requestTimeoutMs_ = requestTimeoutMs;
    if (modelRoot_.isEmpty() || outputRoot_.isEmpty()) {
        error_ = QStringLiteral("Worker roots could not be canonicalized");
        return false;
    }

    server_ = new QLocalServer(this);
    connect(server_, &QLocalServer::newConnection, this, &VoiceCloneWorkerClient::acceptConnection);
    const QString socketName = QStringLiteral("agplayer-voice-clone-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QLocalServer::removeServer(socketName);
    if (!server_->listen(socketName)) {
        error_ = server_->errorString();
        clearTransport();
        return false;
    }
    QString program = launcher.absolutePath;
    QStringList arguments;
    if (launcher.launcher.kind == QStringLiteral("pythonModule")) {
        if (!runtimeRoot.isEmpty()) {
            const QFileInfo rootInfo(runtimeRoot);
            const QString canonicalRoot = QDir::fromNativeSeparators(rootInfo.canonicalFilePath());
            const QFileInfo pythonInfo(QDir(canonicalRoot).filePath(QStringLiteral("python.exe")));
            if (!rootInfo.isDir() || canonicalRoot.isEmpty() || hasReparseAncestor(canonicalRoot)
                || !pythonInfo.isFile() || isReparsePoint(pythonInfo)
                || hasReparseAncestor(pythonInfo.absoluteFilePath())
                || QDir::fromNativeSeparators(pythonInfo.canonicalPath()) != canonicalRoot) {
                error_ = QStringLiteral("Verified external Runtime Pack Python is unavailable");
                clearTransport();
                return false;
            }
            runtimeRoot_ = canonicalRoot;
            program = pythonInfo.absoluteFilePath();
        } else {
            VoiceCloneAdapterManifest runtimeManifest = manifest;
            runtimeManifest.launchers = {AdapterLauncher{
                QStringLiteral("runtime-python"), QStringLiteral("executable"),
                QDir(manifest.runtime.root).filePath(QStringLiteral("python.exe")),
                manifest.runtime.shared}};
            const auto runtime = resolveAdapterLauncher(runtimeManifest,
                                                        QStringLiteral("runtime-python"),
                                                        adapterPackRoot);
            if (!runtime.isValid() || !QFileInfo::exists(runtime.absolutePath)) {
                error_ = QStringLiteral("Trusted Adapter runtime Python is unavailable");
                clearTransport();
                return false;
            }
            program = runtime.absolutePath;
        }
        arguments = {QStringLiteral("-I"), QStringLiteral("-s"), launcher.absolutePath};
    }
    arguments.append({QStringLiteral("--voice-clone-worker"),
                           QStringLiteral("--socket"), socketName,
                           QStringLiteral("--model-root"), modelRoot_,
                           QStringLiteral("--output-root"), outputRoot_,
                           QStringLiteral("--adapter-id"), manifest_.adapterId,
                           QStringLiteral("--adapter-version"), manifest_.adapterVersion,
                           QStringLiteral("--protocol-version"), QString::number(manifest_.protocolVersion)});
    process_.setProgram(program);
    process_.setArguments(arguments);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    handshakeTimer_ = new QTimer(this);
    handshakeTimer_->setSingleShot(true);
    connect(handshakeTimer_, &QTimer::timeout, this, [this] {
        failWorker(QStringLiteral("handshake-timeout"),
                   QStringLiteral("Worker did not complete the protocol handshake"));
    });
    handshakeTimer_->start(qMax(requestTimeoutMs_, kMinimumHandshakeTimeoutMs));
    process_.start();
    if (!process_.waitForStarted(5000)) {
        error_ = process_.errorString();
        clearTransport();
        return false;
    }
    return true;
}

bool VoiceCloneWorkerClient::restart()
{
    if (!launcher_.isValid()) return false;
    const auto manifest = manifest_;
    const auto launcher = launcher_;
    const QString modelRoot = modelRoot_;
    const QString outputRoot = outputRoot_;
    const QString adapterPackRoot = adapterPackRoot_;
    const QString runtimeRoot = runtimeRoot_;
    const int timeout = requestTimeoutMs_;
    return start(manifest, launcher, adapterPackRoot, runtimeRoot, modelRoot, outputRoot, timeout);
}

void VoiceCloneWorkerClient::shutdown()
{
    shuttingDown_ = true;
    if (ready_ && socket_ && socket_->state() == QLocalSocket::ConnectedState
        && process_.state() != QProcess::NotRunning) {
        sendRequest(WorkerOperation::Shutdown);
        process_.waitForFinished(500);
    }
    const bool wasReady = ready_;
    ready_ = false;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->timer) it->timer->deleteLater();
    }
    pending_.clear();
    retiredRequestIds_.clear();
    if (process_.state() != QProcess::NotRunning) {
        process_.terminate();
        if (!process_.waitForFinished(1000)) {
            process_.kill();
            process_.waitForFinished(1000);
        }
    }
    clearTransport();
    if (wasReady) emit readyChanged();
}

QString VoiceCloneWorkerClient::sendRequest(const WorkerOperation operation,
                                            const QJsonObject& payload,
                                            const QString& suppliedRequestId)
{
    if (!socket_ || socket_->state() != QLocalSocket::ConnectedState) return {};
    if (!ready_ && operation != WorkerOperation::Hello
        && operation != WorkerOperation::Capabilities) {
        error_ = QStringLiteral("Worker handshake is not complete");
        return {};
    }
    const QString requestId = suppliedRequestId.isEmpty() ? nextRequestId() : suppliedRequestId;
    if (pending_.contains(requestId)) return {};
    VoiceCloneWorkerMessage request;
    request.kind = WorkerMessageKind::Request;
    request.operation = operation;
    request.requestId = requestId;
    request.adapterId = manifest_.adapterId;
    request.adapterVersion = manifest_.adapterVersion;
    request.protocolVersion = manifest_.protocolVersion;
    request.payload = payload;
    const auto validation = decodeWorkerMessage(encodeWorkerMessage(request), outputRoot_);
    if (!validation.isValid()) {
        error_ = validation.error;
        return {};
    }
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, requestId] {
        if (!pending_.contains(requestId)) return;
        finishPending(requestId);
        emit requestFailed(requestId, QStringLiteral("timeout"), QStringLiteral("Worker request timed out"));
        failWorker(QStringLiteral("timeout"), QStringLiteral("Worker request timed out"));
    });
    pending_.insert(requestId, {operation, timer});
    timer->start(requestTimeoutMs_);
    if (socket_->write(encodeWorkerMessage(request) + '\n') < 0) {
        finishPending(requestId);
        return {};
    }
    socket_->flush();
    return requestId;
}

bool VoiceCloneWorkerClient::abandonRequest(const QString& requestId)
{
    if (!pending_.contains(requestId)) return false;
    finishPending(requestId);
    retiredRequestIds_.insert(requestId);
    return true;
}

bool VoiceCloneWorkerClient::isRunning() const { return process_.state() != QProcess::NotRunning; }
bool VoiceCloneWorkerClient::isReady() const { return ready_; }
bool VoiceCloneWorkerClient::hasPendingRequest(const QString& requestId) const { return pending_.contains(requestId); }
QString VoiceCloneWorkerClient::errorString() const { return error_; }

void VoiceCloneWorkerClient::acceptConnection()
{
    if (!server_ || socket_) return;
    socket_ = server_->nextPendingConnection();
    if (!socket_) return;
    connect(socket_, &QLocalSocket::readyRead, this, &VoiceCloneWorkerClient::readFrames);
    connect(socket_, &QLocalSocket::disconnected, this, [this] {
        if (!shuttingDown_ && process_.state() != QProcess::NotRunning)
            failWorker(QStringLiteral("socket-disconnected"), QStringLiteral("Worker socket disconnected"));
    });
    helloRequestId_ = sendRequest(WorkerOperation::Hello);
    if (helloRequestId_.isEmpty()) failWorker(QStringLiteral("handshake-failed"), error_);
}

void VoiceCloneWorkerClient::readFrames()
{
    while (socket_ && socket_->bytesAvailable() > 0) {
        const qsizetype capacity = kMaximumFrameBytes + 1 - inputBuffer_.size();
        if (capacity <= 0) {
            failWorker(QStringLiteral("frame-too-large"),
                       QStringLiteral("Worker IPC frame exceeded the 1 MiB limit"));
            return;
        }
        inputBuffer_ += socket_->read(qMin(socket_->bytesAvailable(), capacity));
        while (true) {
            const qsizetype newline = inputBuffer_.indexOf('\n');
            if (newline < 0) {
                if (inputBuffer_.size() > kMaximumFrameBytes)
                    failWorker(QStringLiteral("frame-too-large"),
                               QStringLiteral("Worker IPC frame exceeded the 1 MiB limit"));
                break;
            }
            if (newline > kMaximumFrameBytes) {
                failWorker(QStringLiteral("frame-too-large"),
                           QStringLiteral("Worker IPC frame exceeded the 1 MiB limit"));
                return;
            }
            const QByteArray frame = inputBuffer_.left(newline);
            inputBuffer_.remove(0, newline + 1);
            if (frame.isEmpty()) continue;
            const auto decoded = decodeWorkerMessage(frame, outputRoot_);
            if (!decoded.isValid()) {
                failWorker(QStringLiteral("protocol-error"), decoded.error);
                return;
            }
            handleMessage(decoded.message);
            if (!socket_) return;
        }
    }
}

void VoiceCloneWorkerClient::handleMessage(const VoiceCloneWorkerMessage& message)
{
    if (retiredRequestIds_.contains(message.requestId)) {
        if (message.kind != WorkerMessageKind::Progress)
            retiredRequestIds_.remove(message.requestId);
        return;
    }
    if (message.adapterId != manifest_.adapterId
        || message.adapterVersion != manifest_.adapterVersion
        || message.protocolVersion != manifest_.protocolVersion) {
        failWorker(QStringLiteral("identity-mismatch"),
                   QStringLiteral("Worker response identity does not match the trusted Adapter"));
        return;
    }
    if (!pending_.contains(message.requestId)) {
        failWorker(QStringLiteral("correlation-error"), QStringLiteral("Worker returned an unknown requestId"));
        return;
    }
    const WorkerOperation expected = pending_.value(message.requestId).operation;
    if (expected != message.operation) {
        failWorker(QStringLiteral("correlation-error"), QStringLiteral("Worker operation did not match request"));
        return;
    }
    if (message.kind == WorkerMessageKind::Progress) {
        emit progressReceived(message);
        return;
    }
    finishPending(message.requestId);
    if (message.kind == WorkerMessageKind::Error) {
        emit requestFailed(message.requestId, message.workerError.code, message.workerError.message);
        return;
    }
    if (message.kind != WorkerMessageKind::Response) {
        failWorker(QStringLiteral("protocol-error"), QStringLiteral("Unexpected worker message kind"));
        return;
    }
    if (message.requestId == helloRequestId_) {
        capabilitiesRequestId_ = sendRequest(WorkerOperation::Capabilities);
        if (capabilitiesRequestId_.isEmpty()) failWorker(QStringLiteral("handshake-failed"), error_);
    } else if (message.requestId == capabilitiesRequestId_) {
        emit responseReceived(message);
        if (handshakeTimer_) handshakeTimer_->stop();
        ready_ = true;
        emit readyChanged();
        return;
    }
    emit responseReceived(message);
}

void VoiceCloneWorkerClient::failWorker(const QString& code, const QString& message)
{
    if (shuttingDown_) return;
    error_ = message;
    const bool wasReady = ready_;
    ready_ = false;
    const auto ids = pending_.keys();
    for (const QString& id : ids) {
        finishPending(id);
        emit requestFailed(id, code, message);
    }
    shuttingDown_ = true;
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
    clearTransport();
    shuttingDown_ = false;
    if (wasReady) emit readyChanged();
    emit workerTerminated(message);
}

void VoiceCloneWorkerClient::finishPending(const QString& requestId)
{
    const auto pending = pending_.take(requestId);
    if (pending.timer) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
}

void VoiceCloneWorkerClient::clearTransport()
{
    inputBuffer_.clear();
    if (socket_) {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    if (handshakeTimer_) {
        handshakeTimer_->stop();
        handshakeTimer_->deleteLater();
        handshakeTimer_ = nullptr;
    }
}

QString VoiceCloneWorkerClient::nextRequestId() const
{
    return QStringLiteral("r-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

} // namespace agplayer::voice_clone
