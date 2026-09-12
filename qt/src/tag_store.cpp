#include "tag_store.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace {
QString normalizedKey(const QString& value)
{
    return value.trimmed().toCaseFolded();
}
}

TagStore::TagStore(QString filePath, WriteFunction writer)
    : filePath_(std::move(filePath))
    , writer_(std::move(writer))
{
    if (!writer_) {
        writer_ = [](QIODevice& device, const QByteArray& payload) {
            return device.write(payload);
        };
    }
}

QList<TagEntry> TagStore::load() const
{
    if (filePath_.isEmpty()) return {};
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return {};
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) return {};

    QList<TagEntry> entries;
    QSet<QString> seenKeys;
    for (const QJsonValue& value : root.value(QStringLiteral("tags")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString key = normalizedKey(object.value(QStringLiteral("key")).toString());
        const QString displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
        const QColor color(object.value(QStringLiteral("color")).toString());
        if (key.isEmpty() || displayName.isEmpty() || seenKeys.contains(key)) continue;
        entries.append({key, displayName, 0, color});
        seenKeys.insert(key);
    }
    return entries;
}

bool TagStore::save(const QList<TagEntry>& entries) const
{
    if (filePath_.isEmpty()) return false;
    QJsonArray tags;
    QSet<QString> seenKeys;
    for (const TagEntry& entry : entries) {
        const QString key = normalizedKey(entry.key);
        const QString displayName = entry.displayName.trimmed();
        if (key.isEmpty() || displayName.isEmpty() || seenKeys.contains(key)) continue;
        QJsonObject object;
        object.insert(QStringLiteral("key"), key);
        object.insert(QStringLiteral("displayName"), displayName);
        object.insert(QStringLiteral("color"), entry.color.isValid()
            ? entry.color.name(QColor::HexRgb) : QString());
        tags.append(object);
        seenKeys.insert(key);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("tags"), tags);
    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (writer_(file, payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
