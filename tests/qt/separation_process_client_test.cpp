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
    void retryAfterCrashReusesTheSameRequest();
    void heartbeatTimeoutCancelsThenFailsRetryably();
    void rejectsWrongDirectionAndProtocolVersion();
};

namespace {

SeparationProcessClient::Deadlines shortDeadlines()
{
    return {150, 500, 150};
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
            {scenario}, shortDeadlines());
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
    QTRY_COMPARE_WITH_TIMEOUT(progress.count(), 1, 1500);
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!client.isProcessRunning());
}

void SeparationProcessClientTest::retryAfterCrashReusesTheSameRequest()
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
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("unused")}}));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1500);
    QVERIFY(client.retryLast());
    QTRY_COMPARE_WITH_TIMEOUT(result.count(), 1, 1500);
}

void SeparationProcessClientTest::heartbeatTimeoutCancelsThenFailsRetryably()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("cancel")}, shortDeadlines());
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

QTEST_GUILESS_MAIN(SeparationProcessClientTest)
#include "separation_process_client_test.moc"
