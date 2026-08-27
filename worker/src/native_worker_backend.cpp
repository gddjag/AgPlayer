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

QStringList stringArray(const QJsonValue& value, bool* valid)
{
    QStringList result;
    if (!value.isArray()) {
        *valid = false;
        return result;
    }
    for (const QJsonValue& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()) {
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
                          const std::atomic_bool& cancelled,
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
        if (cancelled.load()) return -2;
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
                              const std::atomic_bool& cancelled,
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
            if (cancelled.load()) {
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

QVector<float> overlapWeights(const QVector<qint64>& starts, qsizetype index,
                              int chunkFrames)
{
    QVector<float> weights(chunkFrames, 1.0F);
    if (index > 0) {
        const qint64 overlap = starts.at(index - 1) + chunkFrames - starts.at(index);
        for (qint64 sample = 0; sample < overlap; ++sample) {
            weights[static_cast<qsizetype>(sample)] = overlap > 1
                ? static_cast<float>(sample) / static_cast<float>(overlap - 1)
                : 1.0F;
        }
    }
    if (index + 1 < starts.size()) {
        const qint64 overlap = starts.at(index) + chunkFrames - starts.at(index + 1);
        for (qint64 sample = 0; sample < overlap; ++sample) {
            const qsizetype position = static_cast<qsizetype>(chunkFrames - overlap + sample);
            const float fade = overlap > 1
                ? 1.0F - static_cast<float>(sample) / static_cast<float>(overlap - 1)
                : 1.0F;
            weights[position] = std::min(weights.at(position), fade);
        }
    }
    return weights;
}

class StreamingOverlapPublisher final {
public:
    using Sink = std::function<bool(const QVector<float>&)>;

    explicit StreamingOverlapPublisher(Sink sink) : sink_(std::move(sink)) {}

    bool add(qint64 start, const QVector<float>& samples,
             const QVector<float>& weights)
    {
        if (!takeBefore(start)) return false;
        const qint64 frames = samples.size() / 2;
        if (weights.size() != frames || start < base_) return false;
        const qint64 offset = start - base_;
        const qint64 required = offset + frames;
        if (accumulated_.size() < required * 2) accumulated_.resize(required * 2);
        if (normalization_.size() < required) normalization_.resize(required);
        for (qint64 frame = 0; frame < frames; ++frame) {
            const qint64 destination = offset + frame;
            const float weight = weights.at(static_cast<qsizetype>(frame));
            accumulated_[destination * 2] += samples.at(frame * 2) * weight;
            accumulated_[destination * 2 + 1] += samples.at(frame * 2 + 1) * weight;
            normalization_[destination] += weight;
        }
        return true;
    }

    bool finish(qint64 totalFrames) { return takeBefore(totalFrames); }

private:
    bool takeBefore(qint64 end)
    {
        const qint64 frames = std::clamp<qint64>(end - base_, 0,
                                                 normalization_.size());
        if (frames == 0) return true;
        QVector<float> published(frames * 2);
        for (qint64 frame = 0; frame < frames; ++frame) {
            const float weight = normalization_.at(frame);
            if (weight > 0.0F) {
                published[frame * 2] = accumulated_.at(frame * 2) / weight;
                published[frame * 2 + 1] = accumulated_.at(frame * 2 + 1) / weight;
            }
        }
        if (!sink_(published)) return false;
        accumulated_.remove(0, static_cast<qsizetype>(frames * 2));
        normalization_.remove(0, static_cast<qsizetype>(frames));
        base_ += frames;
        return true;
    }

    Sink sink_;
    QVector<float> accumulated_;
    QVector<float> normalization_;
    qint64 base_ = 0;
};

BackendResult validateSession(const TrustedModelProfile& profile,
                              OrtModelSession& session)
{
    const ContractValidationResult validation =
        validateModelMetadata(profile, session.metadata());
    return validation.ok ? BackendResult{true, {}, {}, {}}
                         : fail(validation.code, validation.message);
}

BackendResult openSession(const NativeStartRequest& request,
                          const TrustedModelProfile& profile,
                          const QString& modelPath, ExecutionProvider provider,
                          int adapterId, std::unique_ptr<OrtModelSession>* session)
{
    auto candidate = std::make_unique<OrtModelSession>();
    OrtOperationResult opened = candidate->open(request.runtimePath, modelPath,
                                                provider, adapterId);
    if (!opened.ok) return fail(opened.code, opened.message);
    BackendResult validated = validateSession(profile, *candidate);
    if (!validated.ok) return validated;
    *session = std::move(candidate);
    return {true, {}, {}, {}};
}

BackendResult proveProvider(const NativeStartRequest& request,
                            const TrustedModelProfile& profile,
                            ExecutionProvider provider, int adapterId)
{
    std::unique_ptr<OrtModelSession> session;
    BackendResult opened = openSession(request, profile, request.modelFiles.front(),
                                       provider, adapterId, &session);
    if (!opened.ok) return opened;
    QVector<qint64> inputShape = profile.inputs.front().shape;
    if (!inputShape.isEmpty() && inputShape.front() < 0) inputShape.front() = 1;
    QVector<qint64> outputShape = profile.outputs.front().shape;
    if (!outputShape.isEmpty() && outputShape.front() < 0) outputShape.front() = 1;
    qsizetype count = 1;
    for (qint64 dimension : inputShape) count *= dimension;
    QVector<float> zeros(count);
    std::atomic_bool notCancelled{false};
    const OrtOperationResult run = session->run(zeros, inputShape, outputShape,
                                                 notCancelled);
    return run.ok ? BackendResult{true, {}, {}, {}}
                  : fail(run.code, run.message);
}

struct ProviderSelection {
    bool ok = false;
    ExecutionProvider provider = ExecutionProvider::Cpu;
    int adapterId = 0;
    QString fallbackReason;
    QString code;
    QString message;
};

ProviderSelection selectProvider(const NativeStartRequest& request,
                                 const TrustedModelProfile& profile)
{
    if (request.device == DeviceMode::Cpu) {
        const BackendResult cpu = proveProvider(request, profile,
                                                ExecutionProvider::Cpu, 0);
        return {cpu.ok, ExecutionProvider::Cpu, 0, {}, cpu.code, cpu.message};
    }
    const QVector<DxgiAdapterInfo> adapters = enumerateDxgiHardwareAdapters();
    QString gpuReason = adapters.isEmpty()
        ? QStringLiteral("No hardware DXGI adapter is available") : QString();
    if (!adapters.isEmpty()) {
        const BackendResult gpu = proveProvider(request, profile,
                                                ExecutionProvider::DirectMl,
                                                adapters.front().deviceId);
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
    const BackendResult cpu = proveProvider(request, profile,
                                            ExecutionProvider::Cpu, 0);
    return {cpu.ok, ExecutionProvider::Cpu, 0, gpuReason, cpu.code, cpu.message};
}

bool encodeStagingWave(const QString& stagingPath, const QString& outputPath,
                       const QString& extension,
                       const std::atomic_bool& cancelled, QString* error)
{
    if (extension.compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
        if (!QFile::rename(stagingPath, outputPath)) {
            *error = QStringLiteral("Could not activate the staged WAV output");
            return false;
        }
        return true;
    }
    agplayer::TranscodeConfig config;
    config.output_path = outputPath.toUtf8().toStdString();
    config.sample_rate = 44100;
    config.channels = 2;
    std::string coreError;
    const ag_result result = agplayer::transcode(
        stagingPath.toUtf8().toStdString(), config, &cancelled,
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
                     const ProviderSelection& provider,
                     const std::atomic_bool& cancelled,
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

    std::unique_ptr<OrtModelSession> session;
    BackendResult opened = openSession(request, trusted, request.modelFiles.front(),
                                       provider.provider, provider.adapterId, &session);
    if (!opened.ok) return opened;

    const QString primaryPath = QDir(transaction.temporaryDirectory())
        .filePath(QStringLiteral(".primary.float.wav"));
    const QString complementPath = QDir(transaction.temporaryDirectory())
        .filePath(QStringLiteral(".complement.float.wav"));
    FloatWaveWriter primaryWriter;
    FloatWaveWriter complementWriter;
    if (!primaryWriter.open(primaryPath, 44100, 2)
        || !complementWriter.open(complementPath, 44100, 2)) {
        return fail(QStringLiteral("output_write_failed"),
                    primaryWriter.errorString() + complementWriter.errorString());
    }

    QVector<float> pendingMix;
    StreamingOverlapPublisher primaryPublisher(
        [&](const QVector<float>& published) {
            if (!primaryWriter.write(published)) return false;
            if (pendingMix.size() < published.size()) return false;
            QVector<float> complement(published.size());
            for (qsizetype index = 0; index < published.size(); ++index) {
                complement[index] = pendingMix.at(index) - published.at(index);
            }
            pendingMix.remove(0, published.size());
            return complementWriter.write(complement);
        });
    StreamingOverlapPublisher mixPublisher(
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
            progress((chunkIndex + 1.0) / starts.size(),
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
                               request.extension, cancelled, &error)) {
            return fail(cancelled.load() ? QStringLiteral("cancelled")
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
                             const QString& destination, QString* error)
{
    if (contributors.size() != 3) return false;
    QFile drums(contributors.at(0));
    QFile bass(contributors.at(1));
    QFile other(contributors.at(2));
    if (!drums.open(QIODevice::ReadOnly) || !bass.open(QIODevice::ReadOnly)
        || !other.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not reopen Demucs staging rows");
        return false;
    }
    for (QFile* file : {&drums, &bass, &other}) file->seek(44);
    float peak = 0.0F;
    for (;;) {
        const QByteArray d = drums.read(64 * 1024);
        const QByteArray b = bass.read(d.size());
        const QByteArray o = other.read(d.size());
        if (d.size() != b.size() || d.size() != o.size()) return false;
        if (d.isEmpty()) break;
        for (qsizetype offset = 0; offset + 4 <= d.size(); offset += 4) {
            const quint32 dbits = qFromLittleEndian<quint32>(d.constData() + offset);
            const quint32 bbits = qFromLittleEndian<quint32>(b.constData() + offset);
            const quint32 obits = qFromLittleEndian<quint32>(o.constData() + offset);
            float dv, bv, ov;
            std::memcpy(&dv, &dbits, 4); std::memcpy(&bv, &bbits, 4);
            std::memcpy(&ov, &obits, 4);
            peak = std::max(peak, std::abs(dv + bv + ov));
        }
    }
    for (QFile* file : {&drums, &bass, &other}) file->seek(44);
    FloatWaveWriter writer;
    if (!writer.open(destination, 44100, 2)) return false;
    const float scale = peak > 1.0F ? 1.0F / peak : 1.0F;
    for (;;) {
        const QByteArray d = drums.read(64 * 1024);
        const QByteArray b = bass.read(d.size());
        const QByteArray o = other.read(d.size());
        if (d.isEmpty()) break;
        QVector<float> summed(d.size() / 4);
        for (qsizetype sample = 0; sample < summed.size(); ++sample) {
            const qsizetype offset = sample * 4;
            const quint32 dbits = qFromLittleEndian<quint32>(d.constData() + offset);
            const quint32 bbits = qFromLittleEndian<quint32>(b.constData() + offset);
            const quint32 obits = qFromLittleEndian<quint32>(o.constData() + offset);
            float dv, bv, ov;
            std::memcpy(&dv, &dbits, 4); std::memcpy(&bv, &bbits, 4);
            std::memcpy(&ov, &obits, 4);
            summed[sample] = (dv + bv + ov) * scale;
        }
        if (!writer.write(summed)) return false;
    }
    if (!writer.finish()) {
        *error = writer.errorString();
        return false;
    }
    return true;
}

BackendResult runDemucs(const NativeStartRequest& request,
                        const TrustedModelProfile& trusted,
                        const QStringList& modelHashes,
                        const ProviderSelection& provider,
                        const std::atomic_bool& cancelled,
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
        if (cancelled.load()) return fail(QStringLiteral("cancelled"),
                                          QStringLiteral("Separation cancelled"));
        const QString modelPath = request.modelFiles.at(modelIndex);
        const int row = trustedDemucsRowForHash(modelHashes.at(modelIndex));
        if (row < 0) return fail(QStringLiteral("model_semantics_invalid"),
                                 QStringLiteral("Demucs hash does not identify its trusted row"));
        const QString stem = profile.rows.at(row);
        const QString stagingPath = QDir(transaction.temporaryDirectory())
            .filePath(QStringLiteral(".%1.float.wav").arg(stem));
        FloatWaveWriter writer;
        if (!writer.open(stagingPath, 44100, 2)) {
            return fail(QStringLiteral("output_write_failed"), writer.errorString());
        }

        std::unique_ptr<OrtModelSession> session;
        BackendResult opened = openSession(request, trusted, modelPath,
                                           provider.provider, provider.adapterId,
                                           &session);
        if (!opened.ok) return opened;
        StreamingOverlapPublisher publisher(
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
                const QVector<float> weights = overlapWeights(starts, chunkIndex,
                                                              profile.chunkSamples);
                if (!publisher.add(start, rowInterleaved, weights)) {
                    return fail(QStringLiteral("output_write_failed"),
                                QStringLiteral("Could not stream Demucs overlap output"));
                }
                const double completed = modelIndex * starts.size() + chunkIndex + 1.0;
                progress(completed / (request.modelFiles.size() * starts.size()),
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
                instrumental, &error)) {
            return fail(QStringLiteral("output_write_failed"), error);
        }
        staging.insert(QStringLiteral("instrumental"), instrumental);
    }
    for (const QString& stem : request.stems) {
        if (!encodeStagingWave(staging.value(stem), transaction.temporaryPath(stem),
                               request.extension, cancelled, &error)) {
            return fail(cancelled.load() ? QStringLiteral("cancelled")
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
                                     &arraysValid);
    request.stems = stringArray(payload.value(QStringLiteral("stems")), &arraysValid);
    const QString device = payload.value(QStringLiteral("device")).toString();
    if (device == QStringLiteral("auto")) request.device = DeviceMode::Auto;
    else if (device == QStringLiteral("cpu")) request.device = DeviceMode::Cpu;
    else if (device == QStringLiteral("gpu")) request.device = DeviceMode::Gpu;
    else arraysValid = false;
    if (!arraysValid || request.runtimePath.isEmpty() || request.inputPath.isEmpty()
        || request.outputDirectory.isEmpty() || request.baseName.isEmpty()
        || request.extension.isEmpty() || request.modelFiles.isEmpty()
        || request.modelFiles.size() > 4 || request.stems.isEmpty()
        || request.stems.size() > 5) {
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

FloatWaveWriter::~FloatWaveWriter()
{
    if (file_.isOpen()) file_.close();
}

bool FloatWaveWriter::open(const QString& path, int sampleRate, int channels)
{
    if (file_.isOpen() || sampleRate <= 0 || channels <= 0 || channels > 8) {
        error_ = QStringLiteral("Invalid float WAV configuration");
        return false;
    }
    file_.setFileName(path);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error_ = file_.errorString();
        return false;
    }
    sampleRate_ = sampleRate;
    channels_ = channels;
    QByteArray header(44, '\0');
    std::memcpy(header.data(), "RIFF", 4);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, header.data() + 16);
    qToLittleEndian<quint16>(3, header.data() + 20); // IEEE float
    qToLittleEndian<quint16>(static_cast<quint16>(channels), header.data() + 22);
    qToLittleEndian<quint32>(static_cast<quint32>(sampleRate), header.data() + 24);
    qToLittleEndian<quint32>(static_cast<quint32>(sampleRate * channels * 4),
                             header.data() + 28);
    qToLittleEndian<quint16>(static_cast<quint16>(channels * 4), header.data() + 32);
    qToLittleEndian<quint16>(32, header.data() + 34);
    std::memcpy(header.data() + 36, "data", 4);
    if (file_.write(header) != header.size()) {
        error_ = file_.errorString();
        file_.close();
        return false;
    }
    return true;
}

bool FloatWaveWriter::write(const QVector<float>& interleavedSamples)
{
    if (!file_.isOpen() || interleavedSamples.size() % channels_ != 0
        || dataBytes_ + static_cast<quint64>(interleavedSamples.size()) * 4
            > std::numeric_limits<quint32>::max()) {
        error_ = QStringLiteral("Float WAV write is invalid or exceeds RIFF limits");
        return false;
    }
    QByteArray bytes(interleavedSamples.size() * 4, Qt::Uninitialized);
    for (qsizetype index = 0; index < interleavedSamples.size(); ++index) {
        quint32 bits = 0;
        const float value = interleavedSamples.at(index);
        std::memcpy(&bits, &value, sizeof(bits));
        qToLittleEndian(bits, bytes.data() + index * 4);
    }
    if (file_.write(bytes) != bytes.size()) {
        error_ = file_.errorString();
        return false;
    }
    dataBytes_ += static_cast<quint64>(bytes.size());
    return true;
}

bool FloatWaveWriter::finish()
{
    if (!file_.isOpen()) return false;
    QByteArray value(4, Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(36 + dataBytes_), value.data());
    if (!file_.seek(4) || file_.write(value) != 4) return false;
    qToLittleEndian<quint32>(static_cast<quint32>(dataBytes_), value.data());
    if (!file_.seek(40) || file_.write(value) != 4 || !file_.flush()) return false;
    file_.close();
    return true;
}

QString FloatWaveWriter::errorString() const { return error_; }

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
                                            const std::atomic_bool& cancelled,
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
    const ProviderSelection provider = selectProvider(request, *trusted);
    if (!provider.ok) return fail(provider.code, provider.message);
    BackendResult separated = trusted->family == QStringLiteral("mdx")
        ? runMdx(request, *trusted, provider, cancelled, progress, transaction)
        : runDemucs(request, *trusted, hashes, provider, cancelled, progress,
                    transaction);
    if (!separated.ok) return separated;
    progress(0.98, QStringLiteral("verification"));
    const TransactionResult committed = transaction.commit(verifyAudio);
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
