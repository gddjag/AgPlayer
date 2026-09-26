#include "native_worker_backend.hpp"

#include "decoder.hpp"

#include <QFile>
#include <QJsonArray>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

using namespace agplayer::separation;

namespace {

class SequencedNativeProviderProbe final : public NativeProviderProbe {
public:
    QVector<DxgiAdapterInfo> adapters{
        {11, QStringLiteral("Primary GPU"), 2048},
        {22, QStringLiteral("Secondary GPU"), 1024},
    };
    int successfulGpuAdapter = -1;
    bool cpuSucceeds = true;
    QVector<int> gpuAttempts;
    int cpuCalls = 0;
    int cudaCalls = 0;
    bool cudaSucceeds = true;

    QVector<DxgiAdapterInfo> hardwareAdapters() override
    {
        return adapters;
    }

    BackendResult prove(const NativeStartRequest&, const TrustedModelProfile&,
                        ExecutionProvider provider, int adapterId,
                        const CancellationToken&) override
    {
        if (provider == ExecutionProvider::Cuda) {
            ++cudaCalls;
            return cudaSucceeds ? BackendResult{true, {}, {}, {}}
                : BackendResult{false, QStringLiteral("cuda_failed"), QStringLiteral("CUDA rejected the model"), {}};
        }
        if (provider == ExecutionProvider::DirectMl || provider == ExecutionProvider::CoreMl) {
            gpuAttempts.push_back(adapterId);
            if (adapterId == successfulGpuAdapter) {
                return {true, {}, {}, {}};
            }
            return {false, QStringLiteral("gpu_probe_failed"),
                    QStringLiteral("adapter %1 rejected").arg(adapterId), {}};
        }
        ++cpuCalls;
        return cpuSucceeds
            ? BackendResult{true, {}, {}, {}}
            : BackendResult{false, QStringLiteral("cpu_probe_failed"),
                            QStringLiteral("CPU rejected"), {}};
    }
};

} // namespace

class SeparationNativeBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void startRequestRequiresBoundedNativeFields();
    void startRequestRejectsOversizedPathsAndNames();
    void startRequestAcceptsOnlyCompleteCustomModelDeclarations();
    void sha256IsComputedFromTheActualFile();
    void streamedModelCaptureIsCancellableAndBounded();
    void capturedModelBytesRejectAReplacementAfterTrustValidation();
    void ffmpegWaveWriterRoundTripsThroughTheProductionDecoder();
    void ffmpegWaveWriterPropagatesDelayedAvioCloseErrors();
    void inferenceProgressLeavesRoomForVerificationAndCompletion();
    void readsTheActualDefaultOnnxOpset();
    void macosProvidersRequireInferenceAndPreserveCpuFallback();
    void gpuSelectionTriesEveryAdapterUntilOnePasses();
    void gpuSelectionReportsEveryAdapterFailure();
    void autoSelectionTriesEveryGpuBeforeCpuFallback();
    void demucsAutoAvoidsUnboundedDirectMlCompilation();
    void demucsGpuUsesAnIsolatedCudaRuntime();
    void probeReportsHardwareGpuCandidateWithoutClaimingInferenceValidation();
};

void SeparationNativeBackendTest::demucsGpuUsesAnIsolatedCudaRuntime()
{
#ifdef Q_OS_MACOS
    QSKIP("Windows DirectML/CUDA contract; macOS CoreML/CPU is tested separately", "");
#endif
    QTemporaryDir runtime;
    QFile provider(runtime.filePath("onnxruntime_providers_cuda.dll"));
    QVERIFY(provider.open(QIODevice::WriteOnly));
    provider.write("test provider presence");
    provider.close();
    NativeStartRequest request;
    request.runtimePath = runtime.filePath("onnxruntime.dll");
    request.device = DeviceMode::Gpu;
    TrustedModelProfile profile;
    profile.family = QStringLiteral("demucs");
    SequencedNativeProviderProbe probe;
    CancellationToken cancellation;
    const auto result = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY2(result.ok, qPrintable(result.message));
    QCOMPARE(result.provider, ExecutionProvider::Cuda);
    QCOMPARE(probe.cudaCalls, 1);
    QCOMPARE(probe.cpuCalls, 0);
    probe.cudaSucceeds = false;
    const auto forced = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY(!forced.ok);
    QCOMPARE(probe.cpuCalls, 0);
    request.device = DeviceMode::Auto;
    const auto automatic = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY(automatic.ok);
    QCOMPARE(automatic.provider, ExecutionProvider::Cpu);
    QVERIFY(automatic.fallbackReason.contains("CUDA"));
}

void SeparationNativeBackendTest::startRequestRequiresBoundedNativeFields()
{
    const StartRequestParseResult empty = parseStartRequest({});
    QVERIFY(!empty.ok);
    QCOMPARE(empty.code, QStringLiteral("invalid_start_request"));

    const QJsonObject valid{
        {QStringLiteral("runtimePath"), QStringLiteral("C:/runtime/onnxruntime.dll")},
        {QStringLiteral("inputPath"), QStringLiteral("C:/audio/source.wav")},
        {QStringLiteral("modelFiles"), QJsonArray{QStringLiteral("C:/models/model.onnx")}},
        {QStringLiteral("outputDirectory"), QStringLiteral("C:/audio")},
        {QStringLiteral("baseName"), QStringLiteral("source")},
        {QStringLiteral("directoryName"), QStringLiteral("source-two-stem")},
        {QStringLiteral("modelName"), QStringLiteral("MDX Inst HQ 3")},
        {QStringLiteral("extension"), QStringLiteral("flac")},
        {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals")}},
        {QStringLiteral("stemLabels"), QJsonArray{QStringLiteral("Vocals")}},
        {QStringLiteral("device"), QStringLiteral("auto")},
    };
    const StartRequestParseResult parsed = parseStartRequest(valid);
    QVERIFY2(parsed.ok, qPrintable(parsed.message));
    QCOMPARE(parsed.request.device, DeviceMode::Auto);
    QCOMPARE(parsed.request.modelName, QStringLiteral("MDX Inst HQ 3"));
    QCOMPARE(parsed.request.modelFiles.size(), 1);
    QCOMPARE(parsed.request.stems, (QStringList{QStringLiteral("vocals")}));

    QJsonObject mp3 = valid;
    mp3.insert(QStringLiteral("extension"), QStringLiteral("mp3"));
    QVERIFY(parseStartRequest(mp3).ok);

    QJsonObject unsupportedExtension = valid;
    unsupportedExtension.insert(QStringLiteral("extension"),
                                QStringLiteral("executable"));
    QCOMPARE(parseStartRequest(unsupportedExtension).code,
             QStringLiteral("invalid_start_request"));

    QJsonObject tooMany = valid;
    QJsonArray files;
    for (int index = 0; index < 5; ++index) files.push_back(QString::number(index));
    tooMany.insert(QStringLiteral("modelFiles"), files);
    QCOMPARE(parseStartRequest(tooMany).code, QStringLiteral("invalid_start_request"));

    QJsonObject unknown = valid;
    unknown.insert(QStringLiteral("surprise"), true);
    QCOMPARE(parseStartRequest(unknown).code, QStringLiteral("invalid_start_request"));
}

void SeparationNativeBackendTest::startRequestRejectsOversizedPathsAndNames()
{
    const QJsonObject base{
        {QStringLiteral("runtimePath"), QStringLiteral("C:/runtime/onnxruntime.dll")},
        {QStringLiteral("inputPath"), QStringLiteral("C:/audio/source.wav")},
        {QStringLiteral("modelFiles"), QJsonArray{QStringLiteral("C:/models/model.onnx")}},
        {QStringLiteral("outputDirectory"), QStringLiteral("C:/audio")},
        {QStringLiteral("baseName"), QStringLiteral("source")},
        {QStringLiteral("directoryName"), QStringLiteral("source-two-stem")},
        {QStringLiteral("modelName"), QStringLiteral("model")},
        {QStringLiteral("extension"), QStringLiteral("wav")},
        {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals")}},
        {QStringLiteral("stemLabels"), QJsonArray{QStringLiteral("Vocals")}},
        {QStringLiteral("device"), QStringLiteral("cpu")},
    };
    for (const QString& field : {QStringLiteral("runtimePath"),
                                 QStringLiteral("inputPath"),
                                 QStringLiteral("outputDirectory")}) {
        QJsonObject oversized = base;
        oversized.insert(field, QString(32768, QLatin1Char('x')));
        QCOMPARE(parseStartRequest(oversized).code,
                 QStringLiteral("invalid_start_request"));
    }
    QJsonObject longModel = base;
    longModel.insert(QStringLiteral("modelFiles"),
                     QJsonArray{QString(32768, QLatin1Char('m'))});
    QCOMPARE(parseStartRequest(longModel).code,
             QStringLiteral("invalid_start_request"));
    QJsonObject longBase = base;
    longBase.insert(QStringLiteral("baseName"), QString(241, QLatin1Char('b')));
    QCOMPARE(parseStartRequest(longBase).code,
             QStringLiteral("invalid_start_request"));
    QJsonObject longModelName = base;
    longModelName.insert(QStringLiteral("modelName"),
                         QString(241, QLatin1Char('m')));
    QCOMPARE(parseStartRequest(longModelName).code,
             QStringLiteral("invalid_start_request"));
    QJsonObject longStemLabel = base;
    longStemLabel.insert(QStringLiteral("stemLabels"),
                         QJsonArray{QString(241, QLatin1Char('s'))});
    QCOMPARE(parseStartRequest(longStemLabel).code,
             QStringLiteral("invalid_start_request"));
    QJsonObject mismatchedStemLabels = base;
    mismatchedStemLabels.insert(QStringLiteral("stemLabels"), QJsonArray{});
    QCOMPARE(parseStartRequest(mismatchedStemLabels).code,
             QStringLiteral("invalid_start_request"));
    QJsonObject longExtension = base;
    longExtension.insert(QStringLiteral("extension"), QString(17, QLatin1Char('e')));
    QCOMPARE(parseStartRequest(longExtension).code,
             QStringLiteral("invalid_start_request"));
}

void SeparationNativeBackendTest::
startRequestAcceptsOnlyCompleteCustomModelDeclarations()
{
    const QString hash(64, QLatin1Char('a'));
    const QJsonObject base{
        {QStringLiteral("runtimePath"), QStringLiteral("C:/runtime/onnxruntime.dll")},
        {QStringLiteral("inputPath"), QStringLiteral("C:/audio/source.wav")},
        {QStringLiteral("modelFiles"), QJsonArray{QStringLiteral("C:/models/custom.onnx")}},
        {QStringLiteral("outputDirectory"), QStringLiteral("C:/audio")},
        {QStringLiteral("baseName"), QStringLiteral("source")},
        {QStringLiteral("directoryName"), QStringLiteral("source-custom")},
        {QStringLiteral("modelName"), QStringLiteral("Custom MDX")},
        {QStringLiteral("extension"), QStringLiteral("wav")},
        {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals")}},
        {QStringLiteral("stemLabels"), QJsonArray{QStringLiteral("Vocals")}},
        {QStringLiteral("device"), QStringLiteral("cpu")},
        {QStringLiteral("modelProfile"), QStringLiteral("uvr-mdxnet-kara")},
        {QStringLiteral("modelSha256"), QJsonArray{hash}},
        {QStringLiteral("modelBytes"), QJsonArray{12345}},
        {QStringLiteral("modelRoles"), QJsonArray{}},
    };
    const StartRequestParseResult parsed = parseStartRequest(base);
    QVERIFY2(parsed.ok, qPrintable(parsed.message));
    QCOMPARE(parsed.request.modelProfile, QStringLiteral("uvr-mdxnet-kara"));
    QCOMPARE(parsed.request.modelSha256, QStringList{hash});
    QCOMPARE(parsed.request.modelBytes, (QVector<qint64>{12'345}));

    for (const QString& missing : {QStringLiteral("modelProfile"),
                                   QStringLiteral("modelSha256"),
                                   QStringLiteral("modelBytes")}) {
        QJsonObject incomplete = base;
        incomplete.remove(missing);
        QCOMPARE(parseStartRequest(incomplete).code,
                 QStringLiteral("invalid_start_request"));
    }

    QJsonObject badHash = base;
    badHash.insert(QStringLiteral("modelSha256"),
                   QJsonArray{QStringLiteral("not-a-hash")});
    QCOMPARE(parseStartRequest(badHash).code,
             QStringLiteral("invalid_start_request"));
}

void SeparationNativeBackendTest::sha256IsComputedFromTheActualFile()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("模型.onnx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("abc"), qint64{3});
    file.close();
    CancellationToken active;
    const ModelArtifactReadResult artifact = readModelArtifact(
        path, false, {}, active);
    QVERIFY2(artifact.ok, qPrintable(artifact.message));
    QCOMPARE(artifact.sha256,
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void SeparationNativeBackendTest::streamedModelCaptureIsCancellableAndBounded()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("large.onnx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(3 * 1024 * 1024, 'm')),
             qint64{3 * 1024 * 1024});
    file.close();

    CancellationToken cancelled;
    const QString trustedSha = QStringLiteral(
        "f695ffaeeacc71331952f965959777d5ef652cea9754523ed8929e21017708c5");
    const QVector<TrustedModelFile> trustedFiles{
        {trustedSha, 3 * 1024 * 1024}};
    const ModelArtifactReadResult interrupted = readModelArtifact(
        path, true, trustedFiles, cancelled,
        [&](qint64 completed, qint64) {
            if (completed >= 1024 * 1024) cancelled.cancel();
        });
    QVERIFY(!interrupted.ok);
    QCOMPARE(interrupted.code, QStringLiteral("cancelled"));
    QVERIFY(interrupted.bytes.isEmpty());

    const QString oversizedPath = temporary.filePath(QStringLiteral("wrong-size.onnx"));
    QFile oversized(oversizedPath);
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    QVERIFY(oversized.resize(3 * 1024 * 1024 + 1));
    oversized.close();
    CancellationToken active;
    const ModelArtifactReadResult rejected = readModelArtifact(
        oversizedPath, true, trustedFiles, active);
    QVERIFY(!rejected.ok);
    QCOMPARE(rejected.code, QStringLiteral("model_size_mismatch"));
}

void SeparationNativeBackendTest::capturedModelBytesRejectAReplacementAfterTrustValidation()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("replace.onnx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("trusted"), qint64{7});
    file.close();

    CancellationToken cancelled;
    const ModelArtifactReadResult trusted = readModelArtifact(
        path, false, {}, cancelled);
    QVERIFY2(trusted.ok, qPrintable(trusted.message));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("changed"), qint64{7});
    file.close();
    const QVector<TrustedModelFile> trustedFiles{
        {trusted.sha256, qint64{7}}};
    const ModelArtifactReadResult replaced = readModelArtifact(
        path, true, trustedFiles, cancelled);
    QVERIFY(!replaced.ok);
    QCOMPARE(replaced.code, QStringLiteral("model_changed"));
    QVERIFY(replaced.bytes.isEmpty());
}

void SeparationNativeBackendTest::ffmpegWaveWriterRoundTripsThroughTheProductionDecoder()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("流式.wav"));
    FfmpegWaveWriter writer;
    QVERIFY(writer.open(path, 44100, 2));
    const QVector<float> expected{0.25F, -0.25F, 0.5F, -0.5F};
    QVERIFY(writer.write(expected));
    QVERIFY(writer.finish());

    agplayer::Decoder decoder;
    QCOMPARE(decoder.open(path.toUtf8().toStdString(), 44100, 2), AG_OK);
    agplayer::DecodedAudioBlock decoded;
    QCOMPARE(decoder.read(decoded), AG_OK);
    QVERIFY(decoded.frames >= 2);
    for (qsizetype index = 0; index < expected.size(); ++index) {
        QVERIFY(std::abs(decoded.samples.at(index) - expected.at(index)) < 1.0e-6F);
    }
}

void SeparationNativeBackendTest::ffmpegWaveWriterPropagatesDelayedAvioCloseErrors()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("delayed-error.wav"));
    FfmpegWaveWriter writer([](AVIOContext** output) {
        const int closeResult = avio_closep(output);
        return closeResult < 0 ? closeResult : AVERROR(EIO);
    });
    QVERIFY(writer.open(path, 44100, 2));
    QVERIFY(writer.write({0.25F, -0.25F}));
    QVERIFY(!writer.finish());
    QVERIFY(writer.errorString().contains(QStringLiteral("close"),
                                          Qt::CaseInsensitive));
}

void SeparationNativeBackendTest::inferenceProgressLeavesRoomForVerificationAndCompletion()
{
    const double halfway = nativeInferenceProgress(1, 2);
    const double finished = nativeInferenceProgress(2, 2);
    QVERIFY(halfway >= 0.0);
    QVERIFY(finished > halfway);
    QVERIFY(finished < 0.98);
}

void SeparationNativeBackendTest::readsTheActualDefaultOnnxOpset()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("tiny.onnx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // ModelProto.opset_import (field 8, length-delimited) containing
    // OperatorSetIdProto.version (field 2, varint) = 17.
    QCOMPARE(file.write(QByteArray::fromHex("42021011")), qint64{4});
    file.close();
    QFile input(path);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(readOnnxDefaultOpset(input.readAll()), 17);

    const QString customOnly = temporary.filePath(QStringLiteral("custom.onnx"));
    QFile custom(customOnly);
    QVERIFY(custom.open(QIODevice::WriteOnly));
    // domain="custom" followed by version=17: no default ai.onnx opset.
    QCOMPARE(custom.write(QByteArray::fromHex("420a0a06637573746f6d1011")), qint64{12});
    custom.close();
    QFile customInput(customOnly);
    QVERIFY(customInput.open(QIODevice::ReadOnly));
    QCOMPARE(readOnnxDefaultOpset(customInput.readAll()), -1);
}

void SeparationNativeBackendTest::gpuSelectionTriesEveryAdapterUntilOnePasses()
{
#ifdef Q_OS_MACOS
    QSKIP("Windows DirectML/CUDA contract; macOS CoreML/CPU is tested separately", "");
#endif
    SequencedNativeProviderProbe probe;
    probe.successfulGpuAdapter = 22;
    NativeStartRequest request;
    request.device = DeviceMode::Gpu;
    CancellationToken cancelled;

    const NativeProviderSelection selection = selectNativeProvider(
        request, TrustedModelProfile{}, cancelled, probe);

    QVERIFY(selection.ok);
    QCOMPARE(selection.provider, ExecutionProvider::DirectMl);
    QCOMPARE(selection.adapterId, 22);
    QCOMPARE(probe.gpuAttempts, (QVector<int>{11, 22}));
    QCOMPARE(probe.cpuCalls, 0);
}

void SeparationNativeBackendTest::gpuSelectionReportsEveryAdapterFailure()
{
#ifdef Q_OS_MACOS
    QSKIP("Windows DirectML/CUDA contract; macOS CoreML/CPU is tested separately", "");
#endif
    SequencedNativeProviderProbe probe;
    NativeStartRequest request;
    request.device = DeviceMode::Gpu;
    CancellationToken cancelled;

    const NativeProviderSelection selection = selectNativeProvider(
        request, TrustedModelProfile{}, cancelled, probe);

    QVERIFY(!selection.ok);
    QCOMPARE(selection.provider, ExecutionProvider::DirectMl);
    QCOMPARE(selection.code, QStringLiteral("gpu_probe_failed"));
    QCOMPARE(probe.gpuAttempts, (QVector<int>{11, 22}));
    QCOMPARE(probe.cpuCalls, 0);
    QVERIFY(selection.message.contains(QStringLiteral("Primary GPU")));
    QVERIFY(selection.message.contains(QStringLiteral("adapter 11 rejected")));
    QVERIFY(selection.message.contains(QStringLiteral("Secondary GPU")));
    QVERIFY(selection.message.contains(QStringLiteral("adapter 22 rejected")));
}

void SeparationNativeBackendTest::demucsAutoAvoidsUnboundedDirectMlCompilation()
{
#ifdef Q_OS_MACOS
    QSKIP("Windows DirectML/CUDA contract; macOS CoreML/CPU is tested separately", "");
#endif
    SequencedNativeProviderProbe probe;
    probe.successfulGpuAdapter = 11;
    NativeStartRequest request;
    request.device = DeviceMode::Auto;
    TrustedModelProfile profile;
    profile.family = QStringLiteral("demucs");
    CancellationToken cancelled;
    const auto automatic = selectNativeProvider(request, profile, cancelled, probe);
    QVERIFY(automatic.ok);
    QCOMPARE(automatic.provider, ExecutionProvider::Cpu);
    QVERIFY(!automatic.fallbackReason.isEmpty());
    QVERIFY(probe.gpuAttempts.isEmpty());
    request.device = DeviceMode::Gpu;
    const auto forced = selectNativeProvider(request, profile, cancelled, probe);
    QVERIFY(!forced.ok);
    QCOMPARE(forced.code, QStringLiteral("demucs_directml_unsupported"));
}

void SeparationNativeBackendTest::autoSelectionTriesEveryGpuBeforeCpuFallback()
{
#ifdef Q_OS_MACOS
    QSKIP("Windows DirectML/CUDA contract; macOS CoreML/CPU is tested separately", "");
#endif
    SequencedNativeProviderProbe probe;
    NativeStartRequest request;
    request.device = DeviceMode::Auto;
    CancellationToken cancelled;

    const NativeProviderSelection selection = selectNativeProvider(
        request, TrustedModelProfile{}, cancelled, probe);

    QVERIFY(selection.ok);
    QCOMPARE(selection.provider, ExecutionProvider::Cpu);
    QCOMPARE(probe.gpuAttempts, (QVector<int>{11, 22}));
    QCOMPARE(probe.cpuCalls, 1);
    QVERIFY(selection.fallbackReason.contains(QStringLiteral("Primary GPU")));
    QVERIFY(selection.fallbackReason.contains(QStringLiteral("adapter 11 rejected")));
    QVERIFY(selection.fallbackReason.contains(QStringLiteral("Secondary GPU")));
    QVERIFY(selection.fallbackReason.contains(QStringLiteral("adapter 22 rejected")));
}

void SeparationNativeBackendTest::
probeReportsHardwareGpuCandidateWithoutClaimingInferenceValidation()
{
    const QString runtimePath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("onnxruntime_test.dll"));
    if (!QFileInfo::exists(runtimePath))
        QSKIP("The native-backend test runtime is not available", "");

    NativeWorkerBackend backend;
    const BackendResult result = backend.probe(
        {{QStringLiteral("runtimePath"), runtimePath}});
    QVERIFY2(result.ok, qPrintable(result.message));

    const bool hasHardwareAdapter = !result.payload.value(
        QStringLiteral("adapters")).toArray().isEmpty();
    QCOMPARE(result.payload.value(QStringLiteral("gpu")).toBool(),
             hasHardwareAdapter);
    const QString reason = result.payload.value(
        QStringLiteral("gpuReason")).toString();
    if (hasHardwareAdapter) {
        QCOMPARE(reason, QStringLiteral(
            "Hardware adapter candidate detected; DirectML will be validated "
            "with a trusted model when separation starts"));
    } else {
        QVERIFY(!result.payload.value(QStringLiteral("gpu")).toBool());
        QCOMPARE(reason, QStringLiteral("No hardware DXGI adapter is available"));
    }
}

void SeparationNativeBackendTest::macosProvidersRequireInferenceAndPreserveCpuFallback()
{
#ifndef Q_OS_MACOS
    QSKIP("macOS hardware policy; Windows adapters are tested separately", "");
#else
    SequencedNativeProviderProbe probe;
    NativeStartRequest request;
    TrustedModelProfile profile;
    profile.family = QStringLiteral("demucs");
    CancellationToken cancellation;
    request.device = DeviceMode::Cpu;
    auto result = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY(result.ok);
    QCOMPARE(result.provider, ExecutionProvider::Cpu);
    QCOMPARE(probe.cpuCalls, 1);
    QVERIFY(probe.gpuAttempts.isEmpty());
    request.device = DeviceMode::Auto;
    result = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY(result.ok);
    QCOMPARE(result.provider, ExecutionProvider::Cpu);
    QVERIFY(!result.fallbackReason.isEmpty());
    const int cpuCalls = probe.cpuCalls;
    request.device = DeviceMode::Gpu;
    result = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY(!result.ok);
    QCOMPARE(probe.cpuCalls, cpuCalls);
#ifdef Q_PROCESSOR_ARM_64
    QVERIFY(!probe.gpuAttempts.isEmpty());
    probe.successfulGpuAdapter = 0;
    result = selectNativeProvider(request, profile, cancellation, probe);
    QVERIFY(result.ok);
    QCOMPARE(result.provider, ExecutionProvider::CoreMl);
#else
    QVERIFY(probe.gpuAttempts.isEmpty());
#endif
#endif
}

QTEST_GUILESS_MAIN(SeparationNativeBackendTest)
#include "separation_native_backend_test.moc"
