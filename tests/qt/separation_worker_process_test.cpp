#include "separation_protocol.hpp"

#include <QJsonDocument>
#include <QProcess>
#include <QTest>

using namespace agplayer::separation;

#ifndef AG_SEPARATION_WORKER_PATH
#error AG_SEPARATION_WORKER_PATH must name the worker executable
#endif

class SeparationWorkerProcessTest final : public QObject {
    Q_OBJECT

private slots:
    void helloMalformedAndShutdownUseNdjsonAndExitCleanly();
    void stdinEofDrainsAndExitsWithinTheClientDeadline();
    void oversizedLineIsRejectedAndTheWorkerStillShutsDown();
    void clientTimeoutCanForceAWorkerCrashWithoutAFalseResult();
};

void SeparationWorkerProcessTest::helloMalformedAndShutdownUseNdjsonAndExitCleanly()
{
    QProcess process;
    process.setProgram(QString::fromUtf8(AG_SEPARATION_WORKER_PATH));
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));

    process.write(encodeProtocolMessage(ProtocolType::Hello,
                                        QStringLiteral("hello-process")));
    QVERIFY(process.waitForReadyRead(3000));
    ProtocolParseResult hello = parseProtocolMessage(process.readLine());
    QVERIFY(hello.ok);
    QCOMPARE(hello.message.type, ProtocolType::Hello);
    QCOMPARE(hello.message.requestId, QStringLiteral("hello-process"));

    process.write(QByteArrayLiteral("{bad\n"));
    QVERIFY(process.waitForReadyRead(3000));
    ProtocolParseResult malformed = parseProtocolMessage(process.readLine());
    QVERIFY(malformed.ok);
    QCOMPARE(malformed.message.type, ProtocolType::Error);
    QCOMPARE(malformed.message.requestId, QStringLiteral("invalid-request"));
    QCOMPARE(malformed.message.payload.value(QStringLiteral("code")).toString(),
             QStringLiteral("malformed_json"));

    process.write(encodeProtocolMessage(ProtocolType::Shutdown,
                                        QStringLiteral("shutdown-process")));
    QVERIFY(process.waitForReadyRead(3000));
    ProtocolParseResult shutdown = parseProtocolMessage(process.readLine());
    QVERIFY(shutdown.ok);
    QCOMPARE(shutdown.message.type, ProtocolType::Shutdown);
    QCOMPARE(shutdown.message.requestId, QStringLiteral("shutdown-process"));
    QVERIFY(process.waitForFinished(3000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QCOMPARE(process.readAllStandardError(), QByteArray());
}

void SeparationWorkerProcessTest::stdinEofDrainsAndExitsWithinTheClientDeadline()
{
    QProcess process;
    process.setProgram(QString::fromUtf8(AG_SEPARATION_WORKER_PATH));
    process.start();
    QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
    process.closeWriteChannel();
    QVERIFY2(process.waitForFinished(3000), qPrintable(process.errorString()));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);

    const ProtocolParseResult shutdown = parseProtocolMessage(process.readLine());
    QVERIFY(shutdown.ok);
    QCOMPARE(shutdown.message.type, ProtocolType::Shutdown);
    QCOMPARE(shutdown.message.requestId, QStringLiteral("stdin-eof"));
}

void SeparationWorkerProcessTest::oversizedLineIsRejectedAndTheWorkerStillShutsDown()
{
    QProcess process;
    process.setProgram(QString::fromUtf8(AG_SEPARATION_WORKER_PATH));
    process.start();
    QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
    process.write(QByteArray(kMaximumProtocolLineBytes + 1, 'x') + '\n');
    QVERIFY(process.waitForReadyRead(5000));
    const ProtocolParseResult rejected = parseProtocolMessage(process.readLine());
    QVERIFY(rejected.ok);
    QCOMPARE(rejected.message.type, ProtocolType::Error);
    QCOMPARE(rejected.message.payload.value(QStringLiteral("code")).toString(),
             QStringLiteral("message_too_large"));

    process.write(encodeProtocolMessage(ProtocolType::Shutdown,
                                        QStringLiteral("after-oversized")));
    QVERIFY(process.waitForReadyRead(3000));
    QVERIFY(process.waitForFinished(3000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
}

void SeparationWorkerProcessTest::clientTimeoutCanForceAWorkerCrashWithoutAFalseResult()
{
    QProcess process;
    process.setProgram(QString::fromUtf8(AG_SEPARATION_WORKER_PATH));
    process.start();
    QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
    process.kill();
    QVERIFY2(process.waitForFinished(3000), qPrintable(process.errorString()));
    QCOMPARE(process.exitStatus(), QProcess::CrashExit);
    const QByteArray output = process.readAllStandardOutput();
    QVERIFY(!output.contains("\"type\":\"result\""));
}

QTEST_GUILESS_MAIN(SeparationWorkerProcessTest)
#include "separation_worker_process_test.moc"
