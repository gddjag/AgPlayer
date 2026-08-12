#include "rename_journal_store.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace agplayer::qt {
namespace {

QString journalDirectory()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString directory = QDir(root).filePath(QStringLiteral("rename-transactions"));
    QDir().mkpath(directory);
    return directory;
}

bool write(const QString& path, QJsonObject document)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(document).toJson(QJsonDocument::Compact)) < 0) return false;
    return file.commit();
}

QJsonObject read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

QString RenameJournalStore::writePrepared(const QString& transactionId, const RenamePlan& plan)
{
    QJsonArray items;
    for (const RenamePlanItem& item : plan.items) {
        items.append(QJsonObject{{QStringLiteral("itemId"), item.itemId},
                                 {QStringLiteral("sourcePath"), item.sourcePath},
                                 {QStringLiteral("targetPath"), item.targetPath},
                                 {QStringLiteral("sha256"), item.sha256},
                                 {QStringLiteral("size"), static_cast<double>(item.size)},
                                 {QStringLiteral("action"), static_cast<int>(item.action)}});
    }
    const QString path = QDir(journalDirectory()).filePath(transactionId + QStringLiteral(".json"));
    return write(path, QJsonObject{{QStringLiteral("transactionId"), transactionId},
                                   {QStringLiteral("state"), QStringLiteral("Prepared")},
                                   {QStringLiteral("items"), items}}) ? path : QString();
}

bool RenameJournalStore::markCompleted(const QString& journalPath, bool committed,
                                       const QString& outcome, const QString& error)
{
    QJsonObject document = read(journalPath);
    if (document.isEmpty()) return false;
    document.insert(QStringLiteral("state"), committed ? QStringLiteral("Committed")
                                                         : QStringLiteral("RolledBack"));
    document.insert(QStringLiteral("outcome"), outcome);
    document.insert(QStringLiteral("error"), error);
    return write(journalPath, document);
}

} // namespace agplayer::qt
