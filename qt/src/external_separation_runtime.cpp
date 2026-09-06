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
#include <QtConcurrent/QtConcurrentRun>
#include "vocal_separation_path_safety.hpp"

static void initializePythonResources() { Q_INIT_RESOURCE(agplayer_separation_python); }
static VocalDownloadFile uvArchive(bool mirror = false) {
    const QString official = QStringLiteral("https://github.com/astral-sh/uv/releases/download/0.8.22/uv-x86_64-pc-windows-msvc.zip");
    // The relay changes transport only; both routes must match the same pinned
    // official release size and SHA-256 before any archive is extracted.
    return {"uv.zip", QUrl(mirror ? QStringLiteral("https://ghfast.top/") + official : official),
            20716936, "5049375aa2a5162f132b2c1cb992e25d42d47d934cab8c174dbe6f60973dcc12"};
}

ExternalSeparationRuntime::ExternalSeparationRuntime(QString root,
    QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), root_(std::move(root)), downloader_(network)
{
    initializePythonResources();
    QFile bundled(":/separation/external_separation_worker.py");
    if (bundled.open(QIODevice::ReadOnly)) bundledWorker_ = bundled.readAll();
    // Updating the application updates its bridge, not the optional Python
    // installation. Reuse a verified environment without any network/process.
    if (vocal_separation_paths::safeExistingFileWithin(python(), root_)
        && vocal_separation_paths::safeExistingFileWithin(
            QDir(root_).filePath("verified-vr-1"), root_))
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
        if (!busy_) return;
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
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        output_.append(process_.readAllStandardOutput());
        if (output_.size() > 16384) output_ = output_.right(16384);
        const QString text = QString::fromUtf8(output_).trimmed();
        const QString last = text.section(QLatin1Char('\n'), -1).left(160);
        if (!last.isEmpty()) emit progress(-1, last); // Unknown dependency total: don't invent percentages.
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (busy_ && !paused_ && error == QProcess::FailedToStart) fail(process_.errorString());
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        if (!busy_ || paused_) return;
        if (code != 0 || status != QProcess::NormalExit) {
            if ((step_ == 2 || step_ == 3) && !mirror_) {
                mirror_ = true;
                emit progress(-1, step_ == 2
                    ? tr("Python 官方线路失败，切换国内备用线路（保留官方发行校验）")
                    : tr("官方包源连接失败，切换清华 PyPI 镜像继续配置"));
                advance();
                return;
            }
            fail(tr("Python 配置阶段 %1 失败：%2").arg(step_).arg(QString::fromUtf8(output_).right(1800)));
            return;
        }
        mirror_ = false;
        ++step_;
        advance();
    });
}

ExternalSeparationRuntime::~ExternalSeparationRuntime()
{
    busy_ = false;
    stopInstaller();
    cacheVerification_.waitForFinished();
}

QString ExternalSeparationRuntime::python() const { return QDir(root_).filePath("env/Scripts/python.exe"); }
QString ExternalSeparationRuntime::workerScript() const { return QDir(root_).filePath("external_separation_worker.py"); }
bool ExternalSeparationRuntime::ready() const
{
    return vocal_separation_paths::safeExistingDirectory(root_)
        && vocal_separation_paths::safeExistingFileWithin(python(), root_)
        && vocal_separation_paths::safeExistingFileWithin(
            QDir(root_).filePath("verified-vr-1"), root_)
        && workerMatchesBundle();
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

bool ExternalSeparationRuntime::start()
{
    if (busy_ || cacheVerification_.isRunning()) return false;
    if (!QDir().mkpath(root_)) return false;
    if (!vocal_separation_paths::safeExistingDirectory(root_)) return false;
    const QString marker = QDir(root_).filePath("verified-vr-1");
    if (QFileInfo::exists(marker) && !QFile::remove(marker)) return false;
    if (!synchronizeWorker()) return false;
    busy_ = true;
    paused_ = false;
    step_ = 0;
    mirror_ = false;
    archiveMirror_ = false;
    emit changed();
    emit progress(0, tr("外置 Python / PyTorch，约 450 MB 下载，约 1.5 GB 磁盘；不修改系统环境"));
    const QString archive = QDir(root_).filePath("uv.zip");
    step_ = -1;
    cacheVerification_.setFuture(QtConcurrent::run([archive] {
        return VocalSeparationInstaller::isVerifiedFile(uvArchive(), archive);
    }));
    return true;
}

void ExternalSeparationRuntime::launch(const QString& program, const QStringList& arguments)
{
    output_.clear();
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
    process_.start(program, arguments);
    process_.closeWriteChannel();
}

void ExternalSeparationRuntime::advance()
{
    const QString uv = QDir(root_).filePath("uv.exe");
    if (step_ == 1) {
        emit progress(-1, tr("解压已校验配置器"));
        launch("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
            "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "
            "$z=[IO.Compression.ZipFile]::OpenRead([IO.Path]::Combine($env:AGPLAYER_PYTHON_ROOT,'uv.zip')); "
            "try {$e=$z.GetEntry('uv.exe'); if(!$e){throw 'Missing uv.exe'}; "
            "[IO.Compression.ZipFileExtensions]::ExtractToFile($e,[IO.Path]::Combine($env:AGPLAYER_PYTHON_ROOT,'uv.exe'),$true) } finally {$z.Dispose()}"});
    } else if (step_ == 2) {
        emit progress(-1, tr("下载独立 Python 3.11 环境"));
        if (QFileInfo(python()).isFile()) { ++step_; advance(); return; }
        launch(uv, {"venv", "--python", "3.11.13", "--managed-python", QDir(root_).filePath("env")});
    } else if (step_ == 3) {
        emit progress(-1, tr("下载 / 安装 PyTorch、VR 适配器和 FFmpeg（保留缓存，可暂停续装）"));
        launch(uv, {"pip", "install", "--index-url", mirror_ ? "https://pypi.tuna.tsinghua.edu.cn/simple" : "https://pypi.org/simple",
                    "--python", python(), "audio-separator[cpu]==0.30.2",
                    "torch==2.5.1", "torchaudio==2.5.1", "torchvision==0.20.1", "numpy==1.26.4", "imageio-ffmpeg==0.6.0",
                    "onnxruntime==1.20.1", "onnx==1.17.0", "numba==0.60.0", "scipy==1.14.1", "librosa==0.10.2.post1"});
    } else if (step_ == 4) {
        emit progress(-1, tr("验证实际 Python / PyTorch / FFmpeg 导入"));
        launch(python(), {workerScript(), "--verify"});
    } else {
        QSaveFile marker(QDir(root_).filePath("verified-vr-1"));
        if (!marker.open(QIODevice::WriteOnly) || marker.write("audio-separator=0.30.2") < 0 || !marker.commit()) {
            fail(tr("无法保存环境校验状态")); return;
        }
        busy_ = false;
        emit progress(1, tr("Python VR 环境已就绪"));
        emit changed();
        emit finished(true, {});
    }
}

void ExternalSeparationRuntime::pause()
{
    if (!busy_ || paused_) return;
    paused_ = true;
    if (step_ == 0) downloader_.pause();
    else if (step_ > 0) stopInstaller();
    emit changed();
}
void ExternalSeparationRuntime::resume()
{
    if (!busy_ || !paused_) return;
    if (process_.state() != QProcess::NotRunning) return;
    paused_ = false;
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
    downloader_.cancel();
    stopInstaller();
    emit changed();
}
void ExternalSeparationRuntime::fail(const QString& error)
{
    busy_ = false;
    emit changed();
    emit finished(false, error);
}

void ExternalSeparationRuntime::stopInstaller()
{
    if (process_.state() == QProcess::NotRunning) return;
#ifdef Q_OS_WIN
    // uv can have a wheel-build child. Kill only this owned process tree.
    QProcess terminateTree;
    terminateTree.start("taskkill.exe", {"/PID", QString::number(process_.processId()), "/T", "/F"});
    if (!terminateTree.waitForFinished(1500)) terminateTree.kill();
#endif
    process_.kill();
    process_.waitForFinished(1500);
}

namespace {
QString nvidiaHardwareName()
{
    // Driver identity is stable for this app process. Avoid repeatedly spawning
    // nvidia-smi when controllers are recreated; model validation remains live.
    static const QString name = [] {
        QProcess hardware;
        hardware.start("nvidia-smi.exe", {"--query-gpu=name", "--format=csv,noheader"});
        if (hardware.waitForFinished(5000) && hardware.exitCode() == 0)
            return QString::fromUtf8(hardware.readAllStandardOutput()).trimmed();
        hardware.kill(); hardware.waitForFinished(1000);
        return QString();
    }();
    return name;
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
        if (phase_ == -1) { ready_ = result.ok; nvidiaAvailable_ = !result.error.isEmpty(); emit changed(); return; }
        if (resumeRequested_ && busy_) {
            resumeRequested_ = false; paused_ = false;
            cancellation_ = std::make_shared<std::atomic_bool>(false); emit changed(); advance(); return;
        }
        if (!busy_ || paused_) return;
        if (phase_ == 1) {
            if (!result.ok) { fail(result.error); return; }
            activeDirectory_ = result.error;
            ready_ = true; busy_ = false; emit changed(); emit finished(true, {}); return;
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
    const QString native = QDir(root_).filePath(activeDirectory_);
    const auto dlls = dlls_; const auto cancellation = cancellation_;
    work_.setFuture(QtConcurrent::run([native, dlls, cancellation] {
        return VocalInstallResult{verifyCudaFiles(native, dlls, cancellation), nvidiaHardwareName()};
    }));
}
CudaSeparationRuntime::~CudaSeparationRuntime() { blockSignals(true); cancel(); work_.waitForFinished(); }
QString CudaSeparationRuntime::libraryPath() const { return QDir(root_).filePath(activeDirectory_ + "/onnxruntime.dll"); }
bool CudaSeparationRuntime::start()
{
    if (busy_ || work_.isRunning() || archives_.isEmpty() || dlls_.isEmpty()) return false;
    if (!QDir().mkpath(QDir(root_).filePath("cache"))
        || !vocal_separation_paths::safeExistingDirectory(root_)) return false;
    if (!VocalSeparationInstaller::hasDiskSpace(root_, 5LL * 1024 * 1024 * 1024)) return false;
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
}
void CudaSeparationRuntime::fail(const QString& error) { busy_ = false; emit changed(); emit finished(false, error); }
