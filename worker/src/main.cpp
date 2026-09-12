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
        const auto submit = [guardedEngine](const QByteArray& message) {
            if (!guardedEngine) return;
            QMetaObject::invokeMethod(guardedEngine, [guardedEngine, message] {
                if (guardedEngine) guardedEngine->acceptLine(message);
            });
        };
        QByteArray line;
        line.reserve(4096);
        bool oversized = false;
        char character = 0;
        while (std::cin.get(character)) {
            if (character == '\n') {
                submit(oversized ? QByteArray(kMaximumProtocolLineBytes + 1, 'x')
                                 : line);
                line.clear();
                oversized = false;
            } else if (line.size() <= kMaximumProtocolLineBytes) {
                line.append(character);
            } else {
                oversized = true;
            }
        }
        if (!line.isEmpty() || oversized) {
            submit(oversized ? QByteArray(kMaximumProtocolLineBytes + 1, 'x')
                             : line);
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
