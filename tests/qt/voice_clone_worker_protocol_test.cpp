#include "voice_clone_adapter_manifest.hpp"
#include "voice_clone_worker_protocol.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace agplayer::voice_clone;

namespace {

QJsonObject identity()
{
    return {{QStringLiteral("adapterId"), QStringLiteral("qwen")},
            {QStringLiteral("adapterVersion"), QStringLiteral("1.2.0")},
            {QStringLiteral("protocolVersion"), 1}};
}

QJsonObject envelope(const QString& kind,
                     const QString& operation,
                     const QString& requestId)
{
    QJsonObject object = identity();
    object.insert(QStringLiteral("kind"), kind);
    object.insert(QStringLiteral("operation"), operation);
    object.insert(QStringLiteral("requestId"), requestId);
    return object;
}

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray adapterManifest(const QString& launcherPath = QStringLiteral("workers/qwen/worker.py"))
{
    return compact({
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("adapterId"), QStringLiteral("qwen")},
        {QStringLiteral("adapterVersion"), QStringLiteral("1.2.0")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("runtime"),
         QJsonObject{{QStringLiteral("id"), QStringLiteral("qwen-shared")},
                     {QStringLiteral("root"), QStringLiteral("runtimes/qwen-shared")},
                     {QStringLiteral("shared"), true}}},
        {QStringLiteral("defaultLauncherId"), QStringLiteral("python-module")},
        {QStringLiteral("launchers"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("python-module")},
                                {QStringLiteral("kind"), QStringLiteral("pythonModule")},
                                {QStringLiteral("path"), launcherPath}}}},
    });
}

} // namespace

class VoiceCloneWorkerProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesTrustedAdapterManifestAndResolvesListedLauncher();
    void rejectsUnlistedAndUnsafeLaunchers();
    void parsesApprovedAdapterManifests();
    void roundTripsHelloIdentityAndRequestId();
    void acceptsUnifiedWorkerOperations_data();
    void acceptsUnifiedWorkerOperations();
    void parsesCapabilitiesGenerationCancellationAndStructuredErrors();
    void preservesStagesAndIndeterminateProgress();
    void rejectsOutputPathsOutsideExplicitRoot();
};

void VoiceCloneWorkerProtocolTest::parsesTrustedAdapterManifestAndResolvesListedLauncher()
{
    const auto parsed = parseAdapterManifest(adapterManifest());
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
    QCOMPARE(parsed.manifest.adapterId, QStringLiteral("qwen"));
    QCOMPARE(parsed.manifest.adapterVersion, QStringLiteral("1.2.0"));
    QCOMPARE(parsed.manifest.protocolVersion, 1);
    QCOMPARE(parsed.manifest.runtime.id, QStringLiteral("qwen-shared"));
    QVERIFY(parsed.manifest.runtime.shared);

    QTemporaryDir pack;
    QVERIFY(pack.isValid());
    const auto launcher = resolveAdapterLauncher(parsed.manifest,
                                                 QStringLiteral("python-module"),
                                                 pack.path());
    QVERIFY2(launcher.isValid(), qPrintable(launcher.error));
    QCOMPARE(QDir::cleanPath(launcher.absolutePath),
             QDir::cleanPath(pack.filePath(QStringLiteral("workers/qwen/worker.py"))));
}

void VoiceCloneWorkerProtocolTest::rejectsUnlistedAndUnsafeLaunchers()
{
    const auto parsed = parseAdapterManifest(adapterManifest());
    QVERIFY(parsed.isValid());
    QTemporaryDir pack;
    QVERIFY(pack.isValid());

    auto launcher = resolveAdapterLauncher(parsed.manifest,
                                           QStringLiteral("powershell"),
                                           pack.path());
    QVERIFY(!launcher.isValid());
    QVERIFY(launcher.error.contains(QStringLiteral("listed"), Qt::CaseInsensitive));

    QVERIFY(!parseAdapterManifest(adapterManifest(QStringLiteral("C:/Windows/System32/cmd.exe")))
                 .isValid());
    QVERIFY(!parseAdapterManifest(adapterManifest(QStringLiteral("../outside/worker.py")))
                 .isValid());
}

void VoiceCloneWorkerProtocolTest::parsesApprovedAdapterManifests()
{
    const QDir adapters(QStringLiteral(AGPLAYER_VOICE_CLONE_ADAPTERS_DIR));
    const QStringList ids{QStringLiteral("qwen"),
                          QStringLiteral("indextts25"),
                          QStringLiteral("cosyvoice3")};
    for (const QString& id : ids) {
        QFile file(adapters.filePath(id + QStringLiteral("/adapter.json")));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const auto parsed = parseAdapterManifest(file.readAll());
        QVERIFY2(parsed.isValid(), qPrintable(id + QStringLiteral(": ") + parsed.error));
        QCOMPARE(parsed.manifest.adapterId, id);
        QCOMPARE(parsed.manifest.protocolVersion, 1);
        QVERIFY(!parsed.manifest.adapterVersion.isEmpty());
    }
}

void VoiceCloneWorkerProtocolTest::roundTripsHelloIdentityAndRequestId()
{
    QJsonObject hello = envelope(QStringLiteral("request"),
                                 QStringLiteral("hello"),
                                 QStringLiteral("hello-7"));
    hello.insert(QStringLiteral("payload"), QJsonObject{});
    const auto decoded = decodeWorkerMessage(compact(hello));
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QCOMPARE(decoded.message.adapterId, QStringLiteral("qwen"));
    QCOMPARE(decoded.message.adapterVersion, QStringLiteral("1.2.0"));
    QCOMPARE(decoded.message.protocolVersion, 1);
    QCOMPARE(decoded.message.requestId, QStringLiteral("hello-7"));

    const auto roundTrip = decodeWorkerMessage(encodeWorkerMessage(decoded.message));
    QVERIFY2(roundTrip.isValid(), qPrintable(roundTrip.error));
    QCOMPARE(roundTrip.message.operation, WorkerOperation::Hello);
    QCOMPARE(roundTrip.message.requestId, QStringLiteral("hello-7"));
}

void VoiceCloneWorkerProtocolTest::acceptsUnifiedWorkerOperations_data()
{
    QTest::addColumn<QString>("operation");
    for (const QString& operation : {QStringLiteral("hello"),
                                     QStringLiteral("capabilities"),
                                     QStringLiteral("load"),
                                     QStringLiteral("cancel"),
                                     QStringLiteral("unload"),
                                     QStringLiteral("shutdown")}) {
        QTest::newRow(qPrintable(operation)) << operation;
    }
}

void VoiceCloneWorkerProtocolTest::acceptsUnifiedWorkerOperations()
{
    QFETCH(QString, operation);
    QJsonObject request = envelope(QStringLiteral("request"), operation, operation + QStringLiteral("-1"));
    request.insert(QStringLiteral("payload"),
                   operation == QStringLiteral("cancel")
                       ? QJsonObject{{QStringLiteral("targetRequestId"), QStringLiteral("generate-1")}}
                       : QJsonObject{});
    const auto decoded = decodeWorkerMessage(compact(request));
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QCOMPARE(workerOperationName(decoded.message.operation), operation);
}

void VoiceCloneWorkerProtocolTest::parsesCapabilitiesGenerationCancellationAndStructuredErrors()
{
    QJsonObject capabilities = envelope(QStringLiteral("response"),
                                        QStringLiteral("capabilities"),
                                        QStringLiteral("caps-1"));
    capabilities.insert(QStringLiteral("payload"),
                        QJsonObject{{QStringLiteral("schema"),
                                     QJsonObject{{QStringLiteral("protocolVersion"), 1},
                                                 {QStringLiteral("groups"), QJsonArray{}},
                                                 {QStringLiteral("parameters"), QJsonArray{}}}}});
    auto decoded = decodeWorkerMessage(compact(capabilities));
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QVERIFY(decoded.message.payload.value(QStringLiteral("schema")).isObject());

    QTemporaryDir outputRoot;
    QVERIFY(outputRoot.isValid());
    QJsonObject generate = envelope(QStringLiteral("request"),
                                    QStringLiteral("generate"),
                                    QStringLiteral("generate-3"));
    generate.insert(QStringLiteral("payload"),
                    QJsonObject{{QStringLiteral("text"), QStringLiteral("hello")},
                                {QStringLiteral("outputPath"), QStringLiteral("jobs/3.wav")},
                                {QStringLiteral("parameters"), QJsonObject{}}});
    decoded = decodeWorkerMessage(compact(generate), outputRoot.path());
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QCOMPARE(QDir::cleanPath(decoded.message.resolvedOutputPath),
             QDir::cleanPath(outputRoot.filePath(QStringLiteral("jobs/3.wav"))));

    QJsonObject cancel = envelope(QStringLiteral("request"),
                                  QStringLiteral("cancel"),
                                  QStringLiteral("cancel-3"));
    cancel.insert(QStringLiteral("payload"),
                  QJsonObject{{QStringLiteral("targetRequestId"), QStringLiteral("generate-3")}});
    decoded = decodeWorkerMessage(compact(cancel));
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QCOMPARE(decoded.message.payload.value(QStringLiteral("targetRequestId")).toString(),
             QStringLiteral("generate-3"));

    QJsonObject error = envelope(QStringLiteral("error"),
                                 QStringLiteral("load"),
                                 QStringLiteral("load-9"));
    error.insert(QStringLiteral("error"),
                 QJsonObject{{QStringLiteral("code"), QStringLiteral("MODEL_LOAD_FAILED")},
                             {QStringLiteral("message"), QStringLiteral("out of memory")},
                             {QStringLiteral("retryable"), false},
                             {QStringLiteral("details"), QJsonObject{{QStringLiteral("device"), QStringLiteral("cpu")}}}});
    decoded = decodeWorkerMessage(compact(error));
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QCOMPARE(decoded.message.workerError.code, QStringLiteral("MODEL_LOAD_FAILED"));
    QCOMPARE(decoded.message.workerError.message, QStringLiteral("out of memory"));
    QVERIFY(!decoded.message.workerError.retryable);
}

void VoiceCloneWorkerProtocolTest::preservesStagesAndIndeterminateProgress()
{
    QJsonObject progress = envelope(QStringLiteral("progress"),
                                    QStringLiteral("generate"),
                                    QStringLiteral("generate-4"));
    progress.insert(QStringLiteral("stage"), QStringLiteral("synthesizing"));
    progress.insert(QStringLiteral("progress"), QJsonValue::Null);
    const auto decoded = decodeWorkerMessage(compact(progress));
    QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
    QCOMPARE(decoded.message.stage, QStringLiteral("synthesizing"));
    QVERIFY(!decoded.message.progress.has_value());
}

void VoiceCloneWorkerProtocolTest::rejectsOutputPathsOutsideExplicitRoot()
{
    QJsonObject generate = envelope(QStringLiteral("request"),
                                    QStringLiteral("generate"),
                                    QStringLiteral("generate-5"));
    generate.insert(QStringLiteral("payload"),
                    QJsonObject{{QStringLiteral("text"), QStringLiteral("hello")},
                                {QStringLiteral("outputPath"), QStringLiteral("C:/temp/escaped.wav")},
                                {QStringLiteral("parameters"), QJsonObject{}}});
    QTemporaryDir outputRoot;
    QVERIFY(outputRoot.isValid());
    auto decoded = decodeWorkerMessage(compact(generate), outputRoot.path());
    QVERIFY(!decoded.isValid());
    QVERIFY(decoded.error.contains(QStringLiteral("relative"), Qt::CaseInsensitive));

    QJsonObject payload = generate.value(QStringLiteral("payload")).toObject();
    payload.insert(QStringLiteral("outputPath"), QStringLiteral("../escaped.wav"));
    generate.insert(QStringLiteral("payload"), payload);
    decoded = decodeWorkerMessage(compact(generate), outputRoot.path());
    QVERIFY(!decoded.isValid());

    payload.insert(QStringLiteral("outputPath"), QStringLiteral("safe.wav"));
    generate.insert(QStringLiteral("payload"), payload);
    decoded = decodeWorkerMessage(compact(generate));
    QVERIFY(!decoded.isValid());
    QVERIFY(decoded.error.contains(QStringLiteral("outputRoot"), Qt::CaseInsensitive));
}

QTEST_APPLESS_MAIN(VoiceCloneWorkerProtocolTest)

#include "voice_clone_worker_protocol_test.moc"
