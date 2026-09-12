#include "lossless_report.hpp"

#include "agplayer_version.hpp"
#include "lossless/lossless_types.hpp"
#include "resource_path.hpp"

#include <QDateTime>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <exception>
#include <algorithm>

namespace {

QString csvCell(QString value)
{
    value.replace(QLatin1Char('"'), QString(2, QLatin1Char('"')));
    return QString(1, QLatin1Char('"')) + value + QLatin1Char('"');
}

QString compactJson(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QVariantList) {
        return QString::fromUtf8(QJsonDocument(
            QJsonArray::fromVariantList(value.toList())).toJson(
                QJsonDocument::Compact));
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        return QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(value.toMap())).toJson(
                QJsonDocument::Compact));
    }
    return value.toString();
}

QByteArray jsonReport(const QVariantList& results)
{
    QJsonObject root{
        {QStringLiteral("schemaVersion"), QStringLiteral("1")},
        // Applies also to historical cached results and source candidates.
        {QStringLiteral("confidenceKind"), QStringLiteral("ordinal_evidence_score")},
        {QStringLiteral("calibrationStatus"), QStringLiteral("uncalibrated")},
        {QStringLiteral("appVersion"),
         QString::fromLatin1(agplayer::version::kVersion)},
        {QStringLiteral("algorithmVersion"),
         results.isEmpty()
             ? QString::fromLatin1(
                   agplayer::lossless::kAlgorithmVersion.data(),
                   static_cast<qsizetype>(
                       agplayer::lossless::kAlgorithmVersion.size()))
             : results.constFirst().toMap()
                   .value(QStringLiteral("algorithmVersion")).toString()},
        {QStringLiteral("generatedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("disclaimer"),
         QString::fromUtf8(agplayer::lossless::kInferenceDisclaimer.data(),
                           static_cast<qsizetype>(
                               agplayer::lossless::kInferenceDisclaimer.size()))},
        {QStringLiteral("results"), QJsonArray::fromVariantList(results)},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray csvReport(const QVariantList& results)
{
    static const QStringList columns{
        QStringLiteral("schemaVersion"),
        QStringLiteral("algorithmVersion"),
        QStringLiteral("appVersion"),
        QStringLiteral("generatedAt"),
        QStringLiteral("parameterVersion"),
        QStringLiteral("analysisStartedAt"),
        QStringLiteral("taskId"),
        QStringLiteral("fileName"),
        QStringLiteral("path"),
        QStringLiteral("container"),
        QStringLiteral("codec"),
        QStringLiteral("sampleRate"),
        QStringLiteral("bitsPerSample"),
        QStringLiteral("channels"),
        QStringLiteral("durationMs"),
        QStringLiteral("fileSize"),
        QStringLiteral("modifiedUnixMs"),
        QStringLiteral("verdictCode"),
        QStringLiteral("verdictText"),
        QStringLiteral("confidence"),
        QStringLiteral("confidenceKind"),
        QStringLiteral("calibrationStatus"),
        QStringLiteral("calibratedProbability"),
        QStringLiteral("coverage"),
        QStringLiteral("spectralEdgeFrequencyHz"),
        QStringLiteral("spectralEdgeDepthDb"),
        QStringLiteral("spectralEdgeStability"),
        QStringLiteral("transientPreEchoMeasured"),
        QStringLiteral("transientPreEchoScore"),
        QStringLiteral("transientCount"),
        QStringLiteral("warnings"),
        QStringLiteral("candidates"),
        QStringLiteral("chain"),
        QStringLiteral("evidence"),
        QStringLiteral("error"),
        QStringLiteral("disclaimer"),
        QStringLiteral("sourceDetails"),
        QStringLiteral("measurements"),
    };
    QByteArray output = columns.join(QLatin1Char(',')).toUtf8();
    output.append('\n');
    const QString generatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QVariant& entry : results) {
        const QVariantMap result = entry.toMap();
        const QVariantMap file = result.value(QStringLiteral("file")).toMap();
        const QVariantMap source = result.value(QStringLiteral("source")).toMap();
        const QVariantMap coverage =
            result.value(QStringLiteral("coverage")).toMap();
        const QVariantMap measurements =
            result.value(QStringLiteral("measurements")).toMap();
        const QVariantList values{
            result.value(QStringLiteral("schemaVersion")),
            result.value(QStringLiteral("algorithmVersion")),
            QString::fromLatin1(agplayer::version::kVersion),
            generatedAt,
            result.value(QStringLiteral("parameterVersion")),
            result.value(QStringLiteral("analysisStartedAt")),
            result.value(QStringLiteral("taskId")),
            result.value(QStringLiteral("fileName")),
            file.value(QStringLiteral("path")),
            source.value(QStringLiteral("container")),
            source.value(QStringLiteral("codec")),
            source.value(QStringLiteral("sampleRate")),
            source.value(QStringLiteral("bitsPerSample")),
            source.value(QStringLiteral("channels")),
            source.value(QStringLiteral("durationMs")),
            file.value(QStringLiteral("fileSize")),
            file.value(QStringLiteral("modifiedUnixMs")),
            result.value(QStringLiteral("verdictCode")),
            result.value(QStringLiteral("verdictText")),
            result.value(QStringLiteral("confidence")),
            result.value(QStringLiteral("confidenceKind"), QStringLiteral("ordinal_evidence_score")),
            result.value(QStringLiteral("calibrationStatus"), QStringLiteral("uncalibrated")),
            result.value(QStringLiteral("calibratedProbability")),
            coverage.value(QStringLiteral("decodedRatio")),
            measurements.value(QStringLiteral("spectralEdgeFrequencyHz")),
            measurements.value(QStringLiteral("spectralEdgeDepthDb")),
            measurements.value(QStringLiteral("spectralEdgeStability")),
            measurements.value(QStringLiteral("transientPreEchoMeasured")),
            measurements.value(QStringLiteral("transientPreEchoScore")),
            measurements.value(QStringLiteral("transientCount")),
            result.value(QStringLiteral("warnings")),
            result.value(QStringLiteral("candidates")),
            result.value(QStringLiteral("chain")),
            result.value(QStringLiteral("evidence")),
            result.value(QStringLiteral("error")),
            QString::fromUtf8(
                agplayer::lossless::kInferenceDisclaimer.data(),
                static_cast<qsizetype>(
                    agplayer::lossless::kInferenceDisclaimer.size())),
            source,
            measurements,
        };
        QStringList cells;
        cells.reserve(values.size());
        for (const QVariant& value : values) cells.append(csvCell(compactJson(value)));
        output.append(cells.join(QLatin1Char(',')).toUtf8());
        output.append('\n');
    }
    return output;
}

} // namespace

LosslessReportWriteResult LosslessReport::write(
    const QString& requestedDestination,
    const QString& requestedFormat,
    const QVariantList& results,
    const QStringList& sourcePaths,
    const CancelCheck& cancelled)
{
    try {
        const auto isCancelled = [&cancelled] { return cancelled && cancelled(); };
        const auto cancelledResult = [] {
            return LosslessReportWriteResult{false, QCoreApplication::translate(
                "LosslessReport", "报告导出已取消")};
        };
        if (isCancelled()) return cancelledResult();
        const QString destination = QFileInfo(requestedDestination)
                                        .absoluteFilePath();
        if (requestedDestination.trimmed().isEmpty()) {
            return {false, QCoreApplication::translate(
                "LosslessReport", "报告路径为空")};
        }
        for (const QString& sourcePath : sourcePaths) {
            if (agplayer::qt::resourcePathsEqual(destination, sourcePath)) {
                return {false, QCoreApplication::translate(
                    "LosslessReport", "报告路径不能覆盖源音频文件")};
            }
        }
        if (QFileInfo(destination).isDir()) {
            return {false, QCoreApplication::translate(
                "LosslessReport", "报告路径指向文件夹")};
        }

        QString format = requestedFormat.trimmed().toCaseFolded();
        while (format.startsWith(QLatin1Char('.'))) format.remove(0, 1);
        if (format.isEmpty()) format = QFileInfo(destination).suffix().toCaseFolded();
        QByteArray bytes;
        if (format == QStringLiteral("json")) {
            bytes = jsonReport(results);
        } else if (format == QStringLiteral("csv")) {
            bytes = csvReport(results);
        } else {
            return {false, QCoreApplication::translate(
                "LosslessReport", "不支持的报告格式：%1").arg(format)};
        }

        if (isCancelled()) return cancelledResult();
        QSaveFile output(destination);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) {
            return {false, QCoreApplication::translate(
                "LosslessReport", "无法创建报告：%1")
                               .arg(output.errorString())};
        }
        // Bound the amount of write work between cooperative cancellation checks.
        // An individual operating-system I/O call can still block on the device.
        constexpr qsizetype chunkBytes = 64 * 1024;
        for (qsizetype offset = 0; offset < bytes.size();) {
            if (isCancelled()) {
                output.cancelWriting();
                return cancelledResult();
            }
            const qsizetype count = std::min(chunkBytes, bytes.size() - offset);
            if (output.write(bytes.constData() + offset, count) != count) {
                output.cancelWriting();
                return {false, QCoreApplication::translate(
                    "LosslessReport", "报告写入不完整：%1")
                                   .arg(output.errorString())};
            }
            offset += count;
        }
        if (isCancelled()) {
            output.cancelWriting();
            return cancelledResult();
        }
        if (!output.commit()) {
            return {false, QCoreApplication::translate(
                "LosslessReport", "无法原子保存报告：%1")
                               .arg(output.errorString())};
        }
        return {true, {}};
    } catch (const std::exception& exception) {
        return {false, QCoreApplication::translate(
            "LosslessReport", "报告写入失败：%1")
                           .arg(QString::fromUtf8(exception.what()))};
    } catch (...) {
        return {false, QCoreApplication::translate(
            "LosslessReport", "报告写入发生未知错误")};
    }
}
