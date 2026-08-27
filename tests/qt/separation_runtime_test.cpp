#include "native_worker_backend.hpp"
#include "ort_session.hpp"
#include "ort_runtime.hpp"
#include "trusted_profiles.hpp"
#include "vocal_separation_catalog.hpp"

#include <onnxruntime_c_api.h>

#include <QFile>
#include <QLibrary>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

#include <future>

using namespace agplayer::separation;

#ifndef AG_SEPARATION_FAKE_ORT_PATH
#error AG_SEPARATION_FAKE_ORT_PATH must name the fake ORT runtime
#endif

namespace {

int releasedTerminateStatuses = 0;

OrtStatus* ORT_API_CALL rejectedTerminate(OrtRunOptions*) noexcept
{
    return reinterpret_cast<OrtStatus*>(quintptr{1});
}

const char* ORT_API_CALL terminateErrorMessage(const OrtStatus*) noexcept
{
    return "terminate refused";
}

void ORT_API_CALL releaseTerminateStatus(OrtStatus*) noexcept
{
    ++releasedTerminateStatuses;
}

class ControlledNativeProbe final : public NativeProviderProbe {
public:
    bool cpu = true;
    bool gpu = false;
    QString reason = QStringLiteral("minimal inference failed");
    int cpuCalls = 0;
    int gpuCalls = 0;
    bool block = false;
    QSemaphore entered;
    QSemaphore release;
    const CancellationToken* observedCancellation = nullptr;

    QVector<DxgiAdapterInfo> hardwareAdapters() override
    {
        return {{7, QStringLiteral("test adapter"), 1024}};
    }

    BackendResult prove(const NativeStartRequest&, const TrustedModelProfile&,
                        ExecutionProvider provider, int,
                        const CancellationToken& cancelled) override
    {
        observedCancellation = &cancelled;
        if (block) {
            entered.release();
            release.acquire();
        }
        if (cancelled.isCancelled()) {
            return {false, QStringLiteral("cancelled"),
                    QStringLiteral("Provider probe cancelled"), {}};
        }
        if (provider == ExecutionProvider::DirectMl) {
            ++gpuCalls;
            return gpu ? BackendResult{true, {}, {}, {}}
                       : BackendResult{false, QStringLiteral("gpu_probe_failed"),
                                       reason, {}};
        }
        ++cpuCalls;
        return cpu ? BackendResult{true, {}, {}, {}}
                   : BackendResult{false, QStringLiteral("cpu_probe_failed"),
                                   QStringLiteral("CPU failed"), {}};
    }
};

} // namespace

class SeparationRuntimeTest final : public QObject {
    Q_OBJECT

private slots:
    void missingAndBadDynamicRuntimeAreRejected();
    void trustedProfilesBindHashesToExactTensorSemantics();
    void workerTrustSizesAndHashesMatchTheInstallCatalog();
    void demucsRowsAreBoundToTrustedHashes();
    void invalidTensorNamesTypesShapesAndOpsetsAreRejected();
    void productionProviderSelectionObservesCancellation();
    void productionAutoSelectionFallsBackToCpuWithReason();
    void directMlSessionUsesApprovedBytesAndDisablesCpuFallback();
    void sessionLoadUsesTheStartCancellationTokenAndUnsubscribesSafely();
    void terminateFailureIsReleasedAndReported();
    void cancellationNotificationDoesNotRequirePolling();
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

void SeparationRuntimeTest::workerTrustSizesAndHashesMatchTheInstallCatalog()
{
    for (const VocalModelCard& card : VocalSeparationCatalog::models()) {
        QStringList hashes;
        QVector<qint64> sizes;
        for (const VocalDownloadFile& file : card.files) {
            hashes.push_back(file.sha256);
            sizes.push_back(file.bytes);
        }
        const auto profile = trustedProfileForHashes(hashes);
        QVERIFY2(profile.has_value(), qPrintable(card.id));
        QCOMPARE(profile->sha256, hashes);
        QCOMPARE(profile->expectedSizeBytes, sizes);
    }
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

void SeparationRuntimeTest::productionProviderSelectionObservesCancellation()
{
    ControlledNativeProbe probe;
    probe.block = true;
    NativeStartRequest request;
    request.device = DeviceMode::Cpu;
    CancellationToken cancelled;
    const TrustedModelProfile profile = *trustedProfileForHashes({QStringLiteral(
        "e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63")});

    auto future = std::async(std::launch::async, [&] {
        return selectNativeProvider(request, profile, cancelled, probe);
    });
    QVERIFY(probe.entered.tryAcquire(1, 2000));
    cancelled.cancel();
    probe.release.release();
    QVERIFY(future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    const NativeProviderSelection selection = future.get();
    QVERIFY(!selection.ok);
    QCOMPARE(selection.code, QStringLiteral("cancelled"));
    QCOMPARE(probe.observedCancellation, &cancelled);
}

void SeparationRuntimeTest::productionAutoSelectionFallsBackToCpuWithReason()
{
    ControlledNativeProbe probe;
    NativeStartRequest request;
    request.device = DeviceMode::Auto;
    CancellationToken cancelled;
    const TrustedModelProfile profile = *trustedProfileForHashes({QStringLiteral(
        "e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63")});

    const NativeProviderSelection selection =
        selectNativeProvider(request, profile, cancelled, probe);
    QVERIFY(selection.ok);
    QCOMPARE(selection.provider, ExecutionProvider::Cpu);
    QCOMPARE(selection.fallbackReason, QStringLiteral("minimal inference failed"));
    QCOMPARE(probe.gpuCalls, 1);
    QCOMPARE(probe.cpuCalls, 1);
}

void SeparationRuntimeTest::directMlSessionUsesApprovedBytesAndDisablesCpuFallback()
{
    const QByteArray approvedModel = QByteArray::fromHex("42021011");
    CancellationToken cancelled;
    OrtModelSession session;
    const OrtOperationResult opened = session.open(
        QString::fromUtf8(AG_SEPARATION_FAKE_ORT_PATH), approvedModel,
        ExecutionProvider::DirectMl, 0, cancelled);
    QVERIFY(!opened.ok);
    QCOMPARE(opened.code, QStringLiteral("model_open_failed"));
    QCOMPARE(opened.message, QStringLiteral("strict-array-session"));
}

void SeparationRuntimeTest::sessionLoadUsesTheStartCancellationTokenAndUnsubscribesSafely()
{
    QLibrary controls(QString::fromUtf8(AG_SEPARATION_FAKE_ORT_PATH));
    QVERIFY2(controls.load(), qPrintable(controls.errorString()));
    const auto entered = reinterpret_cast<bool(*)()>(
        controls.resolve("AgSeparationFakeOrtLoadEntered"));
    const auto calls = reinterpret_cast<int(*)()>(
        controls.resolve("AgSeparationFakeOrtLoadCancellationCalls"));
    const auto reset = reinterpret_cast<void(*)()>(
        controls.resolve("AgSeparationFakeOrtResetLoadState"));
    QVERIFY(entered != nullptr);
    QVERIFY(calls != nullptr);
    QVERIFY(reset != nullptr);

    reset();
    CancellationToken cancelled;
    OrtModelSession session;
    auto future = std::async(std::launch::async, [&] {
        return session.open(QString::fromUtf8(AG_SEPARATION_FAKE_ORT_PATH),
                            QByteArrayLiteral("block-load"),
                            ExecutionProvider::Cpu, 0, cancelled);
    });
    QTRY_VERIFY_WITH_TIMEOUT(entered(), 2000);
    cancelled.cancel();
    QVERIFY(future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    const OrtOperationResult interrupted = future.get();
    QVERIFY(!interrupted.ok);
    QCOMPARE(interrupted.code, QStringLiteral("cancelled"));
    QCOMPARE(calls(), 1);

    reset();
    CancellationToken lateCancellation;
    OrtModelSession completedOpen;
    const OrtOperationResult completed = completedOpen.open(
        QString::fromUtf8(AG_SEPARATION_FAKE_ORT_PATH),
        QByteArray::fromHex("42021011"), ExecutionProvider::DirectMl, 0,
        lateCancellation);
    QVERIFY(!completed.ok);
    QCOMPARE(calls(), 0);
    lateCancellation.cancel();
    QCOMPARE(calls(), 0);
}

void SeparationRuntimeTest::terminateFailureIsReleasedAndReported()
{
    OrtApi api{};
    api.RunOptionsSetTerminate = &rejectedTerminate;
    api.GetErrorMessage = &terminateErrorMessage;
    api.ReleaseStatus = &releaseTerminateStatus;
    releasedTerminateStatuses = 0;

    const OrtOperationResult result = requestOrtRunTermination(
        &api, reinterpret_cast<OrtRunOptions*>(quintptr{2}));
    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("inference_cancel_failed"));
    QCOMPARE(result.message, QStringLiteral("terminate refused"));
    QCOMPARE(releasedTerminateStatuses, 1);
}

void SeparationRuntimeTest::cancellationNotificationDoesNotRequirePolling()
{
    CancellationToken cancellation;
    QSemaphore notified;
    auto subscription = cancellation.notifyOnCancel([&] { notified.release(); });
    cancellation.cancel();
    QVERIFY(notified.tryAcquire(1, 100));
    QVERIFY(cancellation.isCancelled());
}

QTEST_GUILESS_MAIN(SeparationRuntimeTest)
#include "separation_runtime_test.moc"
