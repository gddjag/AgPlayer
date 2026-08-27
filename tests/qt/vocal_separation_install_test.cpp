#include "vocal_separation_catalog.hpp"
#include "vocal_separation_installer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class VocalSeparationInstallTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesPinnedApprovedCatalog();
    void exposesDownloadStateTransitions();
    void rejectsUnsafeCustomManifests();
    void resumesAndActivatesOnlyVerifiedFiles();
    void downloaderUsesRangeAndSignalsInitialState();
    void overlappingStartPreservesActiveTransfer();
    void cancelledRetryDoesNotReconnect();
    void runtimeVerificationRejectsChangedNativeFile();
};

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

void VocalSeparationInstallTest::downloaderUsesRangeAndSignalsInitialState()
{
    const QByteArray payload("range-resume-payload");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source.onnx"));
    QVERIFY(writeFile(source, payload));
    const QString destination = temporary.filePath(QStringLiteral("模型.onnx"));
    QVERIFY(writeFile(VocalSeparationInstaller::partPath(destination), payload.left(5)));

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
    QVERIFY(QFileInfo::exists(destination));
    QVERIFY(states.count() >= 3);
    QCOMPARE(states.at(0).at(0).value<VocalDownloadState>(),
             VocalDownloadState::Downloading);
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
        QSKIP("Pinned DirectML archive fixture is unavailable");
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
    QVERIFY(writeFile(QDir(runtime).filePath(QStringLiteral("onnxruntime.dll")), QByteArray()));
    QVERIFY(!VocalSeparationInstaller::runtimeDirectoryIsVerified(runtime,
                                                                   VocalSeparationCatalog::directMlRuntime().sha256));
}

QTEST_GUILESS_MAIN(VocalSeparationInstallTest)

#include "vocal_separation_install_test.moc"
