#include "native_worker_backend.hpp"

#include "demucs_adapter.hpp"
#include "mdx_adapter.hpp"
#include "ort_session.hpp"
#include "output_transaction.hpp"
#include "trusted_profiles.hpp"

#include "decoder.hpp"
#include "transcoder.hpp"

#include <agplayer/c_api.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <QSet>
#include <QtEndian>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

namespace agplayer::separation {
namespace {

BackendResult fail(const QString& code, const QString& message)
{
    return {false, code, message, {}};
}

BackendResult transactionFailure(const TransactionResult& transaction,
                                 const BackendResult* cause = nullptr)
{
    QJsonArray remainingPaths;
    for (const QString& path : transaction.outputs) remainingPaths.push_back(path);
    QJsonObject diagnostics;
    if (!remainingPaths.isEmpty()) {
        diagnostics.insert(QStringLiteral("remainingPaths"), remainingPaths);
    }
    if (!transaction.causeCode.isEmpty()) {
        diagnostics.insert(QStringLiteral("causeCode"), transaction.causeCode);
        diagnostics.insert(QStringLiteral("causeMessage"), transaction.causeMessage);
    } else if (cause != nullptr) {
        diagnostics.insert(QStringLiteral("causeCode"), cause->code);
        diagnostics.insert(QStringLiteral("causeMessage"), cause->message);
    }
    return {false, transaction.code, transaction.message, diagnostics};
}

BackendResult cleanupAfterFailure(OutputTransaction& transaction,
                                  const BackendResult& cause)
{
    const TransactionResult cleanup = transaction.cancel();
    return cleanup.ok ? cause : transactionFailure(cleanup, &cause);
}

QStringList stringArray(const QJsonValue& value, qsizetype maximumEntryLength,
                        bool* valid)
{
    QStringList result;
    if (!value.isArray()) {
        *valid = false;
        return result;
    }
    for (const QJsonValue& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()
            || entry.toString().size() > maximumEntryLength) {
            *valid = false;
            return {};
        }
        result.push_back(entry.toString());
    }
    return result;
}

QVector<qint64> integerArray(const QJsonValue& value, bool* valid)
{
    QVector<qint64> result;
    if (!value.isArray()) {
        *valid = false;
        return result;
    }
    for (const QJsonValue& entry : value.toArray()) {
        if (!entry.isDouble()) {
            *valid = false;
            return {};
        }
        const qint64 number = entry.toInteger(-1);
        if (number <= 0) {
            *valid = false;
            return {};
        }
        result.push_back(number);
    }
    return result;
}

QString utf8Error(ag_result result)
{
    const char* detail = ag_last_error();
    return detail != nullptr && *detail != '\0'
        ? QString::fromUtf8(detail)
        : QStringLiteral("AgPlayer core error %1").arg(static_cast<int>(result));
}

qint64 countDecodedFrames(const QString& inputPath,
                          const CancellationToken& cancelled,
                          QString* error)
{
    agplayer::Decoder decoder;
    const std::string path = inputPath.toUtf8().toStdString();
    ag_result result = decoder.open(path, 44100, 2);
    if (result != AG_OK) {
        *error = utf8Error(result);
        return -1;
    }
    qint64 frames = 0;
    for (;;) {
        if (cancelled.isCancelled()) return -2;
        agplayer::DecodedAudioBlock block;
        result = decoder.read(block);
        if (result != AG_OK) {
            *error = utf8Error(result);
            return -1;
        }
        frames += static_cast<qint64>(block.frames);
        if (block.end_of_stream) return frames;
    }
}

using ChunkConsumer = std::function<BackendResult(
    qint64, const QVector<float>&, qsizetype)>;

BackendResult streamPcmChunks(const QString& inputPath,
                              const QVector<qint64>& starts,
                              int chunkFrames,
                              const CancellationToken& cancelled,
                              const ChunkConsumer& consume)
{
    agplayer::Decoder decoder;
    const ag_result opened = decoder.open(inputPath.toUtf8().toStdString(), 44100, 2);
    if (opened != AG_OK) return fail(QStringLiteral("decode_failed"), utf8Error(opened));

    QVector<float> buffer;
    qint64 bufferBase = 0;
    qint64 decodedEnd = 0;
    bool end = false;
    for (qsizetype chunkIndex = 0; chunkIndex < starts.size(); ++chunkIndex) {
        const qint64 start = starts.at(chunkIndex);
        const qint64 wantedEnd = start + chunkFrames;
        while (!end && decodedEnd < wantedEnd) {
            if (cancelled.isCancelled()) {
                return fail(QStringLiteral("cancelled"),
                            QStringLiteral("Separation cancelled"));
            }
            agplayer::DecodedAudioBlock block;
            const ag_result read = decoder.read(block);
            if (read != AG_OK) return fail(QStringLiteral("decode_failed"), utf8Error(read));
            const qsizetype oldSize = buffer.size();
            buffer.resize(oldSize + static_cast<qsizetype>(block.frames * 2));
            if (block.frames > 0) {
                std::memcpy(buffer.data() + oldSize, block.samples.data(),
                            block.frames * 2 * sizeof(float));
            }
            decodedEnd += static_cast<qint64>(block.frames);
            end = block.end_of_stream;
        }
        QVector<float> chunk(chunkFrames * 2);
        const qint64 offsetFrames = start - bufferBase;
        const qint64 availableFrames = std::max<qint64>(
            0, std::min<qint64>(chunkFrames, decodedEnd - start));
        if (offsetFrames < 0
            || (offsetFrames + availableFrames) * 2 > buffer.size()) {
            return fail(QStringLiteral("decode_failed"),
                        QStringLiteral("Streaming decoder lost chunk alignment"));
        }
        if (availableFrames > 0) {
            std::memcpy(chunk.data(), buffer.constData() + offsetFrames * 2,
                        static_cast<size_t>(availableFrames * 2) * sizeof(float));
        }
        BackendResult result = consume(start, chunk, chunkIndex);
        if (!result.ok) return result;

        const qint64 nextStart = chunkIndex + 1 < starts.size()
            ? starts.at(chunkIndex + 1) : decodedEnd;
        const qint64 discardFrames = std::clamp<qint64>(
            nextStart - bufferBase, 0, buffer.size() / 2);
        buffer.remove(0, static_cast<qsizetype>(discardFrames * 2));
        bufferBase += discardFrames;
    }
    return {true, {}, {}, {}};
}

BackendResult validateSession(const TrustedModelProfile& profile,
                              OrtModelSession& session)
{
    const ContractValidationResult validation =
        validateModelMetadata(profile, session.metadata());
    return validation.ok ? BackendResult{true, {}, {}, {}}
                         : fail(validation.code, validation.message);
}

BackendResult loadTrustedModelArtifact(const QString& modelPath,
                                       const TrustedModelProfile& profile,
                                       const CancellationToken& cancelled,
                                       ModelArtifactReadResult* artifact)
{
    *artifact = readModelArtifact(modelPath, true,
                                  trustedFilesForProfile(profile), cancelled);
    return artifact->ok ? BackendResult{true, {}, {}, {}}
                        : fail(artifact->code, artifact->message);
}

BackendResult openSession(const NativeStartRequest& request,
                          const TrustedModelProfile& profile,
                          const QByteArray& approvedModelBytes,
                          ExecutionProvider provider,
                          int adapterId, const CancellationToken& cancelled,
                          std::unique_ptr<OrtModelSession>* session)
{
    auto candidate = std::make_unique<OrtModelSession>();
    OrtOperationResult opened = candidate->open(request.runtimePath,
                                                approvedModelBytes, provider,
                                                adapterId, cancelled);
    if (!opened.ok) return fail(opened.code, opened.message);
    BackendResult validated = validateSession(profile, *candidate);
    if (!validated.ok) return validated;
    *session = std::move(candidate);
    return {true, {}, {}, {}};
}

BackendResult proveProvider(const NativeStartRequest& request,
                            const TrustedModelProfile& profile,
                            ExecutionProvider provider, int adapterId,
                            const CancellationToken& cancelled)
{
#ifdef Q_OS_MACOS
    const QStringList probePaths = request.modelFiles;
#else
    const QStringList probePaths = request.modelFiles.mid(0, 1);
#endif
    for (const QString& modelPath : probePaths) {
    ModelArtifactReadResult artifact;
    BackendResult loaded = loadTrustedModelArtifact(
        modelPath, profile, cancelled, &artifact);
    if (!loaded.ok) return loaded;
    std::unique_ptr<OrtModelSession> session;
    BackendResult opened = openSession(request, profile, artifact.bytes,
                                       provider, adapterId, cancelled, &session);
    if (!opened.ok) return opened;
    artifact.bytes.clear();
    artifact.bytes.squeeze();
    QVector<qint64> inputShape = profile.inputs.front().shape;
    if (!inputShape.isEmpty() && inputShape.front() < 0) inputShape.front() = 1;
    QVector<qint64> outputShape = profile.outputs.front().shape;
    if (!outputShape.isEmpty() && outputShape.front() < 0) outputShape.front() = 1;
    qsizetype count = 1;
    for (qint64 dimension : inputShape) count *= dimension;
    QVector<float> zeros(count);
    const OrtOperationResult run = session->run(zeros, inputShape, outputShape,
                                                 cancelled);
    if (!run.ok) return fail(run.code, run.message);
    }
    return request.modelFiles.isEmpty()
        ? fail(QStringLiteral("model_missing"), QStringLiteral("No model files to probe"))
        : BackendResult{true, {}, {}, {}};
}

class OrtNativeProviderProbe final : public NativeProviderProbe {
public:
    QVector<DxgiAdapterInfo> hardwareAdapters() override
    {
        return enumerateDxgiHardwareAdapters();
    }

    BackendResult prove(const NativeStartRequest& request,
                        const TrustedModelProfile& profile,
                        ExecutionProvider provider, int adapterId,
                        const CancellationToken& cancelled) override
    {
        return proveProvider(request, profile, provider, adapterId, cancelled);
    }
};

} // namespace

NativeProviderSelection selectNativeProvider(
    const NativeStartRequest& request, const TrustedModelProfile& profile,
    const CancellationToken& cancelled, NativeProviderProbe& probe)
{
    const auto cancelledResult = [] {
        return NativeProviderSelection{
            false, ExecutionProvider::Cpu, 0, {}, QStringLiteral("cancelled"),
            QStringLiteral("Provider probe cancelled")};
    };
    if (cancelled.isCancelled()) return cancelledResult();
#ifdef Q_OS_MACOS
    QString gpuReason;
#ifdef Q_PROCESSOR_ARM_64
    if (request.device != DeviceMode::Cpu) {
        const BackendResult gpu = probe.prove(request, profile, ExecutionProvider::CoreMl, 0, cancelled);
        if (cancelled.isCancelled()) return cancelledResult();
        if (gpu.ok) return {true, ExecutionProvider::CoreMl, 0, {}, {}, {}};
        gpuReason = QStringLiteral("当前模型 CoreML 验证失败：%1").arg(gpu.message);
    }
#else
    gpuReason = QStringLiteral("Intel Mac 使用 CPU 分离；Apple Silicon 可逐模型验证 CoreML");
#endif
    if (request.device == DeviceMode::Gpu)
        return {false, ExecutionProvider::CoreMl, 0, {}, QStringLiteral("gpu_probe_failed"), gpuReason};
    const BackendResult cpu = probe.prove(request, profile, ExecutionProvider::Cpu, 0, cancelled);
    if (cancelled.isCancelled()) return cancelledResult();
    return {cpu.ok, ExecutionProvider::Cpu, 0, gpuReason, cpu.code, cpu.message};
#else
    const bool cudaRuntime = QFileInfo(QDir(QFileInfo(request.runtimePath).absolutePath())
        .filePath(QStringLiteral("onnxruntime_providers_cuda.dll"))).isFile();
    if (cudaRuntime && request.device != DeviceMode::Cpu) {
        const BackendResult gpu = probe.prove(request, profile, ExecutionProvider::Cuda, 0, cancelled);
        if (cancelled.isCancelled()) return cancelledResult();
        if (gpu.ok) return {true, ExecutionProvider::Cuda, 0, {}, {}, {}};
        if (request.device == DeviceMode::Gpu)
            return {false, ExecutionProvider::Cuda, 0, {}, gpu.code, gpu.message};
        const BackendResult cpu = probe.prove(request, profile, ExecutionProvider::Cpu, 0, cancelled);
        return {cpu.ok, ExecutionProvider::Cpu, 0,
                QStringLiteral("CUDA 模型验证失败，自动回退 CPU：%1").arg(gpu.message), cpu.code, cpu.message};
    }
    // This pinned HTDemucs export expands dramatically during DirectML graph
    // compilation (over 20 GiB on a 4070 Ti SUPER before the first chunk).
    // A provider probe compiles that same graph, so do not attempt it first.
    if (profile.family == QStringLiteral("demucs") && request.device != DeviceMode::Cpu) {
        const QString reason = QStringLiteral(
            "标准五轨模型暂不兼容 DirectML；GPU 分离请在模型卡片配置 NVIDIA CUDA 组件（不需要重装显卡驱动）。自动模式本次回退 CPU，完整歌曲可能耗时很长");
        if (request.device == DeviceMode::Gpu)
            return {false, ExecutionProvider::DirectMl, 0, {},
                    QStringLiteral("demucs_directml_unsupported"), reason};
        const BackendResult cpu = probe.prove(request, profile, ExecutionProvider::Cpu, 0, cancelled);
        if (cancelled.isCancelled()) return cancelledResult();
        return {cpu.ok, ExecutionProvider::Cpu, 0, reason, cpu.code, cpu.message};
    }
    if (request.device == DeviceMode::Cpu) {
        const BackendResult cpu = probe.prove(request, profile,
                                              ExecutionProvider::Cpu, 0,
                                              cancelled);
        if (cancelled.isCancelled()) return cancelledResult();
        return {cpu.ok, ExecutionProvider::Cpu, 0, {}, cpu.code, cpu.message};
    }
    const QVector<DxgiAdapterInfo> adapters = probe.hardwareAdapters();
    QString gpuReason = adapters.isEmpty()
        ? QStringLiteral("No hardware DXGI adapter is available") : QString();
    int lastAdapterId = 0;
    QStringList rawGpuFailures;
    QStringList diagnosticGpuFailures;
    for (const DxgiAdapterInfo& adapter : adapters) {
        lastAdapterId = adapter.deviceId;
        const BackendResult gpu = probe.prove(request, profile,
                                              ExecutionProvider::DirectMl,
                                              adapter.deviceId,
                                              cancelled);
        if (cancelled.isCancelled()) return cancelledResult();
        if (gpu.ok) {
            return {true, ExecutionProvider::DirectMl,
                    adapter.deviceId, {}, {}, {}};
        }
        const QString failure = gpu.message.isEmpty() ? gpu.code : gpu.message;
        rawGpuFailures.push_back(failure);
        diagnosticGpuFailures.push_back(QStringLiteral("%1 (device %2): %3")
                                            .arg(adapter.name.isEmpty()
                                                     ? QStringLiteral("DXGI adapter")
                                                     : adapter.name)
                                            .arg(adapter.deviceId)
                                            .arg(failure));
    }
    if (!rawGpuFailures.isEmpty()) {
        gpuReason = rawGpuFailures.size() == 1
            ? rawGpuFailures.front()
            : diagnosticGpuFailures.join(QStringLiteral("; "));
    }
    if (request.device == DeviceMode::Gpu) {
        return {false, ExecutionProvider::DirectMl, lastAdapterId, {},
                QStringLiteral("gpu_probe_failed"), gpuReason};
    }
    const BackendResult cpu = probe.prove(request, profile,
                                          ExecutionProvider::Cpu, 0,
                                          cancelled);
    if (cancelled.isCancelled()) return cancelledResult();
    return {cpu.ok, ExecutionProvider::Cpu, 0, gpuReason, cpu.code, cpu.message};
#endif
}

namespace {

bool encodeStagingWave(const QString& stagingPath, const QString& outputPath,
                       const CancellationToken& cancelled, QString* error,
                       float outputGain = 1.0F)
{
    QString encodePath = stagingPath;
    if (outputGain < 1.0F) {
        encodePath = stagingPath + QStringLiteral(".gain.wav");
        agplayer::Decoder decoder;
        FfmpegWaveWriter writer;
        if (decoder.open(stagingPath.toUtf8().toStdString(), 44100, 2) != AG_OK
            || !writer.open(encodePath, 44100, 2)) {
            *error = QStringLiteral("Could not open common-gain staging audio");
            return false;
        }
        for (;;) {
            if (cancelled.isCancelled()) return false;
            agplayer::DecodedAudioBlock block;
            if (decoder.read(block) != AG_OK) {
                *error = QStringLiteral("Could not decode common-gain staging audio");
                return false;
            }
            QVector<float> samples(static_cast<qsizetype>(block.frames * 2));
            for (qsizetype i = 0; i < samples.size(); ++i)
                samples[i] = block.samples[static_cast<size_t>(i)] * outputGain;
            if (!samples.isEmpty() && !writer.write(samples)) {
                *error = writer.errorString(); return false;
            }
            if (block.end_of_stream) break;
        }
        if (!writer.finish()) { *error = writer.errorString(); return false; }
    }
    agplayer::TranscodeConfig config;
    config.output_path = outputPath.toUtf8().toStdString();
    config.sample_rate = 44100;
    config.channels = 2;
    std::string coreError;
    const ag_result result = agplayer::transcode(
        encodePath.toUtf8().toStdString(), config, &cancelled.atomicFlag(),
        [](float) {}, coreError);
    if (result != AG_OK) {
        *error = coreError.empty() ? utf8Error(result) : QString::fromUtf8(coreError);
        return false;
    }
    QFile::remove(stagingPath);
    if (encodePath != stagingPath) QFile::remove(encodePath);
    return true;
}

bool verifyAudio(const QString& path)
{
    agplayer::MediaMetadata metadata;
    return agplayer::probe_media_metadata(path.toUtf8().toStdString(), metadata)
            == AG_OK
        && metadata.sample_rate == 44100 && metadata.channels == 2
        && metadata.duration_ms > 0;
}

BackendResult runMdx(const NativeStartRequest& request,
                     const TrustedModelProfile& trusted,
                     const NativeProviderSelection& provider,
                     const CancellationToken& cancelled,
                     const ProgressCallback& progress,
                     OutputTransaction& transaction)
{
    const MdxProfile profile = MdxProfile::forModel(trusted.id);
    QString error;
    const qint64 totalFrames = countDecodedFrames(request.inputPath, cancelled, &error);
    if (totalFrames == -2) return fail(QStringLiteral("cancelled"),
                                       QStringLiteral("Separation cancelled"));
    if (totalFrames <= 0) return fail(QStringLiteral("decode_failed"), error);
    const QVector<qint64> starts = mdxChunkStarts(totalFrames);

    ModelArtifactReadResult artifact;
    BackendResult loaded = loadTrustedModelArtifact(
        request.modelFiles.front(), trusted, cancelled, &artifact);
    if (!loaded.ok) return loaded;
    std::unique_ptr<OrtModelSession> session;
    BackendResult opened = openSession(request, trusted, artifact.bytes,
                                       provider.provider, provider.adapterId,
                                       cancelled, &session);
    if (!opened.ok) return opened;
    artifact.bytes.clear();
    artifact.bytes.squeeze();

    const QString primaryPath = QDir(transaction.temporaryDirectory())
        .filePath(QStringLiteral(".primary.float.wav"));
    const QString complementPath = QDir(transaction.temporaryDirectory())
        .filePath(QStringLiteral(".complement.float.wav"));
    FfmpegWaveWriter primaryWriter;
    FfmpegWaveWriter complementWriter;
    if (!primaryWriter.open(primaryPath, 44100, 2)
        || !complementWriter.open(complementPath, 44100, 2)) {
        return fail(QStringLiteral("output_write_failed"),
                    primaryWriter.errorString() + complementWriter.errorString());
    }

    QVector<float> pendingMix;
    StreamingOverlapAdd primaryPublisher(
        [&](const QVector<float>& published) {
            if (!primaryWriter.write(published)) return false;
            if (pendingMix.size() < published.size()) return false;
            const QVector<float> complement = complementaryStem(
                pendingMix.mid(0, published.size()), published);
            pendingMix.remove(0, published.size());
            return complementWriter.write(complement);
        });
    StreamingOverlapAdd mixPublisher(
        [&](const QVector<float>& published) {
            pendingMix += published;
            return true;
        });

    BackendResult streamed = streamPcmChunks(
        request.inputPath, starts, profile.chunkSamples, cancelled,
        [&](qint64 start, const QVector<float>& mix, qsizetype chunkIndex) {
            const MdxSpectrogram packed = mdxStftPack(mix, profile);
            if (packed.values.isEmpty()) {
                return fail(QStringLiteral("transform_failed"),
                            QStringLiteral("FFmpeg MDX STFT initialization failed"));
            }
            QVector<qint64> inputShape = packed.shape;
            const OrtOperationResult inference = session->run(
                packed.values, inputShape, inputShape, cancelled);
            if (!inference.ok) return fail(inference.code, inference.message);
            MdxSpectrogram output{inputShape, inference.output,
                                  profile.frequencyBins, profile.frames};
            QVector<float> primary = mdxIstftUnpack(output, profile,
                                                    profile.chunkSamples);
            for (float& sample : primary) sample *= profile.compensation;
            const QVector<float> weights = overlapWeights(starts, chunkIndex,
                                                          profile.chunkSamples);
            if (!mixPublisher.add(start, mix, weights)
                || !primaryPublisher.add(start, primary, weights)) {
                return fail(QStringLiteral("output_write_failed"),
                            QStringLiteral("Could not stream MDX overlap output"));
            }
            progress(nativeInferenceProgress(chunkIndex + 1, starts.size()),
                     QStringLiteral("inference"));
            return BackendResult{true, {}, {}, {}};
        });
    if (!streamed.ok) return streamed;
    if (!mixPublisher.finish(totalFrames)
        || !primaryPublisher.finish(totalFrames)
        || !primaryWriter.finish() || !complementWriter.finish()) {
        return fail(QStringLiteral("output_write_failed"),
                    QStringLiteral("Could not finalize MDX staging output"));
    }

    const QHash<QString, QString> staging{
        {profile.primaryStem, primaryPath},
        {profile.primaryStem == QStringLiteral("vocals")
             ? QStringLiteral("instrumental") : QStringLiteral("vocals"),
         complementPath}};
    for (const QString& stem : request.stems) {
        if (!encodeStagingWave(staging.value(stem), transaction.temporaryPath(stem),
                               cancelled, &error)) {
            return fail(cancelled.isCancelled() ? QStringLiteral("cancelled")
                                         : QStringLiteral("encode_failed"), error);
        }
    }
    return {true, {}, {}, {}};
}

QVector<float> planarToInterleaved(const QVector<float>& planar)
{
    const qsizetype frames = planar.size() / 2;
    QVector<float> interleaved(planar.size());
    for (qsizetype frame = 0; frame < frames; ++frame) {
        interleaved[frame * 2] = planar.at(frame);
        interleaved[frame * 2 + 1] = planar.at(frames + frame);
    }
    return interleaved;
}

QVector<float> interleavedToPlanar(const QVector<float>& interleaved)
{
    const qsizetype frames = interleaved.size() / 2;
    QVector<float> planar(interleaved.size());
    for (qsizetype frame = 0; frame < frames; ++frame) {
        planar[frame] = interleaved.at(frame * 2);
        planar[frames + frame] = interleaved.at(frame * 2 + 1);
    }
    return planar;
}

bool deriveAccompanimentWave(const QStringList& contributors,
                             const QString& destination,
                             const CancellationToken& cancelled,
                             QString* error, float* rawPeak)
{
    if (contributors.size() != 3) return false;
    const auto openDecoder = [&](int index,
                                 std::unique_ptr<agplayer::Decoder>* decoder) {
        auto candidate = std::make_unique<agplayer::Decoder>();
        const ag_result opened = candidate->open(
            contributors.at(index).toUtf8().toStdString(), 44100, 2);
        if (opened != AG_OK) {
            *error = utf8Error(opened);
            return false;
        }
        *decoder = std::move(candidate);
        return true;
    };
    const auto asVector = [](const agplayer::DecodedAudioBlock& block) {
        QVector<float> samples(static_cast<qsizetype>(block.frames * 2));
        if (!samples.isEmpty()) {
            std::memcpy(samples.data(), block.samples.data(),
                        static_cast<size_t>(samples.size()) * sizeof(float));
        }
        return samples;
    };

    std::unique_ptr<agplayer::Decoder> drums;
    std::unique_ptr<agplayer::Decoder> bass;
    std::unique_ptr<agplayer::Decoder> other;
    if (!openDecoder(0, &drums) || !openDecoder(1, &bass)
        || !openDecoder(2, &other)) {
        return false;
    }
    float peak = 0.0F;
    for (;;) {
        if (cancelled.isCancelled()) {
            *error = QStringLiteral("Separation cancelled");
            return false;
        }
        agplayer::DecodedAudioBlock d;
        agplayer::DecodedAudioBlock b;
        agplayer::DecodedAudioBlock o;
        const ag_result dr = drums->read(d);
        const ag_result br = bass->read(b);
        const ag_result orr = other->read(o);
        if (dr != AG_OK || br != AG_OK || orr != AG_OK) {
            *error = QStringLiteral("Could not decode Demucs staging rows");
            return false;
        }
        if (d.frames != b.frames || d.frames != o.frames
            || d.end_of_stream != b.end_of_stream
            || d.end_of_stream != o.end_of_stream) {
            *error = QStringLiteral("Demucs staging rows are not aligned");
            return false;
        }
        const float blockPeak = accompanimentPeak(
            asVector(d), asVector(b), asVector(o));
        if (blockPeak < 0.0F) return false;
        peak = std::max(peak, blockPeak);
        if (d.end_of_stream) break;
    }
    if (!openDecoder(0, &drums) || !openDecoder(1, &bass)
        || !openDecoder(2, &other)) {
        return false;
    }
    FfmpegWaveWriter writer;
    if (!writer.open(destination, 44100, 2)) {
        *error = writer.errorString();
        return false;
    }
    *rawPeak = peak;
    for (;;) {
        if (cancelled.isCancelled()) {
            *error = QStringLiteral("Separation cancelled");
            return false;
        }
        agplayer::DecodedAudioBlock d;
        agplayer::DecodedAudioBlock b;
        agplayer::DecodedAudioBlock o;
        const ag_result dr = drums->read(d);
        const ag_result br = bass->read(b);
        const ag_result orr = other->read(o);
        if (dr != AG_OK || br != AG_OK || orr != AG_OK
            || d.frames != b.frames || d.frames != o.frames
            || d.end_of_stream != b.end_of_stream
            || d.end_of_stream != o.end_of_stream) {
            *error = QStringLiteral("Could not decode aligned Demucs staging rows");
            return false;
        }
        const QVector<float> summed = sumAccompaniment(
            asVector(d), asVector(b), asVector(o), 1.0F);
        if (!summed.isEmpty() && !writer.write(summed)) {
            *error = writer.errorString();
            return false;
        }
        if (d.end_of_stream) break;
    }
    if (!writer.finish()) {
        *error = writer.errorString();
        return false;
    }
    return true;
}

BackendResult runDemucs(const NativeStartRequest& request,
                        const TrustedModelProfile& trusted,
                        const NativeProviderSelection& provider,
                        const CancellationToken& cancelled,
                        const ProgressCallback& progress,
                        OutputTransaction& transaction)
{
    QString error;
    const qint64 totalFrames = countDecodedFrames(request.inputPath, cancelled, &error);
    if (totalFrames == -2) return fail(QStringLiteral("cancelled"),
                                       QStringLiteral("Separation cancelled"));
    if (totalFrames <= 0) return fail(QStringLiteral("decode_failed"), error);
    const DemucsProfile profile = DemucsProfile::trusted();
    const QVector<qint64> starts = demucsChunkStarts(totalFrames);
    QHash<QString, QString> staging;
    QJsonObject rawPeaks;
    float maximumPeak = 0.0F;

    for (qsizetype modelIndex = 0; modelIndex < request.modelFiles.size(); ++modelIndex) {
        if (cancelled.isCancelled()) return fail(QStringLiteral("cancelled"),
                                          QStringLiteral("Separation cancelled"));
        const QString modelPath = request.modelFiles.at(modelIndex);
        progress(nativeInferenceProgress(modelIndex * starts.size(),
                                         request.modelFiles.size() * starts.size()),
                 QStringLiteral("model_loading"));
        ModelArtifactReadResult artifact;
        BackendResult loaded = loadTrustedModelArtifact(
            modelPath, trusted, cancelled, &artifact);
        if (!loaded.ok) return loaded;
        const int row = request.modelRoles.isEmpty()
            ? trustedDemucsRowForHash(artifact.sha256)
            : DemucsProfile::trusted().rows.indexOf(request.modelRoles.at(modelIndex));
        if (row < 0) return fail(QStringLiteral("model_semantics_invalid"),
                                 QStringLiteral("Demucs hash does not identify its trusted row"));
        const QString stem = profile.rows.at(row);
        const QString stagingPath = QDir(transaction.temporaryDirectory())
            .filePath(QStringLiteral(".%1.float.wav").arg(stem));
        FfmpegWaveWriter writer;
        if (!writer.open(stagingPath, 44100, 2)) {
            return fail(QStringLiteral("output_write_failed"), writer.errorString());
        }

        std::unique_ptr<OrtModelSession> session;
        BackendResult opened = openSession(request, trusted, artifact.bytes,
                                           provider.provider, provider.adapterId,
                                           cancelled, &session);
        if (!opened.ok) return opened;
        artifact.bytes.clear();
        artifact.bytes.squeeze();
        float publishedPeak = 0.0F;
        StreamingOverlapAdd publisher([&](const QVector<float>& published) {
            for (float sample : published) {
                if (!std::isfinite(sample)) return false;
                publishedPeak = std::max(publishedPeak, std::abs(sample));
            }
            return writer.write(published);
        });
        float rawModelPeak = 0.0F;
        BackendResult streamed = streamPcmChunks(
            request.inputPath, starts, profile.chunkSamples, cancelled,
            [&](qint64 start, const QVector<float>& mix, qsizetype chunkIndex) {
                progress(nativeInferenceProgress(modelIndex * starts.size() + chunkIndex,
                                                  request.modelFiles.size() * starts.size()),
                         QStringLiteral("inference"));
                const QVector<float> planar = interleavedToPlanar(mix);
                const OrtOperationResult inference = session->run(
                    planar, profile.inputShape, profile.outputShape, cancelled);
                if (!inference.ok) return fail(inference.code, inference.message);
                const QVector<float> rowPlanar = selectDemucsRow(inference.output, row);
                const QVector<float> rowInterleaved = planarToInterleaved(rowPlanar);
                for (const float value : rowInterleaved) rawModelPeak = std::max(rawModelPeak, std::abs(value));
                const QVector<float> weights = demucsPublisherWeights(
                    static_cast<int>(chunkIndex), static_cast<int>(starts.size()));
                if (!publisher.add(start, rowInterleaved, weights)) {
                    return fail(QStringLiteral("output_write_failed"),
                                QStringLiteral("Could not stream Demucs overlap output"));
                }
                const qint64 completed = modelIndex * starts.size() + chunkIndex + 1;
                progress(nativeInferenceProgress(
                             completed, request.modelFiles.size() * starts.size()),
                         QStringLiteral("inference"));
                return BackendResult{true, {}, {}, {}};
            });
        if (!streamed.ok) return streamed;
        if (!publisher.finish(totalFrames) || !writer.finish()) {
            return fail(QStringLiteral("output_write_failed"), writer.errorString());
        }
        session.reset(); // at most one Demucs session is resident
        staging.insert(stem, stagingPath);
        rawPeaks.insert(stem, publishedPeak);
        maximumPeak = std::max(maximumPeak, publishedPeak);
        qInfo().noquote() << "Demucs raw model peak" << stem << rawModelPeak << "overlap peak" << publishedPeak;
    }

    if (request.stems.contains(QStringLiteral("instrumental"))) {
        float instrumentalPeak = 0.0F;
        const QString instrumental = QDir(transaction.temporaryDirectory())
            .filePath(QStringLiteral(".instrumental.float.wav"));
        if (!deriveAccompanimentWave(
                {staging.value(QStringLiteral("drums")),
                 staging.value(QStringLiteral("bass")),
                 staging.value(QStringLiteral("other"))},
                instrumental, cancelled, &error, &instrumentalPeak)) {
            return fail(cancelled.isCancelled() ? QStringLiteral("cancelled")
                                                 : QStringLiteral("output_write_failed"),
                        error);
        }
        staging.insert(QStringLiteral("instrumental"), instrumental);
        rawPeaks.insert(QStringLiteral("instrumental"), instrumentalPeak);
        maximumPeak = std::max(maximumPeak, instrumentalPeak);
    }
    // One shared gain preserves all stem relationships; never normalize a row alone.
    const float outputGain = maximumPeak > 0.99F ? 0.99F / maximumPeak : 1.0F;
    qInfo() << "Demucs shared output gain" << outputGain << "raw peaks" << rawPeaks;
    for (const QString& stem : request.stems) {
        if (!encodeStagingWave(staging.value(stem), transaction.temporaryPath(stem),
                               cancelled, &error, outputGain)) {
            return fail(cancelled.isCancelled() ? QStringLiteral("cancelled")
                                         : QStringLiteral("encode_failed"), error);
        }
    }
    return {true, {}, {}, {{QStringLiteral("outputGain"), outputGain},
                           {QStringLiteral("rawPeaks"), rawPeaks}}};
}

} // namespace

StartRequestParseResult parseStartRequest(const QJsonObject& payload)
{
    static const QSet<QString> allowed{
        QStringLiteral("runtimePath"), QStringLiteral("inputPath"),
        QStringLiteral("modelFiles"), QStringLiteral("outputDirectory"),
        QStringLiteral("baseName"), QStringLiteral("directoryName"),
        QStringLiteral("modelName"),
        QStringLiteral("extension"), QStringLiteral("stemLabels"),
        QStringLiteral("stems"), QStringLiteral("device"),
        QStringLiteral("modelProfile"), QStringLiteral("modelSha256"),
        QStringLiteral("modelBytes"), QStringLiteral("modelRoles")};
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            return {false, QStringLiteral("invalid_start_request"),
                    QStringLiteral("Start request contains unsupported fields"), {}};
        }
    }
    NativeStartRequest request;
    request.runtimePath = payload.value(QStringLiteral("runtimePath")).toString();
    request.inputPath = payload.value(QStringLiteral("inputPath")).toString();
    request.outputDirectory = payload.value(QStringLiteral("outputDirectory")).toString();
    request.baseName = payload.value(QStringLiteral("baseName")).toString();
    request.directoryName = payload.value(QStringLiteral("directoryName")).toString();
    request.modelName = payload.value(QStringLiteral("modelName")).toString();
    request.extension = payload.value(QStringLiteral("extension"))
                            .toString().trimmed().toLower();
    bool arraysValid = true;
    request.modelFiles = stringArray(payload.value(QStringLiteral("modelFiles")),
                                     32767, &arraysValid);
    request.stems = stringArray(payload.value(QStringLiteral("stems")), 32,
                                &arraysValid);
    request.stemLabels = stringArray(payload.value(QStringLiteral("stemLabels")),
                                     240, &arraysValid);
    const bool hasCustomDeclaration = payload.contains(QStringLiteral("modelProfile"))
        || payload.contains(QStringLiteral("modelSha256"))
        || payload.contains(QStringLiteral("modelBytes"))
        || payload.contains(QStringLiteral("modelRoles"));
    if (hasCustomDeclaration) {
        request.modelProfile = payload.value(QStringLiteral("modelProfile"))
                                   .toString();
        request.modelSha256 = stringArray(
            payload.value(QStringLiteral("modelSha256")), 64, &arraysValid);
        request.modelBytes = integerArray(
            payload.value(QStringLiteral("modelBytes")), &arraysValid);
        if (payload.contains(QStringLiteral("modelRoles"))) {
            const QJsonArray roles = payload.value(QStringLiteral("modelRoles"))
                                         .toArray();
            if (!payload.value(QStringLiteral("modelRoles")).isArray()) {
                arraysValid = false;
            } else {
                for (const QJsonValue& role : roles) {
                    if (!role.isString() || role.toString().size() > 32) {
                        arraysValid = false;
                        break;
                    }
                    request.modelRoles.push_back(role.toString());
                }
            }
        }
        if (request.modelProfile.isEmpty()
            || request.modelSha256.size() != request.modelFiles.size()
            || request.modelBytes.size() != request.modelFiles.size()) {
            arraysValid = false;
        } else if (!customProfileForDeclaration(
                       request.modelProfile, request.modelSha256,
                       request.modelBytes, request.modelRoles).ok) {
            arraysValid = false;
        }
    }
    const QString device = payload.value(QStringLiteral("device")).toString();
    if (device == QStringLiteral("auto")) request.device = DeviceMode::Auto;
    else if (device == QStringLiteral("cpu")) request.device = DeviceMode::Cpu;
    else if (device == QStringLiteral("gpu")) request.device = DeviceMode::Gpu;
    else arraysValid = false;
    if (!arraysValid || request.runtimePath.isEmpty() || request.inputPath.isEmpty()
        || request.outputDirectory.isEmpty() || request.baseName.isEmpty()
        || request.directoryName.isEmpty()
        || request.modelName.isEmpty()
        || request.extension.isEmpty() || request.modelFiles.isEmpty()
        || request.modelFiles.size() > 4 || request.stems.isEmpty()
        || request.stems.size() > 5 || request.stemLabels.size() != request.stems.size()
        || request.runtimePath.size() > 32767
        || request.inputPath.size() > 32767
        || request.outputDirectory.size() > 32767
        || request.baseName.size() > 240 || request.directoryName.size() > 240
        || request.modelName.size() > 240
        || request.extension.size() > 16) {
        return {false, QStringLiteral("invalid_start_request"),
                QStringLiteral("Start request is missing a required bounded field"), {}};
    }
    static const QSet<QString> supportedExtensions{
        QStringLiteral("wav"), QStringLiteral("flac"), QStringLiteral("mp3")};
    if (!supportedExtensions.contains(request.extension)) {
        return {false, QStringLiteral("invalid_start_request"),
                QStringLiteral("Start request uses an unsupported output format"), {}};
    }
    return {true, {}, {}, request};
}

ModelArtifactReadResult readModelArtifact(
    const QString& path, bool captureBytes,
    const QVector<TrustedModelFile>& trustedFiles,
    const CancellationToken& cancelled,
    const ModelReadProgress& progress)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, QStringLiteral("model_missing"),
                QStringLiteral("A model file cannot be read"), {}, {}};
    }
    const qint64 expectedBytes = file.size();
    if (expectedBytes <= 0) {
        return {false, QStringLiteral("model_invalid"),
                QStringLiteral("Model file is empty"), {}, {}};
    }
    if (!trustedFiles.isEmpty()) {
        const bool allowedSize = std::any_of(
            trustedFiles.cbegin(), trustedFiles.cend(),
            [expectedBytes](const TrustedModelFile& trusted) {
                return trusted.expectedSizeBytes == expectedBytes;
            });
        if (!allowedSize) {
            return {false, QStringLiteral("model_size_mismatch"),
                    QStringLiteral("Model size does not match its trusted profile"), {}, {}};
        }
    } else {
        qint64 maximumBytes = 0;
        for (const TrustedModelFile& trusted : allTrustedModelFiles()) {
            maximumBytes = std::max(maximumBytes, trusted.expectedSizeBytes);
        }
        if (expectedBytes > maximumBytes) {
            return {false, QStringLiteral("model_too_large"),
                    QStringLiteral("Model file exceeds the built-in model bound"), {}, {}};
        }
    }
    if (cancelled.isCancelled()) {
        return {false, QStringLiteral("cancelled"),
                QStringLiteral("Separation cancelled"), {}, {}};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray bytes;
    if (captureBytes) bytes.reserve(static_cast<qsizetype>(expectedBytes));
    qint64 completed = 0;
    while (completed < expectedBytes) {
        if (cancelled.isCancelled()) {
            return {false, QStringLiteral("cancelled"),
                    QStringLiteral("Separation cancelled"), {}, {}};
        }
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty()) {
            return {false, QStringLiteral("model_read_failed"),
                    QStringLiteral("Could not read the complete model file"), {}, {}};
        }
        completed += block.size();
        if (completed > expectedBytes) {
            return {false, QStringLiteral("model_read_failed"),
                    QStringLiteral("Model size changed while it was being read"), {}, {}};
        }
        hash.addData(QByteArrayView(block));
        if (captureBytes) bytes.append(block);
        if (progress) progress(completed, expectedBytes);
        if (cancelled.isCancelled()) {
            return {false, QStringLiteral("cancelled"),
                    QStringLiteral("Separation cancelled"), {}, {}};
        }
    }
    if (file.error() != QFileDevice::NoError || completed != expectedBytes
        || !file.atEnd()) {
        return {false, QStringLiteral("model_read_failed"),
                QStringLiteral("Model size changed while it was being read"), {}, {}};
    }
    const QString sha = QString::fromLatin1(hash.result().toHex());
    if (!trustedFiles.isEmpty()) {
        const auto trusted = std::find_if(
            trustedFiles.cbegin(), trustedFiles.cend(),
            [&sha](const TrustedModelFile& candidate) {
                return candidate.sha256.compare(sha, Qt::CaseInsensitive) == 0;
            });
        if (trusted == trustedFiles.cend()) {
            return {false,
                    captureBytes ? QStringLiteral("model_changed")
                                 : QStringLiteral("model_untrusted"),
                    captureBytes
                        ? QStringLiteral("Model bytes changed after trust validation")
                        : QStringLiteral("Model hash is not on the built-in allowlist"),
                    {}, {}};
        }
        if (trusted->expectedSizeBytes != completed) {
            return {false, QStringLiteral("model_size_mismatch"),
                    QStringLiteral("Model size changed while it was being read"), {}, {}};
        }
    }
    return {true, {}, {}, sha, std::move(bytes)};
}

double nativeInferenceProgress(qint64 completed, qint64 total)
{
    if (total <= 0) return 0.0;
    constexpr double kInferenceCeiling = 0.95;
    return std::clamp(static_cast<double>(completed) / static_cast<double>(total),
                      0.0, 1.0) * kInferenceCeiling;
}

class FfmpegWaveWriter::Impl final {
public:
    explicit Impl(CloseFunction closeFunction)
        : close(std::move(closeFunction))
    {
        if (!close) {
            close = [](AVIOContext** output) { return avio_closep(output); };
        }
    }

    ~Impl()
    {
        if (format != nullptr) {
            if (format->pb != nullptr) (void)close(&format->pb);
            avformat_free_context(format);
        }
    }

    void setError(int code, const QString& prefix)
    {
        char detail[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(code, detail, sizeof(detail));
        error = prefix + QStringLiteral(": ") + QString::fromUtf8(detail);
    }

    AVFormatContext* format = nullptr;
    AVStream* stream = nullptr;
    qint64 nextPts = 0;
    int channels = 0;
    QString error;
    CloseFunction close;
};

FfmpegWaveWriter::FfmpegWaveWriter(CloseFunction close)
    : impl_(std::make_unique<Impl>(std::move(close)))
{
}
FfmpegWaveWriter::~FfmpegWaveWriter() = default;

bool FfmpegWaveWriter::open(const QString& path, int sampleRate, int channels)
{
    if (impl_->format != nullptr || sampleRate <= 0 || channels <= 0 || channels > 8) {
        impl_->error = QStringLiteral("Invalid FFmpeg WAV configuration");
        return false;
    }
    const QByteArray nativePath = path.toUtf8();
    int result = avformat_alloc_output_context2(
        &impl_->format, nullptr, "wav", nativePath.constData());
    if (result < 0 || impl_->format == nullptr) {
        impl_->setError(result, QStringLiteral("Could not create FFmpeg WAV muxer"));
        return false;
    }
    impl_->stream = avformat_new_stream(impl_->format, nullptr);
    if (impl_->stream == nullptr) {
        impl_->error = QStringLiteral("Could not create FFmpeg WAV stream");
        return false;
    }
    AVCodecParameters* parameters = impl_->stream->codecpar;
    parameters->codec_type = AVMEDIA_TYPE_AUDIO;
    parameters->codec_id = AV_CODEC_ID_PCM_F32LE;
    parameters->format = AV_SAMPLE_FMT_FLT;
    parameters->sample_rate = sampleRate;
    parameters->bits_per_coded_sample = 32;
    parameters->block_align = channels * 4;
    parameters->bit_rate = static_cast<qint64>(sampleRate) * channels * 32;
    av_channel_layout_default(&parameters->ch_layout, channels);
    impl_->stream->time_base = AVRational{1, sampleRate};
    impl_->channels = channels;
    result = avio_open(&impl_->format->pb, nativePath.constData(), AVIO_FLAG_WRITE);
    if (result < 0) {
        impl_->setError(result, QStringLiteral("Could not open FFmpeg WAV output"));
        return false;
    }
    result = avformat_write_header(impl_->format, nullptr);
    if (result < 0) {
        impl_->setError(result, QStringLiteral("Could not write FFmpeg WAV header"));
        return false;
    }
    return true;
}

bool FfmpegWaveWriter::write(const QVector<float>& interleavedSamples)
{
    if (impl_->format == nullptr || impl_->stream == nullptr
        || interleavedSamples.isEmpty()
        || interleavedSamples.size() % impl_->channels != 0) {
        impl_->error = QStringLiteral("Invalid FFmpeg WAV sample block");
        return false;
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr
        || av_new_packet(packet,
                         static_cast<int>(interleavedSamples.size() * sizeof(float))) < 0) {
        av_packet_free(&packet);
        impl_->error = QStringLiteral("Could not allocate FFmpeg WAV packet");
        return false;
    }
    for (qsizetype index = 0; index < interleavedSamples.size(); ++index) {
        quint32 bits = 0;
        const float sample = interleavedSamples.at(index);
        std::memcpy(&bits, &sample, sizeof(bits));
        qToLittleEndian(bits, packet->data + index * sizeof(float));
    }
    const qint64 frames = interleavedSamples.size() / impl_->channels;
    packet->stream_index = impl_->stream->index;
    packet->pts = packet->dts = impl_->nextPts;
    packet->duration = frames;
    impl_->nextPts += frames;
    const int result = av_interleaved_write_frame(impl_->format, packet);
    av_packet_free(&packet);
    if (result < 0) {
        impl_->setError(result, QStringLiteral("Could not write FFmpeg WAV samples"));
        return false;
    }
    return true;
}

bool FfmpegWaveWriter::finish()
{
    if (impl_->format == nullptr || impl_->format->pb == nullptr) return false;
    const int trailerResult = av_write_trailer(impl_->format);
    const int closeResult = impl_->close(&impl_->format->pb);
    if (trailerResult < 0) {
        impl_->setError(trailerResult,
                        QStringLiteral("Could not finalize FFmpeg WAV output"));
    } else if (closeResult < 0) {
        impl_->setError(closeResult,
                        QStringLiteral("Could not close FFmpeg WAV output"));
    }
    avformat_free_context(impl_->format);
    impl_->format = nullptr;
    impl_->stream = nullptr;
    return trailerResult >= 0 && closeResult >= 0;
}

QString FfmpegWaveWriter::errorString() const { return impl_->error; }

BackendResult NativeWorkerBackend::probe(const QJsonObject& payload)
{
    CancellationToken cancelled;
    return probeCancellable(payload, cancelled);
}

BackendResult NativeWorkerBackend::probeCancellable(const QJsonObject& payload, const CancellationToken& cancelled)
{
    const QString runtimePath = payload.value(QStringLiteral("runtimePath")).toString();
    DynamicOrtRuntime runtime;
    if (!runtime.load(runtimePath)) {
        return fail(runtime.errorCode(), runtime.errorMessage());
    }
    QJsonArray adapters;
    for (const DxgiAdapterInfo& adapter : enumerateDxgiHardwareAdapters()) {
        adapters.push_back(QJsonObject{
            {QStringLiteral("deviceId"), adapter.deviceId},
            {QStringLiteral("name"), adapter.name},
            {QStringLiteral("dedicatedVideoMemory"),
             static_cast<double>(adapter.dedicatedVideoMemory)}});
    }
    const bool hasHardwareCandidate = !adapters.isEmpty();
    const bool cuda = QFileInfo(QDir(QFileInfo(runtimePath).absolutePath()).filePath("onnxruntime_providers_cuda.dll")).isFile();
    const QString family = payload.value("family").toString();
    if (!family.isEmpty()) {
#ifdef Q_OS_MACOS
        const QString provider = QStringLiteral("coreml");
#else
        const QString provider = cuda ? QStringLiteral("cuda") : QStringLiteral("directml");
#endif
        QString reason;
        bool compatible = false;
        bool validated = false;
#ifdef Q_OS_MACOS
        bool cpuCompatible = false;
        bool cpuValidated = false;
        QString cpuReason;
#endif
        if (family == "vr") reason = QStringLiteral("当前 VR 适配器仅支持 CPU");
#ifndef Q_OS_MACOS
        else if (family == "demucs" && !cuda) reason = QStringLiteral("标准五轨需要可选 NVIDIA CUDA 环境；此固定图不支持 DirectML");
#endif
        else if (payload.value("modelFiles").toArray().isEmpty()) reason = QStringLiteral("模型尚未校验，GPU 兼容性待验证");
        else {
            NativeStartRequest request; request.runtimePath = runtimePath; request.device = DeviceMode::Gpu;
            QStringList hashes;
            for (const auto& value : payload.value("modelFiles").toArray()) {
                if (request.modelFiles.size() >= 4) break;
                const QString path = value.toString(); request.modelFiles.push_back(path);
                const auto artifact = readModelArtifact(path, false, allTrustedModelFiles(), cancelled);
                if (!artifact.ok) { reason = artifact.message; break; }
                hashes.push_back(artifact.sha256);
            }
            const auto profile = trustedProfileForHashes(hashes);
            if (profile && hashes.size() == request.modelFiles.size()) {
                OrtNativeProviderProbe probe;
#ifdef Q_OS_MACOS
                request.device = DeviceMode::Cpu;
                const auto cpuSelection = selectNativeProvider(request, *profile, cancelled, probe);
                cpuValidated = true;
                cpuCompatible = cpuSelection.ok;
                cpuReason = cpuSelection.message;
                request.device = DeviceMode::Gpu;
#endif
                const auto selection = selectNativeProvider(request, *profile, cancelled, probe);
                compatible = selection.ok; validated = selection.ok;
                reason = selection.ok ? QStringLiteral("当前模型已通过 %1 GPU 推理验证").arg(provider.toUpper()) : selection.message;
            } else if (reason.isEmpty()) reason = QStringLiteral("模型缺少受信 GPU 推理契约");
        }
        return {true, {}, {}, {
#ifdef Q_OS_MACOS
            {"cpu", cpuCompatible}, {"cpuValidated", cpuValidated}, {"cpuReason", cpuReason},
#else
            {"cpu", true},
#endif
            {"gpu", compatible}, {"provider", provider},
            {"modelValidated", validated}, {"gpuReason", reason}, {"adapters", adapters}}};
    }
    return {true, {}, {},
            {{QStringLiteral("cpu"), true},
             {QStringLiteral("gpu"), hasHardwareCandidate},
             {QStringLiteral("gpuReason"),
              hasHardwareCandidate
                  ? QStringLiteral("Hardware adapter candidate detected; "
                                   "DirectML will be validated with a trusted "
                                   "model when separation starts")
                  : QStringLiteral("No hardware DXGI adapter is available")},
             {QStringLiteral("adapters"), adapters}}};
}

BackendResult NativeWorkerBackend::separate(const QJsonObject& payload,
                                            const CancellationToken& cancelled,
                                            const ProgressCallback& progress)
{
    const StartRequestParseResult parsed = parseStartRequest(payload);
    if (!parsed.ok) return fail(parsed.code, parsed.message);
    const NativeStartRequest& request = parsed.request;
    if (!QFileInfo(request.runtimePath).isFile()) {
        return fail(QStringLiteral("runtime_missing"),
                    QStringLiteral("onnxruntime.dll does not exist"));
    }
    if (!QFileInfo(request.inputPath).isFile()) {
        return fail(QStringLiteral("input_missing"),
                    QStringLiteral("Input audio does not exist"));
    }
    std::optional<TrustedModelProfile> trusted;
    if (!request.modelProfile.isEmpty()) {
        const CustomProfileResolution custom = customProfileForDeclaration(
            request.modelProfile, request.modelSha256,
            request.modelBytes, request.modelRoles);
        if (!custom.ok) return fail(custom.code, custom.message);
        trusted = custom.profile;
    }
    QStringList hashes;
    const QVector<TrustedModelFile> trustedFiles = trusted.has_value()
        ? trustedFilesForProfile(*trusted) : allTrustedModelFiles();
    for (qsizetype index = 0; index < request.modelFiles.size(); ++index) {
        const QString& model = request.modelFiles.at(index);
        const ModelArtifactReadResult hashed = readModelArtifact(
            model, false, trustedFiles, cancelled);
        if (!hashed.ok) return fail(hashed.code, hashed.message);
        if (!request.modelProfile.isEmpty()
            && hashed.sha256.compare(request.modelSha256.at(index),
                                     Qt::CaseInsensitive) != 0) {
            return fail(QStringLiteral("model_hash_mismatch"),
                        QStringLiteral("Custom model file order does not match its declaration"));
        }
        hashes.push_back(hashed.sha256);
    }
    if (!trusted.has_value()) trusted = trustedProfileForHashes(hashes);
    if (!trusted) return fail(QStringLiteral("model_untrusted"),
                              QStringLiteral("Model hashes are not on the built-in allowlist"));
    const QStringList allowedStems = trusted->family == QStringLiteral("mdx")
        ? QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental")}
        : QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental"),
                      QStringLiteral("drums"), QStringLiteral("bass"),
                      QStringLiteral("other")};
    for (const QString& stem : request.stems) {
        if (!allowedStems.contains(stem)) return fail(
            QStringLiteral("unsupported_stem"),
            QStringLiteral("Selected stem is not produced by the trusted model"));
    }

    const OutputPlan outputPlan{request.outputDirectory, request.baseName,
                                request.extension, request.stems,
                                request.modelName, request.stemLabels,
                                request.directoryName};
    auto transaction = std::make_unique<OutputTransaction>(outputPlan);
    const TransactionResult begun = transaction->begin();
    if (!begun.ok) return transactionFailure(begun);
    progress(0.0, QStringLiteral("provider_probe"));
    OrtNativeProviderProbe providerProbe;
    NativeProviderSelection provider = selectNativeProvider(
        request, *trusted, cancelled, providerProbe);
    if (!provider.ok) {
        return cleanupAfterFailure(
            *transaction, fail(provider.code, provider.message));
    }
    BackendResult separated = trusted->family == QStringLiteral("mdx")
        ? runMdx(request, *trusted, provider, cancelled, progress, *transaction)
        : runDemucs(request, *trusted, provider, cancelled, progress, *transaction);
    if (!separated.ok && request.device == DeviceMode::Auto
        && provider.provider != ExecutionProvider::Cpu
        && !cancelled.isCancelled()) {
        // A lightweight provider probe can succeed even when a large model
        // exceeds a driver's DirectML limits. Auto mode promises a usable
        // result, so discard every partial GPU output and retry once on CPU.
        const QString gpuFailure = separated.message.isEmpty()
            ? separated.code : separated.message;
        const TransactionResult cancelledTransaction = transaction->cancel();
        if (!cancelledTransaction.ok) return transactionFailure(cancelledTransaction);

        NativeStartRequest cpuRequest = request;
        cpuRequest.device = DeviceMode::Cpu;
        NativeProviderSelection cpuProvider = selectNativeProvider(
            cpuRequest, *trusted, cancelled, providerProbe);
        if (!cpuProvider.ok) return fail(cpuProvider.code, cpuProvider.message);
        cpuProvider.fallbackReason = gpuFailure;

        transaction = std::make_unique<OutputTransaction>(outputPlan);
        const TransactionResult fallbackBegun = transaction->begin();
        if (!fallbackBegun.ok) return transactionFailure(fallbackBegun);
        progress(0.0, QStringLiteral("cpu_fallback"));
        separated = trusted->family == QStringLiteral("mdx")
            ? runMdx(cpuRequest, *trusted, cpuProvider, cancelled, progress,
                     *transaction)
            : runDemucs(cpuRequest, *trusted, cpuProvider, cancelled, progress,
                        *transaction);
        provider = std::move(cpuProvider);
    }
    if (!separated.ok) return cleanupAfterFailure(*transaction, separated);
    progress(0.98, QStringLiteral("verification"));
    if (cancelled.isCancelled()) {
        return cleanupAfterFailure(
            *transaction, fail(QStringLiteral("cancelled"),
                               QStringLiteral("Separation cancelled")));
    }
    const TransactionResult committed = transaction->commit(
        verifyAudio, cancelled);
    if (!committed.ok) return transactionFailure(committed);
    QJsonArray outputs;
    for (const QString& path : committed.outputs) outputs.push_back(path);
    progress(1.0, QStringLiteral("completed"));
    QJsonObject completionPayload{
        {QStringLiteral("outputs"), outputs},
        {QStringLiteral("provider"),
         provider.provider == ExecutionProvider::CoreMl ? QStringLiteral("coreml") : provider.provider == ExecutionProvider::Cuda ? QStringLiteral("cuda") : provider.provider == ExecutionProvider::DirectMl
             ? QStringLiteral("directml") : QStringLiteral("cpu")},
        {QStringLiteral("device"), provider.provider == ExecutionProvider::Cpu
             ? QStringLiteral("CPU") : QStringLiteral("GPU %1").arg(provider.adapterId)},
        {QStringLiteral("fallbackReason"), provider.fallbackReason},
        {QStringLiteral("outputGain"), separated.payload.value(QStringLiteral("outputGain")).toDouble(1.0)},
        {QStringLiteral("rawPeaks"), separated.payload.value(QStringLiteral("rawPeaks"))}};
    if (!committed.code.isEmpty()) {
        completionPayload.insert(QStringLiteral("cleanupWarning"), committed.code);
        completionPayload.insert(QStringLiteral("cleanupWarningMessage"), committed.message);
    }
    return {true, {}, {}, completionPayload};
}

} // namespace agplayer::separation
