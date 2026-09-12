#include "separation_protocol.hpp"
#include "output_transaction.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace agplayer::separation;

#ifndef AG_SEPARATION_WORKER_PATH
#error AG_SEPARATION_WORKER_PATH must name the worker executable
#endif
#ifndef AG_SEPARATION_BLOCKING_WORKER_PATH
#error AG_SEPARATION_BLOCKING_WORKER_PATH must name the blocking test worker
#endif

class SeparationWorkerProcessTest final : public QObject {
    Q_OBJECT

private slots:
    void helloMalformedAndShutdownUseNdjsonAndExitCleanly();
    void stdinEofDrainsAndExitsWithinTheClientDeadline();
    void oversizedLineIsRejectedAndTheWorkerStillShutsDown();
    void activePreCommitTimeoutCancelsThenKillsAndRecoversOwnedTemporaryDirectory();
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

void SeparationWorkerProcessTest::activePreCommitTimeoutCancelsThenKillsAndRecoversOwnedTemporaryDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString markerPath = temporary.filePath(QStringLiteral("staged.marker"));

    QProcess process;
    process.setProgram(QString::fromUtf8(AG_SEPARATION_BLOCKING_WORKER_PATH));
    process.start();
    QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
    process.write(encodeProtocolMessage(
        ProtocolType::Start, QStringLiteral("active-timeout"),
        {{QStringLiteral("outputDirectory"), temporary.path()},
         {QStringLiteral("markerPath"), markerPath}}));

    QByteArray observed;
    QElapsedTimer deadline;
    deadline.start();
    while (!QFileInfo::exists(markerPath) && deadline.elapsed() < 3000) {
        if (process.waitForReadyRead(100)) observed += process.readAllStandardOutput();
    }
    QVERIFY2(QFileInfo::exists(markerPath), observed.constData());
    QVERIFY(observed.contains("\"stage\":\"active_precommit\""));
    QCOMPARE(QDir(temporary.path()).entryList(
                 {QStringLiteral(".agplayer-separation-job-*")},
                 QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).size(),
             1);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("timeout"))));

    process.write(encodeProtocolMessage(ProtocolType::Cancel,
                                        QStringLiteral("active-timeout")));
    deadline.restart();
    while (!observed.contains("\"type\":\"cancel\"")
           && deadline.elapsed() < 1000) {
        if (process.waitForReadyRead(100)) observed += process.readAllStandardOutput();
    }
    QVERIFY(observed.contains("\"type\":\"cancel\""));
    QVERIFY(observed.contains("\"accepted\":true"));
    QVERIFY(!process.waitForFinished(250));
    process.kill();
    QVERIFY(process.waitForFinished(3000));
    observed += process.readAllStandardOutput();
    QCOMPARE(process.exitStatus(), QProcess::CrashExit);
    QVERIFY(!observed.contains("\"type\":\"result\""));

    OutputTransaction recovered(
        {temporary.path(), QStringLiteral("recovered"), QStringLiteral("wav"),
         {QStringLiteral("vocals"), QStringLiteral("instrumental")}});
    const TransactionResult begun = recovered.begin();
    QVERIFY2(begun.ok, qPrintable(begun.message));
    QVERIFY(recovered.cancel().ok);
    QCOMPARE(QDir(temporary.path()).entryList(
                 {QStringLiteral(".agplayer-separation-*")},
                 QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList{});
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("timeout"))));
}

QTEST_GUILESS_MAIN(SeparationWorkerProcessTest)
#include "separation_worker_process_test.moc"
