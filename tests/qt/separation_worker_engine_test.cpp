#include "separation_protocol.hpp"
#include "worker_engine.hpp"

#include <QJsonDocument>
#include <QSemaphore>
#include <QSignalSpy>
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

    BackendResult probe(const QJsonObject&) override
    {
        if (blockProbe.load()) {
            probeEntered.release();
            probeRelease.acquire();
        }
        return {true, {}, {}, QJsonObject{{QStringLiteral("cpu"), true}}};
    }

    BackendResult separate(const QJsonObject& payload,
                           const std::atomic_bool& cancelled,
                           const ProgressCallback& progress) override
    {
        const int current = ++running;
        maximumRunning.store(std::max(maximumRunning.load(), current));
        entered.release();
        progress(0.5, QStringLiteral("inference"));
        release.acquire();
        --running;
        if (cancelled.load()) {
            return {false, QStringLiteral("cancelled"),
                    QStringLiteral("Separation cancelled"), {}};
        }
        return {true, {}, {}, QJsonObject{{QStringLiteral("token"),
                                           payload.value(QStringLiteral("token"))}}};
    }
};

} // namespace

class SeparationWorkerEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void helloAndProbeRouteTheirRequestIds();
    void startRunsOffCallerThreadAndAllowsOnlyOneActiveRequest();
    void probeQueueIsBounded();
    void cancellationIsIdempotentAndStaleResultsCannotLeak();
    void cancelledRestartLoopCannotGrowTheQueueWithoutBound();
    void shutdownCancelsWorkAndSignalsOnlyAfterTheQueueDrains();
    void rejectsWorkerOnlyMessageDirections();
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
    QCOMPARE(hello.payload.value(QStringLiteral("protocol")).toInt(), 1);

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

QTEST_GUILESS_MAIN(SeparationWorkerEngineTest)
#include "separation_worker_engine_test.moc"
