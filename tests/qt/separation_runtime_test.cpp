#include "ort_runtime.hpp"
#include "trusted_profiles.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace agplayer::separation;

namespace {

class FakeProbeBackend final : public InferenceProbeBackend {
public:
    bool cpu = true;
    bool gpu = false;
    QString reason = QStringLiteral("minimal inference failed");
    int cpuCalls = 0;
    int gpuCalls = 0;

    bool probeCpu(QString* error) override
    {
        ++cpuCalls;
        if (!cpu && error) *error = QStringLiteral("CPU failed");
        return cpu;
    }

    bool probeDirectMl(QString* error) override
    {
        ++gpuCalls;
        if (!gpu && error) *error = reason;
        return gpu;
    }
};

} // namespace

class SeparationRuntimeTest final : public QObject {
    Q_OBJECT

private slots:
    void missingAndBadDynamicRuntimeAreRejected();
    void trustedProfilesBindHashesToExactTensorSemantics();
    void demucsRowsAreBoundToTrustedHashes();
    void invalidTensorNamesTypesShapesAndOpsetsAreRejected();
    void autoUsesOnlyVerifiedGpuAndFallsBackToCpu();
};

void SeparationRuntimeTest::missingAndBadDynamicRuntimeAreRejected()
{
    DynamicOrtRuntime runtime;
    QVERIFY(!runtime.load(QStringLiteral("Z:/does-not-exist/onnxruntime.dll")));
    QCOMPARE(runtime.errorCode(), QStringLiteral("runtime_missing"));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString badPath = temporary.filePath(QStringLiteral("onnxruntime.dll"));
    QFile bad(badPath);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    QVERIFY(bad.write("not a dll") > 0);
    bad.close();
    QVERIFY(!runtime.load(badPath));
    QCOMPARE(runtime.errorCode(), QStringLiteral("runtime_load_failed"));
}

void SeparationRuntimeTest::trustedProfilesBindHashesToExactTensorSemantics()
{
    const auto kara = trustedProfileForHashes({
        QStringLiteral("e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63")});
    QVERIFY(kara.has_value());
    QCOMPARE(kara->id, QStringLiteral("uvr-mdxnet-kara"));
    QCOMPARE(kara->inputs.front().name, QStringLiteral("input"));
    QCOMPARE(kara->inputs.front().shape, (QVector<qint64>{-1, 4, 2048, 256}));
    QCOMPARE(kara->outputs.front().shape, (QVector<qint64>{-1, 4, 2048, 256}));

    const auto demucs = trustedProfileForHashes({
        QStringLiteral("b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961"),
        QStringLiteral("047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd"),
        QStringLiteral("b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71"),
        QStringLiteral("0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce")});
    QVERIFY(demucs.has_value());
    QCOMPARE(demucs->inputs.front().name, QStringLiteral("mix"));
    QCOMPARE(demucs->outputs.front().name, QStringLiteral("stems"));
    QCOMPARE(demucs->outputs.front().shape, (QVector<qint64>{1, 4, 2, 343980}));

    QVERIFY(!trustedProfileForHashes({QString(64, QLatin1Char('0'))}).has_value());
}

void SeparationRuntimeTest::demucsRowsAreBoundToTrustedHashes()
{
    QCOMPARE(trustedDemucsRowForHash(QStringLiteral(
                 "047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd")), 0);
    QCOMPARE(trustedDemucsRowForHash(QStringLiteral(
                 "b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961")), 1);
    QCOMPARE(trustedDemucsRowForHash(QStringLiteral(
                 "b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71")), 2);
    QCOMPARE(trustedDemucsRowForHash(QStringLiteral(
                 "0CBE651F535415C9D26A7BB614F7D322DD5A080FA0298F2E50F478030A994DCE")), 3);
    QCOMPARE(trustedDemucsRowForHash(QString(64, QLatin1Char('0'))), -1);
}

void SeparationRuntimeTest::invalidTensorNamesTypesShapesAndOpsetsAreRejected()
{
    const TrustedModelProfile profile = *trustedProfileForHashes({
        QStringLiteral("317554b07fe1ea5279a77f2b1520a41ea4b93432560c4ffd08792c30fddf9adc")});
    ModelMetadata valid{profile.minimumOpset, profile.inputs, profile.outputs};
    QVERIFY(validateModelMetadata(profile, valid).ok);

    ModelMetadata invalid = valid;
    invalid.inputs[0].name = QStringLiteral("wrong");
    QCOMPARE(validateModelMetadata(profile, invalid).code, QStringLiteral("tensor_name_mismatch"));
    invalid = valid;
    invalid.outputs[0].type = TensorElementType::Float16;
    QCOMPARE(validateModelMetadata(profile, invalid).code, QStringLiteral("tensor_type_mismatch"));
    invalid = valid;
    invalid.inputs[0].shape[2] = 2048;
    QCOMPARE(validateModelMetadata(profile, invalid).code, QStringLiteral("tensor_shape_mismatch"));
    invalid = valid;
    invalid.opset = profile.minimumOpset - 1;
    QCOMPARE(validateModelMetadata(profile, invalid).code, QStringLiteral("opset_mismatch"));
}

void SeparationRuntimeTest::autoUsesOnlyVerifiedGpuAndFallsBackToCpu()
{
    FakeProbeBackend backend;
    ProviderDecision decision = chooseProvider(DeviceMode::Auto, backend);
    QVERIFY(decision.ok);
    QCOMPARE(decision.provider, ExecutionProvider::Cpu);
    QCOMPARE(decision.fallbackReason, QStringLiteral("minimal inference failed"));
    QCOMPARE(backend.gpuCalls, 1);
    QCOMPARE(backend.cpuCalls, 1);

    backend.gpu = true;
    backend.cpuCalls = backend.gpuCalls = 0;
    decision = chooseProvider(DeviceMode::Auto, backend);
    QVERIFY(decision.ok);
    QCOMPARE(decision.provider, ExecutionProvider::DirectMl);
    QCOMPARE(backend.gpuCalls, 1);
    QCOMPARE(backend.cpuCalls, 0);

    backend.gpu = false;
    decision = chooseProvider(DeviceMode::Gpu, backend);
    QVERIFY(!decision.ok);
    QCOMPARE(decision.code, QStringLiteral("gpu_probe_failed"));
}

QTEST_GUILESS_MAIN(SeparationRuntimeTest)
#include "separation_runtime_test.moc"
