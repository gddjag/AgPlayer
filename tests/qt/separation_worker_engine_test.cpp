#include "separation_protocol.hpp"
#include "output_transaction.hpp"
#include "worker_engine.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <memory>

using namespace agplayer::separation;

namespace {

QByteArray message(ProtocolType type, const QString& requestId,
                   const QJsonObject& payload = {})
{
    return encodeProtocolMessage(type, requestId, payload).trimmed();
}

ProtocolMessage decode(const QVariant& value)
{
    const ProtocolParseResult parsed = parseProtocolMessage(value.toByteArray());
    Q_ASSERT(parsed.ok);
    return parsed.message;
}

class ControlledBackend final : public WorkerBackend {
public:
    QSemaphore entered;
    QSemaphore release;
    QSemaphore probeEntered;
    QSemaphore probeRelease;
    std::atomic_bool blockProbe{false};
    std::atomic_int running{0};
    std::atomic_int maximumRunning{0};

    BackendResult probe(const QJsonObject& payload) override
    {
        if (blockProbe.load()) {
            probeEntered.release();
            probeRelease.acquire();
        }
        return {true, {}, {}, QJsonObject{{QStringLiteral("cpu"), true},
                                        {QStringLiteral("token"), payload.value(QStringLiteral("token"))}}};
    }

    BackendResult separate(const QJsonObject& payload,
                           const CancellationToken& cancelled,
                           const ProgressCallback& progress) override
    {
        const int current = ++running;
        maximumRunning.store(std::max(maximumRunning.load(), current));
        entered.release();
        progress(0.5, QStringLiteral("inference"));
        if (payload.value(QStringLiteral("nonMonotonic")).toBool()) {
            progress(0.8, QStringLiteral("inference"));
            progress(0.2, QStringLiteral("inference"));
        }
        release.acquire();
        --running;
        if (cancelled.isCancelled()) {
            return {false, QStringLiteral("cancelled"),
                    QStringLiteral("Separation cancelled"), {}};
        }
        if (payload.value(QStringLiteral("diagnosticFailure")).toBool()) {
            return {false, QStringLiteral("rollback_failed"),
                    QStringLiteral("Cleanup left output artifacts"),
                    {{QStringLiteral("remainingPaths"),
                      QJsonArray{QStringLiteral("C:/locked-vocals.wav")}},
                     {QStringLiteral("causeCode"), QStringLiteral("cancelled")}}};
        }
        return {true, {}, {}, QJsonObject{{QStringLiteral("token"),
                                           payload.value(QStringLiteral("token"))}}};
    }
};

class CommitBlockingBackend final : public WorkerBackend {
public:
    QSemaphore committed;
    QSemaphore attempted;
    QSemaphore release;
    QString diagnostic;

    BackendResult probe(const QJsonObject&) override
    {
        return {true, {}, {}, {}};
    }

    BackendResult separate(const QJsonObject& payload,
                           const CancellationToken& cancellation,
                           const ProgressCallback&) override
    {
        const auto failed = [&](const QString& code, const QString& detail) {
            diagnostic = code + QStringLiteral(": ") + detail;
            attempted.release();
            return BackendResult{false, code, detail, {}};
        };
        OutputTransaction transaction(
            {payload.value(QStringLiteral("outputDirectory")).toString(),
             QStringLiteral("race"), QStringLiteral("wav"),
             {QStringLiteral("vocals")}});
        const TransactionResult begun = transaction.begin();
        if (!begun.ok) return failed(begun.code, begun.message);
        QFile staged(transaction.temporaryPath(QStringLiteral("vocals")));
        if (!staged.open(QIODevice::WriteOnly) || staged.write("audio") != 5) {
            return failed(QStringLiteral("test_write_failed"),
                          QStringLiteral("Could not stage output: ") + staged.errorString());
        }
        staged.close();
        const TransactionResult published = transaction.commit(
            [](const QString&) { return true; }, cancellation);
        if (!published.ok) {
            return failed(published.code, published.message);
        }
        committed.release();
        attempted.release();
        release.acquire();
        QJsonArray outputs;
        for (const QString& path : published.outputs) outputs.push_back(path);
        return {true, {}, {}, {{QStringLiteral("outputs"), outputs}}};
    }
};

} // namespace

class SeparationWorkerEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void helloAndProbeRouteTheirRequestIds();
    void startRunsOffCallerThreadAndAllowsOnlyOneActiveRequest();
    void probeQueueIsBounded();
    void cancelledProbeCannotCompleteAReusedRequestId();
    void cancellationIsIdempotentAndStaleResultsCannotLeak();
    void cancellationAcknowledgementHistoryIsBounded();
    void cancelledRestartLoopCannotGrowTheQueueWithoutBound();
    void shutdownCancelsWorkAndSignalsOnlyAfterTheQueueDrains();
    void rejectsWorkerOnlyMessageDirections();
    void backendErrorDiagnosticsReachNdjson();
    void requestIdReuseClearsStaleCancellationForEveryMessageType();
    void progressIsMonotonicAtTheProcessBoundary();
    void slowInferencePublishesLivenessWithoutInventingProgress();
    void cancelAfterAtomicCommitIsRejectedAndResultRemainsVisible();
};

void SeparationWorkerEngineTest::helloAndProbeRouteTheirRequestIds()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);

    engine.acceptLine(message(ProtocolType::Hello, QStringLiteral("hello-1")));
    QCOMPARE(output.size(), 1);
    ProtocolMessage hello = decode(output.takeFirst().at(0));
    QCOMPARE(hello.type, ProtocolType::Hello);
    QCOMPARE(hello.requestId, QStringLiteral("hello-1"));
    QCOMPARE(hello.payload.value(QStringLiteral("protocol")).toInt(), 2);

    engine.acceptLine(message(ProtocolType::Probe, QStringLiteral("probe-2")));
    QTRY_COMPARE_WITH_TIMEOUT(output.size(), 1, 2000);
    ProtocolMessage probe = decode(output.takeFirst().at(0));
    QCOMPARE(probe.type, ProtocolType::Probe);
    QCOMPARE(probe.requestId, QStringLiteral("probe-2"));
    QVERIFY(probe.payload.value(QStringLiteral("cpu")).toBool());
}

void SeparationWorkerEngineTest::startRunsOffCallerThreadAndAllowsOnlyOneActiveRequest()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);

    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("job-1"),
                              {{QStringLiteral("token"), 1}}));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("job-2"),
                              {{QStringLiteral("token"), 2}}));
    QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 2000);

    bool sawProgress = false;
    bool sawBusy = false;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        sawProgress |= emitted.type == ProtocolType::Progress
            && emitted.requestId == QStringLiteral("job-1");
        sawBusy |= emitted.type == ProtocolType::Error
            && emitted.requestId == QStringLiteral("job-2")
            && emitted.payload.value(QStringLiteral("code")).toString()
                == QStringLiteral("worker_busy");
    }
    QVERIFY(sawProgress);
    QVERIFY(sawBusy);
    QCOMPARE(backend->maximumRunning.load(), 1);
    backend->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(backend->running.load() == 0, 2000);
}

void SeparationWorkerEngineTest::probeQueueIsBounded()
{
    auto backend = std::make_shared<ControlledBackend>();
    backend->blockProbe.store(true);
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);

    engine.acceptLine(message(ProtocolType::Probe, QStringLiteral("probe-1")));
    QVERIFY(backend->probeEntered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Probe, QStringLiteral("probe-2")));
    engine.acceptLine(message(ProtocolType::Probe, QStringLiteral("probe-3")));

    backend->probeRelease.release(3);
    QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 3, 2000);
    bool queueFull = false;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        queueFull |= emitted.type == ProtocolType::Error
            && emitted.requestId == QStringLiteral("probe-3")
            && emitted.payload.value(QStringLiteral("code")).toString()
                == QStringLiteral("worker_queue_full");
    }
    QVERIFY(queueFull);
}

void SeparationWorkerEngineTest::cancelledProbeCannotCompleteAReusedRequestId()
{
    auto backend = std::make_shared<ControlledBackend>();
    backend->blockProbe.store(true);
    WorkerEngine engine(backend);
    const auto releaseProbes = qScopeGuard([&] { backend->probeRelease.release(2); });
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    const QString id = QStringLiteral("reused-probe");
    engine.acceptLine(message(ProtocolType::Probe, id, {{QStringLiteral("token"), 1}}));
    QVERIFY(backend->probeEntered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Cancel, id));
    engine.acceptLine(message(ProtocolType::Probe, id, {{QStringLiteral("token"), 2}}));
    output.clear();
    backend->probeRelease.release();
    // The single worker has queued the old completion before entering this
    // second probe. Drain that callback while the new probe is still blocked.
    QVERIFY(backend->probeEntered.tryAcquire(1, 2000));
    QCoreApplication::processEvents();
    for (const auto& arguments : output)
        QVERIFY(decode(arguments.at(0)).type != ProtocolType::Probe);
    backend->probeRelease.release();
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const auto& arguments : output) {
            const auto event = decode(arguments.at(0));
            if (event.type == ProtocolType::Probe && event.payload.value(QStringLiteral("token")).toInt() == 2)
                return true;
        }
        return false;
    }(), 2000);
}

void SeparationWorkerEngineTest::cancellationIsIdempotentAndStaleResultsCannotLeak()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);

    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("old-job"),
                              {{QStringLiteral("token"), 1}}));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Cancel, QStringLiteral("old-job")));
    engine.acceptLine(message(ProtocolType::Cancel, QStringLiteral("old-job")));
    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("new-job"),
                              {{QStringLiteral("token"), 2}}));

    backend->release.release();
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    backend->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(backend->running.load() == 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 4, 2000);

    int cancelAcks = 0;
    bool oldTerminalLeaked = false;
    bool newResult = false;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        if (emitted.type == ProtocolType::Cancel
            && emitted.requestId == QStringLiteral("old-job")) {
            ++cancelAcks;
        }
        if ((emitted.type == ProtocolType::Result || emitted.type == ProtocolType::Error)
            && emitted.requestId == QStringLiteral("old-job")) {
            oldTerminalLeaked = true;
        }
        if (emitted.type == ProtocolType::Result
            && emitted.requestId == QStringLiteral("new-job")
            && emitted.payload.value(QStringLiteral("token")).toInt() == 2) {
            newResult = true;
        }
    }
    QCOMPARE(cancelAcks, 2);
    QVERIFY(!oldTerminalLeaked);
    QVERIFY(newResult);
    QCOMPARE(backend->maximumRunning.load(), 1);
}

void SeparationWorkerEngineTest::cancellationAcknowledgementHistoryIsBounded()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);

    constexpr int kHistoryLimit = 16;
    for (int index = 0; index <= kHistoryLimit; ++index) {
        const QString requestId = QStringLiteral("bounded-%1").arg(index);
        engine.acceptLine(message(ProtocolType::Start, requestId));
        QVERIFY(backend->entered.tryAcquire(1, 2000));
        engine.acceptLine(message(ProtocolType::Cancel, requestId));
        backend->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(backend->running.load(), 0, 2000);
        QCoreApplication::processEvents();
    }

    output.clear();
    engine.acceptLine(message(ProtocolType::Cancel, QStringLiteral("bounded-0")));
    QCOMPARE(output.size(), 1);
    const ProtocolMessage expired = decode(output.takeFirst().at(0));
    QCOMPARE(expired.type, ProtocolType::Cancel);
    QVERIFY(!expired.payload.value(QStringLiteral("accepted")).toBool());
}

void SeparationWorkerEngineTest::shutdownCancelsWorkAndSignalsOnlyAfterTheQueueDrains()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    QSignalSpy shutdown(&engine, &WorkerEngine::shutdownReady);

    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("job")));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Shutdown, QStringLiteral("shutdown")));
    QCOMPARE(shutdown.size(), 0);
    backend->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(shutdown.size(), 1, 2000);

    bool shutdownReply = false;
    bool jobTerminalLeaked = false;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        shutdownReply |= emitted.type == ProtocolType::Shutdown
            && emitted.requestId == QStringLiteral("shutdown");
        jobTerminalLeaked |= (emitted.type == ProtocolType::Result
                              || emitted.type == ProtocolType::Error)
            && emitted.requestId == QStringLiteral("job");
    }
    QVERIFY(shutdownReply);
    QVERIFY(!jobTerminalLeaked);
}

void SeparationWorkerEngineTest::cancelledRestartLoopCannotGrowTheQueueWithoutBound()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);

    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("job-1")));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Cancel, QStringLiteral("job-1")));
    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("job-2")));
    engine.acceptLine(message(ProtocolType::Cancel, QStringLiteral("job-2")));
    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("job-3")));

    QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 2000);
    bool queueFull = false;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        queueFull |= emitted.type == ProtocolType::Error
            && emitted.requestId == QStringLiteral("job-3")
            && emitted.payload.value(QStringLiteral("code")).toString()
                == QStringLiteral("worker_queue_full");
    }
    backend->release.release();
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    backend->release.release();
    if (backend->entered.tryAcquire(1, 500)) {
        backend->release.release();
    }
    QTRY_COMPARE_WITH_TIMEOUT(backend->running.load(), 0, 2000);
    QCOMPARE(backend->maximumRunning.load(), 1);
    QVERIFY(queueFull);
}

void SeparationWorkerEngineTest::rejectsWorkerOnlyMessageDirections()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    for (const ProtocolType type : {ProtocolType::Progress, ProtocolType::Result,
                                    ProtocolType::Error}) {
        engine.acceptLine(message(type, QStringLiteral("bad-direction")));
        QCOMPARE(output.size(), 1);
        const ProtocolMessage emitted = decode(output.takeFirst().at(0));
        QCOMPARE(emitted.type, ProtocolType::Error);
        QCOMPARE(emitted.payload.value(QStringLiteral("code")).toString(),
                 QStringLiteral("unsupported_direction"));
    }
}

void SeparationWorkerEngineTest::backendErrorDiagnosticsReachNdjson()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    engine.acceptLine(message(
        ProtocolType::Start, QStringLiteral("diagnostics"),
        {{QStringLiteral("diagnosticFailure"), true}}));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    backend->release.release();

    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const QList<QVariant>& arguments : output) {
            if (decode(arguments.at(0)).type == ProtocolType::Error) return true;
        }
        return false;
    }(), 2000);
    ProtocolMessage terminal;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        if (emitted.type == ProtocolType::Error) terminal = emitted;
    }
    QCOMPARE(terminal.type, ProtocolType::Error);
    QCOMPARE(terminal.payload.value(QStringLiteral("code")).toString(),
             QStringLiteral("rollback_failed"));
    QCOMPARE(terminal.payload.value(QStringLiteral("causeCode")).toString(),
             QStringLiteral("cancelled"));
    QCOMPARE(terminal.payload.value(QStringLiteral("remainingPaths")).toArray(),
             QJsonArray{QStringLiteral("C:/locked-vocals.wav")});
}

void SeparationWorkerEngineTest::requestIdReuseClearsStaleCancellationForEveryMessageType()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    const QString requestId = QStringLiteral("reused");
    engine.acceptLine(message(ProtocolType::Start, requestId));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    engine.acceptLine(message(ProtocolType::Cancel, requestId));
    backend->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(backend->running.load(), 0, 2000);
    QCoreApplication::processEvents();

    output.clear();
    engine.acceptLine(message(ProtocolType::Hello, requestId));
    engine.acceptLine(message(ProtocolType::Cancel, requestId));
    QCOMPARE(output.size(), 2);
    const ProtocolMessage cancel = decode(output.at(1).at(0));
    QCOMPARE(cancel.type, ProtocolType::Cancel);
    QVERIFY(!cancel.payload.value(QStringLiteral("accepted")).toBool());
}

void SeparationWorkerEngineTest::progressIsMonotonicAtTheProcessBoundary()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    engine.acceptLine(message(
        ProtocolType::Start, QStringLiteral("progress"),
        {{QStringLiteral("nonMonotonic"), true}}));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    const auto releaseBackend = qScopeGuard([&] { backend->release.release(); });
    QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 3, 2000);

    double previous = 0.0;
    for (const QList<QVariant>& arguments : output) {
        const ProtocolMessage emitted = decode(arguments.at(0));
        if (emitted.type != ProtocolType::Progress) continue;
        const double fraction = emitted.payload.value(QStringLiteral("fraction")).toDouble();
        QVERIFY(fraction >= previous);
        previous = fraction;
    }
}

void SeparationWorkerEngineTest::slowInferencePublishesLivenessWithoutInventingProgress()
{
    auto backend = std::make_shared<ControlledBackend>();
    WorkerEngine engine(backend);
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    engine.acceptLine(message(ProtocolType::Start, QStringLiteral("slow-model")));
    QVERIFY(backend->entered.tryAcquire(1, 2000));
    const auto releaseBackend = qScopeGuard([&] { backend->release.release(); });
    QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 3500);
    for (const auto& arguments : output) {
        const ProtocolMessage event = decode(arguments.at(0));
        QCOMPARE(event.type, ProtocolType::Progress);
        QCOMPARE(event.payload.value(QStringLiteral("fraction")).toDouble(), 0.5);
        QCOMPARE(event.payload.value(QStringLiteral("stage")).toString(),
                 QStringLiteral("inference"));
    }
    engine.acceptLine(message(ProtocolType::Cancel, QStringLiteral("slow-model")));
    output.clear();
    QTest::qWait(2200);
    QVERIFY(output.isEmpty());
}

void SeparationWorkerEngineTest::cancelAfterAtomicCommitIsRejectedAndResultRemainsVisible()
{
    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());
    auto backend = std::make_shared<CommitBlockingBackend>();
    WorkerEngine engine(backend);
    const auto releaseBackend = qScopeGuard([&] { backend->release.release(); });
    QSignalSpy output(&engine, &WorkerEngine::messageReady);
    const QString requestId = QStringLiteral("commit-race");
    engine.acceptLine(message(
        ProtocolType::Start, requestId,
        {{QStringLiteral("outputDirectory"), outputDirectory.path()}}));
    // Filesystem setup exceeded 2 seconds under concurrent CTest I/O. Wait for
    // its completion without blocking heartbeat delivery or relaxing commit semantics.
    bool attemptReady = false;
    QTRY_VERIFY_WITH_TIMEOUT(attemptReady || (attemptReady = backend->attempted.tryAcquire(1, 0)), 10000);
    QVERIFY2(backend->committed.tryAcquire(), qPrintable(backend->diagnostic));
    QVERIFY(QFileInfo::exists(outputDirectory.filePath(
        QStringLiteral("race/race-vocals.wav"))));

    // Only liveness progress is legal while the committed backend is held.
    for (const auto& arguments : output)
        QCOMPARE(decode(arguments.at(0)).type, ProtocolType::Progress);
    output.clear();
    engine.acceptLine(message(ProtocolType::Cancel, requestId));
    QCOMPARE(output.size(), 1);
    const ProtocolMessage cancel = decode(output.front().at(0));
    QCOMPARE(cancel.type, ProtocolType::Cancel);
    QVERIFY(!cancel.payload.value(QStringLiteral("accepted")).toBool());

    backend->release.release();
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const QList<QVariant>& arguments : output) {
            if (decode(arguments.at(0)).type == ProtocolType::Result) return true;
        }
        return false;
    }(), 2000);
    QVERIFY(QFileInfo::exists(outputDirectory.filePath(
        QStringLiteral("race/race-vocals.wav"))));
}

QTEST_GUILESS_MAIN(SeparationWorkerEngineTest)
#include "separation_worker_engine_test.moc"
