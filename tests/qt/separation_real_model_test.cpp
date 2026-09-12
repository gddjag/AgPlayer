#include "native_worker_backend.hpp"
#include "decoder.hpp"
#include "ort_session.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace agplayer::separation;

namespace {

struct DecodedTrack {
    bool ok = false;
    qint64 frames = 0;
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;
    double rms = 0.0;
    float peak = 0.0F;
};

DecodedTrack decodeTrack(const QString& path, const bool retainSamples,
                         QString* error)
{
    DecodedTrack track;
    agplayer::MediaMetadata metadata;
    if (agplayer::probe_media_metadata(path.toUtf8().toStdString(), metadata)
        != AG_OK) {
        *error = QStringLiteral("metadata probe failed: ") + path;
        return track;
    }
    track.sampleRate = metadata.sample_rate;
    track.channels = metadata.channels;

    agplayer::Decoder decoder;
    if (decoder.open(path.toUtf8().toStdString(), 44100, 2) != AG_OK) {
        *error = QStringLiteral("decode open failed: ") + path;
        return track;
    }
    agplayer::DecodedAudioBlock block;
    double squaredSum = 0.0;
    do {
        if (decoder.read(block) != AG_OK) {
            *error = QStringLiteral("decode read failed: ") + path;
            return track;
        }
        track.frames += static_cast<qint64>(block.frames);
        for (const float sample : block.samples) {
            if (!std::isfinite(sample)) {
                *error = QStringLiteral("non-finite sample: ") + path;
                return track;
            }
            track.peak = std::max(track.peak, std::abs(sample));
            squaredSum += static_cast<double>(sample) * sample;
        }
        if (retainSamples) {
            track.samples.insert(track.samples.end(), block.samples.begin(),
                                 block.samples.end());
        }
    } while (!block.end_of_stream);
    if (retainSamples
        && track.samples.size() != static_cast<std::size_t>(track.frames * 2)) {
        *error = QStringLiteral("decoded channel count mismatch: ") + path;
        return track;
    }
    const double sampleCount = static_cast<double>(track.frames) * 2.0;
    track.rms = sampleCount > 0.0 ? std::sqrt(squaredSum / sampleCount) : 0.0;
    track.ok = true;
    return track;
}

QString stemForOutput(const QString& path, const QStringList& stems,
                      const QString& modelName)
{
    const QString name = QFileInfo(path).completeBaseName();
    for (const QString& stem : stems) {
        if (name.endsWith(QStringLiteral("-") + stem + QStringLiteral("-")
                              + modelName,
                          Qt::CaseInsensitive)) {
            return stem;
        }
    }
    return {};
}

} // namespace

class SeparationRealModelTest final : public QObject {
    Q_OBJECT

private slots:
    void approvedCatalogModelRunsOnRequestedDeviceWhenExplicitlyEnabled();
    void cudaPublisherReferenceWhenExplicitlyEnabled();
};

void SeparationRealModelTest::cudaPublisherReferenceWhenExplicitlyEnabled()
{
    const QString outputRoot = qEnvironmentVariable("AGPLAYER_CUDA_REFERENCE_OUTPUT");
    if (outputRoot.isEmpty()) QSKIP("Opt-in raw CUDA publisher-reference diagnostic");
    QString error;
    const auto input = decodeTrack(qEnvironmentVariable("AGPLAYER_SEPARATION_REAL_AUDIO"), true, &error);
    QVERIFY2(input.ok, qPrintable(error));
    const double gain = qEnvironmentVariable("AGPLAYER_CUDA_REFERENCE_GAIN").toDouble();
    QVERIFY(gain > 0.0 && gain <= 1.0);
    const QStringList names{"drums", "bass", "other", "vocals"};
    QVector<std::vector<float>> stems;
    for (const auto& name : names) {
        const auto track = decodeTrack(QDir(outputRoot).filePath("demucs-" + name + "-demucs.wav"), true, &error);
        QVERIFY2(track.ok, qPrintable(error)); QCOMPARE(track.frames, input.frames);
        stems.push_back(track.samples);
    }
    constexpr int chunk = 343980, stride = 257985, overlap = 85995;
    double boundaryError = 0, interiorError = 0, boundaryEnergy = 0, interiorEnergy = 0;
    QVector<double> chunkErrors(static_cast<int>(input.frames / stride + 1), 0.0);
    for (qint64 frame = 0; frame < input.frames; ++frame) {
        for (int channel = 0; channel < 2; ++channel) {
            const auto i = static_cast<size_t>(frame * 2 + channel);
            const double sum = (stems[0][i] + stems[1][i] + stems[2][i] + stems[3][i]) / gain;
            const double diff = input.samples[i] - sum, energy = double(input.samples[i]) * input.samples[i];
            if (frame >= stride && frame % stride < overlap) { boundaryError += diff * diff; boundaryEnergy += energy; }
            else { interiorError += diff * diff; interiorEnergy += energy; chunkErrors[frame / stride] += diff * diff; }
        }
    }
    qInfo() << "Overlap NRMS" << std::sqrt(boundaryError / boundaryEnergy)
            << "Interior NRMS" << std::sqrt(interiorError / interiorEnergy);
    const int chunkIndex = static_cast<int>(std::max_element(chunkErrors.cbegin(), chunkErrors.cend()) - chunkErrors.cbegin());
    const qint64 start = qint64(chunkIndex) * stride;
    QVector<float> planar(chunk * 2, 0.0F), referenceSum(chunk * 2, 0.0F);
    QVector<float> normalizedPlanar(chunk * 2, 0.0F), normalizedSum(chunk * 2, 0.0F);
    // Meta's public API normalizes using the whole track's mono mean and
    // sample standard deviation, then restores every predicted target row.
    // This is an A/B diagnostic only: the ONNX publisher uses raw input.
    QVERIFY(input.frames > 1);
    double monoMean = 0.0, monoVariance = 0.0;
    for (qint64 f = 0; f < input.frames; ++f)
        monoMean += (double(input.samples[f * 2]) + input.samples[f * 2 + 1]) * 0.5;
    monoMean /= double(input.frames);
    for (qint64 f = 0; f < input.frames; ++f) {
        const double centered = (double(input.samples[f * 2]) + input.samples[f * 2 + 1]) * 0.5 - monoMean;
        monoVariance += centered * centered;
    }
    const double monoScale = std::sqrt(monoVariance / double(input.frames - 1)) + 1e-8;
    qInfo() << "Whole-track mono mean" << monoMean << "sample std plus epsilon" << monoScale;
    for (int f = 0; f < chunk && start + f < input.frames; ++f)
        for (int c = 0; c < 2; ++c) {
            planar[c * chunk + f] = input.samples[(start + f) * 2 + c];
            normalizedPlanar[c * chunk + f] = float((planar[c * chunk + f] - monoMean) / monoScale);
        }
    CancellationToken cancelled;
    double referenceVsNativeError = 0, referenceVsInputError = 0, referenceInputEnergy = 0;
    for (int row = 0; row < names.size(); ++row) {
        QFile model(QDir(qEnvironmentVariable("AGPLAYER_SEPARATION_CATALOG_ROOT"))
                        .filePath("htdemucs_ft_" + names[row] + "_fp16weights.onnx"));
        QVERIFY(model.open(QIODevice::ReadOnly));
        OrtModelSession session;
        const auto opened = session.open(qEnvironmentVariable("AGPLAYER_SEPARATION_ORT_DLL"),
                                         model.readAll(), ExecutionProvider::Cuda, 0, cancelled);
        QVERIFY2(opened.ok, qPrintable(opened.code + ": " + opened.message));
        const auto inferred = session.run(planar, {1, 2, chunk}, {1, 4, 2, chunk}, cancelled);
        QVERIFY2(inferred.ok, qPrintable(inferred.code + ": " + inferred.message));
        const auto normalized = session.run(normalizedPlanar, {1, 2, chunk}, {1, 4, 2, chunk}, cancelled);
        QVERIFY2(normalized.ok, qPrintable(normalized.code + ": " + normalized.message));
        QCOMPARE(inferred.output.size(), 4 * 2 * chunk);
        QCOMPARE(normalized.output.size(), inferred.output.size());
        QVERIFY(std::all_of(inferred.output.cbegin(), inferred.output.cend(), [](float x) { return std::isfinite(x); }));
        QVERIFY(std::all_of(normalized.output.cbegin(), normalized.output.cend(), [](float x) { return std::isfinite(x); }));
        double rawEnergy = 0, normalizedEnergy = 0, deltaEnergy = 0;
        double rawPeak = 0, normalizedPeak = 0;
        qint64 comparedSamples = 0;
        // Publisher contract: raw float32 input, each specialist's own row, no
        // normalization or ensemble average. This bypasses native chunk/OLA/export.
        for (int f = overlap; f < stride && start + f < input.frames; ++f) {
            for (int c = 0; c < 2; ++c) {
                const float value = inferred.output[row * 2 * chunk + c * chunk + f];
                const double normalizedValue = normalized.output[row * 2 * chunk + c * chunk + f] * monoScale + monoMean;
                referenceSum[f * 2 + c] += value;
                normalizedSum[f * 2 + c] += float(normalizedValue);
                rawEnergy += double(value) * value;
                normalizedEnergy += normalizedValue * normalizedValue;
                deltaEnergy += (normalizedValue - value) * (normalizedValue - value);
                rawPeak = std::max(rawPeak, std::abs(double(value)));
                normalizedPeak = std::max(normalizedPeak, std::abs(normalizedValue));
                ++comparedSamples;
                const double diff = value - stems[row][(start + f) * 2 + c] / gain;
                referenceVsNativeError += diff * diff;
            }
        }
        QVERIFY(comparedSamples > 0);
        QVERIFY(rawEnergy > 0 && normalizedEnergy > 0);
        qInfo() << "Same-session target-row A/B" << names[row] << "samples" << comparedSamples
                << "raw RMS/peak" << std::sqrt(rawEnergy / comparedSamples) << rawPeak
                << "normalized RMS/peak" << std::sqrt(normalizedEnergy / comparedSamples) << normalizedPeak
                << "delta/raw NRMS" << std::sqrt(deltaEnergy / rawEnergy);
    }
    double normalizedVsInputError = 0;
    for (int f = overlap; f < stride && start + f < input.frames; ++f) {
        for (int c = 0; c < 2; ++c) {
            const double value = input.samples[(start + f) * 2 + c];
            const double diff = value - referenceSum[f * 2 + c];
            referenceVsInputError += diff * diff; referenceInputEnergy += value * value;
            const double normalizedDiff = value - normalizedSum[f * 2 + c];
            normalizedVsInputError += normalizedDiff * normalizedDiff;
        }
    }
    const double difference = std::sqrt(referenceVsNativeError / referenceInputEnergy);
    double bestShiftError = referenceVsInputError;
    int bestShift = 0;
    for (int shift = -8; shift <= 8; ++shift) {
        double shiftedError = 0;
        for (int f = overlap + 8; f < stride - 8 && start + f < input.frames; ++f) {
            for (int c = 0; c < 2; ++c) {
                const double diff = input.samples[(start + f) * 2 + c] - referenceSum[(f + shift) * 2 + c];
                shiftedError += diff * diff;
            }
        }
        if (shiftedError < bestShiftError) { bestShiftError = shiftedError; bestShift = shift; }
    }
    qInfo() << "Independent raw CUDA reference chunk" << chunkIndex << "start" << start
            << "reference/native NRMS" << difference
            << "reference/input NRMS" << std::sqrt(referenceVsInputError / referenceInputEnergy)
            << "normalized/input NRMS (diagnostic, not an official quality guarantee)"
            << std::sqrt(normalizedVsInputError / referenceInputEnergy)
            << "best alignment shift within +/-8 samples" << bestShift;
    QVERIFY2(difference < 0.003, "Raw CUDA publisher reference must match native unscaled output");
}

void SeparationRealModelTest::
approvedCatalogModelRunsOnRequestedDeviceWhenExplicitlyEnabled()
{
    const QString runtime = qEnvironmentVariable("AGPLAYER_SEPARATION_ORT_DLL");
    const QString input = qEnvironmentVariable("AGPLAYER_SEPARATION_REAL_AUDIO");
    const QString catalogRoot = qEnvironmentVariable("AGPLAYER_SEPARATION_CATALOG_ROOT");
    const QString modelFilter = qEnvironmentVariable(
        "AGPLAYER_SEPARATION_REAL_MODEL_FILTER").trimmed().toLower();
    QString requestedDevice = qEnvironmentVariable(
        "AGPLAYER_SEPARATION_REAL_DEVICE").trimmed().toLower();
    const bool durationSmoke = qEnvironmentVariableIntValue(
        "AGPLAYER_SEPARATION_REAL_DURATION_SMOKE") != 0;
    if (requestedDevice.isEmpty()) requestedDevice = QStringLiteral("cpu");
    QVERIFY2(requestedDevice == QStringLiteral("cpu")
                 || requestedDevice == QStringLiteral("gpu")
                 || requestedDevice == QStringLiteral("auto"),
             qPrintable(QStringLiteral("Unknown real device: ") + requestedDevice));
    if (runtime.isEmpty() || input.isEmpty() || catalogRoot.isEmpty()) {
        QSKIP("Opt-in real-model test requires AGPLAYER_SEPARATION_ORT_DLL, "
              "AGPLAYER_SEPARATION_REAL_AUDIO, and AGPLAYER_SEPARATION_CATALOG_ROOT");
    }

    const QDir catalog(catalogRoot);
    QString decodeError;
    const DecodedTrack inputTrack = decodeTrack(input, !durationSmoke, &decodeError);
    QVERIFY2(inputTrack.ok, qPrintable(decodeError));
    QCOMPARE(inputTrack.sampleRate, 44100);
    QCOMPARE(inputTrack.channels, 2);
    QVERIFY(inputTrack.rms > 0.0);
    const QList<QPair<QString, QStringList>> approved{
        {QStringLiteral("kara"),
         {catalog.filePath(QStringLiteral("UVR_MDXNET_KARA.onnx"))}},
        {QStringLiteral("hq3"),
         {catalog.filePath(QStringLiteral("UVR-MDX-NET-Inst_HQ_3.onnx"))}},
        {QStringLiteral("kim"),
         {catalog.filePath(QStringLiteral("Kim_Vocal_2.onnx"))}},
        {QStringLiteral("hq1"),
         {catalog.filePath(QStringLiteral("UVR-MDX-NET-Inst_HQ_1.onnx"))}},
        {QStringLiteral("voc-ft"),
         {catalog.filePath(QStringLiteral("UVR-MDX-NET-Voc_FT.onnx"))}},
        {QStringLiteral("demucs"),
         {catalog.filePath(QStringLiteral("htdemucs_ft_bass_fp16weights.onnx")),
          catalog.filePath(QStringLiteral("htdemucs_ft_drums_fp16weights.onnx")),
          catalog.filePath(QStringLiteral("htdemucs_ft_other_fp16weights.onnx")),
          catalog.filePath(QStringLiteral("htdemucs_ft_vocals_fp16weights.onnx"))}},
    };
    int exercisedModels = 0;
    for (const auto& entry : approved) {
        if (!modelFilter.isEmpty() && entry.first != modelFilter) continue;
        ++exercisedModels;
        QJsonArray modelFiles;
        for (const QString& model : entry.second) {
            QVERIFY2(QFileInfo::exists(model), qPrintable(model));
            modelFiles.push_back(model);
        }
        QFile inspectedModel(entry.second.first());
        QVERIFY(inspectedModel.open(QIODevice::ReadOnly));
        const int actualOpset = readOnnxDefaultOpset(inspectedModel.readAll());
        qInfo().noquote() << entry.first << "actual default opset" << actualOpset;
        QTemporaryDir output;
        QVERIFY(output.isValid());
        if (qEnvironmentVariableIntValue("AGPLAYER_SEPARATION_KEEP_OUTPUTS")) {
            output.setAutoRemove(false); qInfo().noquote() << "Output directory:" << output.path();
        }
        NativeWorkerBackend backend;
        CancellationToken cancelled;
        const QStringList stems = entry.first == QStringLiteral("demucs")
            ? QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental"),
                          QStringLiteral("drums"), QStringLiteral("bass"),
                          QStringLiteral("other")}
            : QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental")};
        QJsonArray requestedStems;
        QJsonArray stemLabels;
        for (const QString& stem : stems) {
            requestedStems.push_back(stem);
            stemLabels.push_back(stem);
        }
        const BackendResult result = backend.separate(
            {{QStringLiteral("runtimePath"), runtime},
             {QStringLiteral("inputPath"), input},
             {QStringLiteral("modelFiles"), modelFiles},
             {QStringLiteral("outputDirectory"), output.path()},
             {QStringLiteral("baseName"), entry.first},
             {QStringLiteral("directoryName"),
              entry.first + QStringLiteral("-real-model")},
             {QStringLiteral("modelName"), entry.first},
             {QStringLiteral("extension"), QStringLiteral("wav")},
             {QStringLiteral("stems"), requestedStems},
             {QStringLiteral("stemLabels"), stemLabels},
             {QStringLiteral("device"), requestedDevice}},
            cancelled, [](double, const QString&) {});
        QVERIFY2(result.ok,
                 qPrintable(entry.first + QStringLiteral(": ") + result.code
                            + QStringLiteral(": ") + result.message));
        const QString selectedProvider =
            result.payload.value(QStringLiteral("provider")).toString();
        if (requestedDevice == QStringLiteral("auto")) {
            QVERIFY2(selectedProvider == QStringLiteral("directml") || selectedProvider == QStringLiteral("cuda")
                         || selectedProvider == QStringLiteral("cpu"),
                     qPrintable(QStringLiteral("Auto selected unknown provider: ")
                                + selectedProvider));
        } else {
            const QString expectedGpu = qEnvironmentVariable("AGPLAYER_SEPARATION_EXPECT_PROVIDER", "directml");
            QCOMPARE(selectedProvider, requestedDevice == QStringLiteral("gpu") ? expectedGpu : QStringLiteral("cpu"));
        }
        qInfo().noquote() << "Actual provider:" << selectedProvider << "device:"
                         << result.payload.value(QStringLiteral("device")).toString();
        const double outputGain = result.payload.value(QStringLiteral("outputGain")).toDouble(1.0);
        QVERIFY(std::isfinite(outputGain) && outputGain > 0.0 && outputGain <= 1.0);
        qInfo() << "Reported common output gain" << outputGain << "raw peaks" << result.payload.value(QStringLiteral("rawPeaks"));
        const QJsonArray outputs = result.payload.value(QStringLiteral("outputs")).toArray();
        QCOMPARE(outputs.size(), stems.size());
        QHash<QString, DecodedTrack> decoded;
        for (const QJsonValue& outputValue : outputs) {
            const QString path = outputValue.toString();
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
            const QString stem = stemForOutput(path, stems, entry.first);
            QVERIFY2(!stem.isEmpty(), qPrintable(QStringLiteral("Unknown output: ") + path));
            QVERIFY2(!decoded.contains(stem), qPrintable(QStringLiteral("Duplicate stem: ") + stem));
            decodeError.clear();
            const DecodedTrack track = decodeTrack(path, !durationSmoke, &decodeError);
            QVERIFY2(track.ok, qPrintable(decodeError));
            QCOMPARE(track.sampleRate, 44100);
            QCOMPARE(track.channels, 2);
            QCOMPARE(track.frames, inputTrack.frames);
            qInfo().noquote() << stem << "frames" << track.frames << "rms" << track.rms << "peak" << track.peak;
            QVERIFY2(track.rms > 1.0e-10,
                     qPrintable(stem + QStringLiteral(" is silent")));
            QVERIFY2(track.peak <= 1.0001F,
                     qPrintable(stem + QStringLiteral(" clips at ")
                                + QString::number(track.peak, 'g', 9)));
            if (entry.first == QStringLiteral("demucs")) {
                QVERIFY2(track.peak <= 0.9901F,
                         "Demucs shared headroom gain must prevent integer-output clipping");
            }
            decoded.insert(stem, track);
        }
        QCOMPARE(decoded.size(), stems.size());

        if (durationSmoke) continue;
        const auto vocalsIt = decoded.constFind(QStringLiteral("vocals"));
        const auto instrumentalIt = decoded.constFind(QStringLiteral("instrumental"));
        QVERIFY(vocalsIt != decoded.cend());
        QVERIFY(instrumentalIt != decoded.cend());
        const DecodedTrack& vocals = vocalsIt.value();
        const DecodedTrack& instrumental = instrumentalIt.value();
        double squaredError = 0.0;
        for (std::size_t index = 0; index < inputTrack.samples.size(); ++index) {
            const double difference = static_cast<double>(inputTrack.samples.at(index))
                - (vocals.samples.at(index) + instrumental.samples.at(index)) / outputGain;
            squaredError += difference * difference;
        }
        const double reconstructionRms = std::sqrt(
            squaredError / static_cast<double>(inputTrack.samples.size()));
        const double normalizedError = reconstructionRms / inputTrack.rms;
        qInfo().noquote() << entry.first << "reconstruction normalized RMS"
                          << normalizedError;
        if (entry.first == QStringLiteral("demucs")) {
            double error = 0;
            const auto& drums = decoded.constFind("drums").value().samples;
            const auto& bass = decoded.constFind("bass").value().samples;
            const auto& other = decoded.constFind("other").value().samples;
            double maximumAccompanimentError = 0;
            for (std::size_t i = 0; i < inputTrack.samples.size(); ++i) {
                const double difference = inputTrack.samples[i] - (vocals.samples[i]
                    + drums[i] + bass[i] + other[i]) / outputGain;
                error += difference * difference;
                maximumAccompanimentError = std::max(maximumAccompanimentError,
                    std::abs(double(instrumental.samples[i]) - drums[i] - bass[i] - other[i]));
            }
            qInfo() << "Four independent rows reconstruction NRMS" << std::sqrt(error / inputTrack.samples.size()) / inputTrack.rms;
            qInfo() << "Accompaniment component sum maximum error" << maximumAccompanimentError;
            QVERIFY2(maximumAccompanimentError < 1.0e-4, "Derived accompaniment must preserve the sum of the three exported components");
        }
        QVERIFY(std::isfinite(normalizedError));
        // Demucs predicts independent sources; its training objective does not
        // impose mixture consistency. Source-sum NRMS is diagnostic, not a
        // separation-quality oracle. CUDA target-row/native parity is tested
        // independently in cudaPublisherReferenceWhenExplicitlyEnabled().
        // MDX accompaniment is defined by subtraction and must reconstruct.
        if (entry.first != QStringLiteral("demucs")) {
            QVERIFY2(normalizedError < 1.0e-4,
                     qPrintable(entry.first + QStringLiteral(" reconstruction error: ")
                                + QString::number(normalizedError, 'g', 9)));
        }
    }
    QVERIFY2(exercisedModels > 0,
             qPrintable(QStringLiteral("Unknown real-model filter: ")
                        + modelFilter));
}

QTEST_GUILESS_MAIN(SeparationRealModelTest)
#include "separation_real_model_test.moc"
