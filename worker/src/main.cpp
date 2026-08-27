#include "native_worker_backend.hpp"
#include "separation_protocol.hpp"
#include "worker_engine.hpp"

#include <QCoreApplication>
#include <QPointer>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace agplayer::separation;

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    auto backend = std::make_shared<NativeWorkerBackend>();
    WorkerEngine engine(backend);
    QObject::connect(&engine, &WorkerEngine::messageReady,
                     &application, [](const QByteArray& line) {
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
        std::fflush(stdout);
    });
    QObject::connect(&engine, &WorkerEngine::shutdownReady,
                     &application, &QCoreApplication::quit);

    const QPointer<WorkerEngine> guardedEngine(&engine);
    std::thread([guardedEngine] {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!guardedEngine) return;
            const QByteArray message(line.data(), static_cast<qsizetype>(line.size()));
            QMetaObject::invokeMethod(guardedEngine, [guardedEngine, message] {
                if (guardedEngine) guardedEngine->acceptLine(message);
            });
        }
        if (guardedEngine) {
            const QByteArray shutdown = encodeProtocolMessage(
                ProtocolType::Shutdown, QStringLiteral("stdin-eof")).trimmed();
            QMetaObject::invokeMethod(guardedEngine, [guardedEngine, shutdown] {
                if (guardedEngine) guardedEngine->acceptLine(shutdown);
            });
        }
    }).detach();

    return application.exec();
}
