#include "vocal_separation_history.hpp"
#include "vocal_separation_path_safety.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace {

constexpr qsizetype kMaximumHistoryRecords = 500;
constexpr qint64 kMaximumHistoryBytes = 1024 * 1024;

QVariantMap withAvailability(QVariantMap record)
{
    QVariantList stems = record.value(QStringLiteral("stems")).toList();
    for (QVariant& value : stems) {
        QVariantMap stem = value.toMap();
        stem.insert(QStringLiteral("available"),
                    vocal_separation_paths::safeExistingFile(
                        stem.value(QStringLiteral("path")).toString()));
        value = stem;
    }
    record.insert(QStringLiteral("stems"), stems);
    return record;
}

} // namespace

VocalSeparationHistoryStore::VocalSeparationHistoryStore(QString filePath)
    : filePath_(std::move(filePath))
{
}

QVariantList VocalSeparationHistoryStore::load() const
{
    QFile file(filePath_);
    if (!vocal_separation_paths::openRegularFileForRead(&file)
        || file.size() > kMaximumHistoryBytes) return {};
    const QByteArray bytes = file.read(kMaximumHistoryBytes + 1);
    if (bytes.size() > kMaximumHistoryBytes
        || file.size() > kMaximumHistoryBytes) return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) return {};
    QVariantList result;
    const QJsonArray records = document.array();
    const qsizetype first = std::max<qsizetype>(0, records.size() - kMaximumHistoryRecords);
    result.reserve(records.size() - first);
    for (qsizetype index = first; index < records.size(); ++index) {
        if (!records.at(index).isObject()) continue;
        result.push_back(withAvailability(records.at(index).toObject().toVariantMap()));
    }
    return result;
}

bool VocalSeparationHistoryStore::append(const QVariantMap& record)
{
    if (record.value(QStringLiteral("id")).toString().isEmpty()
        || !record.value(QStringLiteral("stems")).canConvert<QVariantList>()) {
        return false;
    }
    QVariantList records = load();
    records.push_back(record);
    if (records.size() > kMaximumHistoryRecords) {
        records.erase(records.begin(), records.begin()
                      + (records.size() - kMaximumHistoryRecords));
    }
    return save(records);
}

bool VocalSeparationHistoryStore::save(const QVariantList& records) const
{
    const QFileInfo target(filePath_);
    if (!QDir().mkpath(target.absolutePath())) return false;
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(
        QJsonArray::fromVariantList(records)).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
