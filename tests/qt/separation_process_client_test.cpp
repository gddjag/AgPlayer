#include "separation_process_client.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifndef AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH
#error AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH must name the test worker
#endif

class SeparationProcessClientTest final : public QObject {
    Q_OBJECT

private slots:
    void doesNotLaunchBeforeAnExplicitRequest();
    void helloTimeoutAndCrashAreRetryableErrors();
    void ignoresStaleMessagesAndAcceptsCurrentResult();
    void cancellationIsIdempotentAndBounded();
    void cancellationBeforeHelloNeverDispatchesPendingRequest();
    void lateMessagesAfterCancellationAreIgnored();
    void newExplicitRequestAfterCrashUsesTheSuppliedPayload();
    void heartbeatTimeoutCancelsThenFailsRetryably();
    void rejectsWrongDirectionAndProtocolVersion();
    void rejectsAnOversizedRemainingProtocolTailImmediately();
};

namespace {

SeparationProcessClient::Deadlines shortDeadlines()
{
    // Windows process creation can take seconds under antivirus or I/O load.
    // Keep the tests bounded without turning normal startup jitter into a
    // protocol failure.
    return {5000, 5000, 1000};
}

} // namespace

void SeparationProcessClientTest::doesNotLaunchBeforeAnExplicitRequest()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("stale")}, shortDeadlines());
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
    QVERIFY(!client.isProcessRunning());
    QTest::qWait(200);
    QVERIFY(!client.isProcessRunning());
}

void SeparationProcessClientTest::helloTimeoutAndCrashAreRetryableErrors()
{
    for (const QString& scenario : {QStringLiteral("hello-timeout"),
                                    QStringLiteral("crash")}) {
        SeparationProcessClient client(
            QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
            {scenario}, {300, 5000, 1000});
        QSignalSpy failed(&client, &SeparationProcessClient::failed);
        QVERIFY(client.startProbe({{QStringLiteral("runtimePath"), QStringLiteral("unused")}}));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1500);
        QCOMPARE(client.state(), SeparationProcessClient::Error);
        QVERIFY(failed.first().at(1).toBool());
    }
}

void SeparationProcessClientTest::ignoresStaleMessagesAndAcceptsCurrentResult()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("stale")}, shortDeadlines());
    QSignalSpy stale(&client, &SeparationProcessClient::staleMessageIgnored);
    QSignalSpy progress(&client, &SeparationProcessClient::progressReceived);
    QSignalSpy result(&client, &SeparationProcessClient::resultReceived);
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("unused")}}));
    QTRY_COMPARE_WITH_TIMEOUT(result.count(), 1, 1500);
    QCOMPARE(stale.count(), 1);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.first().at(0).toDouble(), 0.5);
    QCOMPARE(result.first().at(0).toJsonObject()
                 .value(QStringLiteral("provider")).toString(),
             QStringLiteral("cpu"));
}

void SeparationProcessClientTest::cancellationIsIdempotentAndBounded()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("cancel")}, shortDeadlines());
    QSignalSpy progress(&client, &SeparationProcessClient::progressReceived);
    QSignalSpy cancelled(&client, &SeparationProcessClient::cancelled);
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("unused")}}));
    QTRY_VERIFY_WITH_TIMEOUT(progress.count() == 1 || failed.count() == 1, 1500);
    QVERIFY2(progress.count() == 1,
             failed.isEmpty()
                 ? "Worker produced neither progress nor an error"
                 : qPrintable(failed.first().at(0).toString()));
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!client.isProcessRunning());
}

void SeparationProcessClientTest::
cancellationBeforeHelloNeverDispatchesPendingRequest()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString marker = temporary.filePath(QStringLiteral("start.marker"));
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("delayed-hello"), marker}, {500, 500, 150});
    QSignalSpy result(&client, &SeparationProcessClient::resultReceived);
    QSignalSpy cancelled(&client, &SeparationProcessClient::cancelled);
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({}));
    QTRY_VERIFY_WITH_TIMEOUT(client.isProcessRunning(), 500);
    QCOMPARE(client.state(), SeparationProcessClient::Starting);
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
    QCOMPARE(result.count(), 0);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!QFileInfo::exists(marker));
}

void SeparationProcessClientTest::lateMessagesAfterCancellationAreIgnored()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("late-after-cancel")}, shortDeadlines());
    QSignalSpy progress(&client, &SeparationProcessClient::progressReceived);
    QSignalSpy result(&client, &SeparationProcessClient::resultReceived);
    QSignalSpy probe(&client, &SeparationProcessClient::probeReceived);
    QSignalSpy cancelled(&client, &SeparationProcessClient::cancelled);
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({}));
    QTRY_COMPARE_WITH_TIMEOUT(progress.count(), 1, 1500);
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(result.count(), 0);
    QCOMPARE(probe.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
}

void SeparationProcessClientTest::newExplicitRequestAfterCrashUsesTheSuppliedPayload()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("retry"),
         temporary.filePath(QStringLiteral("attempt.marker"))},
        shortDeadlines());
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QSignalSpy result(&client, &SeparationProcessClient::resultReceived);
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("request-A")}}));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1500);
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("request-B")}}));
    QTRY_COMPARE_WITH_TIMEOUT(result.count(), 1, 1500);
    QCOMPARE(result.first().at(0).toJsonObject()
                 .value(QStringLiteral("echoInput")).toString(),
             QStringLiteral("request-B"));
}

void SeparationProcessClientTest::heartbeatTimeoutCancelsThenFailsRetryably()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("cancel")}, {5000, 500, 1000});
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({}));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1500);
    QVERIFY(failed.first().at(1).toBool());
    QCOMPARE(client.state(), SeparationProcessClient::Error);
    QVERIFY(!client.isProcessRunning());
}

void SeparationProcessClientTest::rejectsWrongDirectionAndProtocolVersion()
{
    for (const QString& scenario : {QStringLiteral("wrong-direction"),
                                    QStringLiteral("wrong-shutdown"),
                                    QStringLiteral("wrong-version")}) {
        SeparationProcessClient client(
            QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
            {scenario}, shortDeadlines());
        QSignalSpy failed(&client, &SeparationProcessClient::failed);
        QVERIFY(client.startJob({}));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1500);
        QCOMPARE(client.state(), SeparationProcessClient::Error);
        if (scenario == QStringLiteral("wrong-shutdown")) {
            QVERIFY(failed.first().at(0).toString().contains(
                QStringLiteral("方向")));
        }
    }
}

void SeparationProcessClientTest::
rejectsAnOversizedRemainingProtocolTailImmediately()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("tail-after-line")}, {150, 2'000, 150});
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({}));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 500);
    QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("大小")));
}

QTEST_GUILESS_MAIN(SeparationProcessClientTest)
#include "separation_process_client_test.moc"
