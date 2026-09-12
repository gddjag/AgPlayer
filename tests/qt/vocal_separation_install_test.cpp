#include "vocal_separation_catalog.hpp"
#include "vocal_separation_installer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <filesystem>

class VocalSeparationInstallTest final : public QObject {
    Q_OBJECT

private slots:
    void verifiedDestinationIsReusedWithoutNetwork();
    void independentDownloadersCannotWriteTheSameDestination();
    void exposesPinnedApprovedCatalog();
    void domesticMirrorOnlyRewritesSupportedHuggingFaceDownloads();
    void exposesDownloadStateTransitions();
    void installerOwnsCanonicalRuntimePaths();
    void rejectsUnsafeCustomManifests();
    void resumesAndActivatesOnlyVerifiedFiles();
    void downloaderSignalsInitialState();
    void completePartVerificationIsAsynchronous();
    void downloaderRehashesActivatedDestinationBeforeCompletion();
    void modelDeletionRejectsUnsafeFlatDirectoryWithoutPartialRemoval();
    void overlappingStartPreservesActiveTransfer();
    void cancelledRetryDoesNotReconnect();
    void runtimeVerificationRejectsChangedNativeFile();
    void cancelledRuntimeExtractionStopsAndCleansKnownStaging();
    void runtimeExtractionRefusesUnsafePreexistingStaging();
    void downloaderRefusesUnsafePartialFile();
    void downloaderRejectsResponseLargerThanManifest();
    void downloaderCompletesLargeBoundedResponses();
    void httpResumeValidatesRangeAndFallback();
    void pauseAndCancelPreventBackoffReconnect();
};

void VocalSeparationInstallTest::domesticMirrorOnlyRewritesSupportedHuggingFaceDownloads()
{
    const QUrl huggingFace(QStringLiteral(
        "https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/main/model.onnx"));
    const QUrl mirrored = vocalDomesticMirrorUrl(huggingFace);
    QCOMPARE(mirrored.host(), QStringLiteral("hf-mirror.com"));
    QCOMPARE(mirrored.path(), huggingFace.path());
    QVERIFY(vocalDomesticMirrorUrl(QUrl(QStringLiteral(
        "https://github.com/TRvlvr/model_repo/releases/download/model.onnx"))).isEmpty());
}

namespace {

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject validMdxManifest()
{
    return {
        {QStringLiteral("id"), QStringLiteral("local-mdx")},
        {QStringLiteral("family"), QStringLiteral("MDX")},
        {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals"),
                                              QStringLiteral("instrumental")}},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
            {QStringLiteral("name"), QStringLiteral("model.onnx")},
            {QStringLiteral("bytes"), 4},
            {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("shape"), QJsonArray{1, 2, 3072}}
        }}}
    };
}

VocalDownloadFile downloadFileFor(const QByteArray& payload, const QUrl& url,
                                  const QString& name = QStringLiteral("model.onnx"))
{
    VocalDownloadFile file;
    file.fileName = name;
    file.url = url;
    file.bytes = payload.size();
    file.sha256 = sha256(payload);
    return file;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

void disableProxyForLocalTests()
{
    qputenv("http_proxy", "");
    qputenv("https_proxy", "");
    qputenv("HTTP_PROXY", "");
    qputenv("HTTPS_PROXY", "");
}

class LocalHttpServer final : public QObject {
public:
    enum class Mode { Valid206, Wrong206, Full200, Http500 };
    LocalHttpServer(QByteArray body, Mode mode) : m_body(std::move(body)), m_mode(mode)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                ++m_connections;
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request = QByteArray{}]() mutable {
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n")) return;
                    const int at = request.toLower().indexOf("range: bytes=");
                    qint64 offset = 0;
                    if (at >= 0) {
                        const int begin = at + 13;
                        offset = request.mid(begin, request.indexOf('-', begin) - begin).toLongLong();
                        m_range = request.mid(at, request.indexOf("\r\n", at) - at);
                    }
                    if (m_mode == Mode::Http500) {
                        socket->write("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 11\r\nConnection: close\r\n\r\nerror-body!");
                        socket->flush(); socket->disconnectFromHost(); return;
                    }
                    const bool partial = m_mode != Mode::Full200;
                    const qint64 servedOffset = m_mode == Mode::Wrong206 ? offset + 1 : offset;
                    const QByteArray body = partial ? m_body.mid(servedOffset) : m_body;
                    QByteArray response = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                    if (partial) response += "Content-Range: bytes " + QByteArray::number(servedOffset)
                        + "-" + QByteArray::number(m_body.size() - 1) + "/" + QByteArray::number(m_body.size()) + "\r\n";
                    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                    socket->write(response); socket->flush(); socket->disconnectFromHost();
                });
            }
        });
    }
    bool start() { return m_server.listen(QHostAddress::LocalHost); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/model.onnx").arg(m_server.serverPort())); }
    QByteArray range() const { return m_range; }
    int connections() const { return m_connections; }
private:
    QTcpServer m_server; QByteArray m_body; Mode m_mode; QByteArray m_range; int m_connections = 0;
};

} // namespace

void VocalSeparationInstallTest::exposesPinnedApprovedCatalog()
{
    const QList<VocalModelCard> models = VocalSeparationCatalog::models();
    QCOMPARE(models.size(), 3);
    QCOMPARE(models.at(0).id, QStringLiteral("uvr-mdxnet-kara"));
    QCOMPARE(models.at(0).files.at(0).bytes, qint64{29'704'436});
    QCOMPARE(models.at(0).files.at(0).sha256,
             QStringLiteral("e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63"));
    QCOMPARE(models.at(1).files.at(0).url.toString(),
             QStringLiteral("https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-MDX-NET-Inst_HQ_3.onnx"));
    QCOMPARE(models.at(2).family, VocalModelFamily::Demucs);
    QCOMPARE(models.at(2).files.size(), 4);
    QCOMPARE(models.at(2).files.at(3).bytes, qint64{165'612'636});
    QVERIFY(models.at(2).provenance.contains(QStringLiteral("Meta Demucs")));
    QVERIFY(models.at(2).provenance.contains(QStringLiteral("StemSplit")));

    const VocalRuntimePackage runtime = VocalSeparationCatalog::directMlRuntime();
    QCOMPARE(runtime.id, QStringLiteral("onnxruntime-directml-1.24.4"));
    QCOMPARE(runtime.bytes, qint64{12'458'649});
    QCOMPARE(runtime.sha256,
             QStringLiteral("57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681"));
}

void VocalSeparationInstallTest::exposesDownloadStateTransitions()
{
    VocalDownloadStateMachine state;
    QCOMPARE(state.state(), VocalDownloadState::Idle);
    QVERIFY(state.start());
    QCOMPARE(state.state(), VocalDownloadState::Downloading);
    QVERIFY(state.pause());
    QCOMPARE(state.state(), VocalDownloadState::Paused);
    QVERIFY(state.resume());
    QCOMPARE(state.state(), VocalDownloadState::Downloading);
    state.cancel();
    QCOMPARE(state.state(), VocalDownloadState::Cancelled);
    QVERIFY(!state.resume());
}

void VocalSeparationInstallTest::installerOwnsCanonicalRuntimePaths()
{
    const QString root = QDir::cleanPath(QStringLiteral("C:/portable/runtime"));
    const QString version = VocalSeparationInstaller::runtimeVersionDirectory(root);
    QCOMPARE(version, QDir(root).filePath(
        VocalSeparationCatalog::nativeRuntime().id));
#ifdef Q_OS_MACOS
    QCOMPARE(VocalSeparationInstaller::runtimeLibraryPath(root),
             QDir(version).filePath(QStringLiteral("libonnxruntime.dylib")));
    QCOMPARE(VocalSeparationCatalog::nativeRuntime().bytes, qint64{103'790'494});
#else
    QCOMPARE(VocalSeparationInstaller::runtimeLibraryPath(root),
             QDir(version).filePath(QStringLiteral("onnxruntime.dll")));
#endif
}

void VocalSeparationInstallTest::rejectsUnsafeCustomManifests()
{
    const CustomManifestValidationResult valid = validateCustomModelManifest(validMdxManifest());
    QVERIFY2(valid.accepted, qPrintable(valid.error));

    QJsonObject traversal = validMdxManifest();
    QJsonArray files = traversal.value(QStringLiteral("files")).toArray();
    QJsonObject file = files.first().toObject();
    file.insert(QStringLiteral("name"), QStringLiteral("../model.onnx"));
    files.replace(0, file);
    traversal.insert(QStringLiteral("files"), files);
    QVERIFY(!validateCustomModelManifest(traversal).accepted);

    QJsonObject unsafe = validMdxManifest();
    unsafe.insert(QStringLiteral("customOps"), true);
    QVERIFY(!validateCustomModelManifest(unsafe).accepted);

    QJsonObject badStem = validMdxManifest();
    badStem.insert(QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals"), QStringLiteral("guitar")});
    QVERIFY(!validateCustomModelManifest(badStem).accepted);

    QJsonObject script = validMdxManifest();
    files = script.value(QStringLiteral("files")).toArray();
    file = files.first().toObject();
    file.insert(QStringLiteral("name"), QStringLiteral("install.ps1"));
    files.replace(0, file);
    script.insert(QStringLiteral("files"), files);
    QVERIFY(!validateCustomModelManifest(script).accepted);

    QJsonObject missingHash = validMdxManifest();
    files = missingHash.value(QStringLiteral("files")).toArray();
    file = files.first().toObject();
    file.remove(QStringLiteral("sha256"));
    files.replace(0, file);
    missingHash.insert(QStringLiteral("files"), files);
    QVERIFY(!validateCustomModelManifest(missingHash).accepted);

    QJsonObject wrongShape = validMdxManifest();
    files = wrongShape.value(QStringLiteral("files")).toArray();
    file = files.first().toObject();
    file.insert(QStringLiteral("shape"), QJsonArray{1, 3, 1});
    files.replace(0, file);
    wrongShape.insert(QStringLiteral("files"), files);
    QVERIFY(!validateCustomModelManifest(wrongShape).accepted);
}

void VocalSeparationInstallTest::resumesAndActivatesOnlyVerifiedFiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("模型安装"));
    QVERIFY(QDir().mkpath(root));

    const QByteArray payload("verified-model-payload");
    VocalDownloadFile file;
    file.fileName = QStringLiteral("model.onnx");
    file.bytes = payload.size();
    file.sha256 = sha256(payload);

    const QString destination = QDir(root).filePath(file.fileName);
    QCOMPARE(VocalSeparationInstaller::resumeOffset(destination), qint64{0});
    QFile part(VocalSeparationInstaller::partPath(destination));
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.left(7)), qint64{7});
    part.close();
    QCOMPARE(VocalSeparationInstaller::resumeOffset(destination), qint64{7});

    QVERIFY(!VocalSeparationInstaller::activateVerifiedPart(file, destination).ok);
    QVERIFY(QFileInfo::exists(VocalSeparationInstaller::partPath(destination)));
    QVERIFY(!QFileInfo::exists(destination));

    const QString wrongHashDestination = QDir(root).filePath(QStringLiteral("wrong-hash.onnx"));
    QFile wrongHashPart(VocalSeparationInstaller::partPath(wrongHashDestination));
    QVERIFY(wrongHashPart.open(QIODevice::WriteOnly));
    QCOMPARE(wrongHashPart.write(payload), qint64{payload.size()});
    wrongHashPart.close();
    VocalDownloadFile wrongHash = file;
    wrongHash.sha256 = QString(64, QLatin1Char('0'));
    QVERIFY(!VocalSeparationInstaller::activateVerifiedPart(wrongHash,
                                                            wrongHashDestination).ok);
    QVERIFY(QFileInfo::exists(VocalSeparationInstaller::partPath(wrongHashDestination)));
    QVERIFY(!QFileInfo::exists(wrongHashDestination));

    QVERIFY(part.open(QIODevice::Append));
    QCOMPARE(part.write(payload.mid(7)), qint64{payload.size() - 7});
    part.close();
    const VocalInstallResult result = VocalSeparationInstaller::activateVerifiedPart(file, destination);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(VocalSeparationInstaller::partPath(destination)));
    QFile installed(destination);
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), payload);
}

void VocalSeparationInstallTest::verifiedDestinationIsReusedWithoutNetwork()
{
    QTemporaryDir temporary;
    const QByteArray payload("verified-cached-runtime");
    const QString destination = temporary.filePath("runtime.nupkg");
    QVERIFY(writeFile(destination, payload));
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    downloader.start(downloadFileFor(payload, QUrl(QStringLiteral("http://127.0.0.1:%1/archive").arg(server.serverPort()))), destination);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || server.hasPendingConnections(), 3000);
    QVERIFY2(!server.hasPendingConnections(), "A verified archive must be reused after an interrupted installation");
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.first().first().value<VocalInstallResult>().ok);
    QFile file(destination); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), payload);
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

bool createDirectoryLink(const QString& linkPath, const QString& targetPath)
{
    std::error_code error;
    std::filesystem::create_directory_symlink(
        nativePath(targetPath), nativePath(linkPath), error);
#ifdef Q_OS_WIN
    if (error) {
        QProcess process;
        process.setProgram(QStringLiteral("cmd.exe"));
        process.setArguments(
            {QStringLiteral("/d"), QStringLiteral("/c"), QStringLiteral("mklink"),
             QStringLiteral("/J"), QDir::toNativeSeparators(linkPath),
             QDir::toNativeSeparators(targetPath)});
        process.start();
        if (process.waitForFinished(5'000) && process.exitCode() == 0) {
            error.clear();
        }
    }
#endif
    return !error;
}

class DirectoryLinkGuard final {
public:
    explicit DirectoryLinkGuard(QString path) : m_path(std::move(path)) {}
    ~DirectoryLinkGuard()
    {
#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(m_path);
        RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()));
#else
        QFile::remove(m_path);
#endif
    }

private:
    QString m_path;
};

void VocalSeparationInstallTest::independentDownloadersCannotWriteTheSameDestination()
{
    QTemporaryDir temporary;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    const QString destination = temporary.filePath("model.onnx");
    const auto file = downloadFileFor("same-model", QUrl(QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort())));
    VocalSeparationDownloader first(&network), second(&network);
    QSignalSpy failed(&second, &VocalSeparationDownloader::finished);
    first.start(file, destination);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> socket(server.nextPendingConnection());
    second.start(file, destination);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.first().first().value<VocalInstallResult>().ok);
    QVERIFY(second.error().contains("locked"));
    QCOMPARE(first.state(), VocalDownloadState::Downloading);
    QVERIFY(!server.hasPendingConnections());
    first.cancel();
}

void VocalSeparationInstallTest::downloaderSignalsInitialState()
{
    const QByteArray payload("range-resume-payload");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source.onnx"));
    QVERIFY(writeFile(source, payload));
    const QString destination = temporary.filePath(QStringLiteral("模型.onnx"));

    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy states(&downloader, &VocalSeparationDownloader::stateChanged);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    downloader.start(downloadFileFor(payload, QUrl::fromLocalFile(source)), destination);
    const bool completed = finished.wait(3'000);
    if (!completed) {
        downloader.cancel();
    }
    QVERIFY(completed);
    QCOMPARE(finished.count(), 1);
    QVERIFY(states.count() >= 3);
    QCOMPARE(states.at(0).at(0).value<VocalDownloadState>(),
             VocalDownloadState::Downloading);
}

void VocalSeparationInstallTest::completePartVerificationIsAsynchronous()
{
    const QByteArray payload(4 * 1024 * 1024, 'v');
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString destination = temporary.filePath(QStringLiteral("model.onnx"));
    QVERIFY(writeFile(VocalSeparationInstaller::partPath(destination), payload));

    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    downloader.start(downloadFileFor(payload, QUrl::fromLocalFile(destination)),
                     destination);

    QCOMPARE(downloader.state(), VocalDownloadState::Verifying);
    QCOMPARE(finished.count(), 0);
    QVERIFY(finished.wait(5'000));
    QCOMPARE(downloader.state(), VocalDownloadState::Complete);
    QVERIFY(QFileInfo::exists(destination));
}

void VocalSeparationInstallTest::downloaderRehashesActivatedDestinationBeforeCompletion()
{
    const QByteArray payload(64 * 1024 * 1024, 'r');
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString destination = temporary.filePath(QStringLiteral("model.onnx"));
    QVERIFY(writeFile(VocalSeparationInstaller::partPath(destination), payload));

    QFileSystemWatcher directoryWatcher;
    QVERIFY(directoryWatcher.addPath(temporary.path()));
    bool replacedAfterActivation = false;
    connect(&directoryWatcher, &QFileSystemWatcher::directoryChanged,
            this, [&](const QString&) {
        if (replacedAfterActivation || !QFileInfo::exists(destination)) return;
        replacedAfterActivation = writeFile(destination, QByteArray("tampered"));
    });

    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    downloader.start(downloadFileFor(payload, QUrl::fromLocalFile(destination)),
                     destination);

    QVERIFY(finished.wait(10'000));
    QVERIFY(replacedAfterActivation);
    QCOMPARE(downloader.state(), VocalDownloadState::Failed);
}

void VocalSeparationInstallTest::modelDeletionRejectsUnsafeFlatDirectoryWithoutPartialRemoval()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const VocalModelCard model = VocalSeparationCatalog::models().first();
    const QString modelsRoot = temporary.filePath(QStringLiteral("models"));
    const QString modelRoot = QDir(modelsRoot).filePath(model.id);
    QVERIFY(QDir().mkpath(modelRoot));
    const QString expected = QDir(modelRoot).filePath(model.files.first().fileName);
    const QString partial = VocalSeparationInstaller::partPath(expected);
    const QString unexpected = QDir(modelRoot).filePath(QStringLiteral("keep-me.txt"));
    QVERIFY(writeFile(expected, QByteArray("installed")));
    QVERIFY(writeFile(partial, QByteArray("partial")));
    QVERIFY(writeFile(unexpected, QByteArray("sentinel")));

    const VocalInstallResult rejected =
        VocalSeparationInstaller::deleteModelFiles(model, modelsRoot);
    QVERIFY(!rejected.ok);
    QVERIFY(QFileInfo::exists(expected));
    QVERIFY(QFileInfo::exists(partial));
    QVERIFY(QFileInfo::exists(unexpected));

    QVERIFY(QFile::remove(unexpected));
    const VocalInstallResult removed =
        VocalSeparationInstaller::deleteModelFiles(model, modelsRoot);
    QVERIFY2(removed.ok, qPrintable(removed.error));
    QVERIFY(!QFileInfo::exists(modelRoot));
}

void VocalSeparationInstallTest::overlappingStartPreservesActiveTransfer()
{
    const QByteArray activePayload("active-transfer");
    const QByteArray rejectedPayload("rejected-transfer");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source.onnx"));
    QVERIFY(writeFile(source, activePayload));
    const QString activeDestination = temporary.filePath(QStringLiteral("active.onnx"));
    const QString rejectedDestination = temporary.filePath(QStringLiteral("rejected.onnx"));

    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    downloader.start(downloadFileFor(activePayload, QUrl::fromLocalFile(source), QStringLiteral("active.onnx")),
                     activeDestination);
    downloader.start(downloadFileFor(rejectedPayload, QUrl::fromLocalFile(source), QStringLiteral("rejected.onnx")),
                     rejectedDestination);
    const bool completed = finished.wait(3'000);
    if (!completed) {
        downloader.cancel();
    }
    QVERIFY(completed);
    QCOMPARE(finished.count(), 1);
    QVERIFY(QFileInfo::exists(activeDestination));
    QVERIFY(!QFileInfo::exists(rejectedDestination));
    QVERIFY(!QFileInfo::exists(VocalSeparationInstaller::partPath(rejectedDestination)));
}

void VocalSeparationInstallTest::cancelledRetryDoesNotReconnect()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    const QByteArray expectedPayload("never-arrives");
    downloader.start(downloadFileFor(expectedPayload, QUrl(QStringLiteral("http://127.0.0.1:1/model.onnx"))),
                     temporary.filePath(QStringLiteral("cancelled.onnx")));
    QTest::qWait(100);
    downloader.cancel();
    QTest::qWait(750);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("cancelled.onnx"))));
    QCOMPARE(downloader.state(), VocalDownloadState::Cancelled);
}

void VocalSeparationInstallTest::runtimeVerificationRejectsChangedNativeFile()
{
    const QString packagePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation)).filePath(QStringLiteral("agplayer-ort-1.24.4.nupkg"));
    if (!QFileInfo(packagePath).isFile()) {
        QSKIP("Pinned DirectML archive fixture is unavailable", "");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const VocalInstallResult installed = VocalSeparationInstaller::installDirectMlRuntime(
        packagePath, temporary.path());
    QVERIFY2(installed.ok, qPrintable(installed.error));
    const QString runtime = QDir(temporary.path()).filePath(
        QStringLiteral("onnxruntime-directml-1.24.4"));
    QVERIFY(VocalSeparationInstaller::runtimeDirectoryIsVerified(
        runtime, VocalSeparationCatalog::directMlRuntime().sha256));
    const QString unexpected = QDir(runtime).filePath(QStringLiteral("unexpected.dll"));
    QVERIFY(writeFile(unexpected, QByteArray("unexpected")));
    QVERIFY(!VocalSeparationInstaller::runtimeDirectoryIsVerified(
        runtime, VocalSeparationCatalog::directMlRuntime().sha256));
    QVERIFY(QFile::remove(unexpected));
    QVERIFY(VocalSeparationInstaller::runtimeDirectoryIsVerified(
        runtime, VocalSeparationCatalog::directMlRuntime().sha256));
    QVERIFY(writeFile(QDir(runtime).filePath(QStringLiteral("onnxruntime.dll")), QByteArray()));
    QVERIFY(!VocalSeparationInstaller::runtimeDirectoryIsVerified(runtime,
                                                                   VocalSeparationCatalog::directMlRuntime().sha256));
}

void VocalSeparationInstallTest::cancelledRuntimeExtractionStopsAndCleansKnownStaging()
{
    const QString packagePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation)).filePath(QStringLiteral("agplayer-ort-1.24.4.nupkg"));
    if (!QFileInfo(packagePath).isFile()) {
        QSKIP("Pinned DirectML archive fixture is unavailable", "");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString versioned = VocalSeparationInstaller::runtimeVersionDirectory(
        temporary.path());
    const QString staging = versioned + QStringLiteral(".staging");
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    QFuture<bool> cancellationFuture = QtConcurrent::run([=] {
        for (int attempt = 0; attempt < 5'000; ++attempt) {
            if (QFileInfo::exists(staging)) {
                cancellation->store(true, std::memory_order_release);
                return true;
            }
            QThread::msleep(1);
        }
        return false;
    });
    QElapsedTimer elapsed;
    elapsed.start();
    const VocalInstallResult installed =
        VocalSeparationInstaller::installDirectMlRuntime(
            packagePath, temporary.path(), cancellation);
    cancellationFuture.waitForFinished();
    QVERIFY(cancellationFuture.result());
    QVERIFY(!installed.ok);
    QVERIFY2(elapsed.elapsed() < 5'000, "Cancellation did not stop extraction promptly");
    QVERIFY(!QFileInfo::exists(versioned));
    QVERIFY(!QFileInfo::exists(staging));
}

void VocalSeparationInstallTest::runtimeExtractionRefusesUnsafePreexistingStaging()
{
    const QString packagePath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation)).filePath(QStringLiteral("agplayer-ort-1.24.4.nupkg"));
    if (!QFileInfo(packagePath).isFile()) {
        QSKIP("Pinned DirectML archive fixture is unavailable", "");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString staging = VocalSeparationInstaller::runtimeVersionDirectory(
        temporary.path()) + QStringLiteral(".staging");
    const QString sentinel = QDir(staging).filePath(
        QStringLiteral("nested/keep-me.txt"));
    QVERIFY(QDir().mkpath(QFileInfo(sentinel).absolutePath()));
    QVERIFY(writeFile(sentinel, QByteArray("sentinel")));

    const VocalInstallResult installed =
        VocalSeparationInstaller::installDirectMlRuntime(
            packagePath, temporary.path());
    QVERIFY(!installed.ok);
    QVERIFY(QFileInfo::exists(sentinel));
}

void VocalSeparationInstallTest::downloaderRefusesUnsafePartialFile()
{
    const QByteArray payload("verified-model-payload");
    QTemporaryDir holder;
    QTemporaryDir external;
    QVERIFY(holder.isValid());
    QVERIFY(external.isValid());
    const QString outside = external.filePath(QStringLiteral("outside.bin"));
    const QByteArray sentinel("outside");
    QVERIFY(writeFile(outside, sentinel));
    const QString destination = holder.filePath(QStringLiteral("model.onnx"));
    const QString partial = VocalSeparationInstaller::partPath(destination);
    if (!createDirectoryLink(partial, external.path())) {
        QSKIP("directory symlink or junction creation is unavailable", "");
    }
    DirectoryLinkGuard linkGuard(partial);

    LocalHttpServer server(payload, LocalHttpServer::Mode::Full200);
    QVERIFY(server.start());
    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    downloader.start(downloadFileFor(payload, server.url()), destination);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3'000);
    const VocalInstallResult result =
        finished.first().first().value<VocalInstallResult>();
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("Unsafe partial download path")),
             qPrintable(result.error));
    QCOMPARE(server.connections(), 0);
    QFile outsideFile(outside);
    QVERIFY(outsideFile.open(QIODevice::ReadOnly));
    QCOMPARE(outsideFile.readAll(), sentinel);
}

void VocalSeparationInstallTest::downloaderRejectsResponseLargerThanManifest()
{
    const QByteArray expected("expected-model");
    const QByteArray oversized = expected + QByteArray("-unexpected-excess");
    LocalHttpServer server(oversized, LocalHttpServer::Mode::Full200);
    QVERIFY(server.start());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    disableProxyForLocalTests();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    VocalSeparationDownloader downloader(&network);
    QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
    const QString destination = temporary.filePath(QStringLiteral("model.onnx"));
    downloader.start(downloadFileFor(expected, server.url()), destination);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3'000);
    const VocalInstallResult result =
        finished.first().first().value<VocalInstallResult>();
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("exceeded expected size")),
             qPrintable(result.error));
    const QString partial = VocalSeparationInstaller::partPath(destination);
    QVERIFY(!QFileInfo::exists(partial)
            || QFileInfo(partial).size() <= expected.size());
}

void VocalSeparationInstallTest::downloaderCompletesLargeBoundedResponses()
{
    QByteArray payload(256 * 1024, 'a');
    for (qsizetype index = 0; index < payload.size(); index += 4096) {
        payload[index] = char('a' + (index / 4096) % 26);
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source.onnx"));
    QVERIFY(writeFile(source, payload));

    const auto verifyDownload = [&](const QUrl& url, const QString& name) {
        disableProxyForLocalTests();
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        VocalSeparationDownloader downloader(&network);
        QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
        const QString destination = temporary.filePath(name);
        downloader.start(downloadFileFor(payload, url, name), destination);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5'000);
        const VocalInstallResult result =
            finished.first().first().value<VocalInstallResult>();
        QVERIFY2(result.ok, qPrintable(result.error));
        QFile downloaded(destination);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), payload);
    };

    verifyDownload(QUrl::fromLocalFile(source), QStringLiteral("local.onnx"));
    LocalHttpServer server(payload, LocalHttpServer::Mode::Full200);
    QVERIFY(server.start());
    verifyDownload(server.url(), QStringLiteral("http.onnx"));
}

void VocalSeparationInstallTest::httpResumeValidatesRangeAndFallback()
{
    const QByteArray payload("http-resume-payload");
    auto run = [&](LocalHttpServer::Mode mode, bool expectSuccess) {
        LocalHttpServer server(payload, mode); QVERIFY(server.start());
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString destination = temp.filePath(QStringLiteral("model.onnx"));
        QVERIFY(writeFile(VocalSeparationInstaller::partPath(destination), payload.left(5)));
        disableProxyForLocalTests(); QNetworkAccessManager network; network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        VocalSeparationDownloader downloader(&network); QSignalSpy finished(&downloader, &VocalSeparationDownloader::finished);
        downloader.start(downloadFileFor(payload, server.url()), destination);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 3'000);
        QCOMPARE(server.range().toLower(), QByteArray("range: bytes=5-"));
        QCOMPARE(QFileInfo::exists(destination), expectSuccess);
        if (expectSuccess) { QFile installed(destination); QVERIFY(installed.open(QIODevice::ReadOnly)); QCOMPARE(installed.readAll(), payload); }
        else QVERIFY(QFileInfo::exists(VocalSeparationInstaller::partPath(destination)));
    };
    run(LocalHttpServer::Mode::Valid206, true);
    run(LocalHttpServer::Mode::Wrong206, false);
    run(LocalHttpServer::Mode::Full200, true);
}

void VocalSeparationInstallTest::pauseAndCancelPreventBackoffReconnect()
{
    const QByteArray payload("retry-payload");
    auto verify = [&](bool pause) {
        LocalHttpServer server(payload, LocalHttpServer::Mode::Http500); QVERIFY(server.start());
        QTemporaryDir temp; QVERIFY(temp.isValid()); disableProxyForLocalTests();
        QNetworkAccessManager network; network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        VocalSeparationDownloader downloader(&network); QSignalSpy replies(&network, &QNetworkAccessManager::finished);
        const QString destination = temp.filePath(QStringLiteral("model.onnx"));
        QVERIFY(writeFile(VocalSeparationInstaller::partPath(destination), payload.left(4)));
        downloader.start(downloadFileFor(payload, server.url()), destination);
        QTRY_COMPARE_WITH_TIMEOUT(server.connections(), 1, 1'000);
        QTRY_COMPARE_WITH_TIMEOUT(replies.count(), 1, 1'000);
        if (pause) downloader.pause(); else downloader.cancel();
        QTest::qWait(750); QCOMPARE(server.connections(), 1);
        QCOMPARE(VocalSeparationInstaller::resumeOffset(destination), qint64{4});
        QFile partial(VocalSeparationInstaller::partPath(destination)); QVERIFY(partial.open(QIODevice::ReadOnly));
        QCOMPARE(partial.readAll(), payload.left(4));
        if (pause) { downloader.resume(); QTRY_VERIFY_WITH_TIMEOUT(server.connections() > 1, 1'000); QCOMPARE(server.range().toLower(), QByteArray("range: bytes=4-")); downloader.cancel(); }
    };
    verify(false); verify(true);
}

QTEST_GUILESS_MAIN(VocalSeparationInstallTest)

#include "vocal_separation_install_test.moc"
