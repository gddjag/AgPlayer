#include "vocal_separation_installer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStorageInfo>
#include <QTimer>

namespace {

constexpr int kMaxAttempts = 3;

QString hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError) {
            return {};
        }
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

VocalInstallResult fail(const QString& error)
{
    return {false, error};
}

bool fileMatches(const VocalDownloadFile& file, const QString& path)
{
    return QFileInfo(path).size() == file.bytes
        && hashFile(path) == file.sha256;
}

bool isSha256(const QString& value)
{
    if (value.size() != 64) {
        return false;
    }
    for (const QChar character : value) {
        if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
              || (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

const QList<VocalDownloadFile>& runtimeFiles()
{
    static const QList<VocalDownloadFile> files{
        {QStringLiteral("onnxruntime.dll"), {}, 17'328'152,
         QStringLiteral("e7eedec6a6f26dc39dc948276a75ef6d2bee3fff944d874ceed0bbd3b97bff40")},
        {QStringLiteral("onnxruntime_providers_shared.dll"), {}, 22'040,
         QStringLiteral("265c8daf29637cb259cac8be9f08f2cd45f3883f0f0e4949cbfddd5b4cbec3b6")},
    };
    return files;
}

} // namespace

VocalDownloadState VocalDownloadStateMachine::state() const
{
    return m_state;
}

bool VocalDownloadStateMachine::start()
{
    if (m_state != VocalDownloadState::Idle && m_state != VocalDownloadState::Failed
        && m_state != VocalDownloadState::Cancelled) {
        return false;
    }
    m_state = VocalDownloadState::Downloading;
    return true;
}

bool VocalDownloadStateMachine::pause()
{
    if (m_state != VocalDownloadState::Downloading) {
        return false;
    }
    m_state = VocalDownloadState::Paused;
    return true;
}

bool VocalDownloadStateMachine::resume()
{
    if (m_state != VocalDownloadState::Paused) {
        return false;
    }
    m_state = VocalDownloadState::Downloading;
    return true;
}

void VocalDownloadStateMachine::cancel()
{
    m_state = VocalDownloadState::Cancelled;
}

void VocalDownloadStateMachine::fail()
{
    m_state = VocalDownloadState::Failed;
}

void VocalDownloadStateMachine::verify()
{
    m_state = VocalDownloadState::Verifying;
}

void VocalDownloadStateMachine::complete()
{
    m_state = VocalDownloadState::Complete;
}

QString VocalSeparationInstaller::partPath(const QString& destination)
{
    return destination + QStringLiteral(".part");
}

qint64 VocalSeparationInstaller::resumeOffset(const QString& destination)
{
    const QFileInfo part(partPath(destination));
    return part.isFile() ? part.size() : 0;
}

bool VocalSeparationInstaller::hasDiskSpace(const QString& destination,
                                             qint64 bytesRequired)
{
    if (bytesRequired <= 0) {
        return true;
    }
    QStorageInfo storage(QFileInfo(destination).absolutePath());
    return storage.isValid() && storage.isReady()
        && storage.bytesAvailable() >= bytesRequired;
}

VocalInstallResult VocalSeparationInstaller::activateVerifiedPart(
    const VocalDownloadFile& file, const QString& destination)
{
    const QString part = partPath(destination);
    if (!fileMatches(file, part)) {
        return fail(QStringLiteral("Downloaded file did not match its expected SHA-256 or size"));
    }
    const QFileInfo target(destination);
    if (!QDir().mkpath(target.absolutePath())) {
        return fail(QStringLiteral("Cannot create model directory"));
    }
    if (QFileInfo::exists(destination)) {
        if (fileMatches(file, destination)) {
            QFile::remove(part);
            return {true, {}};
        }
        return fail(QStringLiteral("Refusing to replace an existing unverified file"));
    }
    if (!QFile::rename(part, destination)) {
        return fail(QStringLiteral("Cannot atomically activate verified download"));
    }
    return {true, {}};
}

VocalInstallResult VocalSeparationInstaller::installDirectMlRuntime(
    const QString& nupkgPath, const QString& runtimeRoot)
{
    const VocalRuntimePackage package = VocalSeparationCatalog::directMlRuntime();
    VocalDownloadFile archive;
    archive.fileName = QStringLiteral("runtime.nupkg");
    archive.bytes = package.bytes;
    archive.sha256 = package.sha256;
    if (!fileMatches(archive, nupkgPath)) {
        return fail(QStringLiteral("DirectML runtime package did not match its pinned SHA-256 or size"));
    }

    const QString versionedRoot = QDir(runtimeRoot).filePath(package.id);
    const QString stagingRoot = versionedRoot + QStringLiteral(".staging");
    if (QFileInfo::exists(versionedRoot)) {
        if (!runtimeDirectoryIsVerified(versionedRoot, package.sha256)) {
            return fail(QStringLiteral("Existing DirectML runtime failed integrity verification"));
        }
        return {true, {}};
    }
    QDir(stagingRoot).removeRecursively();
    if (!QDir().mkpath(stagingRoot)) {
        return fail(QStringLiteral("Cannot create DirectML runtime staging directory"));
    }

    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "$zip=[IO.Compression.ZipFile]::OpenRead($env:AGPLAYER_RUNTIME_ARCHIVE); "
        "try { $names=@('onnxruntime.dll','onnxruntime_providers_shared.dll'); "
        "foreach($name in $names) { $entry=$zip.GetEntry('runtimes/win-x64/native/'+$name); "
        "if($null -eq $entry){throw 'Missing native runtime file: '+$name}; "
        "$target=[IO.Path]::Combine($env:AGPLAYER_RUNTIME_STAGING,$name); "
        "$input=$entry.Open(); try { $output=[IO.File]::Open($target,[IO.FileMode]::CreateNew); try {$input.CopyTo($output)} finally {$output.Dispose()} } finally {$input.Dispose()} } "
        "} finally { $zip.Dispose() }");
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("AGPLAYER_RUNTIME_ARCHIVE"), nupkgPath);
    environment.insert(QStringLiteral("AGPLAYER_RUNTIME_STAGING"), stagingRoot);
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-Command"), script});
    process.closeWriteChannel();
    const bool processFinished = process.waitForFinished(60'000);
    if (!processFinished) {
        process.kill();
        process.waitForFinished();
    }
    if (!processFinished || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        QDir(stagingRoot).removeRecursively();
        return fail(QStringLiteral("Cannot extract pinned DirectML runtime: %1")
                        .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
    }
    for (const VocalDownloadFile& file : runtimeFiles()) {
        if (!fileMatches(file, QDir(stagingRoot).filePath(file.fileName))) {
            QDir(stagingRoot).removeRecursively();
            return fail(QStringLiteral("Extracted DirectML runtime file failed integrity verification"));
        }
    }
    QFile stagingMarker(QDir(stagingRoot).filePath(QStringLiteral("runtime.sha256")));
    if (!stagingMarker.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || stagingMarker.write(package.sha256.toLatin1())
               != static_cast<qint64>(package.sha256.size())) {
        QDir(stagingRoot).removeRecursively();
        return fail(QStringLiteral("Cannot write DirectML runtime verification marker"));
    }
    stagingMarker.close();
    if (!QDir().rename(stagingRoot, versionedRoot)) {
        QDir(stagingRoot).removeRecursively();
        return fail(QStringLiteral("Cannot atomically activate DirectML runtime"));
    }
    return {true, {}};
}

bool VocalSeparationInstaller::runtimeDirectoryIsVerified(
    const QString& runtimeDirectory, const QString& expectedArchiveSha256)
{
    if (!isSha256(expectedArchiveSha256)) {
        return false;
    }
    QFile marker(QDir(runtimeDirectory).filePath(QStringLiteral("runtime.sha256")));
    if (!marker.open(QIODevice::ReadOnly)
        || QString::fromLatin1(marker.readAll()).trimmed() != expectedArchiveSha256) {
        return false;
    }
    for (const VocalDownloadFile& file : runtimeFiles()) {
        if (!fileMatches(file, QDir(runtimeDirectory).filePath(file.fileName))) {
            return false;
        }
    }
    return true;
}

VocalSeparationDownloader::VocalSeparationDownloader(QNetworkAccessManager* network,
                                                       QObject* parent)
    : QObject(parent), m_network(network)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        issueRequest(m_operation);
    });
}

VocalDownloadState VocalSeparationDownloader::state() const
{
    return m_state.state();
}

QString VocalSeparationDownloader::error() const
{
    return m_error;
}

void VocalSeparationDownloader::start(const VocalDownloadFile& file,
                                      const QString& destination)
{
    if (m_state.state() == VocalDownloadState::Downloading
        || m_state.state() == VocalDownloadState::Paused
        || m_state.state() == VocalDownloadState::Verifying) {
        m_error = QStringLiteral("Download is already active");
        return;
    }
    if (m_network == nullptr || !file.url.isValid() || file.bytes <= 0
        || file.sha256.size() != 64 || destination.isEmpty()) {
        finishFailure(QStringLiteral("Invalid download request"));
        return;
    }
    if (!m_state.start()) {
        m_error = QStringLiteral("Download is already active");
        return;
    }
    m_retryTimer.stop();
    ++m_operation;
    m_file = file;
    m_destination = destination;
    m_attempt = 0;
    m_error.clear();
    setState(VocalDownloadState::Downloading);
    issueRequest(m_operation);
}

void VocalSeparationDownloader::pause()
{
    if (!m_state.pause()) {
        return;
    }
    m_retryTimer.stop();
    ++m_operation;
    if (m_reply != nullptr) {
        m_reply->abort();
        m_reply = nullptr;
    }
    setState(VocalDownloadState::Paused);
}

void VocalSeparationDownloader::resume()
{
    if (!m_state.resume()) {
        return;
    }
    ++m_operation;
    setState(VocalDownloadState::Downloading);
    issueRequest(m_operation);
}

void VocalSeparationDownloader::cancel()
{
    m_state.cancel();
    m_retryTimer.stop();
    ++m_operation;
    if (m_reply != nullptr) {
        m_reply->abort();
        m_reply = nullptr;
    }
    setState(VocalDownloadState::Cancelled);
}

void VocalSeparationDownloader::issueRequest(quint64 operation)
{
    if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading
        || m_reply != nullptr) {
        return;
    }
    m_resumeOffset = VocalSeparationInstaller::resumeOffset(m_destination);
    if (m_resumeOffset > m_file.bytes) {
        QFile::remove(VocalSeparationInstaller::partPath(m_destination));
        m_resumeOffset = 0;
    }
    if (m_resumeOffset == m_file.bytes) {
        m_state.verify();
        setState(VocalDownloadState::Verifying);
        const VocalInstallResult result = VocalSeparationInstaller::activateVerifiedPart(
            m_file, m_destination);
        if (result.ok) {
            m_state.complete();
            setState(VocalDownloadState::Complete);
        } else {
            QFile::remove(VocalSeparationInstaller::partPath(m_destination));
            m_state.fail();
            setState(VocalDownloadState::Failed);
        }
        emit finished(result);
        return;
    }
    if (!VocalSeparationInstaller::hasDiskSpace(m_destination,
                                                m_file.bytes - m_resumeOffset)) {
        finishFailure(QStringLiteral("Insufficient disk space for download"));
        return;
    }
    QNetworkRequest request(m_file.url);
    if (m_resumeOffset > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_resumeOffset) + "-");
    }
    QNetworkReply* const reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, operation, reply] {
        if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading) {
            return;
        }
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_resumeOffset > 0 && status == 206) {
            const QByteArray expected = "bytes " + QByteArray::number(m_resumeOffset) + "-";
            if (!reply->rawHeader("Content-Range").startsWith(expected)) {
                finishFailure(QStringLiteral("Server returned an invalid Content-Range"));
            }
        } else if (m_resumeOffset > 0) {
            QFile part(VocalSeparationInstaller::partPath(m_destination));
            if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                finishFailure(QStringLiteral("Cannot restart partial download"));
                return;
            }
            part.close();
            m_resumeOffset = 0;
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, operation, reply] {
        if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading) {
            reply->readAll();
            return;
        }
        QFile part(VocalSeparationInstaller::partPath(m_destination));
        if (!part.open(QIODevice::WriteOnly | QIODevice::Append)) {
            finishFailure(QStringLiteral("Cannot write partial download"));
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            return;
        }
        if (part.write(bytes) != bytes.size()) {
            part.close();
            finishFailure(QStringLiteral("Cannot write complete partial download"));
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, operation](qint64 received, qint64 total) {
        if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading) {
            return;
        }
        emit progressChanged(m_resumeOffset + received,
                             total < 0 ? -1 : m_resumeOffset + total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, operation, reply] {
        if (m_reply == reply) {
            m_reply = nullptr;
        }
        const QNetworkReply::NetworkError networkError = reply->error();
        reply->deleteLater();
        if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading) {
            return;
        }
        if (networkError != QNetworkReply::NoError) {
            if (++m_attempt < kMaxAttempts) {
                m_retryTimer.start(250 * m_attempt);
                return;
            }
            finishFailure(QStringLiteral("Download failed after retries"));
            return;
        }
        m_state.verify();
        setState(VocalDownloadState::Verifying);
        const VocalInstallResult result = VocalSeparationInstaller::activateVerifiedPart(
            m_file, m_destination);
        if (result.ok) {
            m_state.complete();
            setState(VocalDownloadState::Complete);
        } else {
            m_state.fail();
            setState(VocalDownloadState::Failed);
        }
        emit finished(result);
    });
}

void VocalSeparationDownloader::setState(VocalDownloadState state)
{
    emit stateChanged(state);
}

void VocalSeparationDownloader::finishFailure(const QString& error)
{
    m_error = error;
    m_retryTimer.stop();
    ++m_operation;
    if (m_reply != nullptr) {
        m_reply->abort();
        m_reply = nullptr;
    }
    m_state.fail();
    setState(VocalDownloadState::Failed);
    emit finished({false, error});
}
