#include "vocal_separation_installer.hpp"

#include "vocal_separation_path_safety.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStorageInfo>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr int kMaxAttempts = 3;
constexpr qint64 kDownloadReadBufferSize = 64 * 1024;

bool isSafePartialDownloadPath(const QString& path)
{
    const auto kind = vocal_separation_paths::safePathKind(path);
    return kind == vocal_separation_paths::SafePathKind::Missing
        || (kind == vocal_separation_paths::SafePathKind::RegularFile
            && vocal_separation_paths::safeExistingFile(path));
}

bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancellation)
{
    return cancellation
        && cancellation->load(std::memory_order_acquire);
}

QString hashOpenFile(
    QFile* file,
    const std::shared_ptr<std::atomic_bool>& cancellation = {})
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file->atEnd()) {
        if (isCancelled(cancellation)) return {};
        const QByteArray bytes = file->read(1024 * 1024);
        if (bytes.isEmpty() && file->error() != QFile::NoError) {
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

bool fileMatches(
    const VocalDownloadFile& file, const QString& path,
    const std::shared_ptr<std::atomic_bool>& cancellation = {})
{
    QFile candidate(path);
    return vocal_separation_paths::openRegularFileForRead(&candidate)
        && candidate.size() == file.bytes
        && hashOpenFile(&candidate, cancellation) == file.sha256;
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
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_ARM_64
        {QStringLiteral("libonnxruntime.dylib"), {}, 24'870'696,
         QStringLiteral("cf0c4da3d0ccae9b71cc854333e716d162869a3c42f9017b142408542fcec9b7")},
#else
        {QStringLiteral("libonnxruntime.dylib"), {}, 27'920'304,
         QStringLiteral("b25f77bf7c58ef0fbcdd5fb090301df4e80f82cf0000ceed1042418729b6594d")},
#endif
#else
        {QStringLiteral("onnxruntime.dll"), {}, 17'328'152,
         QStringLiteral("e7eedec6a6f26dc39dc948276a75ef6d2bee3fff944d874ceed0bbd3b97bff40")},
        {QStringLiteral("onnxruntime_providers_shared.dll"), {}, 22'040,
         QStringLiteral("265c8daf29637cb259cac8be9f08f2cd45f3883f0f0e4949cbfddd5b4cbec3b6")},
#endif
    };
    return files;
}

QSet<QString> runtimeAllowedNames()
{
    QSet<QString> names{QStringLiteral("runtime.sha256")};
    for (const VocalDownloadFile& file : runtimeFiles())
        names.insert(file.fileName);
    return names;
}

} // namespace

QString VocalSeparationInstaller::runtimeVersionDirectory(
    const QString& runtimeRoot)
{
    return QDir(runtimeRoot).filePath(
        VocalSeparationCatalog::nativeRuntime().id);
}

QString VocalSeparationInstaller::runtimeLibraryPath(const QString& runtimeRoot)
{
    return QDir(runtimeVersionDirectory(runtimeRoot))
#ifdef Q_OS_MACOS
        .filePath(QStringLiteral("libonnxruntime.dylib"));
#else
        .filePath(QStringLiteral("onnxruntime.dll"));
#endif
}

VocalDownloadState VocalDownloadStateMachine::state() const
{
    return m_state;
}

bool VocalDownloadStateMachine::start()
{
    if (m_state != VocalDownloadState::Idle && m_state != VocalDownloadState::Failed
        && m_state != VocalDownloadState::Cancelled
        && m_state != VocalDownloadState::Complete) {
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
    const QString path = partPath(destination);
    return vocal_separation_paths::safePathKind(path)
            == vocal_separation_paths::SafePathKind::RegularFile
            && vocal_separation_paths::safeExistingFile(path)
        ? QFileInfo(path).size() : 0;
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

bool VocalSeparationInstaller::isVerifiedFile(const VocalDownloadFile& file,
                                              const QString& path,
                                              const std::shared_ptr<std::atomic_bool>& cancellation)
{
    return fileMatches(file, path, cancellation);
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
    if (!fileMatches(file, destination)) {
        QFile::remove(destination);
        return fail(QStringLiteral("Activated download failed final integrity verification"));
    }
    return {true, {}};
}

VocalInstallResult VocalSeparationInstaller::deleteModelFiles(
    const VocalModelCard& model, const QString& modelsRoot)
{
    if (model.id.isEmpty() || QFileInfo(model.id).fileName() != model.id) {
        return fail(QStringLiteral("Invalid model identifier"));
    }
    const QString directory = QDir(modelsRoot).filePath(model.id);
    const auto kind = vocal_separation_paths::safePathKind(directory);
    if (kind == vocal_separation_paths::SafePathKind::Missing) return {true, {}};
    if (!vocal_separation_paths::safeExistingDirectory(modelsRoot)
        || !vocal_separation_paths::safeExistingPathWithin(
            directory, modelsRoot,
            vocal_separation_paths::SafePathKind::Directory)) {
        return fail(QStringLiteral("Refusing to delete an unsafe model directory"));
    }
    QSet<QString> allowedNames;
    for (const VocalDownloadFile& file : model.files) {
        allowedNames.insert(file.fileName);
        allowedNames.insert(file.fileName + QStringLiteral(".part"));
    }
    if (!vocal_separation_paths::validateFlatDirectory(directory, allowedNames)) {
        return fail(QStringLiteral("Refusing to delete a non-flat or unexpected model directory"));
    }
    if (!vocal_separation_paths::removeKnownFlatDirectory(directory, allowedNames)) {
        return fail(QStringLiteral("Cannot delete model files"));
    }
    return {true, {}};
}

VocalInstallResult VocalSeparationInstaller::installDirectMlRuntime(
    const QString& nupkgPath, const QString& runtimeRoot,
    const std::shared_ptr<std::atomic_bool>& cancellation)
{
    QLockFile installationLock(runtimeRoot + QStringLiteral(".install.lock"));
    if (!QDir().mkpath(QFileInfo(runtimeRoot).absolutePath()) || !installationLock.tryLock())
        return fail(QStringLiteral("ONNX Runtime installation target is locked"));
    const auto cancelledResult = [] {
        return fail(QStringLiteral("ONNX Runtime installation was cancelled"));
    };
    if (isCancelled(cancellation)) return cancelledResult();
    const VocalRuntimePackage package = VocalSeparationCatalog::nativeRuntime();
    VocalDownloadFile archive;
    archive.fileName = QStringLiteral("runtime.nupkg");
    archive.bytes = package.bytes;
    archive.sha256 = package.sha256;
    if (!fileMatches(archive, nupkgPath, cancellation)) {
        if (isCancelled(cancellation)) return cancelledResult();
        return fail(QStringLiteral("ONNX Runtime package did not match its pinned SHA-256 or size"));
    }

    const QString versionedRoot = runtimeVersionDirectory(runtimeRoot);
    const QString stagingRoot = versionedRoot + QStringLiteral(".staging");
    const auto versionKind = vocal_separation_paths::safePathKind(versionedRoot);
    if (versionKind != vocal_separation_paths::SafePathKind::Missing) {
        if (!runtimeDirectoryIsVerified(
                versionedRoot, package.sha256, cancellation)) {
            return fail(QStringLiteral("Existing ONNX Runtime failed integrity verification"));
        }
        return {true, {}};
    }
    const QSet<QString> allowedNames = runtimeAllowedNames();
    if (!vocal_separation_paths::removeKnownFlatDirectory(
            stagingRoot, allowedNames)) {
        return fail(QStringLiteral("Refusing to clean an unsafe ONNX Runtime staging directory"));
    }
    if (isCancelled(cancellation)) return cancelledResult();
    if (!QDir().mkpath(stagingRoot)) {
        return fail(QStringLiteral("Cannot create ONNX Runtime staging directory"));
    }
    const auto cleanupStaging = [&] {
        return vocal_separation_paths::removeKnownFlatDirectory(
            stagingRoot, allowedNames);
    };
    if (isCancelled(cancellation)) {
        cleanupStaging();
        return cancelledResult();
    }

#ifndef Q_OS_MACOS
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "$zip=[IO.Compression.ZipFile]::OpenRead($env:AGPLAYER_RUNTIME_ARCHIVE); "
        "try { $names=@('onnxruntime.dll','onnxruntime_providers_shared.dll'); "
        "foreach($name in $names) { $entry=$zip.GetEntry('runtimes/win-x64/native/'+$name); "
        "if($null -eq $entry){throw 'Missing native runtime file: '+$name}; "
        "$target=[IO.Path]::Combine($env:AGPLAYER_RUNTIME_STAGING,$name); "
        "$input=$entry.Open(); try { $output=[IO.File]::Open($target,[IO.FileMode]::CreateNew); try {$input.CopyTo($output)} finally {$output.Dispose()} } finally {$input.Dispose()} } "
        "} finally { $zip.Dispose() }");
#endif
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("AGPLAYER_RUNTIME_ARCHIVE"), nupkgPath);
    environment.insert(QStringLiteral("AGPLAYER_RUNTIME_STAGING"), stagingRoot);
    process.setProcessEnvironment(environment);
#ifdef Q_OS_MACOS
    // Extract only the known regular file into the verified empty staging
    // directory; archive paths and symlinks are never materialized.
#ifdef Q_PROCESSOR_ARM_64
    const QString member = QStringLiteral("runtimes/osx-arm64/native/libonnxruntime.dylib");
#else
    const QString member = QStringLiteral("runtimes/osx-x64/native/libonnxruntime.dylib");
#endif
    process.setStandardOutputFile(QDir(stagingRoot).filePath(QStringLiteral("libonnxruntime.dylib")));
    process.start(QStringLiteral("/usr/bin/unzip"), {QStringLiteral("-p"), nupkgPath, member});
#else
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-Command"), script});
#endif
    process.closeWriteChannel();
    QElapsedTimer extractionTimer;
    extractionTimer.start();
    bool processFinished = false;
    while (extractionTimer.elapsed() < 60'000) {
        if (isCancelled(cancellation)) {
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(1'000);
            }
            cleanupStaging();
            return cancelledResult();
        }
        if (process.waitForFinished(50)
            || process.state() == QProcess::NotRunning) {
            processFinished = true;
            break;
        }
    }
    if (!processFinished && process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1'000);
    }
    if (!processFinished || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        const QString processError = QString::fromLocal8Bit(
            process.readAllStandardError().right(4'096)).trimmed();
        cleanupStaging();
        return fail(QStringLiteral("Cannot extract pinned ONNX Runtime: %1")
                        .arg(processError));
    }
    for (const VocalDownloadFile& file : runtimeFiles()) {
        if (isCancelled(cancellation)) {
            cleanupStaging();
            return cancelledResult();
        }
        if (!fileMatches(file, QDir(stagingRoot).filePath(file.fileName),
                         cancellation)) {
            cleanupStaging();
            if (isCancelled(cancellation)) return cancelledResult();
            return fail(QStringLiteral("Extracted ONNX Runtime file failed integrity verification"));
        }
    }
    if (isCancelled(cancellation)) {
        cleanupStaging();
        return cancelledResult();
    }
    QFile stagingMarker(QDir(stagingRoot).filePath(QStringLiteral("runtime.sha256")));
    if (!stagingMarker.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || stagingMarker.write(package.sha256.toLatin1())
               != static_cast<qint64>(package.sha256.size())) {
        cleanupStaging();
        return fail(QStringLiteral("Cannot write ONNX Runtime verification marker"));
    }
    stagingMarker.close();
    if (isCancelled(cancellation)) {
        cleanupStaging();
        return cancelledResult();
    }
    if (!vocal_separation_paths::validateFlatDirectory(
            stagingRoot, allowedNames)) {
        cleanupStaging();
        return fail(QStringLiteral("ONNX Runtime staging directory contained unexpected entries"));
    }
    if (!QDir().rename(stagingRoot, versionedRoot)) {
        cleanupStaging();
        return fail(QStringLiteral("Cannot atomically activate ONNX Runtime"));
    }
    if (isCancelled(cancellation)) {
        vocal_separation_paths::removeKnownFlatDirectory(
            versionedRoot, allowedNames);
        return cancelledResult();
    }
    if (!runtimeDirectoryIsVerified(
            versionedRoot, package.sha256, cancellation)) {
        vocal_separation_paths::removeKnownFlatDirectory(
            versionedRoot, allowedNames);
        return fail(QStringLiteral("Activated ONNX Runtime failed final integrity verification"));
    }
    return {true, {}};
}

bool VocalSeparationInstaller::runtimeDirectoryIsVerified(
    const QString& runtimeDirectory, const QString& expectedArchiveSha256,
    const std::shared_ptr<std::atomic_bool>& cancellation)
{
    if (isCancelled(cancellation) || !isSha256(expectedArchiveSha256)) {
        return false;
    }
    const QSet<QString> allowedNames = runtimeAllowedNames();
    if (!vocal_separation_paths::safeExistingDirectory(runtimeDirectory)
        || !vocal_separation_paths::validateFlatDirectory(
            runtimeDirectory, allowedNames)) {
        return false;
    }
    QFile marker(QDir(runtimeDirectory).filePath(QStringLiteral("runtime.sha256")));
    if (!vocal_separation_paths::openRegularFileForReadWithin(
            &marker, runtimeDirectory)
        || marker.size() > 128
        || QString::fromLatin1(marker.read(129)).trimmed() != expectedArchiveSha256) {
        return false;
    }
    for (const VocalDownloadFile& file : runtimeFiles()) {
        if (isCancelled(cancellation)
            || !fileMatches(file,
                            QDir(runtimeDirectory).filePath(file.fileName),
                            cancellation)) {
            return false;
        }
    }
    return !isCancelled(cancellation);
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
    m_destinationLock = std::make_unique<QLockFile>(destination + QStringLiteral(".download.lock"));
    if (!m_destinationLock->tryLock()) {
        m_destinationLock.reset();
        finishFailure(QStringLiteral("Download destination is locked"));
        return;
    }
    if (!m_state.start()) {
        m_destinationLock.reset();
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
    if (QFileInfo::exists(m_destination)) verifyExistingDestination(m_operation);
    else issueRequest(m_operation);
}

void VocalSeparationDownloader::verifyExistingDestination(quint64 operation)
{
    if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading) return;
    const auto file = m_file;
    const auto destination = m_destination;
    m_state.verify();
    setState(VocalDownloadState::Verifying);
    auto* watcher = new QFutureWatcher<bool>(this);
    m_verificationWatcher = watcher;
    connect(watcher, &QFutureWatcher<bool>::finished, this,
        [this, watcher, operation] {
            const bool verified = watcher->result();
            watcher->deleteLater();
            if (m_verificationWatcher == watcher) m_verificationWatcher = nullptr;
            if (operation != m_operation || m_state.state() != VocalDownloadState::Verifying) return;
            if (!verified) {
                finishFailure(QStringLiteral("Existing download failed integrity verification; refusing to overwrite"));
                return;
            }
            m_state.complete();
            setState(VocalDownloadState::Complete);
            emit finished(VocalInstallResult{true, {}});
        });
    watcher->setFuture(QtConcurrent::run([file, destination] {
        return VocalSeparationInstaller::isVerifiedFile(file, destination);
    }));
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
    if (QFileInfo::exists(m_destination)) verifyExistingDestination(m_operation);
    else issueRequest(m_operation);
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
    m_verificationWatcher = nullptr;
    setState(VocalDownloadState::Cancelled);
}

void VocalSeparationDownloader::issueRequest(quint64 operation)
{
    if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading
        || m_reply != nullptr) {
        return;
    }
    const QString partialPath = VocalSeparationInstaller::partPath(m_destination);
    if (!isSafePartialDownloadPath(partialPath)) {
        finishFailure(QStringLiteral("Unsafe partial download path"));
        return;
    }
    m_resumeOffset = VocalSeparationInstaller::resumeOffset(m_destination);
    if (m_resumeOffset > m_file.bytes) {
        QFile::remove(partialPath);
        m_resumeOffset = 0;
    }
    if (m_resumeOffset == m_file.bytes) {
        verifyAndActivate(operation);
        return;
    }
    if (!VocalSeparationInstaller::hasDiskSpace(m_destination,
                                                m_file.bytes - m_resumeOffset)) {
        finishFailure(QStringLiteral("Insufficient disk space for download"));
        return;
    }
    QNetworkRequest request(m_file.url);
    request.setTransferTimeout(15'000); // No incoming data: retry, then try the backup route.
    if (m_resumeOffset > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_resumeOffset) + "-");
    }
    QNetworkReply* const reply = m_network->get(request);
    reply->setReadBufferSize(kDownloadReadBufferSize);
    m_reply = reply;
    m_acceptResponseBody = !m_file.url.scheme().startsWith(QStringLiteral("http"),
                                                            Qt::CaseInsensitive);
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
                return;
            }
            m_acceptResponseBody = true;
        } else if (m_resumeOffset > 0 && status == 200) {
            const QString partialPath = VocalSeparationInstaller::partPath(m_destination);
            if (!isSafePartialDownloadPath(partialPath)
                || !QFile::remove(partialPath)) {
                finishFailure(QStringLiteral("Unsafe partial download path"));
                return;
            }
            m_resumeOffset = 0;
            m_acceptResponseBody = true;
        } else if (m_resumeOffset == 0 && status == 200) {
            m_acceptResponseBody = true;
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, operation, reply] {
        if (operation != m_operation || m_state.state() != VocalDownloadState::Downloading) {
            reply->readAll();
            return;
        }
        if (!m_acceptResponseBody) {
            reply->readAll();
            return;
        }
        const QString partialPath = VocalSeparationInstaller::partPath(m_destination);
        if (!isSafePartialDownloadPath(partialPath)) {
            finishFailure(QStringLiteral("Unsafe partial download path"));
            return;
        }
        QFile part(partialPath);
        if (!vocal_separation_paths::openRegularFileForAppend(&part)) {
            finishFailure(QStringLiteral("Cannot write partial download"));
            return;
        }
        qint64 currentSize = part.size();
        while (reply->bytesAvailable() > 0) {
            if (currentSize < 0 || currentSize > m_file.bytes) {
                part.close();
                finishFailure(QStringLiteral("Download exceeded expected size"));
                return;
            }
            const qint64 remaining = m_file.bytes - currentSize;
            const qint64 readLimit = remaining >= kDownloadReadBufferSize
                ? kDownloadReadBufferSize : remaining + 1;
            const QByteArray bytes = reply->read(readLimit);
            if (bytes.isEmpty()) break;
            if (bytes.size() > remaining) {
                part.close();
                finishFailure(QStringLiteral("Download exceeded expected size"));
                return;
            }
            if (part.write(bytes) != bytes.size()) {
                part.close();
                finishFailure(QStringLiteral("Cannot write complete partial download"));
                return;
            }
            currentSize += bytes.size();
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
            finishFailure(QStringLiteral("Download failed after retries (%1, HTTP %2, network %3): %4")
                .arg(reply->url().host())
                .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                .arg(int(networkError)).arg(reply->errorString()));
            return;
        }
        verifyAndActivate(operation);
    });
}

void VocalSeparationDownloader::verifyAndActivate(quint64 operation)
{
    if (operation != m_operation || m_verificationWatcher != nullptr) return;
    m_state.verify();
    setState(VocalDownloadState::Verifying);
    const VocalDownloadFile file = m_file;
    const QString destination = m_destination;
    const QString part = VocalSeparationInstaller::partPath(destination);
    auto* const watcher = new QFutureWatcher<bool>(this);
    m_verificationWatcher = watcher;
    connect(watcher, &QFutureWatcher<bool>::finished,
            this, [this, watcher, operation, file, destination] {
        const bool verified = watcher->result();
        watcher->deleteLater();
        if (m_verificationWatcher == watcher)
            m_verificationWatcher = nullptr;
        if (operation != m_operation
            || m_state.state() != VocalDownloadState::Verifying) return;
        VocalInstallResult result;
        const QString part = VocalSeparationInstaller::partPath(destination);
        if (!verified) {
            QFile::remove(part);
            result = {false,
                QStringLiteral("Downloaded file did not match its expected SHA-256 or size")};
        } else if (QFileInfo::exists(destination)) {
            result = {false,
                QStringLiteral("Refusing to replace an existing file")};
        } else if (QFileInfo(part).size() != file.bytes
                   || !QDir().mkpath(QFileInfo(destination).absolutePath())
                   || !QFile::rename(part, destination)) {
            result = {false,
                QStringLiteral("Cannot atomically activate verified download")};
        } else {
            auto* const finalWatcher = new QFutureWatcher<bool>(this);
            m_verificationWatcher = finalWatcher;
            connect(finalWatcher, &QFutureWatcher<bool>::finished,
                    this, [this, finalWatcher, operation, destination] {
                const bool finalVerified = finalWatcher->result();
                finalWatcher->deleteLater();
                if (m_verificationWatcher == finalWatcher)
                    m_verificationWatcher = nullptr;
                if (operation != m_operation
                    || m_state.state() != VocalDownloadState::Verifying) return;
                if (!finalVerified) {
                    QFile::remove(destination);
                    m_state.fail();
                    setState(VocalDownloadState::Failed);
                    const VocalInstallResult failed = {
                        false,
                        QStringLiteral("Activated download failed final integrity verification")};
                    m_error = failed.error;
                    emit finished(failed);
                    return;
                }
                m_state.complete();
                setState(VocalDownloadState::Complete);
                emit finished({true, {}});
            });
            finalWatcher->setFuture(QtConcurrent::run(
                [file, destination] {
                    return VocalSeparationInstaller::isVerifiedFile(file, destination);
                }));
            return;
        }
        if (result.ok) {
            m_state.complete();
            setState(VocalDownloadState::Complete);
        } else {
            m_state.fail();
            setState(VocalDownloadState::Failed);
        }
        emit finished(result);
    });
    m_verificationWatcher->setFuture(QtConcurrent::run(
        [file, part] {
            return VocalSeparationInstaller::isVerifiedFile(file, part);
        }));
}

void VocalSeparationDownloader::setState(VocalDownloadState state)
{
    if (state == VocalDownloadState::Complete || state == VocalDownloadState::Failed
        || state == VocalDownloadState::Cancelled) m_destinationLock.reset();
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
