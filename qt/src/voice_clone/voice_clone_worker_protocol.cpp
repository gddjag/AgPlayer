#include "voice_clone_worker_protocol.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace agplayer::voice_clone {
namespace {

WorkerMessageKind messageKind(const QString& name)
{
    if (name == QStringLiteral("request")) return WorkerMessageKind::Request;
    if (name == QStringLiteral("response")) return WorkerMessageKind::Response;
    if (name == QStringLiteral("progress")) return WorkerMessageKind::Progress;
    if (name == QStringLiteral("error")) return WorkerMessageKind::Error;
    return WorkerMessageKind::Unknown;
}

QString messageKindName(const WorkerMessageKind kind)
{
    switch (kind) {
    case WorkerMessageKind::Request: return QStringLiteral("request");
    case WorkerMessageKind::Response: return QStringLiteral("response");
    case WorkerMessageKind::Progress: return QStringLiteral("progress");
    case WorkerMessageKind::Error: return QStringLiteral("error");
    case WorkerMessageKind::Unknown: return {};
    }
    return {};
}

WorkerOperation workerOperation(const QString& name)
{
    if (name == QStringLiteral("hello")) return WorkerOperation::Hello;
    if (name == QStringLiteral("capabilities")) return WorkerOperation::Capabilities;
    if (name == QStringLiteral("load")) return WorkerOperation::Load;
    if (name == QStringLiteral("generate")) return WorkerOperation::Generate;
    if (name == QStringLiteral("cancel")) return WorkerOperation::Cancel;
    if (name == QStringLiteral("unload")) return WorkerOperation::Unload;
    if (name == QStringLiteral("shutdown")) return WorkerOperation::Shutdown;
    return WorkerOperation::Unknown;
}

bool validIdentifier(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    return pattern.match(value).hasMatch();
}

bool safeRelativePath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized)
        || QFileInfo(normalized).isAbsolute() || normalized.contains(QLatin1Char(':'))) {
        return false;
    }
    const QString clean = QDir::cleanPath(normalized);
    return clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"))
           && clean != QStringLiteral(".");
}

bool pathIsWithin(const QString& root, const QString& candidate)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return candidate.startsWith(root + QLatin1Char('/'), sensitivity);
}

QString validateKindAndOperation(const VoiceCloneWorkerMessage& message)
{
    if (message.kind == WorkerMessageKind::Unknown) return QStringLiteral("unknown worker message kind");
    if (message.operation == WorkerOperation::Unknown) return QStringLiteral("unknown worker operation");
    if (message.kind == WorkerMessageKind::Progress
        && message.operation != WorkerOperation::Generate
        && message.operation != WorkerOperation::Load) {
        return QStringLiteral("progress is only valid for load or generate");
    }
    return {};
}

} // namespace

QString workerOperationName(const WorkerOperation operation)
{
    switch (operation) {
    case WorkerOperation::Hello: return QStringLiteral("hello");
    case WorkerOperation::Capabilities: return QStringLiteral("capabilities");
    case WorkerOperation::Load: return QStringLiteral("load");
    case WorkerOperation::Generate: return QStringLiteral("generate");
    case WorkerOperation::Cancel: return QStringLiteral("cancel");
    case WorkerOperation::Unload: return QStringLiteral("unload");
    case WorkerOperation::Shutdown: return QStringLiteral("shutdown");
    case WorkerOperation::Unknown: return {};
    }
    return {};
}

QByteArray encodeWorkerMessage(const VoiceCloneWorkerMessage& message)
{
    QJsonObject object{{QStringLiteral("kind"), messageKindName(message.kind)},
                       {QStringLiteral("operation"), workerOperationName(message.operation)},
                       {QStringLiteral("requestId"), message.requestId},
                       {QStringLiteral("adapterId"), message.adapterId},
                       {QStringLiteral("adapterVersion"), message.adapterVersion},
                       {QStringLiteral("protocolVersion"), message.protocolVersion}};
    if (message.kind == WorkerMessageKind::Progress) {
        object.insert(QStringLiteral("stage"), message.stage);
        object.insert(QStringLiteral("progress"),
                      message.progress.has_value() ? QJsonValue(*message.progress)
                                                   : QJsonValue(QJsonValue::Null));
    } else if (message.kind == WorkerMessageKind::Error) {
        object.insert(QStringLiteral("error"),
                      QJsonObject{{QStringLiteral("code"), message.workerError.code},
                                  {QStringLiteral("message"), message.workerError.message},
                                  {QStringLiteral("retryable"), message.workerError.retryable},
                                  {QStringLiteral("details"), message.workerError.details}});
    } else {
        object.insert(QStringLiteral("payload"), message.payload);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

WorkerMessageDecodeResult decodeWorkerMessage(const QByteArray& json, const QString& outputRoot)
{
    WorkerMessageDecodeResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("invalid worker JSON: %1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject object = document.object();
    VoiceCloneWorkerMessage& message = result.message;
    message.kind = messageKind(object.value(QStringLiteral("kind")).toString());
    message.operation = workerOperation(object.value(QStringLiteral("operation")).toString());
    message.requestId = object.value(QStringLiteral("requestId")).toString();
    message.adapterId = object.value(QStringLiteral("adapterId")).toString();
    message.adapterVersion = object.value(QStringLiteral("adapterVersion")).toString();
    message.protocolVersion = object.value(QStringLiteral("protocolVersion")).toInt(-1);

    result.error = validateKindAndOperation(message);
    if (!result.error.isEmpty()) return result;
    if (!validIdentifier(message.requestId) || !validIdentifier(message.adapterId)
        || !validIdentifier(message.adapterVersion) || message.protocolVersion != 1) {
        result.error = QStringLiteral("requestId and adapter identity triple are invalid");
        return result;
    }

    if (message.kind == WorkerMessageKind::Progress) {
        static const QSet<QString> stages{QStringLiteral("preparing"),
                                          QStringLiteral("loading"),
                                          QStringLiteral("synthesizing"),
                                          QStringLiteral("encoding"),
                                          QStringLiteral("finalizing")};
        message.stage = object.value(QStringLiteral("stage")).toString();
        const QJsonValue progress = object.value(QStringLiteral("progress"));
        if (!stages.contains(message.stage) || (!progress.isNull() && !progress.isDouble())) {
            result.error = QStringLiteral("progress requires a supported stage and number or null");
            return result;
        }
        if (progress.isDouble()) {
            const double value = progress.toDouble();
            if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
                result.error = QStringLiteral("progress must be between zero and one");
                return result;
            }
            message.progress = value;
        }
        return result;
    }

    if (message.kind == WorkerMessageKind::Error) {
        if (!object.value(QStringLiteral("error")).isObject()) {
            result.error = QStringLiteral("error message requires structured error data");
            return result;
        }
        const QJsonObject error = object.value(QStringLiteral("error")).toObject();
        message.workerError.code = error.value(QStringLiteral("code")).toString();
        message.workerError.message = error.value(QStringLiteral("message")).toString();
        message.workerError.retryable = error.value(QStringLiteral("retryable")).toBool();
        message.workerError.details = error.value(QStringLiteral("details")).toObject();
        if (!validIdentifier(message.workerError.code)
            || message.workerError.message.trimmed().isEmpty()
            || !error.value(QStringLiteral("retryable")).isBool()
            || (error.contains(QStringLiteral("details"))
                && !error.value(QStringLiteral("details")).isObject())) {
            result.error = QStringLiteral("structured error is invalid");
        }
        return result;
    }

    if (!object.value(QStringLiteral("payload")).isObject()) {
        result.error = QStringLiteral("request and response messages require an object payload");
        return result;
    }
    message.payload = object.value(QStringLiteral("payload")).toObject();
    if (message.operation == WorkerOperation::Capabilities
        && message.kind == WorkerMessageKind::Response
        && !message.payload.value(QStringLiteral("schema")).isObject()) {
        result.error = QStringLiteral("capabilities response requires a schema object");
        return result;
    }
    if (message.operation == WorkerOperation::Cancel
        && message.kind == WorkerMessageKind::Request
        && !validIdentifier(message.payload.value(QStringLiteral("targetRequestId")).toString())) {
        result.error = QStringLiteral("cancel requires a targetRequestId");
        return result;
    }
    if (message.operation == WorkerOperation::Generate
        && message.kind == WorkerMessageKind::Request) {
        if (outputRoot.trimmed().isEmpty() || !QDir::isAbsolutePath(outputRoot)) {
            result.error = QStringLiteral("generate requires an explicit absolute outputRoot");
            return result;
        }
        const QString outputPath = message.payload.value(QStringLiteral("outputPath")).toString();
        if (!safeRelativePath(outputPath)) {
            result.error = QStringLiteral("generation outputPath must be relative to outputRoot");
            return result;
        }
        const QString root = QDir::fromNativeSeparators(QDir::cleanPath(outputRoot));
        message.resolvedOutputPath = QDir::fromNativeSeparators(
            QDir::cleanPath(QDir(root).absoluteFilePath(outputPath)));
        if (!pathIsWithin(root, message.resolvedOutputPath)) {
            result.error = QStringLiteral("generation outputPath escapes outputRoot");
            message.resolvedOutputPath.clear();
        }
    }
    return result;
}

} // namespace agplayer::voice_clone
