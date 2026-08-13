#include "voice_clone/voice_clone_capability_schema.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>

using agplayer::voice_clone::validateCapabilitySchema;

namespace {

[[noreturn]] void fail(const QString& message)
{
    std::cerr << message.toStdString() << '\n';
    std::exit(1);
}

QJsonObject request(const QString& operation,
                    const QString& requestId,
                    const QString& adapterId,
                    const QJsonObject& payload = {})
{
    return {{QStringLiteral("kind"), QStringLiteral("request")},
            {QStringLiteral("operation"), operation},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("adapterId"), adapterId},
            {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")},
            {QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("payload"), payload}};
}

QJsonObject transact(QLocalSocket* socket, const QJsonObject& message)
{
    const QString requestId = message.value(QStringLiteral("requestId")).toString();
    const QByteArray frame = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (socket->write(frame) != frame.size() || !socket->waitForBytesWritten(3000)) {
        fail(QStringLiteral("Could not write request %1").arg(requestId));
    }

    QByteArray buffer;
    while (true) {
        if (!socket->canReadLine() && !socket->waitForReadyRead(5000)) {
            fail(QStringLiteral("Timed out waiting for %1").arg(requestId));
        }
        buffer += socket->readAll();
        while (true) {
            const qsizetype newline = buffer.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                fail(QStringLiteral("Worker returned invalid JSON"));
            }
            const QJsonObject response = document.object();
            if (response.value(QStringLiteral("requestId")).toString() != requestId) continue;
            if (response.value(QStringLiteral("kind")).toString() == QStringLiteral("progress")) continue;
            return response;
        }
    }
}

QString argument(const QStringList& arguments, const QString& name)
{
    const qsizetype index = arguments.indexOf(name);
    if (index < 0 || index + 1 >= arguments.size()) fail(QStringLiteral("Missing %1").arg(name));
    return arguments.at(index + 1);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const QString python = argument(arguments, QStringLiteral("--python"));
    const QString worker = argument(arguments, QStringLiteral("--worker"));
    const QString adapterId = argument(arguments, QStringLiteral("--adapter-id"));
    const QString expectedParameter = argument(arguments, QStringLiteral("--expected-parameter"));

    QTemporaryDir modelRoot;
    QTemporaryDir outputRoot;
    if (!modelRoot.isValid() || !outputRoot.isValid()) fail(QStringLiteral("Temporary roots unavailable"));
    QFile reference(modelRoot.filePath(QStringLiteral("reference.wav")));
    if (!reference.open(QIODevice::WriteOnly)) fail(reference.errorString());
    reference.write("contract");
    reference.close();

    QLocalServer server;
    const QString socketName = QStringLiteral("agplayer-contract-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QLocalServer::removeServer(socketName);
    if (!server.listen(socketName)) fail(server.errorString());

    QProcess process;
    process.setProgram(python);
    process.setArguments({QStringLiteral("-I"),
                          QStringLiteral("-s"),
                          worker,
                          QStringLiteral("--contract-test"),
                          QStringLiteral("--voice-clone-worker"),
                          QStringLiteral("--socket"), socketName,
                          QStringLiteral("--model-root"), modelRoot.path(),
                          QStringLiteral("--output-root"), outputRoot.path(),
                          QStringLiteral("--adapter-id"), adapterId,
                          QStringLiteral("--adapter-version"), QStringLiteral("1.0.0"),
                          QStringLiteral("--protocol-version"), QStringLiteral("1")});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) fail(process.errorString());
    if (!server.waitForNewConnection(5000)) {
        fail(QStringLiteral("Worker did not connect: %1").arg(QString::fromUtf8(process.readAllStandardError())));
    }
    QLocalSocket* socket = server.nextPendingConnection();
    if (!socket) fail(QStringLiteral("Worker connection missing"));

    QJsonObject response = transact(socket, request(QStringLiteral("hello"), QStringLiteral("hello-1"), adapterId));
    if (response.value(QStringLiteral("kind")) != QStringLiteral("response")
        || response.value(QStringLiteral("adapterId")) != adapterId
        || response.value(QStringLiteral("payload")).toObject()
               .value(QStringLiteral("workerVersion")).toString().isEmpty()) {
        fail(QStringLiteral("Invalid hello response"));
    }

    response = transact(socket, request(QStringLiteral("capabilities"), QStringLiteral("caps-1"), adapterId));
    const QJsonObject schema = response.value(QStringLiteral("payload")).toObject()
                                   .value(QStringLiteral("schema")).toObject();
    const auto validation = validateCapabilitySchema(schema);
    if (!validation.isValid()) fail(QStringLiteral("Invalid schema: %1").arg(validation.errorString()));
    bool foundExpectedParameter = false;
    for (const QJsonValue& value : schema.value(QStringLiteral("parameters")).toArray()) {
        foundExpectedParameter |= value.toObject().value(QStringLiteral("key")) == expectedParameter;
    }
    if (!foundExpectedParameter) fail(QStringLiteral("Missing baseline parameter %1").arg(expectedParameter));

    response = transact(
        socket,
        request(QStringLiteral("load"), QStringLiteral("load-invalid-root"), adapterId,
                QJsonObject{{QStringLiteral("modelRoot"),
                             modelRoot.filePath(QStringLiteral("missing"))}}));
    if (response.value(QStringLiteral("kind")) != QStringLiteral("error")
        || response.value(QStringLiteral("error")).toObject()
               .value(QStringLiteral("code")) != QStringLiteral("INVALID_PATH")) {
        fail(QStringLiteral("Invalid model root did not produce a protocol error"));
    }

    response = transact(socket, request(QStringLiteral("load"), QStringLiteral("load-1"), adapterId));
    if (response.value(QStringLiteral("kind")) != QStringLiteral("response")
        || !response.value(QStringLiteral("payload")).toObject()
                .value(QStringLiteral("loaded")).toBool()) {
        fail(QStringLiteral("Contract load failed"));
    }

    const QJsonObject generatePayload{
        {QStringLiteral("text"), QStringLiteral("contract")},
        {QStringLiteral("referenceAudioPath"), reference.fileName()},
        {QStringLiteral("outputPath"), QStringLiteral("contract.wav")},
        {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("notAParameter"), true}}}};
    response = transact(socket,
                        request(QStringLiteral("generate"), QStringLiteral("generate-1"), adapterId,
                                generatePayload));
    if (response.value(QStringLiteral("kind")) != QStringLiteral("error")
        || response.value(QStringLiteral("error")).toObject()
               .value(QStringLiteral("code")) != QStringLiteral("INVALID_PARAMETERS")) {
        fail(QStringLiteral("Invalid parameter was not rejected"));
    }

    response = transact(socket,
                        request(QStringLiteral("cancel"), QStringLiteral("cancel-1"), adapterId,
                                QJsonObject{{QStringLiteral("targetRequestId"),
                                             QStringLiteral("generate-1")}}));
    if (response.value(QStringLiteral("kind")) != QStringLiteral("response")) {
        fail(QStringLiteral("Cancel contract failed"));
    }

    response = transact(socket, request(QStringLiteral("shutdown"), QStringLiteral("shutdown-1"), adapterId));
    if (response.value(QStringLiteral("kind")) != QStringLiteral("response")) {
        fail(QStringLiteral("Shutdown contract failed"));
    }
    socket->disconnectFromServer();
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        fail(QStringLiteral("Worker did not exit cleanly: %1").arg(QString::fromUtf8(process.readAllStandardError())));
    }

    std::cout << adapterId.toStdString() << " contract passed\n";
    return 0;
}
