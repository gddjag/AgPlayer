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

struct TrustedModelArtifact {
    QByteArray bytes;
    QString sha256;
};

BackendResult loadTrustedModelArtifact(const QString& modelPath,
                                       const TrustedModelProfile& profile,
                                       TrustedModelArtifact* artifact)
{
    QFile file(modelPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("model_missing"),
                    QStringLiteral("A model file cannot be read"));
    }
    if (file.size() <= 0 || file.size() > (std::numeric_limits<int>::max)()) {
        return fail(QStringLiteral("model_invalid"),
                    QStringLiteral("Model size is not supported"));
    }
    QByteArray bytes = file.readAll();
    if (bytes.size() != file.size() || file.error() != QFileDevice::NoError) {
        return fail(QStringLiteral("model_read_failed"),
                    QStringLiteral("Could not capture the approved model bytes"));
    }
    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (!profile.sha256.contains(sha, Qt::CaseInsensitive)) {
        return fail(QStringLiteral("model_changed"),
                    QStringLiteral("Model bytes changed after trust validation"));
    }
    artifact->bytes = std::move(bytes);
    artifact->sha256 = sha;
    return {true, {}, {}, {}};
}

BackendResult openSession(const NativeStartRequest& request,
                          const TrustedModelProfile& profile,
                          const QByteArray& approvedModelBytes,
                          ExecutionProvider provider,
                          int adapterId, std::unique_ptr<OrtModelSession>* session)
{
    auto candidate = std::make_unique<OrtModelSession>();
    OrtOperationResult opened = candidate->open(request.runtimePath,
                                                approvedModelBytes, provider,
                                                adapterId);
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
    TrustedModelArtifact artifact;
    BackendResult loaded = loadTrustedModelArtifact(
        request.modelFiles.front(), profile, &artifact);
    if (!loaded.ok) return loaded;
    std::unique_ptr<OrtModelSession> session;
    BackendResult opened = openSession(request, profile, artifact.bytes,
                                       provider, adapterId, &session);
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
    return run.ok ? BackendResult{true, {}, {}, {}}
                  : fail(run.code, run.message);
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
    if (!adapters.isEmpty()) {
        const BackendResult gpu = probe.prove(request, profile,
                                              ExecutionProvider::DirectMl,
                                              adapters.front().deviceId,
                                              cancelled);
        if (cancelled.isCancelled()) return cancelledResult();
        if (gpu.ok) {
            return {true, ExecutionProvider::DirectMl,
                    adapters.front().deviceId, {}, {}, {}};
        }
        gpuReason = gpu.message;
        if (request.device == DeviceMode::Gpu) {
            return {false, ExecutionProvider::DirectMl,
                    adapters.front().deviceId, {},
                    QStringLiteral("gpu_probe_failed"), gpu.message};
        }
    } else if (request.device == DeviceMode::Gpu) {
        return {false, ExecutionProvider::DirectMl, 0, {},
                QStringLiteral("gpu_probe_failed"), gpuReason};
    }
    const BackendResult cpu = probe.prove(request, profile,
                                          ExecutionProvider::Cpu, 0,
                                          cancelled);
    if (cancelled.isCancelled()) return cancelledResult();
    return {cpu.ok, ExecutionProvider::Cpu, 0, gpuReason, cpu.code, cpu.message};
}

namespace {

bool encodeStagingWave(const QString& stagingPath, const QString& outputPath,
                       const CancellationToken& cancelled, QString* error)
{
    agplayer::TranscodeConfig config;
    config.output_path = outputPath.toUtf8().toStdString();
    config.sample_rate = 44100;
    config.channels = 2;
    std::string coreError;
    const ag_result result = agplayer::transcode(
        stagingPath.toUtf8().toStdString(), config, &cancelled.atomicFlag(),
        [](float) {}, coreError);
    if (result != AG_OK) {
        *error = coreError.empty() ? utf8Error(result) : QString::fromUtf8(coreError);
        return false;
    }
    QFile::remove(stagingPath);
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
    const MdxProfile profile = trusted.id == QStringLiteral("uvr-mdxnet-kara")
        ? MdxProfile::kara() : MdxProfile::hq3();
    QString error;
    const qint64 totalFrames = countDecodedFrames(request.inputPath, cancelled, &error);
    if (totalFrames == -2) return fail(QStringLiteral("cancelled"),
                                       QStringLiteral("Separation cancelled"));
    if (totalFrames <= 0) return fail(QStringLiteral("decode_failed"), error);
    const QVector<qint64> starts = mdxChunkStarts(totalFrames);

    TrustedModelArtifact artifact;
    BackendResult loaded = loadTrustedModelArtifact(
        request.modelFiles.front(), trusted, &artifact);
    if (!loaded.ok) return loaded;
    std::unique_ptr<OrtModelSession> session;
    BackendResult opened = openSession(request, trusted, artifact.bytes,
                                       provider.provider, provider.adapterId, &session);
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
                             QString* error)
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
    const float scale = peak > 1.0F ? 1.0F / peak : 1.0F;
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
            asVector(d), asVector(b), asVector(o), scale);
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

    for (qsizetype modelIndex = 0; modelIndex < request.modelFiles.size(); ++modelIndex) {
        if (cancelled.isCancelled()) return fail(QStringLiteral("cancelled"),
                                          QStringLiteral("Separation cancelled"));
        const QString modelPath = request.modelFiles.at(modelIndex);
        TrustedModelArtifact artifact;
        BackendResult loaded = loadTrustedModelArtifact(modelPath, trusted, &artifact);
        if (!loaded.ok) return loaded;
        const int row = trustedDemucsRowForHash(artifact.sha256);
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
                                           &session);
        if (!opened.ok) return opened;
        artifact.bytes.clear();
        artifact.bytes.squeeze();
        StreamingOverlapAdd publisher(
            [&](const QVector<float>& published) { return writer.write(published); });
        BackendResult streamed = streamPcmChunks(
            request.inputPath, starts, profile.chunkSamples, cancelled,
            [&](qint64 start, const QVector<float>& mix, qsizetype chunkIndex) {
                const QVector<float> planar = interleavedToPlanar(mix);
                const OrtOperationResult inference = session->run(
                    planar, profile.inputShape, profile.outputShape, cancelled);
                if (!inference.ok) return fail(inference.code, inference.message);
                const QVector<float> rowPlanar = selectDemucsRow(inference.output, row);
                const QVector<float> rowInterleaved = planarToInterleaved(rowPlanar);
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
    }

    if (request.stems.contains(QStringLiteral("instrumental"))) {
        const QString instrumental = QDir(transaction.temporaryDirectory())
            .filePath(QStringLiteral(".instrumental.float.wav"));
        if (!deriveAccompanimentWave(
                {staging.value(QStringLiteral("drums")),
                 staging.value(QStringLiteral("bass")),
                 staging.value(QStringLiteral("other"))},
                instrumental, cancelled, &error)) {
            return fail(cancelled.isCancelled() ? QStringLiteral("cancelled")
                                                 : QStringLiteral("output_write_failed"),
                        error);
        }
        staging.insert(QStringLiteral("instrumental"), instrumental);
    }
    for (const QString& stem : request.stems) {
        if (!encodeStagingWave(staging.value(stem), transaction.temporaryPath(stem),
                               cancelled, &error)) {
            return fail(cancelled.isCancelled() ? QStringLiteral("cancelled")
                                         : QStringLiteral("encode_failed"), error);
        }
    }
    return {true, {}, {}, {}};
}

} // namespace

StartRequestParseResult parseStartRequest(const QJsonObject& payload)
{
    static const QSet<QString> allowed{
        QStringLiteral("runtimePath"), QStringLiteral("inputPath"),
        QStringLiteral("modelFiles"), QStringLiteral("outputDirectory"),
        QStringLiteral("baseName"), QStringLiteral("extension"),
        QStringLiteral("stems"), QStringLiteral("device")};
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
    request.extension = payload.value(QStringLiteral("extension")).toString();
    bool arraysValid = true;
    request.modelFiles = stringArray(payload.value(QStringLiteral("modelFiles")),
                                     32767, &arraysValid);
    request.stems = stringArray(payload.value(QStringLiteral("stems")), 32,
                                &arraysValid);
    const QString device = payload.value(QStringLiteral("device")).toString();
    if (device == QStringLiteral("auto")) request.device = DeviceMode::Auto;
    else if (device == QStringLiteral("cpu")) request.device = DeviceMode::Cpu;
    else if (device == QStringLiteral("gpu")) request.device = DeviceMode::Gpu;
    else arraysValid = false;
    if (!arraysValid || request.runtimePath.isEmpty() || request.inputPath.isEmpty()
        || request.outputDirectory.isEmpty() || request.baseName.isEmpty()
        || request.extension.isEmpty() || request.modelFiles.isEmpty()
        || request.modelFiles.size() > 4 || request.stems.isEmpty()
        || request.stems.size() > 5 || request.runtimePath.size() > 32767
        || request.inputPath.size() > 32767
        || request.outputDirectory.size() > 32767
        || request.baseName.size() > 240 || request.extension.size() > 16) {
        return {false, QStringLiteral("invalid_start_request"),
                QStringLiteral("Start request is missing a required bounded field"), {}};
    }
    return {true, {}, {}, request};
}

QString hashFileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) return {};
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
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
    ~Impl()
    {
        if (format != nullptr) {
            if (format->pb != nullptr) avio_closep(&format->pb);
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
};

FfmpegWaveWriter::FfmpegWaveWriter() : impl_(std::make_unique<Impl>()) {}
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
    const int result = av_write_trailer(impl_->format);
    if (result < 0) {
        impl_->setError(result, QStringLiteral("Could not finalize FFmpeg WAV output"));
        return false;
    }
    avio_closep(&impl_->format->pb);
    avformat_free_context(impl_->format);
    impl_->format = nullptr;
    impl_->stream = nullptr;
    return true;
}

QString FfmpegWaveWriter::errorString() const { return impl_->error; }

BackendResult NativeWorkerBackend::probe(const QJsonObject& payload)
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
    return {true, {}, {},
            {{QStringLiteral("cpu"), true},
             {QStringLiteral("gpu"), false},
             {QStringLiteral("gpuReason"),
              QStringLiteral("GPU is verified only by a trusted-model minimal inference at start")},
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
    QStringList hashes;
    for (const QString& model : request.modelFiles) {
        const QString hash = hashFileSha256(model);
        if (hash.isEmpty()) return fail(QStringLiteral("model_missing"),
                                        QStringLiteral("A model file cannot be read"));
        hashes.push_back(hash);
    }
    const std::optional<TrustedModelProfile> trusted = trustedProfileForHashes(hashes);
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

    OutputTransaction transaction({request.outputDirectory, request.baseName,
                                   request.extension, request.stems});
    const TransactionResult begun = transaction.begin();
    if (!begun.ok) return fail(begun.code, begun.message);
    progress(0.0, QStringLiteral("provider_probe"));
    OrtNativeProviderProbe providerProbe;
    const NativeProviderSelection provider = selectNativeProvider(
        request, *trusted, cancelled, providerProbe);
    if (!provider.ok) return fail(provider.code, provider.message);
    BackendResult separated = trusted->family == QStringLiteral("mdx")
        ? runMdx(request, *trusted, provider, cancelled, progress, transaction)
        : runDemucs(request, *trusted, provider, cancelled, progress, transaction);
    if (!separated.ok) return separated;
    progress(0.98, QStringLiteral("verification"));
    if (cancelled.isCancelled()) {
        transaction.cancel();
        return fail(QStringLiteral("cancelled"),
                    QStringLiteral("Separation cancelled"));
    }
    const TransactionResult committed = transaction.commit(
        verifyAudio, cancelled.atomicFlag());
    if (!committed.ok) return fail(committed.code, committed.message);
    QJsonArray outputs;
    for (const QString& path : committed.outputs) outputs.push_back(path);
    progress(1.0, QStringLiteral("completed"));
    return {true, {}, {},
            {{QStringLiteral("outputs"), outputs},
             {QStringLiteral("provider"),
              provider.provider == ExecutionProvider::DirectMl
                  ? QStringLiteral("directml") : QStringLiteral("cpu")},
             {QStringLiteral("fallbackReason"), provider.fallbackReason}}};
}

} // namespace agplayer::separation
