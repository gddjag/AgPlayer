#include "rename_journal_store.hpp"

#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
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

QString hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString stagePath(const QString& source, const QString& transaction, int itemId)
{
    return QFileInfo(source).dir().filePath(
        QStringLiteral(".agplayer-rename-%1-%2.tmp").arg(transaction).arg(itemId));
}

QString backupPath(const QString& target, const QString& transaction, int itemId)
{
    return QFileInfo(target).dir().filePath(
        QStringLiteral(".agplayer-overwrite-%1-%2.bak").arg(transaction).arg(itemId));
}

struct RecoveryItem {
    int itemId = -1;
    QString source;
    QString target;
    QString sha256;
    qint64 size = 0;
    RenameAction action = RenameAction::NoOp;
    QString located;
    QString recoveryStage;
};

bool matches(const QString& path, const RecoveryItem& item)
{
    const QFileInfo info(path);
    return info.exists() && info.size() == item.size && hashFile(path) == item.sha256;
}

bool recoverJournal(const QString& path, const QJsonObject& document, QString* error)
{
    const QString transaction = document.value(QStringLiteral("transactionId")).toString();
    QList<RecoveryItem> items;
    for (const QJsonValue& value : document.value(QStringLiteral("items")).toArray()) {
        const QJsonObject object = value.toObject();
        RecoveryItem item;
        item.itemId = object.value(QStringLiteral("itemId")).toInt(-1);
        item.source = object.value(QStringLiteral("sourcePath")).toString();
        item.target = object.value(QStringLiteral("targetPath")).toString();
        item.sha256 = object.value(QStringLiteral("sha256")).toString();
        item.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble(-1));
        item.action = static_cast<RenameAction>(object.value(QStringLiteral("action")).toInt());
        if (item.action == RenameAction::Rename || item.action == RenameAction::Overwrite)
            items.append(std::move(item));
    }
    if (transaction.isEmpty() || items.isEmpty()) {
        if (error) *error = QStringLiteral("事务日志内容不完整");
        return false;
    }

    QSet<QString> claimed;
    for (RecoveryItem& item : items) {
        const QString staged = stagePath(item.source, transaction, item.itemId);
        const QStringList candidates{item.source, staged, item.target};
        for (const QString& candidate : candidates) {
            const QString key = QDir::cleanPath(candidate).toCaseFolded();
            if (!claimed.contains(key) && matches(candidate, item)) {
                item.located = candidate;
                claimed.insert(key);
                break;
            }
        }
        if (item.located.isEmpty()) {
            if (error) *error = QStringLiteral("找不到与日志校验值匹配的文件");
            return false;
        }
        if (item.action == RenameAction::Overwrite) {
            const QString backup = backupPath(item.target, transaction, item.itemId);
            if (!QFileInfo::exists(backup) && !QFileInfo::exists(item.target)) {
                if (error) *error = QStringLiteral("覆盖目标及其备份均已丢失");
                return false;
            }
            if (item.located == item.target && !QFileInfo::exists(backup)) {
                if (error) *error = QStringLiteral("覆盖备份不存在，无法无损恢复");
                return false;
            }
        }
    }

    QList<QPair<QString, QString>> moved;
    auto rollbackMoves = [&moved]() {
        for (int i = moved.size() - 1; i >= 0; --i) {
            if (QFileInfo::exists(moved[i].second) && !QFileInfo::exists(moved[i].first))
                QFile::rename(moved[i].second, moved[i].first);
        }
    };
    for (RecoveryItem& item : items) {
        if (QDir::cleanPath(item.located).compare(QDir::cleanPath(item.source),
                                                  Qt::CaseInsensitive) == 0)
            continue;
        item.recoveryStage = QFileInfo(item.source).dir().filePath(
            QStringLiteral(".agplayer-recovery-%1-%2.tmp")
                .arg(transaction).arg(item.itemId));
        if (QFileInfo::exists(item.recoveryStage)
            || !QFile::rename(item.located, item.recoveryStage)) {
            rollbackMoves();
            if (error) *error = QStringLiteral("无法暂存待恢复文件");
            return false;
        }
        moved.append({item.located, item.recoveryStage});
    }
    for (RecoveryItem& item : items) {
        if (item.recoveryStage.isEmpty()) continue;
        if (QFileInfo::exists(item.source)
            || !QFile::rename(item.recoveryStage, item.source)) {
            rollbackMoves();
            if (error) *error = QStringLiteral("无法恢复原文件名");
            return false;
        }
    }
    for (const RecoveryItem& item : items) {
        if (item.action != RenameAction::Overwrite) continue;
        const QString backup = backupPath(item.target, transaction, item.itemId);
        if (!QFileInfo::exists(backup)) continue;
        if (QFileInfo::exists(item.target) || !QFile::rename(backup, item.target)) {
            if (error) *error = QStringLiteral("无法恢复被覆盖文件");
            return false;
        }
    }
    return RenameJournalStore::markCompleted(path, false,
                                               QStringLiteral("RecoveredAtStartup"));
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

RenameRecoveryReport RenameJournalStore::recoverIncomplete()
{
    RenameRecoveryReport report;
    const QDir directory(journalDirectory());
    const QFileInfoList journals = directory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo& journal : journals) {
        const QJsonObject document = read(journal.absoluteFilePath());
        if (document.value(QStringLiteral("state")).toString()
            != QStringLiteral("Prepared")) continue;
        QString error;
        if (recoverJournal(journal.absoluteFilePath(), document, &error)) {
            ++report.recovered;
        } else {
            ++report.failed;
            report.failedJournals.append(journal.absoluteFilePath());
        }
    }
    return report;
}

} // namespace agplayer::qt
