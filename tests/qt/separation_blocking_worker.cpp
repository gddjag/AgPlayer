#include "output_transaction.hpp"
#include "separation_protocol.hpp"
#include "worker_engine.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QPointer>
#include <QThread>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

using namespace agplayer::separation;

namespace {

class BlockingRenameOps final : public NativeOutputFileOps {
public:
    explicit BlockingRenameOps(QString markerPath)
        : markerPath_(std::move(markerPath))
    {
    }

    bool renameFile(const QString& source, const QString& destination) override
    {
        if (!NativeOutputFileOps::renameFile(source, destination)) return false;
        QFile marker(markerPath_);
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write("first-rename-complete");
            marker.close();
        }
        for (;;) QThread::msleep(1000);
    }

private:
    QString markerPath_;
};

class BlockingBackend final : public WorkerBackend {
public:
    BackendResult probe(const QJsonObject&) override
    {
        return {true, {}, {}, {{QStringLiteral("test"), true}}};
    }

    BackendResult separate(const QJsonObject& payload,
                           const CancellationToken& cancelled,
                           const ProgressCallback& progress) override
    {
        auto operations = std::make_shared<BlockingRenameOps>(
            payload.value(QStringLiteral("markerPath")).toString());
        OutputTransaction transaction(
            {payload.value(QStringLiteral("outputDirectory")).toString(),
             QStringLiteral("timeout"), QStringLiteral("wav"),
             {QStringLiteral("vocals"), QStringLiteral("instrumental")}},
            operations);
        const TransactionResult begun = transaction.begin();
        if (!begun.ok) return {false, begun.code, begun.message, {}};
        for (const QString& stem : {QStringLiteral("vocals"),
                                    QStringLiteral("instrumental")}) {
            QFile file(transaction.temporaryPath(stem));
            if (!file.open(QIODevice::WriteOnly) || file.write("audio") != 5) {
                return {false, QStringLiteral("test_write_failed"),
                        QStringLiteral("Could not stage test output"), {}};
            }
        }
        progress(0.5, QStringLiteral("active_commit"));
        const TransactionResult committed = transaction.commit(
            [](const QString&) { return true; }, cancelled.atomicFlag());
        return committed.ok
            ? BackendResult{true, {}, {}, {}}
            : BackendResult{false, committed.code, committed.message, {}};
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    auto backend = std::make_shared<BlockingBackend>();
    WorkerEngine engine(backend);
    QObject::connect(&engine, &WorkerEngine::messageReady,
                     &application, [](const QByteArray& line) {
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
        std::fflush(stdout);
    });
    QObject::connect(&engine, &WorkerEngine::shutdownReady,
                     &application, &QCoreApplication::quit);

    const QPointer<WorkerEngine> guarded(&engine);
    std::thread([guarded] {
        QByteArray line;
        while (std::cin.good()) {
            std::string input;
            if (!std::getline(std::cin, input)) break;
            line = QByteArray::fromStdString(input);
            QMetaObject::invokeMethod(guarded, [guarded, line] {
                if (guarded) guarded->acceptLine(line);
            });
        }
    }).detach();
    return application.exec();
}
