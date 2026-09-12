#include "separation_protocol.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <cstdio>
#include <chrono>
#include <iostream>
#include <thread>

using namespace agplayer::separation;

namespace {

void send(ProtocolType type, const QString& requestId,
          const QJsonObject& payload = {})
{
    const QByteArray line = encodeProtocolMessage(type, requestId, payload);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString scenario = app.arguments().value(1);
    const QString markerPath = app.arguments().value(2);
    if (scenario == QStringLiteral("delayed-marker-child")) {
        QSaveFile ready(markerPath + QStringLiteral(".ready"));
        if (!ready.open(QIODevice::WriteOnly)) return 10;
        ready.write(QByteArray::number(QCoreApplication::applicationPid()));
        if (!ready.commit()) return 11;
        // Stay alive until the owning Job terminates us. The parent test
        // observes the process handle, not a scheduling-sensitive delayed file.
        return app.exec();
    }
    QProcess child;
    if (scenario == QStringLiteral("spawn-child-and-fail")) {
#ifdef Q_OS_UNIX
        // Normal subprocesses inherit the worker's process group, as Python
        // FFmpeg children do; startDetached intentionally creates a new one.
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("delayed-marker-child"), markerPath});
        if (!child.waitForStarted()) return 9;
#else
        if (!QProcess::startDetached(
                QCoreApplication::applicationFilePath(),
                {QStringLiteral("delayed-marker-child"), markerPath})) {
            return 9;
        }
#endif
    }
    std::thread([&app, scenario, markerPath] {
        std::string line;
        while (std::getline(std::cin, line)) {
            const QByteArray bytes = QByteArray::fromStdString(line);
            const ProtocolParseResult parsed = parseProtocolMessage(bytes);
            if (!parsed.ok) continue;
            const ProtocolMessage message = parsed.message;
            if (message.type == ProtocolType::Hello) {
                if (scenario == QStringLiteral("hello-timeout")) continue;
                if (scenario == QStringLiteral("delayed-hello")) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                send(ProtocolType::Hello, message.requestId,
                     {{QStringLiteral("protocol"), kSeparationProtocolVersion},
                      {QStringLiteral("worker"), QStringLiteral("test")}});
                if (scenario == QStringLiteral("crash")) {
                    std::exit(7);
                }
                if (scenario == QStringLiteral("retry")
                    && !QFileInfo::exists(markerPath)) {
                    QFile marker(markerPath);
                    if (marker.open(QIODevice::WriteOnly)) marker.write("attempted");
                    std::exit(8);
                }
            } else if (message.type == ProtocolType::Probe) {
                send(ProtocolType::Probe, message.requestId,
                     {{QStringLiteral("cpu"), true},
                      {QStringLiteral("gpu"), scenario == QStringLiteral("gpu-probe")},
                      {QStringLiteral("gpuReason"), QStringLiteral("No tested GPU")}});
            } else if (message.type == ProtocolType::Start) {
                if (scenario.startsWith(QStringLiteral("capture-payload"))
                    && !markerPath.isEmpty()) {
                    QFile marker(markerPath);
                    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        marker.write(QJsonDocument(message.payload).toJson(
                            QJsonDocument::Compact));
                    }
                }
                if (scenario == QStringLiteral("delayed-hello")
                    && !markerPath.isEmpty()) {
                    QFile marker(markerPath);
                    if (marker.open(QIODevice::WriteOnly)) marker.write("started");
                }
                if (scenario == QStringLiteral("wrong-direction")
                    || scenario == QStringLiteral("spawn-child-and-fail")) {
                    if (scenario == QStringLiteral("spawn-child-and-fail")) {
                        const QString trigger = markerPath + QStringLiteral(".fail");
                        QElapsedTimer preparation;
                        preparation.start();
                        while (preparation.elapsed() < 5000
                               && !QFileInfo::exists(trigger)) {
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(5));
                        }
                        if (!QFileInfo::exists(trigger)) return;
                    }
                    send(ProtocolType::Start, message.requestId);
                    continue;
                }
                if (scenario == QStringLiteral("wrong-shutdown")) {
                    send(ProtocolType::Shutdown, message.requestId);
                    continue;
                }
                if (scenario == QStringLiteral("wrong-version")) {
                    const QByteArray invalid = QByteArrayLiteral(
                        "{\"protocol\":")
                        + QByteArray::number(kSeparationProtocolVersion + 1)
                        + QByteArrayLiteral(",\"requestId\":\"")
                        + message.requestId.toUtf8()
                        + QByteArrayLiteral(
                            "\",\"type\":\"progress\",\"payload\":{}}\n");
                    std::fwrite(invalid.constData(), 1,
                                static_cast<size_t>(invalid.size()), stdout);
                    std::fflush(stdout);
                    continue;
                }
                if (scenario == QStringLiteral("stale")) {
                    send(ProtocolType::Error, QStringLiteral("old-request"),
                         {{QStringLiteral("code"), QStringLiteral("old")},
                          {QStringLiteral("message"), QStringLiteral("stale")}});
                }
                send(ProtocolType::Progress, message.requestId,
                      {{QStringLiteral("fraction"), 0.5},
                       {QStringLiteral("stage"), QStringLiteral("inference")}});
                if (scenario == QStringLiteral("tail-after-line")) {
                    const QByteArray tail(2 * 1024 * 1024, 'x');
                    std::fwrite(tail.constData(), 1,
                                static_cast<size_t>(tail.size()), stdout);
                    std::fflush(stdout);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
                if (scenario == QStringLiteral("delayed-result")
                    || scenario == QStringLiteral("long-delayed-result")) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(
                        scenario == QStringLiteral("long-delayed-result")
                            ? 1000 : 200));
                }
                if (scenario != QStringLiteral("cancel")
                    && scenario != QStringLiteral("late-after-cancel")) {
                    QJsonArray outputs;
                    const QString outputRoot = message.payload.value(
                        QStringLiteral("outputDirectory")).toString();
                    const QString inputPath = message.payload.value(
                        QStringLiteral("inputPath")).toString();
                    const QString extension = message.payload.value(
                        QStringLiteral("extension")).toString(QStringLiteral("wav"));
                    const QString resultRoot = QDir(outputRoot).filePath(
                        scenario == QStringLiteral("escaped-result")
                            ? QStringLiteral("../escaped-result")
                            : QStringLiteral("test-result"));
                    QDir().mkpath(resultRoot);
                    int outputIndex = 0;
                    for (const QJsonValue& value : message.payload.value(
                             QStringLiteral("stems")).toArray()) {
                        const QString output = QDir(resultRoot).filePath(
                            value.toString() + QLatin1Char('.') + extension);
                        if (scenario != QStringLiteral("missing-result")
                            && (scenario != QStringLiteral("partial-result")
                                || outputIndex == 0)) {
                            if (scenario == QStringLiteral("invalid-audio")) {
                                QFile invalid(output);
                                if (invalid.open(QIODevice::WriteOnly))
                                    invalid.write("not audio");
                            } else {
                                QFile::copy(inputPath, output);
                            }
                        }
                        outputs.push_back(output);
                        ++outputIndex;
                    }
                    send(ProtocolType::Result, message.requestId,
                         {{QStringLiteral("outputs"), outputs},
                          {QStringLiteral("provider"), QStringLiteral("cpu")},
                          {QStringLiteral("echoInput"), inputPath},
                          {QStringLiteral("fallbackReason"), QStringLiteral("No tested GPU")}});
                }
            } else if (message.type == ProtocolType::Cancel) {
                if (scenario == QStringLiteral("late-after-cancel")) {
                    send(ProtocolType::Progress, message.requestId,
                         {{QStringLiteral("fraction"), 0.9},
                          {QStringLiteral("stage"), QStringLiteral("late")}});
                    send(ProtocolType::Result, message.requestId,
                         {{QStringLiteral("outputs"), QJsonArray{}},
                          {QStringLiteral("provider"), QStringLiteral("cpu")}});
                    send(ProtocolType::Probe, message.requestId,
                         {{QStringLiteral("cpu"), true},
                          {QStringLiteral("gpu"), true}});
                }
                send(ProtocolType::Cancel, message.requestId,
                     {{QStringLiteral("accepted"), true}});
            } else if (message.type == ProtocolType::Shutdown) {
                if (scenario == QStringLiteral("capture-payload-delayed-shutdown")) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                send(ProtocolType::Shutdown, message.requestId,
                     {{QStringLiteral("accepted"), true}});
                QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
                return;
            }
        }
    }).detach();
    return app.exec();
}
