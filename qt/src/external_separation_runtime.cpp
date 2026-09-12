#include "external_separation_runtime.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QUuid>
#include <QScopeGuard>
#include <QDateTime>
#include <QStandardPaths>
#include <utility>
#include <QtConcurrent/QtConcurrentRun>
#include "vocal_separation_path_safety.hpp"
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

static void initializePythonResources() { Q_INIT_RESOURCE(agplayer_separation_python); }
static VocalDownloadFile uvArchive(bool mirror = false) {
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_ARM_64
    const QString name = QStringLiteral("uv-aarch64-apple-darwin.tar.gz");
    constexpr qint64 bytes = 18'194'566;
    const QString hash = QStringLiteral("3f61099e261e449527141dbf125629fab33ad696468c8c90cebbac40185a306c");
#else
    const QString name = QStringLiteral("uv-x86_64-apple-darwin.tar.gz");
    constexpr qint64 bytes = 19'569'884;
    const QString hash = QStringLiteral("76638fdcfa91357858771551a1c88de1f7c3b270b33ab1866f8a0618d9e442d8");
#endif
    const QString official = QStringLiteral("https://github.com/astral-sh/uv/releases/download/0.8.22/") + name;
    return {name, QUrl(mirror ? QStringLiteral("https://ghfast.top/") + official : official), bytes, hash};
#else
    const QString official = QStringLiteral("https://github.com/astral-sh/uv/releases/download/0.8.22/uv-x86_64-pc-windows-msvc.zip");
    // The relay changes transport only; both routes must match the same pinned
    // official release size and SHA-256 before any archive is extracted.
    return {"uv.zip", QUrl(mirror ? QStringLiteral("https://ghfast.top/") + official : official),
            20716936, "5049375aa2a5162f132b2c1cb992e25d42d47d934cab8c174dbe6f60973dcc12"};
#endif
}

static QString pythonVersion() {
#ifdef Q_OS_MACOS
    return QStringLiteral("3.10.18"); // diffq's official Mac wheels include cp310.
#else
    return QStringLiteral("3.11.13");
#endif
}

static const QByteArray& verifiedVrMarker()
{
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_ARM_64
    static const QByteArray marker = QByteArrayLiteral(
        "audio-separator=0.30.2\npython=3.10.18\ntorch=2.5.1\nplatform=macos-arm64\n"
        "verification=external-separation-worker-v2\n");
#else
    static const QByteArray marker = QByteArrayLiteral(
        "audio-separator=0.24.1\npython=3.10.18\ntorch=2.2.2\nplatform=macos-x86_64\n"
        "verification=external-separation-worker-v2\n");
#endif
#else
    static const QByteArray marker = QByteArrayLiteral(
        "audio-separator=0.30.2\nverification=external-separation-worker-v1\n");
#endif
    return marker;
}

ExternalSeparationRuntime::ExternalSeparationRuntime(QString root,
    QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), root_(std::move(root)), downloader_(network),
      inactivity_(this), ioPoll_(this), stopDeadline_(this)
{
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_ARM_64
    root_ = QDir(root_).filePath(QStringLiteral("macos-arm64"));
#else
    root_ = QDir(root_).filePath(QStringLiteral("macos-x86_64"));
#endif
#endif
    initializePythonResources();
    QFile bundled(":/separation/external_separation_worker.py");
    if (bundled.open(QIODevice::ReadOnly)) bundledWorker_ = bundled.readAll();
    // Updating the application updates its bridge, not the optional Python
    // installation. Reuse a verified environment without any network/process.
    if (pythonPathIsSafe())
        synchronizeWorker();
    connect(&downloader_, &VocalSeparationDownloader::progressChanged, this,
            [this](qint64 received, qint64 total) {
        emit progress(total > 0 ? 0.1 * double(received) / double(total) : 0,
                      tr("下载配置器 %1 / %2 MB").arg(received / 1048576).arg(total / 1048576));
    });
    connect(&downloader_, &VocalSeparationDownloader::finished, this,
            [this](const VocalInstallResult& result) {
        if (!busy_) return;
        if (!result.ok) {
            appendLog(QStringLiteral("archive source=%1 error=%2")
                          .arg(archiveMirror_ ? "backup" : "official", result.error));
            if (!archiveMirror_) {
                archiveMirror_ = true;
                emit progress(0, tr("官方配置器线路失败，切换国内备用线路（仍验证官方 SHA-256）"));
                downloader_.start(uvArchive(true), QDir(root_).filePath("uv.zip"));
                return;
            }
            fail(tr("配置器官方及国内备用线路均失败：%1").arg(result.error));
            return;
        }
        step_ = 1;
        if (!paused_) advance();
    });
    connect(&cacheVerification_, &QFutureWatcher<bool>::finished, this, [this] {
        if (!busy_) { installationLock_.reset(); return; }
        if (cacheVerification_.result()) {
            step_ = 1;
            if (!paused_) advance();
            return;
        }
        const QString archive = QDir(root_).filePath("uv.zip");
        if (QFileInfo::exists(archive)
            && (!vocal_separation_paths::safeExistingFileWithin(archive, root_) || !QFile::remove(archive))) {
            fail(tr("拒绝替换不安全的 Python 配置器缓存")); return;
        }
        step_ = 0;
        if (!paused_) downloader_.start(uvArchive(), archive);
    });
    // uv's HTTP timeout covers reads, not a stalled resolver/unpacker. Keep a
    // separate no-progress deadline, including quiet disk I/O during wheels.
    inactivity_.setObjectName("pythonInstallerInactivity");
    inactivity_.setSingleShot(true);
    inactivity_.setInterval(120000);
    connect(&inactivity_, &QTimer::timeout, this, [this] {
        if (!busy_ || paused_ || stopping_) return;
        stopError_ = tr("安装阶段长时间无进展（inactivity timeout）");
        appendLog(stopError_);
        stopInstaller();
    });
    ioPoll_.setInterval(2000);
    connect(&ioPoll_, &QTimer::timeout, this, [this] {
#ifdef Q_OS_WIN
        const auto pid = process_.processId();
        if (!pid || stopping_ || paused_) return;
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
        if (!handle) return;
        IO_COUNTERS counters{};
        const bool ok = GetProcessIoCounters(handle, &counters);
        CloseHandle(handle);
        const quint64 bytes = counters.ReadTransferCount + counters.WriteTransferCount;
        if (ok && bytes != processIoBytes_) {
            processIoBytes_ = bytes;
            inactivity_.start();
        }
#endif
    });
    stopDeadline_.setSingleShot(true);
    stopDeadline_.setInterval(1500);
    connect(&stopDeadline_, &QTimer::timeout, this, [this] {
        terminateTree_.kill();
        process_.kill();
        finishStoppedInstaller();
    });
    connect(&terminateTree_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this] { process_.kill(); finishStoppedInstaller(); });
    connect(&terminateTree_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process_.kill();
            QTimer::singleShot(0, this, &ExternalSeparationRuntime::finishStoppedInstaller);
        }
    });
    connect(&process_, &QProcess::started, this, [this] {
        if (stopping_) {
            stopping_ = false;
            stopInstaller();
        }
    });
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = process_.readAllStandardOutput();
        output_.append(bytes);
        if (!bytes.isEmpty()) {
            appendLog(QString::fromUtf8(bytes));
            if (busy_ && !paused_ && !stopping_) inactivity_.start();
        }
        if (output_.size() > 16384) output_ = output_.right(16384);
        const QString text = QString::fromUtf8(output_).trimmed();
        const QString last = text.section(QLatin1Char('\n'), -1).left(160);
        if (!last.isEmpty()) emit progress(-1, last); // Unknown dependency total: don't invent percentages.
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        inactivity_.stop(); ioPoll_.stop();
        // FailedToStart may be delivered while QProcess is still unwinding.
        const auto generation = generation_;
        const auto diagnostic = process_.errorString();
        QTimer::singleShot(0, this, [this, generation, diagnostic] {
            if (generation != generation_) return;
            if (stopping_) finishStoppedInstaller();
            else if (busy_ && !paused_) {
                stageFailed(tr("无法启动配置程序：%1").arg(diagnostic));
            }
        });
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        inactivity_.stop(); ioPoll_.stop();
        if (stopping_) { finishStoppedInstaller(); return; }
        if (!busy_ || paused_) return;
        if (code != 0 || status != QProcess::NormalExit) {
            stageFailed(QStringLiteral("exit=%1 %2").arg(code).arg(QString::fromUtf8(output_).right(1800)));
            return;
        }
        mirror_ = false;
        ++step_;
        queueAdvance();
    });
}

ExternalSeparationRuntime::~ExternalSeparationRuntime()
{
    busy_ = false;
    stopInstaller();
    if (terminateTree_.state() != QProcess::NotRunning) {
        if (!terminateTree_.waitForFinished(1500)) terminateTree_.kill();
        terminateTree_.waitForFinished(500);
    }
    if (process_.state() != QProcess::NotRunning) {
        process_.kill(); process_.waitForFinished(1500);
    }
    cacheVerification_.waitForFinished();
}

QString ExternalSeparationRuntime::python() const {
#ifdef Q_OS_MACOS
    return QDir(root_).filePath("env/bin/python");
#else
    return QDir(root_).filePath("env/Scripts/python.exe");
#endif
}
bool ExternalSeparationRuntime::pythonPathIsSafe() const {
    using namespace vocal_separation_paths;
    if (safeExistingFileWithin(python(), root_)) return true;
#ifdef Q_OS_MACOS
    // uv venvs legitimately symlink the interpreter to its managed Python.
    // Permit only this final link, whose resolved regular file stays in root.
    const QFileInfo executable(python());
    const QString target = executable.canonicalFilePath();
    return executable.isSymbolicLink()
        && safeExistingPathWithin(executable.absolutePath(), root_, SafePathKind::Directory)
        && safeExistingFileWithin(target, QDir(root_).filePath("python"));
#else
    return false;
#endif
}
QString ExternalSeparationRuntime::workerScript() const { return QDir(root_).filePath("external_separation_worker.py"); }
bool ExternalSeparationRuntime::ready() const
{
    return vocal_separation_paths::safeExistingDirectory(root_)
        && pythonPathIsSafe()
        && markerMatchesVerificationContract()
        && workerMatchesBundle();
}

bool ExternalSeparationRuntime::markerMatchesVerificationContract() const
{
    const auto& expected = verifiedVrMarker();
    QFile marker(QDir(root_).filePath("verified-vr-1"));
    return vocal_separation_paths::openRegularFileForReadWithin(&marker, root_)
        && marker.read(expected.size() + 1) == expected;
}

bool ExternalSeparationRuntime::workerMatchesBundle() const
{
    if (bundledWorker_.isEmpty()) return false;
    QFile worker(workerScript());
    return vocal_separation_paths::openRegularFileForReadWithin(&worker, root_)
        && worker.read(bundledWorker_.size() + 1) == bundledWorker_;
}

bool ExternalSeparationRuntime::synchronizeWorker()
{
    using namespace vocal_separation_paths;
    const auto safeDestination = [this] {
        const auto kind = safePathKind(workerScript());
        return safeExistingDirectory(root_)
            && (kind == SafePathKind::Missing
                || safeExistingFileWithin(workerScript(), root_));
    };
    if (bundledWorker_.isEmpty() || !safeDestination()) return false;
    if (workerMatchesBundle()) return true;
    QSaveFile target(workerScript());
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly)
        || target.write(bundledWorker_) != bundledWorker_.size()
        || !safeDestination() || !target.commit()) return false;
    return workerMatchesBundle();
}

bool ExternalSeparationRuntime::clearVerificationMarker()
{
    using namespace vocal_separation_paths;
    const QString marker = QDir(root_).filePath("verified-vr-1");
    const auto kind = safePathKind(marker);
    return kind == SafePathKind::Missing
        || (safeExistingFileWithin(marker, root_) && QFile::remove(marker));
}

void ExternalSeparationRuntime::verifyCachedConfigurator()
{
    const QString archive = QDir(root_).filePath("uv.zip");
    step_ = -1;
    cacheVerification_.setFuture(QtConcurrent::run([archive] {
        return VocalSeparationInstaller::isVerifiedFile(uvArchive(), archive);
    }));
}

bool ExternalSeparationRuntime::start()
{
    if (busy_ || stopping_ || process_.state() != QProcess::NotRunning || cacheVerification_.isRunning()) return false;
    if (!QDir().mkpath(root_)) return false;
    if (!vocal_separation_paths::safeExistingDirectory(root_)) return false;
    installationLock_ = std::make_unique<QLockFile>(root_ + QStringLiteral(".install.lock"));
    if (!installationLock_->tryLock()) { installationLock_.reset(); return false; }
    const auto unlockFailedStart = qScopeGuard([this] {
        if (!busy_) installationLock_.reset();
    });
    if (!synchronizeWorker()) return false;
    const bool verifyExistingEnvironment = pythonPathIsSafe();
    if (!verifyExistingEnvironment && !clearVerificationMarker()) return false;
    busy_ = true;
    paused_ = false;
    step_ = 0;
    mirror_ = false;
    archiveMirror_ = false;
    verifyingExistingEnvironment_ = verifyExistingEnvironment;
    repairAttempted_ = false;
    resumeRequested_ = false;
    stopError_.clear();
    appendLog(QStringLiteral("begin optional Python configuration"));
    emit changed();
    if (verifyingExistingEnvironment_) {
        step_ = 4;
        emit progress(-1, tr("验证已有 Python / PyTorch / FFmpeg 环境"));
        queueAdvance();
        return true;
    }
    emit progress(0, tr("外置 Python / PyTorch，约 450 MB 下载，约 1.5 GB 磁盘；不修改系统环境"));
    verifyCachedConfigurator();
    return true;
}

void ExternalSeparationRuntime::launch(const QString& program, const QStringList& arguments)
{
    if (!busy_ || paused_ || stopping_) return;
    ++generation_;
    output_.clear();
    processIoBytes_ = 0;
    appendLog(QStringLiteral("launch program=%1").arg(QFileInfo(program).fileName()));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("UV_PYTHON_INSTALL_DIR", QDir(root_).filePath("python"));
    environment.insert("UV_CACHE_DIR", QDir(root_).filePath("cache"));
    environment.insert("UV_NO_CONFIG", "1");
    environment.insert("UV_HTTP_TIMEOUT", "15");
    environment.insert("UV_HTTP_RETRIES", "1");
    environment.insert("UV_PYTHON_INSTALL_REGISTRY", "false");
    // uv's pinned binary contains official Python release URLs and checksums.
    // Never replace its download metadata with data supplied by the relay.
    environment.remove("UV_PYTHON_DOWNLOADS_JSON_URL");
    environment.remove("UV_PYTHON_INSTALL_MIRROR");
    if (step_ == 2 && mirror_)
        environment.insert("UV_PYTHON_INSTALL_MIRROR",
            "https://ghfast.top/https://github.com/astral-sh/python-build-standalone/releases/download");
    environment.insert("RUST_LOG", "error");
    environment.insert("AGPLAYER_PYTHON_ROOT", root_);
    process_.setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
    // Give uv, Python and their children a dedicated process group so cancel
    // never leaves pip/build/download subprocesses behind.
    process_.setChildProcessModifier([] { if (::setsid() < 0) ::_exit(127); });
#endif
    process_.start(program, arguments);
    process_.closeWriteChannel();
    inactivity_.start();
    ioPoll_.start();
}

void ExternalSeparationRuntime::advance()
{
    if (!busy_ || paused_ || stopping_ || process_.state() != QProcess::NotRunning) return;
#ifdef Q_OS_MACOS
    const QString uv = QDir(root_).filePath("uv");
#else
    const QString uv = QDir(root_).filePath("uv.exe");
#endif
    if (step_ == 1) {
        emit progress(-1, tr("解压已校验配置器"));
#ifdef Q_OS_MACOS
        if (vocal_separation_paths::safePathKind(uv) != vocal_separation_paths::SafePathKind::Missing
            && !vocal_separation_paths::safeExistingFileWithin(uv, root_)) {
            fail(tr("拒绝替换不安全的配置器路径")); return;
        }
        const QString member = uvArchive().fileName.chopped(7) + QStringLiteral("/uv");
        launch(QStringLiteral("/usr/bin/tar"), {QStringLiteral("-xzf"), QDir(root_).filePath("uv.zip"),
            QStringLiteral("--strip-components=1"), QStringLiteral("-C"), root_, member});
#else
        launch("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
            "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "
            "$z=[IO.Compression.ZipFile]::OpenRead([IO.Path]::Combine($env:AGPLAYER_PYTHON_ROOT,'uv.zip')); "
            "try {$e=$z.GetEntry('uv.exe'); if(!$e){throw 'Missing uv.exe'}; "
            "[IO.Compression.ZipFileExtensions]::ExtractToFile($e,[IO.Path]::Combine($env:AGPLAYER_PYTHON_ROOT,'uv.exe'),$true) } finally {$z.Dispose()}"});
#endif
    } else if (step_ == 2) {
#ifdef Q_OS_MACOS
        if (!vocal_separation_paths::safeExistingFileWithin(uv, root_)
            || !QFile::setPermissions(uv, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            fail(tr("无法安全设置配置器执行权限")); return;
        }
#endif
        if (!recoverIncompletePython()) {
            fail(tr("无法安全恢复不完整的 Python 环境；请查看 install.log")); return;
        }
        emit progress(-1, tr("下载独立 Python %1 环境").arg(pythonVersion()));
        if (QFileInfo(python()).isFile()) { ++step_; advance(); return; }
        launch(uv, {"venv", "--python", pythonVersion(), "--managed-python", QDir(root_).filePath("env")});
    } else if (step_ == 3) {
        emit progress(-1, tr("下载 / 安装 PyTorch、VR 适配器和 FFmpeg（保留缓存，可暂停续装）"));
        QStringList arguments{"pip", "install"};
#ifdef Q_OS_MACOS
        arguments.append({"--only-binary", ":all:"});
#endif
        if (repairAttempted_) arguments.append("--reinstall");
        arguments.append({"--index-url", mirror_ ? "https://pypi.tuna.tsinghua.edu.cn/simple" : "https://pypi.org/simple",
                          "--python", python(),
#if defined(Q_OS_MACOS) && !defined(Q_PROCESSOR_ARM_64)
                          "audio-separator[cpu]==0.24.1", "torch==2.2.2", "torchaudio==2.2.2", "torchvision==0.17.2",
#else
                          "audio-separator[cpu]==0.30.2",
                          "torch==2.5.1", "torchaudio==2.5.1", "torchvision==0.20.1",
#endif
                          "numpy==1.26.4", "imageio-ffmpeg==0.6.0",
#ifdef Q_OS_MACOS
                          "onnxruntime==1.18.1", "diffq==0.2.4",
#else
                          "onnxruntime==1.20.1",
#endif
                          "onnx==1.17.0", "numba==0.60.0", "scipy==1.14.1", "librosa==0.10.2.post1"});
        launch(uv, arguments);
    } else if (step_ == 4) {
        emit progress(-1, tr("验证实际 Python / PyTorch / FFmpeg 导入"));
        launch(python(), {workerScript(), "--verify"});
    } else {
        if (!clearVerificationMarker()) {
            fail(tr("无法安全保存环境校验状态")); return;
        }
        QSaveFile marker(QDir(root_).filePath("verified-vr-1"));
        const auto& expected = verifiedVrMarker();
        if (!marker.open(QIODevice::WriteOnly) || marker.write(expected) != expected.size() || !marker.commit()) {
            fail(tr("无法保存环境校验状态")); return;
        }
        busy_ = false;
        verifyingExistingEnvironment_ = false;
        repairAttempted_ = false;
        appendLog(QStringLiteral("verified Python environment ready"));
        installationLock_.reset();
        emit progress(1, tr("Python VR 环境已就绪"));
        emit changed();
        emit finished(true, {});
    }
}

void ExternalSeparationRuntime::pause()
{
    if (!busy_ || paused_) return;
    paused_ = true;
    resumeRequested_ = false;
    stopError_.clear();
    appendLog(QStringLiteral("paused; retaining cache and partial installation"));
    if (step_ == 0) downloader_.pause();
    else if (step_ > 0) stopInstaller();
    emit changed();
}
void ExternalSeparationRuntime::resume()
{
    if (!busy_ || !paused_) return;
    if (stopping_ || process_.state() != QProcess::NotRunning) {
        resumeRequested_ = true;
        emit progress(-1, tr("正在停止当前配置进程，随后自动继续"));
        return;
    }
    paused_ = false;
    appendLog(QStringLiteral("resumed"));
    emit changed();
    if (step_ == 0) {
        if (downloader_.state() == VocalDownloadState::Paused) downloader_.resume();
        else if (downloader_.state() != VocalDownloadState::Verifying)
            downloader_.start(uvArchive(archiveMirror_), QDir(root_).filePath("uv.zip"));
    } else if (step_ > 0) advance();
}
void ExternalSeparationRuntime::cancel()
{
    busy_ = false;
    paused_ = false;
    resumeRequested_ = false;
    ++generation_;
    appendLog(QStringLiteral("cancelled; retaining cache"));
    downloader_.cancel();
    stopInstaller();
    if (!stopping_ && !cacheVerification_.isRunning()) installationLock_.reset();
    emit changed();
}
void ExternalSeparationRuntime::fail(const QString& error)
{
    if (process_.state() == QProcess::NotRunning && !cacheVerification_.isRunning()) installationLock_.reset();
    inactivity_.stop(); ioPoll_.stop();
    appendLog(QStringLiteral("failure: %1").arg(error));
    busy_ = false;
    emit changed();
    emit finished(false, error + tr("；安装日志：%1").arg(QDir(root_).filePath("install.log")));
}

void ExternalSeparationRuntime::stopInstaller()
{
    inactivity_.stop(); ioPoll_.stop();
    if (stopping_) return;
    stopping_ = true;
    if (process_.state() == QProcess::NotRunning) { finishStoppedInstaller(); return; }
    if (!process_.processId()) return; // started/FailedToStart completes this transition.
#ifdef Q_OS_WIN
    // No wait on the GUI thread. Do not reuse the process until tree termination
    // finishes, so a late taskkill can never target a subsequent installation.
    terminateTree_.start("taskkill.exe", {"/PID", QString::number(process_.processId()), "/T", "/F"});
#else
    ::kill(-static_cast<pid_t>(process_.processId()), SIGKILL);
    process_.kill();
#endif
    stopDeadline_.start();
}

void ExternalSeparationRuntime::finishStoppedInstaller()
{
    if (!stopping_ || process_.state() != QProcess::NotRunning
        || terminateTree_.state() != QProcess::NotRunning) return;
    stopDeadline_.stop();
    stopping_ = false;
    if (!busy_) { if (!cacheVerification_.isRunning()) installationLock_.reset(); return; }
    if (paused_) {
        if (resumeRequested_) { resumeRequested_ = false; resume(); }
        return;
    }
    if (!stopError_.isEmpty()) {
        const auto error = std::exchange(stopError_, {});
        stageFailed(error);
    }
}

void ExternalSeparationRuntime::stageFailed(const QString& error)
{
    appendLog(QStringLiteral("stage failure: %1").arg(error));
    if (step_ == 4 && verifyingExistingEnvironment_ && !repairAttempted_) {
        verifyingExistingEnvironment_ = false;
        repairAttempted_ = true;
        if (!clearVerificationMarker()) {
            fail(tr("无法安全清除失效的环境校验状态")); return;
        }
        emit progress(-1, tr("已有环境验证失败，使用已校验配置器修复（保留环境和缓存）"));
        verifyCachedConfigurator();
        return;
    }
    if ((step_ == 2 || step_ == 3) && !mirror_) {
        mirror_ = true;
        emit progress(-1, step_ == 2
            ? tr("Python 官方线路失败或超时，切换国内备用线路（保留官方发行校验）")
            : tr("官方包源失败或超时，切换清华 PyPI 镜像继续配置"));
        queueAdvance();
    } else {
        fail(tr("Python 配置阶段 %1 失败：%2").arg(step_).arg(error));
    }
}

void ExternalSeparationRuntime::queueAdvance()
{
    const auto generation = generation_;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == generation_) advance();
    });
}

void ExternalSeparationRuntime::appendLog(const QString& detail)
{
    using namespace vocal_separation_paths;
    const QString path = QDir(root_).filePath("install.log");
    if (!safeExistingDirectory(root_)
        || (safePathKind(path) != SafePathKind::Missing && !safeExistingFileWithin(path, root_))) return;
    // Keep bounded diagnostics locally, including both routes. No credentials,
    // environment dump or user input audio is explicitly added to this log.
    if (QFileInfo(path).size() > 1024 * 1024) {
        QFile old(path);
        if (!old.open(QIODevice::ReadOnly) || !old.seek(old.size() - 512 * 1024)) return;
        const auto tail = old.readAll(); old.close();
        QSaveFile trimmed(path); trimmed.setDirectWriteFallback(false);
        if (!trimmed.open(QIODevice::WriteOnly) || trimmed.write(tail) != tail.size() || !trimmed.commit()) return;
    }
    QFile log(path);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    log.write(QStringLiteral("%1 stage=%2 source=%3 %4\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .arg(step_).arg(mirror_ ? "backup" : "official", detail.left(65536)).toUtf8());
}

bool ExternalSeparationRuntime::recoverIncompletePython()
{
    using namespace vocal_separation_paths;
    const auto quarantine = [this](const QString& directory, const QString& executable, const QString& label) {
        if (safePathKind(directory) == SafePathKind::Missing) return true;
        if (!safeExistingPathWithin(directory, root_, SafePathKind::Directory)) return false;
        if (safeExistingFileWithin(executable, root_)) return true;
        if (safePathKind(executable) != SafePathKind::Missing) return false;
        const QString destination = QDir(root_).filePath(".incomplete-" + label + "-"
            + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!lexicallyWithin(destination, root_) || !QDir().rename(directory, destination)) return false;
        appendLog(QStringLiteral("preserved incomplete %1 as %2").arg(label, QFileInfo(destination).fileName()));
        return true;
    };
#ifdef Q_OS_MACOS
    // uv owns its managed version directory names. Preserve partial downloads;
    // validate the venv's confined interpreter link before reusing it.
    if (pythonPathIsSafe()) return true;
    return quarantine(QDir(root_).filePath("env"), python(), "env");
#else
    const QString managed = QDir(root_).filePath("python/cpython-3.11.13-windows-x86_64-none");
    return quarantine(managed, QDir(managed).filePath("python.exe"), "python")
        && quarantine(QDir(root_).filePath("env"), python(), "env");
#endif
}

namespace {
QJsonObject nvidiaHardware()
{
#ifdef Q_OS_MACOS
    return {{"available", false}, {"diagnostic", "macOS 使用 CoreML / MPS 或 CPU"}};
#else
    QString program = QStandardPaths::findExecutable("nvidia-smi.exe");
    if (program.isEmpty()) {
        const auto standard = QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/nvidia-smi.exe");
        if (QFileInfo::exists(standard)) program = standard;
    }
    if (program.isEmpty()) return {{"available", false}, {"diagnostic", "nvidia-smi not found"}};
    QProcess process;
    process.start(program, {"--query-gpu=name,driver_version", "--format=csv,noheader"});
    if (!process.waitForFinished(5000)) {
        process.kill(); process.waitForFinished(1000);
        return {{"available", false}, {"diagnostic", "NVIDIA driver query timed out"}};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {{"available", false}, {"diagnostic", QString::fromUtf8(process.readAllStandardError()).trimmed().left(300)}};
    QStringList names, versions;
    for (const auto& line : QString::fromUtf8(process.readAllStandardOutput()).split('\n')) {
        const auto comma = line.lastIndexOf(',');
        if (comma <= 0 || line.mid(comma + 1).trimmed().isEmpty()) continue;
        names.append(line.left(comma).trimmed());
        const auto version = line.mid(comma + 1).trimmed();
        if (!versions.contains(version)) versions.append(version);
    }
    return {{"available", !names.isEmpty()}, {"name", names.join(" / ")},
            {"driverVersion", versions.join(" / ")}, {"diagnostic", names.isEmpty() ? "Empty NVIDIA driver response" : ""}};
#endif
}
bool verifyCudaFiles(const QString& root, const QList<VocalDownloadFile>& files,
                     const std::shared_ptr<std::atomic_bool>& cancellation)
{
    if (!vocal_separation_paths::safeExistingDirectory(root)) return false;
    for (const auto& file : files) {
        const QString path = QDir(root).filePath(file.fileName);
        if (!vocal_separation_paths::safeExistingFileWithin(path, root)
            || !VocalSeparationInstaller::isVerifiedFile(file, path, cancellation)) return false;
    }
    return !files.isEmpty();
}
}

CudaSeparationRuntime::CudaSeparationRuntime(QString root, QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), root_(std::move(root)), downloader_(network),
      cancellation_(std::make_shared<std::atomic_bool>(false))
{
    initializePythonResources();
    QFile active(QDir(root_).filePath("active-cuda"));
    if (vocal_separation_paths::openRegularFileForReadWithin(&active, root_)) {
        const QString name = QString::fromUtf8(active.read(200)).trimmed();
        if (name.startsWith("native-") && name.size() < 100 && !name.contains('/') && !name.contains('\\')) activeDirectory_ = name;
    }
    QFile file(":/separation/cuda-runtime-manifest.json");
    if (file.open(QIODevice::ReadOnly)) {
        const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
        const auto parse = [](const QJsonArray& values) {
            QList<VocalDownloadFile> result;
            for (const auto& value : values) {
                const auto entry = value.toObject();
                result.push_back({entry.value("name").toString(), QUrl(entry.value("url").toString()),
                    entry.value("bytes").toInteger(), entry.value("sha256").toString()});
            }
            return result;
        };
        archives_ = parse(manifest.value("files").toArray());
        dlls_ = parse(manifest.value("dlls").toArray());
    }
    connect(&downloader_, &VocalSeparationDownloader::progressChanged, this, [this](qint64 received, qint64 total) {
        qint64 previous = 0, all = 0;
        for (int i = 0; i < archives_.size(); ++i) { all += archives_[i].bytes; if (i < index_) previous += archives_[i].bytes; }
        emit progress(all > 0 ? double(previous + received) / double(all) : -1,
            tr("下载 CUDA 组件 %1 / %2：%3 / %4 MB").arg(index_ + 1).arg(archives_.size()).arg(received / 1048576).arg(total / 1048576));
    });
    connect(&downloader_, &VocalSeparationDownloader::finished, this, [this](const VocalInstallResult& result) {
        if (!busy_) return;
        if (!result.ok) {
            if (!mirror_) { mirror_ = true; advance(); return; }
            fail(tr("CUDA 官方及国内线路均失败：%1").arg(result.error)); return;
        }
        ++index_; mirror_ = false; if (!paused_) advance();
    });
    connect(&work_, &QFutureWatcher<VocalInstallResult>::finished, this, [this] {
        const auto result = work_.result();
        if (!busy_) installationLock_.reset();
        if (phase_ == -1) {
            ready_ = result.ok;
            hardware_ = QJsonDocument::fromJson(result.error.toUtf8()).object().toVariantMap();
            nvidiaAvailable_ = hardware_.value("available").toBool();
            emit changed(); return;
        }
        if (resumeRequested_ && busy_) {
            resumeRequested_ = false; paused_ = false;
            cancellation_ = std::make_shared<std::atomic_bool>(false); emit changed(); advance(); return;
        }
        if (!busy_ || paused_) return;
        if (phase_ == 1) {
            if (!result.ok) { fail(result.error); return; }
            activeDirectory_ = result.error;
            ready_ = true; busy_ = false; installationLock_.reset(); emit changed(); emit finished(true, {}); return;
        }
        if (result.ok) { ++index_; mirror_ = false; advance(); return; }
        auto archive = archives_.at(index_);
        if (mirror_) {
            QString url = archive.url.toString();
            url.replace("https://files.pythonhosted.org/packages/", "https://pypi.tuna.tsinghua.edu.cn/packages/");
            archive.url = QUrl(url);
            emit progress(-1, tr("官方 CUDA 下载中断，切换清华镜像（保持官方 SHA-256）"));
        }
        downloader_.start(archive, QDir(root_).filePath("cache/" + archive.fileName));
    });
    refreshHardware();
}
CudaSeparationRuntime::~CudaSeparationRuntime() { blockSignals(true); cancel(); work_.waitForFinished(); }
void CudaSeparationRuntime::refreshHardware()
{
    if (busy_ || work_.isRunning()) return;
    phase_ = -1;
    const QString native = QDir(root_).filePath(activeDirectory_);
    const auto dlls = dlls_; const auto cancellation = cancellation_;
    work_.setFuture(QtConcurrent::run([native, dlls, cancellation] {
        return VocalInstallResult{verifyCudaFiles(native, dlls, cancellation),
            QString::fromUtf8(QJsonDocument(nvidiaHardware()).toJson(QJsonDocument::Compact))};
    }));
}
QString CudaSeparationRuntime::hardwareSummary() const
{
    if (checking()) return tr("正在自动检测 NVIDIA 显卡、驱动及应用 CUDA 组件");
    if (nvidiaAvailable_)
        return tr("已识别 %1 · 驱动 %2 已就绪").arg(hardwareName(), driverVersion());
    return tr("尚未确认可用的 NVIDIA 驱动（%1）；这不代表其他显卡或 DirectML 不可用")
        .arg(hardware_.value("diagnostic").toString());
}
QString CudaSeparationRuntime::libraryPath() const { return QDir(root_).filePath(activeDirectory_ + "/onnxruntime.dll"); }
bool CudaSeparationRuntime::start()
{
    if (ready_) return true;
    if (busy_ || work_.isRunning() || archives_.isEmpty() || dlls_.isEmpty()) return false;
    if (!QDir().mkpath(QDir(root_).filePath("cache"))
        || !vocal_separation_paths::safeExistingDirectory(root_)) return false;
    if (!VocalSeparationInstaller::hasDiskSpace(root_, 5LL * 1024 * 1024 * 1024)) return false;
    installationLock_ = std::make_unique<QLockFile>(root_ + QStringLiteral(".install.lock"));
    if (!installationLock_->tryLock()) { installationLock_.reset(); return false; }
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    busy_ = true; paused_ = false; resumeRequested_ = false; index_ = 0; mirror_ = false;
    emit changed(); advance(); return true;
}
void CudaSeparationRuntime::advance()
{
    if (!busy_ || paused_ || work_.isRunning()) return;
    const auto cancellation = cancellation_;
    if (index_ < archives_.size()) {
        phase_ = 0;
        const auto archive = archives_.at(index_);
        const QString path = QDir(root_).filePath("cache/" + archive.fileName);
        emit progress(-1, tr("检查已下载 CUDA 缓存 %1 / %2").arg(index_ + 1).arg(archives_.size()));
        work_.setFuture(QtConcurrent::run([archive, path, cancellation] {
            return VocalInstallResult{vocal_separation_paths::safeExistingFile(path)
                && VocalSeparationInstaller::isVerifiedFile(archive, path, cancellation), {}};
        })); return;
    }
    phase_ = 1;
    emit progress(-1, tr("解压并验证 CUDA / cuDNN 原生组件；不修改系统或 Python VR 环境"));
    const QString root = root_; const auto dlls = dlls_; const auto archives = archives_;
    work_.setFuture(QtConcurrent::run([root, dlls, archives, cancellation] {
        const QString name = "native-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString staging = ".staging-" + name;
        const QString native = QDir(root).filePath(staging);
        if (!QDir().mkpath(native)) return VocalInstallResult{false, "Cannot create CUDA staging directory"};
        QSet<QString> allowedNames;
        for (const auto& dll : dlls) allowedNames.insert(dll.fileName);
        const auto cleanStaging = qScopeGuard([native, allowedNames] {
            if (QFileInfo::exists(native)) vocal_separation_paths::removeKnownFlatDirectory(native, allowedNames);
        });
        if (!vocal_separation_paths::safeExistingDirectory(native)) return VocalInstallResult{false, "Unsafe CUDA directory"};
        for (const auto& dll : dlls) {
            const auto path = QDir(native).filePath(dll.fileName);
            if (QFileInfo::exists(path) && !vocal_separation_paths::safeExistingFileWithin(path, native))
                return VocalInstallResult{false, "Unsafe CUDA component path"};
        }
        QStringList names, paths;
        for (const auto& dll : dlls) names.push_back(dll.fileName);
        for (const auto& archive : archives) paths.push_back(QDir(root).filePath("cache/" + archive.fileName));
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("AGPLAYER_CUDA_TARGET", native); env.insert("AGPLAYER_CUDA_NAMES", names.join('|'));
        env.insert("AGPLAYER_CUDA_ARCHIVES", paths.join('|')); process.setProcessEnvironment(env);
        process.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
            "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "
            "$names=$env:AGPLAYER_CUDA_NAMES.Split('|'); foreach($archive in $env:AGPLAYER_CUDA_ARCHIVES.Split('|')) { "
            "$z=[IO.Compression.ZipFile]::OpenRead($archive); try {foreach($e in $z.Entries) {if($names -contains $e.Name) {"
            "[IO.Compression.ZipFileExtensions]::ExtractToFile($e,[IO.Path]::Combine($env:AGPLAYER_CUDA_TARGET,$e.Name),$true)"
            "}}} finally {$z.Dispose()} }"});
        process.closeWriteChannel();
        QElapsedTimer timer; timer.start();
        while (!process.waitForFinished(250)) {
            if (cancellation->load() || timer.elapsed() > 180000) { process.kill(); process.waitForFinished(); return VocalInstallResult{false, "CUDA extraction cancelled or timed out"}; }
        }
        if (process.exitCode() != 0 || process.exitStatus() != QProcess::NormalExit)
            return VocalInstallResult{false, QString::fromUtf8(process.readAllStandardError()).right(1200)};
        if (!verifyCudaFiles(native, dlls, cancellation)) return VocalInstallResult{false, "CUDA component integrity verification failed"};
        if (cancellation->load()) return VocalInstallResult{false, "CUDA installation cancelled"};
        const QString version = QDir(root).filePath(name);
        if (!QDir().rename(native, version)) return VocalInstallResult{false, "Cannot activate CUDA version"};
        QSaveFile marker(QDir(root).filePath("active-cuda")); marker.setDirectWriteFallback(false);
        if (!marker.open(QIODevice::WriteOnly) || marker.write(name.toUtf8()) < 0 || !marker.commit())
            return VocalInstallResult{false, "Cannot publish CUDA activation marker"};
        return VocalInstallResult{true, name};
    }));
}
void CudaSeparationRuntime::pause() {
    if (!busy_ || paused_) return;
    paused_ = true; cancellation_->store(true); downloader_.pause(); emit changed();
}
void CudaSeparationRuntime::resume() {
    if (!busy_ || !paused_) return;
    if (work_.isRunning()) { resumeRequested_ = true; emit progress(-1, tr("正在结束当前校验，随后自动继续")); return; }
    paused_ = false; cancellation_ = std::make_shared<std::atomic_bool>(false); emit changed();
    if (downloader_.state() == VocalDownloadState::Paused) downloader_.resume(); else advance();
}
void CudaSeparationRuntime::cancel() {
    busy_ = false; paused_ = false; resumeRequested_ = false; cancellation_->store(true); downloader_.cancel(); emit changed();
    if (!work_.isRunning()) installationLock_.reset();
}
void CudaSeparationRuntime::fail(const QString& error) { busy_ = false; installationLock_.reset(); emit changed(); emit finished(false, error); }
