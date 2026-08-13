#include "voice_clone_controller.hpp"
#include "voice_clone_package_manager.hpp"
#include "voice_clone_worker_protocol.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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
            if (request.operation == WorkerOperation::Generate) {
                const QString relative = request.payload.value(QStringLiteral("outputPath")).toString();
                QFile wav(QDir(outputRoot).filePath(relative));
                QDir().mkpath(QFileInfo(wav).absolutePath());
                if (!wav.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QCoreApplication::exit(92);
                    return;
                }
                wav.write("RIFFtest-WAVE");
                wav.close();
                if (text == QStringLiteral("slow")) {
                    delayed = request.requestId;
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

bool writeAdapterPack(const QString& root, const QString& adapterId = QStringLiteral("qwen"))
{
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

} // namespace

class VoiceCloneControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void lifecycleCorrelationAndCleanup();
    void rejectsInvalidDynamicParametersAndUnacceptedIndexLicense();
    void handlesOomCrashTimeoutAndRestartWithoutKillingHost();
    void lazyLoadProbeMarksRejectedModelInvalid();
};

void VoiceCloneControllerTest::lifecycleCorrelationAndCleanup()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString pack = root.filePath(QStringLiteral("pack"));
    const QString model = root.filePath(QStringLiteral("model"));
    QVERIFY(QDir().mkpath(pack));
    QVERIFY(QDir().mkpath(model));
    QVERIFY(writeAdapterPack(pack));
    const QString invalidDirectory = root.filePath(
        QStringLiteral("models/voice-clone/invalid/model"));
    QVERIFY(QDir().mkpath(invalidDirectory));
    QFile invalidManifest(QDir(invalidDirectory).filePath(QStringLiteral("agplayer-model.json")));
    QVERIFY(invalidManifest.open(QIODevice::WriteOnly));
    invalidManifest.write("{}");
    invalidManifest.close();
    VoiceClonePackageManager licenses(root.filePath(QStringLiteral("packages")));
    VoiceCloneController controller(root.path(), QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), &licenses);
    bool foundInvalid = false;
    for (const QVariant& value : controller.models()) {
        if (value.toMap().value(QStringLiteral("installState")) == QStringLiteral("invalid"))
            foundInvalid = true;
    }
    QVERIFY(foundInvalid);
    controller.setRequestTimeoutMs(2000);
    QVERIFY2(controller.configureAdapter(QDir(pack).filePath(QStringLiteral("adapter.json")), pack),
             qPrintable(controller.errorString()));
    QVERIFY(controller.selectModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"), model));

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

    const QString canceled = controller.generate(QStringLiteral("hang"), {},
                                                  {{QStringLiteral("seed"), 4},
                                                   {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(controller.cancel(canceled));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(canceled), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(canceled)));
    QVERIFY(controller.unloadModel());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.modelLoaded(), 3000);
    controller.shutdown();
    QVERIFY(!controller.workerRunning());
}

void VoiceCloneControllerTest::rejectsInvalidDynamicParametersAndUnacceptedIndexLicense()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString pack = root.filePath(QStringLiteral("pack"));
    QVERIFY(QDir().mkpath(pack));
    QVERIFY(writeAdapterPack(pack, QStringLiteral("indextts25")));
    VoiceClonePackageManager licenses(root.filePath(QStringLiteral("packages")));
    VoiceCloneController controller(root.path(), QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), &licenses);
    QVERIFY(controller.configureAdapter(QDir(pack).filePath(QStringLiteral("adapter.json")), pack));
    QVERIFY(controller.selectModel(QStringLiteral("IndexTeam/IndexTTS-2.5"), root.path()));
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
}

void VoiceCloneControllerTest::handlesOomCrashTimeoutAndRestartWithoutKillingHost()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString pack = root.filePath(QStringLiteral("pack"));
    QVERIFY(QDir().mkpath(pack));
    QVERIFY(writeAdapterPack(pack));
    VoiceClonePackageManager licenses(root.filePath(QStringLiteral("packages")));
    VoiceCloneController controller(root.path(), QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), &licenses);
    controller.setRequestTimeoutMs(250);
    QVERIFY(controller.configureAdapter(QDir(pack).filePath(QStringLiteral("adapter.json")), pack));
    QVERIFY(controller.selectModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"), root.path()));
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
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString pack = root.filePath(QStringLiteral("pack"));
    const QString model = root.filePath(QStringLiteral("reject-load"));
    QVERIFY(QDir().mkpath(pack));
    QVERIFY(QDir().mkpath(model));
    QVERIFY(writeAdapterPack(pack));
    VoiceClonePackageManager licenses(root.filePath(QStringLiteral("packages")));
    VoiceCloneController controller(root.path(), QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), &licenses);
    QVERIFY(controller.configureAdapter(QDir(pack).filePath(QStringLiteral("adapter.json")), pack));
    const QString modelId = QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    QVERIFY(controller.selectModel(modelId, model));
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
