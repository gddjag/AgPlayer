#include "external_separation_runtime.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include "vocal_separation_path_safety.hpp"

static void initializePythonResources() { Q_INIT_RESOURCE(agplayer_separation_python); }
static VocalDownloadFile uvArchive() {
    return {"uv.zip", QUrl("https://github.com/astral-sh/uv/releases/download/0.8.22/uv-x86_64-pc-windows-msvc.zip"),
            20716936, "5049375aa2a5162f132b2c1cb992e25d42d47d934cab8c174dbe6f60973dcc12"};
}

ExternalSeparationRuntime::ExternalSeparationRuntime(QString root,
    QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), root_(std::move(root)), downloader_(network)
{
    initializePythonResources();
    connect(&downloader_, &VocalSeparationDownloader::progressChanged, this,
            [this](qint64 received, qint64 total) {
        emit progress(total > 0 ? 0.1 * double(received) / double(total) : 0,
                      tr("下载配置器 %1 / %2 MB").arg(received / 1048576).arg(total / 1048576));
    });
    connect(&downloader_, &VocalSeparationDownloader::finished, this,
            [this](const VocalInstallResult& result) {
        if (!busy_) return;
        if (!result.ok) { fail(result.error); return; }
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
            if (step_ == 3 && !mirror_) {
                mirror_ = true;
                emit progress(-1, tr("官方包源连接失败，切换清华 PyPI 镜像继续配置"));
                advance();
                return;
            }
            fail(tr("Python 配置阶段 %1 失败：%2").arg(step_).arg(QString::fromUtf8(output_).right(1800)));
            return;
        }
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
    return QFileInfo(python()).isFile() && QFileInfo(workerScript()).isFile()
        && QFileInfo(QDir(root_).filePath("verified-vr-1")).isFile();
}

bool ExternalSeparationRuntime::start()
{
    if (busy_ || cacheVerification_.isRunning()) return false;
    if (!QDir().mkpath(root_)) return false;
    if (!vocal_separation_paths::safeExistingDirectory(root_)) return false;
    const QString marker = QDir(root_).filePath("verified-vr-1");
    if (QFileInfo::exists(marker) && !QFile::remove(marker)) return false;
    QFile source(":/separation/external_separation_worker.py");
    QSaveFile target(workerScript());
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) return false;
    const QByteArray script = source.readAll();
    if (target.write(script) != script.size() || !target.commit()) return false;
    busy_ = true;
    paused_ = false;
    step_ = 0;
    mirror_ = false;
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
    environment.insert("UV_HTTP_TIMEOUT", "180");
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
            downloader_.start(uvArchive(), QDir(root_).filePath("uv.zip"));
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
