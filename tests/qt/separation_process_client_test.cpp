#include "separation_process_client.hpp"

#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <cerrno>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
#endif

#ifndef AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH
#error AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH must name the test worker
#endif

class SeparationProcessClientTest final : public QObject {
    Q_OBJECT

private slots:
    void doesNotLaunchBeforeAnExplicitRequest();
    void helloTimeoutAndCrashAreRetryableErrors();
    void failedStartReportsOnceAndCanRetry();
    void ignoresStaleMessagesAndAcceptsCurrentResult();
    void cancellationIsIdempotentAndBounded();
    void cancellationBeforeHelloNeverDispatchesPendingRequest();
    void lateMessagesAfterCancellationAreIgnored();
    void newExplicitRequestAfterCrashUsesTheSuppliedPayload();
    void failureSignalCanRetrySynchronously();
    void heartbeatTimeoutCancelsThenFailsRetryably();
    void rejectsWrongDirectionAndProtocolVersion();
    void rejectsAnOversizedRemainingProtocolTailImmediately();
    void protocolFailureKeepsTheEventLoopResponsive();
    void processFailureTerminatesDescendants();
    void unixCancellationTerminatesDescendants();
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
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("unused")}}));
    QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty() || !failed.isEmpty(), shortDeadlines().helloMs);
    QVERIFY2(failed.isEmpty(), qPrintable(failed.isEmpty() ? QString() : failed.first().at(0).toString()));
    QCOMPARE(result.count(), 1);
    QCOMPARE(stale.count(), 1);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.first().at(0).toDouble(), 0.5);
    QCOMPARE(result.first().at(0).toJsonObject()
                 .value(QStringLiteral("provider")).toString(),
             QStringLiteral("cpu"));
    QTRY_VERIFY_WITH_TIMEOUT(client.canAcceptRequest(), 1500);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
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
    QTRY_VERIFY_WITH_TIMEOUT(progress.count() == 1 || failed.count() == 1, shortDeadlines().helloMs);
    QVERIFY2(progress.count() == 1,
             failed.isEmpty()
                 ? "Worker produced neither progress nor an error"
                 : qPrintable(failed.first().at(0).toString()));
    QElapsedTimer cancellationTime;
    cancellationTime.start();
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!client.isProcessRunning());
    QVERIFY2(cancellationTime.elapsed() <= 1500, "Cancellation exceeded its 1500 ms behavior deadline");
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
    QElapsedTimer cancellationTime;
    cancellationTime.start();
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
    QCOMPARE(result.count(), 0);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!QFileInfo::exists(marker));
    QVERIFY2(cancellationTime.elapsed() <= 1500, "Pre-hello cancellation exceeded 1500 ms");
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
    QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() || !failed.isEmpty(), shortDeadlines().helloMs);
    QVERIFY2(failed.isEmpty(), qPrintable(failed.isEmpty() ? QString() : failed.first().at(0).toString()));
    QCOMPARE(progress.count(), 1);
    QElapsedTimer cancellationTime;
    cancellationTime.start();
    client.cancel();
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1500);
    QCOMPARE(progress.count(), 1);
    QCOMPARE(result.count(), 0);
    QCOMPARE(probe.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
    QVERIFY2(cancellationTime.elapsed() <= 1500, "Cancellation exceeded its 1500 ms behavior deadline");
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
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, shortDeadlines().helloMs);
    QVERIFY(client.startJob({{QStringLiteral("inputPath"), QStringLiteral("request-B")}}));
    QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty() || failed.count() > 1, shortDeadlines().helloMs);
    QVERIFY2(failed.count() == 1, qPrintable(failed.last().at(0).toString()));
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.first().at(0).toJsonObject()
                 .value(QStringLiteral("echoInput")).toString(),
             QStringLiteral("request-B"));
    QTRY_VERIFY_WITH_TIMEOUT(client.canAcceptRequest(), 1500);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
}

void SeparationProcessClientTest::failedStartReportsOnceAndCanRetry()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    SeparationProcessClient client(
        temporary.filePath(QStringLiteral("worker-does-not-exist.exe")), {},
        shortDeadlines());
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QSignalSpy probe(&client, &SeparationProcessClient::probeReceived);
    bool retryAttempted = false;
    bool retryAccepted = false;
    connect(&client, &SeparationProcessClient::failed, this,
            [&](const QString&, bool) {
        if (retryAttempted) return;
        retryAttempted = true;
        retryAccepted = client.setWorker(
            QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
            {QStringLiteral("stale")})
            && client.startProbe({});
    });

    QVERIFY(client.startProbe({}));
    QTRY_VERIFY_WITH_TIMEOUT(!probe.isEmpty() || failed.count() > 1, shortDeadlines().helloMs);
    QVERIFY2(failed.count() == 1, qPrintable(failed.isEmpty() ? QStringLiteral("Missing startup failure") : failed.last().at(0).toString()));
    QCOMPARE(probe.count(), 1);
    QCOMPARE(failed.count(), 1);
    QVERIFY(retryAccepted);
    QTRY_VERIFY_WITH_TIMEOUT(client.canAcceptRequest(), 1500);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
}

void SeparationProcessClientTest::failureSignalCanRetrySynchronously()
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
    bool retried = false;
    bool retryAccepted = false;
    connect(&client, &SeparationProcessClient::failed, this,
            [&](const QString&, bool) {
        if (retried) return;
        retried = true;
        retryAccepted = client.startJob(
            {{QStringLiteral("inputPath"), QStringLiteral("request-B")}});
    });

    QVERIFY(client.startJob(
        {{QStringLiteral("inputPath"), QStringLiteral("request-A")}}));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, shortDeadlines().helloMs);
    QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty() || failed.count() > 1, shortDeadlines().helloMs);
    QVERIFY2(failed.count() == 1, qPrintable(failed.last().at(0).toString()));
    QCOMPARE(result.count(), 1);
    QCOMPARE(failed.count(), 1);
    QVERIFY(retryAccepted);
    QCOMPARE(result.first().at(0).toJsonObject()
                 .value(QStringLiteral("echoInput")).toString(),
             QStringLiteral("request-B"));
    QTRY_VERIFY_WITH_TIMEOUT(client.canAcceptRequest(), 1500);
    QCOMPARE(client.state(), SeparationProcessClient::Stopped);
}

void SeparationProcessClientTest::heartbeatTimeoutCancelsThenFailsRetryably()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("cancel")}, {5000, 500, 1000});
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QSignalSpy progress(&client, &SeparationProcessClient::progressReceived);
    QVERIFY(client.startJob({}));
    QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() || !failed.isEmpty(), shortDeadlines().helloMs);
    QVERIFY2(failed.isEmpty(), qPrintable(failed.isEmpty() ? QString() : failed.first().at(0).toString()));
    QCOMPARE(progress.count(), 1);
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
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, shortDeadlines().helloMs);
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
        {QStringLiteral("tail-after-line")}, {shortDeadlines().helloMs, 5'000, 150});
    QSignalSpy progress(&client, &SeparationProcessClient::progressReceived);
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QVERIFY(client.startJob({}));
    QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty() || !failed.isEmpty(), shortDeadlines().helloMs);
    QVERIFY2(!progress.isEmpty(), qPrintable(failed.isEmpty() ? QStringLiteral("No initial progress") : failed.first().at(0).toString()));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 500);
    QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("大小")));
}

void SeparationProcessClientTest::protocolFailureKeepsTheEventLoopResponsive()
{
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("wrong-direction")}, shortDeadlines());
    QSignalSpy failed(&client, &SeparationProcessClient::failed);
    QElapsedTimer interval;
    interval.start();
    qint64 longestGap = 0;
    QTimer pulse;
    pulse.setInterval(5);
    connect(&pulse, &QTimer::timeout, this, [&] {
        longestGap = qMax(longestGap, interval.restart());
    });
    pulse.start();
    QVERIFY(client.startJob({}));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!client.isProcessRunning(), 3000);
    QTest::qWait(20);
    QVERIFY2(longestGap < 500,
             qPrintable(QStringLiteral("Worker termination blocked the event loop for %1 ms")
                            .arg(longestGap)));
    QVERIFY(client.canAcceptRequest());
}

void SeparationProcessClientTest::processFailureTerminatesDescendants()
{
#ifndef Q_OS_WIN
    QSKIP("Windows Job Object behavior is Windows-specific");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString marker = temporary.filePath(QStringLiteral("descendant.marker"));
    const QString readyMarker = marker + QStringLiteral(".ready");
    SeparationProcessClient client(
        QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("spawn-child-and-fail"), marker}, shortDeadlines());
    QSignalSpy failed(&client, &SeparationProcessClient::failed);

    QVERIFY(client.startJob({}));
    QTRY_VERIFY_WITH_TIMEOUT((QFileInfo::exists(readyMarker)
                             && client.state() == SeparationProcessClient::Busy)
                                || !failed.isEmpty(), shortDeadlines().helloMs);
    QVERIFY2(failed.isEmpty(), qPrintable(failed.isEmpty() ? QString() : failed.first().at(0).toString()));
    QFile ready(readyMarker);
    QVERIFY(ready.open(QIODevice::ReadOnly));
    bool validPid = false;
    const DWORD childPid = ready.readAll().toULong(&validPid);
    QVERIFY(validPid && childPid != 0);
    HANDLE child = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, childPid);
    QVERIFY2(child, "Could not observe the real descendant process");
    const auto cleanupChild = qScopeGuard([child] {
        if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT)
            TerminateProcess(child, ERROR_PROCESS_ABORTED);
        CloseHandle(child);
    });
    QCOMPARE(WaitForSingleObject(child, 0), DWORD(WAIT_TIMEOUT));
    // Launch preparation ends here. The explicit trigger starts the failure
    // behavior clock; process startup cannot consume the termination budget.
    QElapsedTimer failureTime;
    failureTime.start();
    QFile trigger(marker + QStringLiteral(".fail"));
    QVERIFY(trigger.open(QIODevice::WriteOnly));
    trigger.close();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
    QVERIFY(!client.isProcessRunning());
    QTRY_COMPARE_WITH_TIMEOUT(WaitForSingleObject(child, 0), DWORD(WAIT_OBJECT_0), 2000);
    QVERIFY2(failureTime.elapsed() <= 2000,
             "The worker or its descendant exceeded the 2000 ms failure-cleanup deadline");
#endif
}

void SeparationProcessClientTest::unixCancellationTerminatesDescendants()
{
#ifndef Q_OS_UNIX
    QSKIP("Unix process-group cancellation is Unix-specific");
#else
    QTemporaryDir temporary;
    const QString marker = temporary.filePath(QStringLiteral("descendant.marker"));
    SeparationProcessClient client(QString::fromUtf8(AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH),
        {QStringLiteral("spawn-child-and-fail"), marker}, shortDeadlines());
    QSignalSpy cancelled(&client, &SeparationProcessClient::cancelled);
    QVERIFY(client.startJob({}));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker + QStringLiteral(".ready"))
        && client.state() == SeparationProcessClient::Busy, 5000);
    QFile ready(marker + QStringLiteral(".ready"));
    QVERIFY(ready.open(QIODevice::ReadOnly));
    bool ok = false;
    const pid_t child = static_cast<pid_t>(ready.readAll().toLongLong(&ok));
    QVERIFY(ok && child > 0);
    const auto cleanup = qScopeGuard([child] { ::kill(child, SIGKILL); });
    QCOMPARE(::kill(child, 0), 0);
    client.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(::kill(child, 0) == -1 && errno == ESRCH, 3000);
    QVERIFY(!client.isProcessRunning());
#endif
}

QTEST_GUILESS_MAIN(SeparationProcessClientTest)
#include "separation_process_client_test.moc"
