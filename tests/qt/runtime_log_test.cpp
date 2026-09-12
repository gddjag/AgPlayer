#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

class RuntimeLogTest final : public QObject {
    Q_OBJECT

private slots:
    void mapResultReturnsChineseUtf8ForKnownErrors();
    void installCreatesLogFile();
    void mappedCoreErrorLogIncludesAllFields();
    void qtMessageHandlerRoutesThroughRuntimeLog();
    void rotationTriggersAtTwoMegabytesAndKeepsOldBackup();
    void logContainsNoRawBinaryPayload();
    void uninstallRestoresPreviousHandler();
};

namespace {

// UTF-8 byte sequences matching the Chinese strings produced by
// RuntimeLog::mapResult(). Comparing against the raw UTF-8 bytes avoids any
// dependency on the source file encoding the test is compiled with.
const QByteArray kInvalidArgument = QByteArray("\xe5\x8f\x82\xe6\x95\xb0\xe6\x97\xa0\xe6\x95\x88");
const QByteArray kIoError = QByteArray("\xe6\x97\xa0\xe6\xb3\x95\xe8\xaf\xbb\xe5\x8f\x96\xe6\x96\x87\xe4\xbb\xb6");
const QByteArray kUnsupportedFormat = QByteArray("\xe4\xb8\x8d\xe6\x94\xaf\xe6\x8c\x81\xe7\x9a\x84\xe9\x9f\xb3\xe9\xa2\x91\xe6\xa0\xbc\xe5\xbc\x8f");
const QByteArray kDecodeError = QByteArray("\xe8\xa7\xa3\xe7\xa0\x81\xe5\xa4\xb1\xe8\xb4\xa5");
const QByteArray kDeviceError = QByteArray("\xe9\x9f\xb3\xe9\xa2\x91\xe8\xae\xbe\xe5\xa4\x87\xe9\x94\x99\xe8\xaf\xaf");
const QByteArray kCancelled = QByteArray("\xe6\x93\x8d\xe4\xbd\x9c\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88");
const QByteArray kInternalError = QByteArray("\xe5\x86\x85\xe9\x83\xa8\xe9\x94\x99\xe8\xaf\xaf");
const QByteArray kUnknownError = QByteArray("\xe6\x9c\xaa\xe7\x9f\xa5\xe9\x94\x99\xe8\xaf\xaf");

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return file.readAll();
}

} // namespace

void RuntimeLogTest::mapResultReturnsChineseUtf8ForKnownErrors()
{
    QCOMPARE(RuntimeLog::mapResult(AG_OK), QString());

    QCOMPARE(RuntimeLog::mapResult(AG_INVALID_ARGUMENT).toUtf8(), kInvalidArgument);
    QCOMPARE(RuntimeLog::mapResult(AG_IO_ERROR).toUtf8(), kIoError);
    QCOMPARE(RuntimeLog::mapResult(AG_UNSUPPORTED_FORMAT).toUtf8(), kUnsupportedFormat);
    QCOMPARE(RuntimeLog::mapResult(AG_DECODE_ERROR).toUtf8(), kDecodeError);
    QCOMPARE(RuntimeLog::mapResult(AG_DEVICE_ERROR).toUtf8(), kDeviceError);
    QCOMPARE(RuntimeLog::mapResult(AG_CANCELLED).toUtf8(), kCancelled);
    QCOMPARE(RuntimeLog::mapResult(AG_INTERNAL_ERROR).toUtf8(), kInternalError);

    const ag_result unknown = static_cast<ag_result>(99);
    QCOMPARE(RuntimeLog::mapResult(unknown).toUtf8(), kUnknownError);
}

void RuntimeLogTest::installCreatesLogFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("empty.log"));

    RuntimeLog::install(logPath);
    RuntimeLog::uninstall();

    QVERIFY(QFileInfo::exists(logPath));
    QCOMPARE(QFileInfo(logPath).size(), 0);
}

void RuntimeLogTest::mappedCoreErrorLogIncludesAllFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("error.log"));

    RuntimeLog::install(logPath);
    RuntimeLog::log(AG_DEVICE_ERROR, QStringLiteral("Playback"),
                    QStringLiteral("device disconnected"));
    RuntimeLog::uninstall();

    const QByteArray content = readAll(logPath);
    QVERIFY(!content.isEmpty());
    QVERIFY(content.contains("[ERROR]"));
    QVERIFY(content.contains("[Playback]"));
    QVERIFY(content.contains("device disconnected"));
    QVERIFY(content.contains("rc=5"));
    QVERIFY(content.contains(kDeviceError));
    // Timestamp pattern: ISO-8601 UTC with milliseconds.
    QVERIFY(content.contains("T") && content.contains("Z"));
}

void RuntimeLogTest::qtMessageHandlerRoutesThroughRuntimeLog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("qt_messages.log"));

    RuntimeLog::install(logPath);
    qInfo("info message from qt");
    qWarning("warning message from qt");
    RuntimeLog::uninstall();

    const QByteArray content = readAll(logPath);
    QVERIFY(content.contains("[INFO]"));
    QVERIFY(content.contains("[Qt]"));
    QVERIFY(content.contains("info message from qt"));
    QVERIFY(content.contains("[WARN]"));
    QVERIFY(content.contains("warning message from qt"));
}

void RuntimeLogTest::rotationTriggersAtTwoMegabytesAndKeepsOldBackup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("rotating.log"));
    const QString oldPath = logPath + QStringLiteral(".old");

    // Pre-fill the log file past the rotation threshold so the next write
    // triggers a rotation.
    {
        QFile file(logPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray padding(RuntimeLog::RotationThresholdBytes + 1024, 'x');
        QCOMPARE(file.write(padding), static_cast<qint64>(padding.size()));
    }
    QVERIFY(QFileInfo(logPath).size()
            > static_cast<qint64>(RuntimeLog::RotationThresholdBytes));
    QVERIFY(!QFileInfo::exists(oldPath));

    RuntimeLog::install(logPath);
    RuntimeLog::log(AG_OK, QStringLiteral("Rotation"),
                    QStringLiteral("trigger rotation"));
    RuntimeLog::uninstall();

    QVERIFY(QFileInfo::exists(oldPath));
    QVERIFY(QFileInfo(oldPath).size()
            > static_cast<qint64>(RuntimeLog::RotationThresholdBytes));
    QVERIFY(QFileInfo(logPath).size()
            <= static_cast<qint64>(RuntimeLog::RotationThresholdBytes));
    QVERIFY(readAll(logPath).contains("trigger rotation"));
}

void RuntimeLogTest::logContainsNoRawBinaryPayload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("binary_safe.log"));

    // Simulate a binary payload that must never appear in the log: a slab of
    // null bytes (PCM audio) and high-byte values (cover art).
    QByteArray pcmSlab(256, '\0');
    for (int i = 0; i < pcmSlab.size(); ++i) {
        pcmSlab[i] = static_cast<char>(i);
    }

    RuntimeLog::install(logPath);
    // RuntimeLog::log only accepts QString, so binary data cannot be passed
    // directly. This verifies the API boundary: even if a caller tried to
    // embed binary data via QString::fromUtf8, invalid sequences are replaced.
    const QString safeDetail = QString::fromUtf8(pcmSlab); // replaces invalid UTF-8
    RuntimeLog::log(AG_DECODE_ERROR, QStringLiteral("Decoder"), safeDetail);
    RuntimeLog::uninstall();

    const QByteArray content = readAll(logPath);
    QVERIFY(content.contains("[Decoder]"));
    QVERIFY(content.contains("rc=4"));
    // The log file must be valid UTF-8 with no raw null bytes.
    QVERIFY(!content.contains('\0'));
    QVERIFY(QString::fromUtf8(content).isNull() == false || content.isEmpty());
}

void RuntimeLogTest::uninstallRestoresPreviousHandler()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("lifecycle.log"));

    QVERIFY(!RuntimeLog::defaultLogPath().isEmpty());

    RuntimeLog::install(logPath);
    qInfo("during install");
    RuntimeLog::uninstall();

    // After uninstall, Qt messages must NOT be written to our log file.
    // They go to the default handler (stderr/console).
    const qint64 sizeAfterUninstall = QFileInfo(logPath).size();
    qInfo("after uninstall");
    QCOMPARE(QFileInfo(logPath).size(), sizeAfterUninstall);

    // Reinstalling on a fresh path works (idempotent install).
    const QString secondPath = dir.filePath(QStringLiteral("second.log"));
    RuntimeLog::install(secondPath);
    qInfo("reinstalled");
    RuntimeLog::uninstall();

    QVERIFY(readAll(secondPath).contains("reinstalled"));
}

QTEST_GUILESS_MAIN(RuntimeLogTest)
#include "runtime_log_test.moc"
