#include "native_worker_backend.hpp"
#include "decoder.hpp"

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

QString stemForOutput(const QString& path, const QStringList& stems)
{
    const QString name = QFileInfo(path).completeBaseName();
    for (const QString& stem : stems) {
        if (name.endsWith(QStringLiteral("-") + stem, Qt::CaseInsensitive)) {
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
};

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
                 || requestedDevice == QStringLiteral("gpu"),
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
        NativeWorkerBackend backend;
        CancellationToken cancelled;
        const QStringList stems = entry.first == QStringLiteral("demucs")
            ? QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental"),
                          QStringLiteral("drums"), QStringLiteral("bass"),
                          QStringLiteral("other")}
            : QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental")};
        QJsonArray requestedStems;
        for (const QString& stem : stems) requestedStems.push_back(stem);
        const BackendResult result = backend.separate(
            {{QStringLiteral("runtimePath"), runtime},
             {QStringLiteral("inputPath"), input},
             {QStringLiteral("modelFiles"), modelFiles},
             {QStringLiteral("outputDirectory"), output.path()},
             {QStringLiteral("baseName"), entry.first},
             {QStringLiteral("extension"), QStringLiteral("wav")},
             {QStringLiteral("stems"), requestedStems},
             {QStringLiteral("device"), requestedDevice}},
            cancelled, [](double, const QString&) {});
        QVERIFY2(result.ok,
                 qPrintable(entry.first + QStringLiteral(": ") + result.code
                            + QStringLiteral(": ") + result.message));
        QCOMPARE(result.payload.value(QStringLiteral("provider")).toString(),
                 requestedDevice == QStringLiteral("gpu")
                     ? QStringLiteral("directml") : QStringLiteral("cpu"));
        const QJsonArray outputs = result.payload.value(QStringLiteral("outputs")).toArray();
        QCOMPARE(outputs.size(), stems.size());
        QHash<QString, DecodedTrack> decoded;
        for (const QJsonValue& outputValue : outputs) {
            const QString path = outputValue.toString();
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
            const QString stem = stemForOutput(path, stems);
            QVERIFY2(!stem.isEmpty(), qPrintable(QStringLiteral("Unknown output: ") + path));
            QVERIFY2(!decoded.contains(stem), qPrintable(QStringLiteral("Duplicate stem: ") + stem));
            decodeError.clear();
            const DecodedTrack track = decodeTrack(path, !durationSmoke, &decodeError);
            QVERIFY2(track.ok, qPrintable(decodeError));
            QCOMPARE(track.sampleRate, 44100);
            QCOMPARE(track.channels, 2);
            QCOMPARE(track.frames, inputTrack.frames);
            QVERIFY2(track.rms > 1.0e-10,
                     qPrintable(stem + QStringLiteral(" is silent")));
            QVERIFY2(track.peak <= 1.0001F,
                     qPrintable(stem + QStringLiteral(" clips at ")
                                + QString::number(track.peak, 'g', 9)));
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
                - vocals.samples.at(index) - instrumental.samples.at(index);
            squaredError += difference * difference;
        }
        const double reconstructionRms = std::sqrt(
            squaredError / static_cast<double>(inputTrack.samples.size()));
        const double normalizedError = reconstructionRms / inputTrack.rms;
        qInfo().noquote() << entry.first << "reconstruction normalized RMS"
                          << normalizedError;
        const double maximumReconstructionError =
            entry.first == QStringLiteral("demucs") ? 0.05 : 1.0e-4;
        QVERIFY2(std::isfinite(normalizedError)
                     && normalizedError < maximumReconstructionError,
                 qPrintable(entry.first + QStringLiteral(" reconstruction error: ")
                            + QString::number(normalizedError, 'g', 9)));
    }
    QVERIFY2(exercisedModels > 0,
             qPrintable(QStringLiteral("Unknown real-model filter: ")
                        + modelFilter));
}

QTEST_GUILESS_MAIN(SeparationRealModelTest)
#include "separation_real_model_test.moc"
