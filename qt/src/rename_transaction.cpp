#include "rename_transaction.hpp"
#include "rename_journal_store.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace agplayer::qt {
namespace {

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

QString stagePath(const QString& source, const QString& transaction, int item)
{
    const QFileInfo info(source);
    return info.dir().filePath(QStringLiteral(".agplayer-rename-%1-%2.tmp")
        .arg(transaction).arg(item));
}

QString backupPath(const QString& target, const QString& transaction, int item)
{
    const QFileInfo info(target);
    return info.dir().filePath(QStringLiteral(".agplayer-overwrite-%1-%2.bak")
        .arg(transaction).arg(item));
}

struct Step {
    RenamePlanItem item;
    QString stage;
    RenameOverwriteBackup backup;
};

bool rollback(QList<Step>& steps, int backedUp, int staged, int committed)
{
    Q_UNUSED(backedUp)
    bool ok = true;
    for (int i = committed - 1; i >= 0; --i) {
        if (QFileInfo::exists(steps[i].item.targetPath)
            && !QFile::rename(steps[i].item.targetPath, steps[i].stage)) ok = false;
    }
    for (int i = staged - 1; i >= 0; --i) {
        if (QFileInfo::exists(steps[i].stage)
            && !QFile::rename(steps[i].stage, steps[i].item.sourcePath)) ok = false;
    }
    for (int i = steps.size() - 1; i >= 0; --i) {
        const RenameOverwriteBackup& backup = steps[i].backup;
        if (!backup.backupPath.isEmpty() && QFileInfo::exists(backup.backupPath)
            && !QFile::rename(backup.backupPath, backup.targetPath)) ok = false;
    }
    return ok;
}

RenameTransactionResult failure(QString code, QString text, QList<Step>& steps,
                                int backedUp, int staged, int committed,
                                const QString& journalPath)
{
    RenameTransactionResult result;
    const bool rolledBack = rollback(steps, backedUp, staged, committed);
    result.errorCode = rolledBack ? std::move(code) : QStringLiteral("rollback-failed");
    result.errorText = rolledBack ? std::move(text) : QStringLiteral("重命名失败，自动回滚未完整完成");
    result.journalPath = journalPath;
    RenameJournalStore::markCompleted(journalPath, false,
                                      rolledBack ? QStringLiteral("RolledBack")
                                                 : QStringLiteral("RollbackFailed"),
                                      result.errorText);
    return result;
}

} // namespace

void RenameTransaction::notifyTestHook(QStringView point, const QString& path) const
{
    if (testHook_) {
        testHook_(point, path);
    }
}

RenameTransactionResult RenameTransaction::execute(const RenamePlan& plan,
                                                   const std::atomic_bool* cancel) const
{
    QList<Step> steps;
    const QString transaction = QUuid::createUuid().toString(QUuid::Id128);
    const QString journalPath = RenameJournalStore::writePrepared(transaction, plan);
    if (journalPath.isEmpty()) {
        return {false, false, QStringLiteral("journal-write-failed"),
                QStringLiteral("无法创建重命名事务日志"), {}, {}};
    }
    for (const RenamePlanItem& item : plan.items) {
        if (item.action != RenameAction::Rename
            && item.action != RenameAction::Overwrite) continue;
        if (!QFileInfo::exists(item.sourcePath) || QFileInfo(item.sourcePath).size() != item.size
            || hashFile(item.sourcePath) != item.sha256) {
            RenameJournalStore::markCompleted(journalPath, false, QStringLiteral("SourceChanged"));
            return {false, false, QStringLiteral("source-changed"), QStringLiteral("文件在重命名前已变更"), journalPath, {}};
        }
        Step step{item, stagePath(item.sourcePath, transaction, item.itemId), {}};
        if (item.action == RenameAction::Overwrite) {
            if (!QFileInfo::exists(item.targetPath)) {
                RenameJournalStore::markCompleted(journalPath, false,
                                                  QStringLiteral("OverwriteTargetMissing"));
                return {false, false, QStringLiteral("overwrite-target-missing"),
                        QStringLiteral("要覆盖的目标文件已不存在"), journalPath, {}};
            }
            step.backup = {item.itemId, item.targetPath,
                           backupPath(item.targetPath, transaction, item.itemId),
                           hashFile(item.targetPath), QFileInfo(item.targetPath).size()};
        }
        steps.append(std::move(step));
    }
    int backedUp = 0;
    int staged = 0;
    int committed = 0;
    for (Step& step : steps) {
        if (step.item.action != RenameAction::Overwrite) continue;
        if (step.backup.sha256.isEmpty() || QFileInfo::exists(step.backup.backupPath)
            || !QFile::rename(step.backup.targetPath, step.backup.backupPath)) {
            return failure(QStringLiteral("backup-failed"),
                           QStringLiteral("无法备份将被覆盖的目标文件"),
                           steps, backedUp, staged, committed, journalPath);
        }
        ++backedUp;
        notifyTestHook(u"after-overwrite-backup", step.backup.backupPath);
        const QFileInfo backupInfo(step.backup.backupPath);
        if (!backupInfo.exists() || backupInfo.size() != step.backup.size
            || hashFile(step.backup.backupPath) != step.backup.sha256) {
            return failure(QStringLiteral("overwrite-target-changed"),
                           QStringLiteral("要覆盖的目标文件在备份时已变更"),
                           steps, backedUp, staged, committed, journalPath);
        }
    }
    for (Step& step : steps) {
        if (cancel != nullptr && cancel->load()) {
            RenameTransactionResult result = failure(QStringLiteral("cancelled"), QStringLiteral("重命名已取消并回滚"), steps, backedUp, staged, committed, journalPath);
            result.cancelled = true;
            return result;
        }
        if (QFileInfo::exists(step.stage) || !QFile::rename(step.item.sourcePath, step.stage)) {
            return failure(QStringLiteral("stage-failed"), QStringLiteral("无法创建同目录暂存文件"), steps, backedUp, staged, committed, journalPath);
        }
        ++staged;
        notifyTestHook(u"after-source-stage", step.stage);
        const QFileInfo stagedInfo(step.stage);
        if (!stagedInfo.exists() || stagedInfo.size() != step.item.size
            || hashFile(step.stage) != step.item.sha256) {
            return failure(QStringLiteral("source-changed"),
                           QStringLiteral("文件在暂存时已变更"),
                           steps, backedUp, staged, committed, journalPath);
        }
    }
    for (Step& step : steps) {
        if (cancel != nullptr && cancel->load()) {
            RenameTransactionResult result = failure(QStringLiteral("cancelled"), QStringLiteral("重命名已取消并回滚"), steps, backedUp, staged, committed, journalPath);
            result.cancelled = true;
            return result;
        }
        if (!QFile::rename(step.stage, step.item.targetPath)) {
            return failure(QStringLiteral("commit-failed"), QStringLiteral("重命名提交或完整性校验失败"), steps, backedUp, staged, committed, journalPath);
        }
        ++committed;
        notifyTestHook(u"after-target-move", step.item.targetPath);
        if (hashFile(step.item.targetPath) != step.item.sha256) {
            return failure(QStringLiteral("commit-failed"), QStringLiteral("重命名提交或完整性校验失败"), steps, backedUp, staged, committed, journalPath);
        }
    }
    notifyTestHook(u"before-journal-finalize", journalPath);
    if (!RenameJournalStore::markCompleted(journalPath, true,
                                           QStringLiteral("Committed"))) {
        return failure(QStringLiteral("journal-finalize-failed"),
                       QStringLiteral("无法确认重命名事务日志，已回滚文件操作"),
                       steps, backedUp, staged, committed, journalPath);
    }
    RenameUndoRecord undoRecord{plan, {}};
    for (const Step& step : steps) {
        if (!step.backup.backupPath.isEmpty()) {
            undoRecord.overwriteBackups.append(step.backup);
        }
    }
    return {true, false, {}, {}, journalPath, std::move(undoRecord)};
}

RenameTransactionResult RenameTransaction::undo(const RenameUndoRecord& record) const
{
    for (const RenamePlanItem& item : record.plan.items) {
        if (item.action != RenameAction::Rename
            && item.action != RenameAction::Overwrite) continue;
        if (!QFileInfo::exists(item.targetPath) || QFileInfo(item.targetPath).size() != item.size
            || hashFile(item.targetPath) != item.sha256) {
            return {false, false, QStringLiteral("undo-target-changed"), QStringLiteral("无法安全撤销：目标文件已被外部替换或修改"), {}};
        }
    }
    for (const RenameOverwriteBackup& backup : record.overwriteBackups) {
        if (!QFileInfo::exists(backup.backupPath)
            || QFileInfo(backup.backupPath).size() != backup.size
            || hashFile(backup.backupPath) != backup.sha256) {
            return {false, false, QStringLiteral("undo-backup-changed"),
                    QStringLiteral("无法安全撤销：覆盖备份已丢失或被修改"), {}};
        }
    }

    QList<RenameSource> sources;
    QList<QString> targets;
    for (const RenamePlanItem& item : record.plan.items) {
        if (item.action != RenameAction::Rename
            && item.action != RenameAction::Overwrite) continue;
        sources.append({item.itemId, item.targetPath, item.sha256, {}, item.size});
        targets.append(item.sourcePath);
    }
    RenameTransactionResult restored = execute(makePlanForTests(sources, targets));
    if (!restored.committed) return restored;
    for (const RenameOverwriteBackup& backup : record.overwriteBackups) {
        if (QFileInfo::exists(backup.targetPath)
            || !QFile::rename(backup.backupPath, backup.targetPath)) {
            return {false, false, QStringLiteral("undo-backup-restore-failed"),
                    QStringLiteral("原文件名已恢复，但被覆盖文件的备份恢复失败"),
                    restored.journalPath, {}};
        }
    }
    restored.undoRecord = {};
    return restored;
}

bool RenameTransaction::discardUndo(const RenameUndoRecord& record)
{
    bool complete = true;
    for (const RenameOverwriteBackup& backup : record.overwriteBackups) {
        const QFileInfo backupInfo(backup.backupPath);
        if (!backupInfo.exists()) {
            continue;
        }
        if (backupInfo.size() != backup.size
            || hashFile(backup.backupPath) != backup.sha256
            || !QFile::remove(backup.backupPath)) {
            complete = false;
        }
    }
    return complete;
}

RenamePlan RenameTransaction::makePlanForTests(const QList<RenameSource>& sources,
                                                const QList<QString>& targets)
{
    RenamePlan plan;
    for (int i = 0; i < sources.size(); ++i) {
        RenamePlanItem item;
        item.itemId = sources[i].itemId;
        item.sourcePath = sources[i].sourcePath;
        item.targetPath = targets.value(i);
        item.proposedFileName = QFileInfo(item.targetPath).fileName();
        item.sha256 = sources[i].sha256;
        item.size = sources[i].size;
        item.action = RenameAction::Rename;
        plan.items.append(std::move(item));
    }
    plan.executable = !plan.items.isEmpty();
    return plan;
}

} // namespace agplayer::qt
