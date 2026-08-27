#include "native_worker_backend.hpp"

#include "decoder.hpp"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace agplayer::separation;

class SeparationNativeBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void startRequestRequiresBoundedNativeFields();
    void startRequestRejectsOversizedPathsAndNames();
    void sha256IsComputedFromTheActualFile();
    void ffmpegWaveWriterRoundTripsThroughTheProductionDecoder();
    void inferenceProgressLeavesRoomForVerificationAndCompletion();
    void readsTheActualDefaultOnnxOpset();
};

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
        {QStringLiteral("extension"), QStringLiteral("flac")},
        {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals")}},
        {QStringLiteral("device"), QStringLiteral("auto")},
    };
    const StartRequestParseResult parsed = parseStartRequest(valid);
    QVERIFY2(parsed.ok, qPrintable(parsed.message));
    QCOMPARE(parsed.request.device, DeviceMode::Auto);
    QCOMPARE(parsed.request.modelFiles.size(), 1);
    QCOMPARE(parsed.request.stems, (QStringList{QStringLiteral("vocals")}));

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
        {QStringLiteral("extension"), QStringLiteral("wav")},
        {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals")}},
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
    QJsonObject longExtension = base;
    longExtension.insert(QStringLiteral("extension"), QString(17, QLatin1Char('e')));
    QCOMPARE(parseStartRequest(longExtension).code,
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
    QCOMPARE(hashFileSha256(path),
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
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

QTEST_GUILESS_MAIN(SeparationNativeBackendTest)
#include "separation_native_backend_test.moc"
