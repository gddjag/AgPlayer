#include "native_worker_backend.hpp"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace agplayer::separation;

class SeparationNativeBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void startRequestRequiresBoundedNativeFields();
    void sha256IsComputedFromTheActualFile();
    void rawFloatWaveWriterStreamsAndFinalizesAProbeableHeader();
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

void SeparationNativeBackendTest::rawFloatWaveWriterStreamsAndFinalizesAProbeableHeader()
{
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("流式.wav"));
    FloatWaveWriter writer;
    QVERIFY(writer.open(path, 44100, 2));
    QVERIFY(writer.write({0.25F, -0.25F, 0.5F, -0.5F}));
    QVERIFY(writer.finish());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray header = file.read(44);
    QCOMPARE(header.left(4), QByteArrayLiteral("RIFF"));
    QCOMPARE(header.mid(8, 4), QByteArrayLiteral("WAVE"));
    QCOMPARE(qFromLittleEndian<quint32>(header.constData() + 40), quint32{16});
    QCOMPARE(file.size(), qint64{60});
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
    QCOMPARE(readOnnxDefaultOpset(path), 17);

    const QString customOnly = temporary.filePath(QStringLiteral("custom.onnx"));
    QFile custom(customOnly);
    QVERIFY(custom.open(QIODevice::WriteOnly));
    // domain="custom" followed by version=17: no default ai.onnx opset.
    QCOMPARE(custom.write(QByteArray::fromHex("420a0a06637573746f6d1011")), qint64{12});
    custom.close();
    QCOMPARE(readOnnxDefaultOpset(customOnly), -1);
}

QTEST_GUILESS_MAIN(SeparationNativeBackendTest)
#include "separation_native_backend_test.moc"
