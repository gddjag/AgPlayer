#include "voice_clone_controller.hpp"
#include "voice_clone_package_manager.hpp"
#include "voice_clone_package_manifest.hpp"
#include "voice_clone_worker_protocol.hpp"
#include "voice_clone_host_controller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace agplayer::voice_clone;

namespace {

QJsonObject liveSchema()
{
    auto control = [](const QString& key, const QString& group, const QString& type,
                      const QJsonValue& defaultValue) {
        return QJsonObject{{QStringLiteral("key"), key},
                           {QStringLiteral("label"), key},
                           {QStringLiteral("description"), key + QStringLiteral(" parameter")},
                           {QStringLiteral("group"), group},
                           {QStringLiteral("type"), type},
                           {QStringLiteral("default"), defaultValue},
                           {QStringLiteral("required"), false}};
    };
    QJsonObject seed = control(QStringLiteral("seed"), QStringLiteral("basic"),
                               QStringLiteral("int"), 1);
    seed.insert(QStringLiteral("minimum"), 0);
    seed.insert(QStringLiteral("maximum"), 99);
    QJsonObject mode = control(QStringLiteral("mode"), QStringLiteral("basic"),
                               QStringLiteral("enum"), QStringLiteral("fast"));
    mode.insert(QStringLiteral("options"), QJsonArray{QStringLiteral("fast"), QStringLiteral("hq")});
    QJsonObject style = control(QStringLiteral("style"), QStringLiteral("advanced"),
                                QStringLiteral("string"), QStringLiteral(""));
    style.insert(QStringLiteral("visibleWhen"),
                 QJsonObject{{QStringLiteral("key"), QStringLiteral("mode")},
                             {QStringLiteral("equals"), QStringLiteral("hq")}});
    return {{QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("groups"),
             QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("basic")},
                                    {QStringLiteral("label"), QStringLiteral("Basic")}},
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("advanced")},
                                    {QStringLiteral("label"), QStringLiteral("Advanced")}}}},
            {QStringLiteral("parameters"),
             QJsonArray{seed, mode, style}}};
}

VoiceCloneWorkerMessage responseFor(const VoiceCloneWorkerMessage& request)
{
    VoiceCloneWorkerMessage response;
    response.kind = WorkerMessageKind::Response;
    response.operation = request.operation;
    response.requestId = request.requestId;
    response.adapterId = request.adapterId;
    response.adapterVersion = request.adapterVersion;
    response.protocolVersion = request.protocolVersion;
    switch (request.operation) {
    case WorkerOperation::Hello:
        response.payload = {{QStringLiteral("workerVersion"), QStringLiteral("test-1")}};
        break;
    case WorkerOperation::Capabilities:
        response.payload = {{QStringLiteral("schema"), liveSchema()}};
        break;
    case WorkerOperation::Load:
        response.payload = {{QStringLiteral("loaded"), true}};
        break;
    case WorkerOperation::Generate:
        response.payload = {{QStringLiteral("outputPath"),
                             request.payload.value(QStringLiteral("outputPath"))}};
        break;
    default: break;
    }
    return response;
}

int runWorker(const QStringList& arguments)
{
    const auto valueAfter = [&arguments](const QString& option) {
        const int index = arguments.indexOf(option);
        return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString{};
    };
    const QString socketName = valueAfter(QStringLiteral("--socket"));
    const QString outputRoot = valueAfter(QStringLiteral("--output-root"));
    const QString modelRoot = valueAfter(QStringLiteral("--model-root"));
    QLocalSocket socket;
    socket.connectToServer(socketName);
    if (!socket.waitForConnected(5000)) return 90;

    QByteArray input;
    QString delayed;
    QString cancelFailureTarget;
    VoiceCloneWorkerMessage delayedLoad;
    QObject::connect(&socket, &QLocalSocket::readyRead, &socket, [&] {
        input += socket.readAll();
        while (true) {
            const qsizetype newline = input.indexOf('\n');
            if (newline < 0) break;
            const QByteArray frame = input.left(newline);
            input.remove(0, newline + 1);
            const auto decoded = decodeWorkerMessage(frame, outputRoot);
            if (!decoded.isValid()) {
                QCoreApplication::exit(91);
                return;
            }
            const auto& request = decoded.message;
            const QString text = request.payload.value(QStringLiteral("text")).toString();
            if (request.operation == WorkerOperation::Shutdown) {
                QFile marker(QDir(outputRoot).filePath(QStringLiteral("shutdown.marker")));
                if (marker.open(QIODevice::WriteOnly)) marker.write("shutdown");
                socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
                socket.flush();
                QCoreApplication::exit(0);
                return;
            }
            if (request.operation == WorkerOperation::Load
                && QFileInfo(request.payload.value(QStringLiteral("modelRoot")).toString()).fileName()
                       == QStringLiteral("slow-load")) {
                delayedLoad = request;
                continue;
            }
            if (request.operation == WorkerOperation::Load && !delayedLoad.requestId.isEmpty()) {
                socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
                socket.write(encodeWorkerMessage(responseFor(delayedLoad)) + '\n');
                delayedLoad = {};
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Load
                && QFileInfo(modelRoot).fileName() == QStringLiteral("reject-load")) {
                VoiceCloneWorkerMessage error = responseFor(request);
                error.kind = WorkerMessageKind::Error;
                error.workerError = {QStringLiteral("model-invalid"),
                                     QStringLiteral("adapter rejected local model"), false, {}};
                socket.write(encodeWorkerMessage(error) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate && text == QStringLiteral("crash")) {
                QCoreApplication::exit(23);
                return;
            }
            if (request.operation == WorkerOperation::Generate && text == QStringLiteral("hang")) continue;
            if (request.operation == WorkerOperation::Cancel
                && request.payload.value(QStringLiteral("targetRequestId")).toString()
                       == cancelFailureTarget) {
                VoiceCloneWorkerMessage error = responseFor(request);
                error.kind = WorkerMessageKind::Error;
                error.workerError = {QStringLiteral("cancel-failed"),
                                     QStringLiteral("worker rejected cancellation"), false, {}};
                socket.write(encodeWorkerMessage(error) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate && text == QStringLiteral("oom")) {
                VoiceCloneWorkerMessage error = responseFor(request);
                error.kind = WorkerMessageKind::Error;
                error.workerError = {QStringLiteral("out-of-memory"), QStringLiteral("worker OOM"), true, {}};
                socket.write(encodeWorkerMessage(error) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate
                && text == QStringLiteral("bad-identity")) {
                VoiceCloneWorkerMessage bad = responseFor(request);
                bad.adapterId = QStringLiteral("other-adapter");
                socket.write(encodeWorkerMessage(bad) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate
                && text == QStringLiteral("oversize-frame")) {
                socket.write(QByteArray(1024 * 1024 + 1, 'x'));
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate) {
                const QString relative = request.payload.value(QStringLiteral("outputPath")).toString();
                const QString path = QDir(outputRoot).filePath(relative);
                if (text == QStringLiteral("symlink-part")) {
                    const QString target = QDir(outputRoot).filePath(QStringLiteral("symlink-target.wav"));
                    QFile targetFile(target);
                    if (!targetFile.open(QIODevice::WriteOnly) || targetFile.write("RIFF-link") < 0) {
                        QCoreApplication::exit(93);
                        return;
                    }
                    targetFile.close();
#ifdef Q_OS_WIN
                    bool linked = CreateSymbolicLinkW(
                        QDir::toNativeSeparators(path).toStdWString().c_str(),
                        QDir::toNativeSeparators(target).toStdWString().c_str(), 0) != 0;
                    if (!linked) {
                        const QString targetDirectory = QDir(outputRoot).filePath(
                            QStringLiteral("symlink-target-directory"));
                        QDir().mkpath(targetDirectory);
                        linked = QProcess::execute(QStringLiteral("cmd.exe"),
                                                   {QStringLiteral("/d"), QStringLiteral("/c"),
                                                    QStringLiteral("mklink"), QStringLiteral("/J"),
                                                    QDir::toNativeSeparators(path),
                                                    QDir::toNativeSeparators(targetDirectory)}) == 0;
                    }
#else
                    const bool linked = QFile::link(target, path);
#endif
                    if (!linked) {
                        VoiceCloneWorkerMessage error = responseFor(request);
                        error.kind = WorkerMessageKind::Error;
                        error.workerError = {QStringLiteral("symlink-unavailable"),
                                             QStringLiteral("symlink unavailable"), false, {}};
                        socket.write(encodeWorkerMessage(error) + '\n');
                    } else {
                        socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
                    }
                    socket.flush();
                    continue;
                }
#ifdef Q_OS_WIN
                if (text == QStringLiteral("hold-cancel")) {
                    const HANDLE held = CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(),
                                                    GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                                    FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (held == INVALID_HANDLE_VALUE) {
                        QCoreApplication::exit(94);
                        return;
                    }
                    DWORD written = 0;
                    WriteFile(held, "RIFF-held", 9, &written, nullptr);
                    continue;
                }
#endif
                QFile wav(path);
                QDir().mkpath(QFileInfo(wav).absolutePath());
                if (!wav.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QCoreApplication::exit(92);
                    return;
                }
                wav.write("RIFFtest-WAVE");
                wav.close();
                if (text == QStringLiteral("slow") || text == QStringLiteral("cancel-error")) {
                    delayed = request.requestId;
                    if (text == QStringLiteral("cancel-error")) cancelFailureTarget = request.requestId;
                    continue;
                }
            }
            socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
            if (request.operation == WorkerOperation::Generate
                && text == QStringLiteral("fast") && !delayed.isEmpty()) {
                auto late = request;
                late.requestId = delayed;
                late.payload.insert(QStringLiteral("outputPath"),
                                    QStringLiteral("requests/%1/generated.wav.part").arg(delayed));
                socket.write(encodeWorkerMessage(responseFor(late)) + '\n');
                delayed.clear();
            }
            socket.flush();
        }
    });
    return QCoreApplication::exec();
}

bool writeAdapterPack(const QString& pluginRoot, const QString& adapterId = QStringLiteral("qwen"))
{
    const QString root = QDir(pluginRoot).filePath(
        QStringLiteral("adapters/%1/1.0.0").arg(adapterId));
    const QString launcherRelative = QStringLiteral("workers/test-worker.exe");
    if (!QDir(root).mkpath(QStringLiteral("workers"))) return false;
    const QString launcher = QDir(root).filePath(launcherRelative);
    QFile::remove(launcher);
    if (!QFile::copy(QCoreApplication::applicationFilePath(), launcher)) return false;
    QFile manifest(QDir(root).filePath(QStringLiteral("adapter.json")));
    if (!manifest.open(QIODevice::WriteOnly)) return false;
    manifest.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("runtime"), QJsonObject{{QStringLiteral("id"), QStringLiteral("test-runtime")},
                                                {QStringLiteral("root"), QStringLiteral("runtime")},
                                                {QStringLiteral("shared"), false}}},
        {QStringLiteral("defaultLauncherId"), QStringLiteral("test")},
        {QStringLiteral("launchers"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("test")},
                                                             {QStringLiteral("kind"), QStringLiteral("executable")},
                                                             {QStringLiteral("path"), launcherRelative},
                                                             {QStringLiteral("shared"), false}}}}
    }).toJson());
    return true;
}

bool copyRegistry(const QString& pluginRoot)
{
    const QString target = QDir(pluginRoot).filePath(QStringLiteral("registry/models.json"));
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) return false;
    return QFile::copy(QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), target);
}

QString writeModel(const QString& modelsRoot,
                   const QString& stableId,
                   const QString& adapterId,
                   const QString& leaf = QStringLiteral("model"),
                   const QString& licenseRevision = QStringLiteral("license-2026-08-13"))
{
    const QString directory = QDir(modelsRoot).filePath(
        QStringLiteral("installed/%1").arg(leaf));
    if (!QDir().mkpath(directory)) return {};
    QFile data(QDir(directory).filePath(QStringLiteral("config.json")));
    if (!data.open(QIODevice::WriteOnly) || data.write("{}") != 2) return {};
    const bool index = adapterId == QStringLiteral("indextts25");
    const QString url = index
                            ? QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5")
                            : QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    QJsonObject license{{QStringLiteral("name"),
                         index ? QStringLiteral("bilibili Model Use License Agreement")
                               : QStringLiteral("Apache-2.0")},
                        {QStringLiteral("url"), url}};
    if (index) license.insert(QStringLiteral("revision"), licenseRevision);
    const QJsonObject manifest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("stableId"), stableId},
        {QStringLiteral("displayName"), stableId},
        {QStringLiteral("description"), QStringLiteral("test model")},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("runtimeId"), adapterId},
        {QStringLiteral("revision"), QStringLiteral("main")},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("provider"), QStringLiteral("official")},
                                                {QStringLiteral("url"), url}}},
        {QStringLiteral("license"), license},
        {QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("config.json")}}}}
    };
    QFile file(QDir(directory).filePath(QStringLiteral("agplayer-model.json")));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(manifest).toJson());
    return directory;
}

struct TestLayout {
    QTemporaryDir root;
    QString pluginRoot;
    QString modelsRoot;
    QString packagesRoot;

    TestLayout()
        : pluginRoot(root.filePath(QStringLiteral("plugin"))),
          modelsRoot(root.filePath(QStringLiteral("data/models/voice-clone"))),
          packagesRoot(root.filePath(QStringLiteral("packages")))
    {
        QDir().mkpath(pluginRoot);
        QDir().mkpath(modelsRoot);
        copyRegistry(pluginRoot);
    }
};

} // namespace

class VoiceCloneControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void lifecycleCorrelationAndCleanup();
    void rejectsInvalidDynamicParametersAndUnacceptedIndexLicense();
    void handlesOomCrashTimeoutAndRestartWithoutKillingHost();
    void lazyLoadProbeMarksRejectedModelInvalid();
    void rejectsExternalAdapterAndModelRoots();
    void canceledLateResponseAndStaleLoadAreIgnored();
    void lockedTemporaryOutputIsCleanedAfterWorkerStops();
    void rejectsOversizedFrameAndSymlinkPart();
    void shutdownUsesProtocolAndModuleLoadsThroughHost();
};

void VoiceCloneControllerTest::lifecycleCorrelationAndCleanup()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qwen-test");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    const QString invalidDirectory = QDir(layout.modelsRoot).filePath(
        QStringLiteral("invalid/model"));
    QVERIFY(QDir().mkpath(invalidDirectory));
    QFile invalidManifest(QDir(invalidDirectory).filePath(QStringLiteral("agplayer-model.json")));
    QVERIFY(invalidManifest.open(QIODevice::WriteOnly));
    invalidManifest.write("{}");
    invalidManifest.close();
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.metaObject()->indexOfMethod("selectModel(QString)") >= 0);
    QVERIFY(controller.metaObject()->indexOfMethod(
                "generate(QString,QString,QJsonObject)") >= 0);
    bool foundInvalid = false;
    bool foundModelMetadata = false;
    for (const QVariant& value : controller.models()) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("installState")) == QStringLiteral("invalid"))
            foundInvalid = true;
        if (model.value(QStringLiteral("stableId")) == modelId) {
            QVERIFY(!model.value(QStringLiteral("description")).toString().isEmpty());
            QVERIFY(!model.value(QStringLiteral("licenseName")).toString().isEmpty());
            QVERIFY(model.contains(QStringLiteral("officialProjectUrl")));
            QVERIFY(model.contains(QStringLiteral("huggingFaceUrl")));
            QVERIFY(model.contains(QStringLiteral("modelScopeUrl")));
            QVERIFY(model.contains(QStringLiteral("capabilityPreview")));
            QVERIFY(model.contains(QStringLiteral("requiresLicenseAcceptance")));
            foundModelMetadata = true;
        }
    }
    QVERIFY(foundInvalid);
    QVERIFY(foundModelMetadata);
    controller.setRequestTimeoutMs(2000);
    QVERIFY2(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")),
             qPrintable(controller.errorString()));
    QVERIFY(controller.selectModel(modelId));

    QSignalSpy ready(&controller, &VoiceCloneController::workerReadyChanged);
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(ready.count() > 0);
    QCOMPARE(controller.basicParameters().size(), 2);
    QCOMPARE(controller.advancedParameters().size(), 1);
    QVERIFY(controller.advancedSettingsAvailable());
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    QSignalSpy finished(&controller, &VoiceCloneController::generationFinished);
    const QString slow = controller.generate(QStringLiteral("slow"), {},
                                             {{QStringLiteral("seed"), 2},
                                              {QStringLiteral("mode"), QStringLiteral("fast")}});
    const QString fast = controller.generate(QStringLiteral("fast"), {},
                                             {{QStringLiteral("seed"), 3},
                                              {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(!slow.isEmpty());
    QVERIFY(!fast.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
    QCOMPARE(finished.at(0).at(0).toString(), fast);
    QCOMPARE(finished.at(1).at(0).toString(), slow);
    for (const auto& arguments : finished) {
        const QString output = arguments.at(1).toString();
        QVERIFY(QFileInfo::exists(output));
        QVERIFY(!QFileInfo::exists(output + QStringLiteral(".part")));
    }
    const QString managedOutput = finished.at(0).at(1).toString();
    const QString savedCopy = layout.root.filePath(QStringLiteral("saved-result.wav"));
    QVERIFY(controller.saveResult(managedOutput, savedCopy));
    QVERIFY(QFileInfo::exists(savedCopy));
    QCOMPARE(QFileInfo(savedCopy).size(), QFileInfo(managedOutput).size());
    QVERIFY(!controller.deleteResult(savedCopy));
    QVERIFY(controller.deleteResult(managedOutput));
    QVERIFY(!QFileInfo::exists(managedOutput));

    const QString canceled = controller.generate(QStringLiteral("hang"), {},
                                                  {{QStringLiteral("seed"), 4},
                                                   {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(controller.cancel(canceled));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(canceled), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(controller.requestDirectory(canceled)), 3000);
    QVERIFY(controller.unloadModel());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.modelLoaded(), 3000);
    controller.shutdown();
    QVERIFY(!controller.workerRunning());
}

void VoiceCloneControllerTest::rejectsInvalidDynamicParametersAndUnacceptedIndexLicense()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot, QStringLiteral("indextts25")));
    const QString indexId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    QVERIFY(!writeModel(layout.modelsRoot, indexId, QStringLiteral("indextts25"),
                        QStringLiteral("index")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("indextts25"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(indexId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("unknown"), 1}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("seed"), 100},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("slow")}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")},
                                 {QStringLiteral("style"), QStringLiteral("hidden")}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("license-blocked"), {},
                                {{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}}).isEmpty());
    QVERIFY(controller.errorString().contains(QStringLiteral("license"), Qt::CaseInsensitive));

    VoiceClonePackageManifest package;
    package.packageId = QStringLiteral("index-test");
    package.modelId = indexId;
    package.adapterId = QStringLiteral("indextts25");
    package.version = QStringLiteral("1");
    package.revision = QStringLiteral("main");
    package.licenseUrl = QUrl(QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5"));
    package.licenseRevision = QStringLiteral("license-2026-08-13");
    package.files.append({QStringLiteral("model.bin"), QUrl(QStringLiteral("https://example.com/model.bin")),
                          QByteArray(64, 'a')});
    licenses.start(package);
    QCOMPARE(licenses.state(), VoiceClonePackageManager::LicenseRequired);
    QVERIFY(!licenses.acceptLicense(package.licenseUrl, QStringLiteral("wrong-revision")));
    QVERIFY(licenses.acceptLicense(package.licenseUrl, package.licenseRevision));
    const QString accepted = controller.generate(QStringLiteral("accepted"), {},
                                                 {{QStringLiteral("seed"), 1},
                                                  {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(!accepted.isEmpty());
}

void VoiceCloneControllerTest::handlesOomCrashTimeoutAndRestartWithoutKillingHost()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qwen-failure-test");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    controller.setRequestTimeoutMs(250);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    const QString oom = controller.generate(QStringLiteral("oom"), {},
                                            {{QStringLiteral("seed"), 1},
                                             {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(oom), 3000);
    QVERIFY(failed.last().at(1).toString().contains(QStringLiteral("memory")));
    QVERIFY(controller.workerRunning());

    const QString mismatched = controller.generate(QStringLiteral("bad-identity"), {},
                                                   {{QStringLiteral("seed"), 1},
                                                    {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(mismatched)));
    QVERIFY(controller.restartWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    const QString crash = controller.generate(QStringLiteral("crash"), {},
                                              {{QStringLiteral("seed"), 1},
                                               {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(crash)));
    QVERIFY(QCoreApplication::instance() != nullptr);
    QVERIFY(controller.restartWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    const QString timedOut = controller.generate(QStringLiteral("hang"), {},
                                                 {{QStringLiteral("seed"), 1},
                                                  {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(timedOut), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(timedOut)));
    QVERIFY(!controller.workerRunning());
    QVERIFY(QCoreApplication::instance() != nullptr);
}

void VoiceCloneControllerTest::lazyLoadProbeMarksRejectedModelInvalid()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qwen-reject-test");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen"),
                        QStringLiteral("reject-load")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    QVERIFY(controller.loadModel());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(!controller.modelLoaded());
    bool invalid = false;
    for (const QVariant& value : controller.models()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("stableId")) == modelId
            && row.value(QStringLiteral("installState")) == QStringLiteral("invalid")) {
            invalid = true;
            QVERIFY(row.value(QStringLiteral("diagnostic")).toString().contains(
                QStringLiteral("rejected")));
        }
    }
    QVERIFY(invalid);
}

void VoiceCloneControllerTest::rejectsExternalAdapterAndModelRoots()
{
    TestLayout layout;
    QTemporaryDir external;
    QVERIFY(layout.root.isValid());
    QVERIFY(external.isValid());
    QVERIFY(writeAdapterPack(external.path()));
    QVERIFY(!writeModel(external.path(), QStringLiteral("local/external"),
                        QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(!controller.configureAdapter(QStringLiteral("../external"), QStringLiteral("1.0.0")));
    QVERIFY(!controller.selectModel(QStringLiteral("local/external")));
}

void VoiceCloneControllerTest::canceledLateResponseAndStaleLoadAreIgnored()
{
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString slowId = QStringLiteral("local/slow-load");
    const QString fastId = QStringLiteral("local/fast-load");
    QVERIFY(!writeModel(layout.modelsRoot, slowId, QStringLiteral("qwen"),
                        QStringLiteral("slow-load")).isEmpty());
    QVERIFY(!writeModel(layout.modelsRoot, fastId, QStringLiteral("qwen"),
                        QStringLiteral("fast-load")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(slowId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QVERIFY(controller.selectModel(fastId));
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    QVERIFY(controller.workerRunning());

    QSignalSpy finished(&controller, &VoiceCloneController::generationFinished);
    const QJsonObject parameters{{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}};
    const QString retired = controller.generate(QStringLiteral("slow"), {}, parameters);
    QVERIFY(controller.cancel(retired));
    const QString current = controller.generate(QStringLiteral("fast"), {}, parameters);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QCOMPARE(finished.last().at(0).toString(), current);
    QVERIFY(controller.workerRunning());
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(retired)));

    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    const QString cancelError = controller.generate(QStringLiteral("cancel-error"), {}, parameters);
    const QString cancelErrorDirectory = controller.requestDirectory(cancelError);
    QVERIFY(controller.cancel(cancelError));
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(cancelErrorDirectory), 3000);
}

void VoiceCloneControllerTest::lockedTemporaryOutputIsCleanedAfterWorkerStops()
{
#ifndef Q_OS_WIN
    QSKIP("Windows locked-file semantics test");
#else
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/held-output");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    const QJsonObject parameters{{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}};
    const QString request = controller.generate(QStringLiteral("hold-cancel"), {}, parameters);
    const QString directory = controller.requestDirectory(request);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(directory), 1000);
    QVERIFY(controller.cancel(request));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(directory), 3000);
    QCOMPARE(controller.pendingCleanupCount(), 0);
#endif
}

void VoiceCloneControllerTest::rejectsOversizedFrameAndSymlinkPart()
{
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/security-output");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    controller.setRequestTimeoutMs(2000);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    const QJsonObject parameters{{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}};
    controller.generate(QStringLiteral("oversize-frame"), {}, parameters);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(failed.last().at(1).toString().contains(QStringLiteral("frame")));

    QVERIFY(controller.restartWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    const int before = failed.count();
    controller.generate(QStringLiteral("symlink-part"), {}, parameters);
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() > before, 3000);
    if (failed.last().at(1).toString() == QStringLiteral("symlink-unavailable"))
        QSKIP("Symbolic-link privilege is unavailable");
    QCOMPARE(failed.last().at(1).toString(), QStringLiteral("unsafe-output"));
}

void VoiceCloneControllerTest::shutdownUsesProtocolAndModuleLoadsThroughHost()
{
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/shutdown");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    controller.shutdown();
    QVERIFY(QFileInfo::exists(QDir(layout.pluginRoot).filePath(
        QStringLiteral("cache/shutdown.marker"))));

    QTemporaryDir moduleRoot;
    QVERIFY(moduleRoot.isValid());
    const QString source = QString::fromUtf8(AGPLAYER_VOICE_CLONE_PLUGIN);
    const QString library = QFileInfo(source).fileName();
    QVERIFY(QFile::copy(source, moduleRoot.filePath(library)));
    QVERIFY(copyRegistry(moduleRoot.path()));
    QVERIFY(QDir().mkpath(moduleRoot.filePath(QStringLiteral("models/voice-clone"))));
    QFile manifest(moduleRoot.filePath(QStringLiteral("agplayer-voice-clone.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("pluginId"), QStringLiteral("agplayer.voice-clone")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("availableVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("platform"), QStringLiteral("windows")},
        {QStringLiteral("architecture"), QStringLiteral("x86_64")},
        {QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("library"), library}}).toJson());
    manifest.close();
    qputenv("AGPLAYER_VOICE_CLONE_ROOT", moduleRoot.path().toUtf8());
    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    QVERIFY2(host.openPlugin(), qPrintable(host.errorString()));
    QVERIFY(host.pluginController() != nullptr);
    QVERIFY(host.mainQmlUrl().isValid());
    QCOMPARE(host.mainQmlUrl().scheme(), QStringLiteral("qrc"));
    QFile workspace(QStringLiteral(":") + host.mainQmlUrl().path());
    QVERIFY2(workspace.open(QIODevice::ReadOnly), qPrintable(workspace.errorString()));
    QVERIFY(workspace.readAll().contains("VoiceCloneModelBar"));
    host.closePlugin();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    qunsetenv("AGPLAYER_VOICE_CLONE_ROOT");
    QVERIFY(QFile::rename(moduleRoot.filePath(library),
                          moduleRoot.filePath(library + QStringLiteral(".unloaded"))));
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--voice-clone-worker"))) {
        return runWorker(application.arguments());
    }
    VoiceCloneControllerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "voice_clone_controller_test.moc"
