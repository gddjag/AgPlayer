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

struct Step { RenamePlanItem item; QString stage; };

bool rollback(QList<Step>& steps, int staged, int committed)
{
    bool ok = true;
    for (int i = committed - 1; i >= 0; --i) {
        if (QFileInfo::exists(steps[i].item.targetPath)
            && !QFile::rename(steps[i].item.targetPath, steps[i].stage)) ok = false;
    }
    for (int i = staged - 1; i >= 0; --i) {
        if (QFileInfo::exists(steps[i].stage)
            && !QFile::rename(steps[i].stage, steps[i].item.sourcePath)) ok = false;
    }
    return ok;
}

RenameTransactionResult failure(QString code, QString text, QList<Step>& steps,
                                int staged, int committed)
{
    RenameTransactionResult result;
    const bool rolledBack = rollback(steps, staged, committed);
    result.errorCode = rolledBack ? std::move(code) : QStringLiteral("rollback-failed");
    result.errorText = rolledBack ? std::move(text) : QStringLiteral("重命名失败，自动回滚未完整完成");
    return result;
}

} // namespace

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
        if (item.action != RenameAction::Rename) continue;
        if (!QFileInfo::exists(item.sourcePath) || QFileInfo(item.sourcePath).size() != item.size
            || hashFile(item.sourcePath) != item.sha256) {
            RenameJournalStore::markCompleted(journalPath, false, QStringLiteral("SourceChanged"));
            return {false, false, QStringLiteral("source-changed"), QStringLiteral("文件在重命名前已变更"), journalPath, {}};
        }
        steps.append({item, stagePath(item.sourcePath, transaction, item.itemId)});
    }
    int staged = 0;
    int committed = 0;
    for (Step& step : steps) {
        if (cancel != nullptr && cancel->load()) {
            RenameTransactionResult result = failure(QStringLiteral("cancelled"), QStringLiteral("重命名已取消并回滚"), steps, staged, committed);
            result.cancelled = true;
            return result;
        }
        if (QFileInfo::exists(step.stage) || !QFile::rename(step.item.sourcePath, step.stage)) {
            return failure(QStringLiteral("stage-failed"), QStringLiteral("无法创建同目录暂存文件"), steps, staged, committed);
        }
        ++staged;
    }
    for (Step& step : steps) {
        if (cancel != nullptr && cancel->load()) {
            RenameTransactionResult result = failure(QStringLiteral("cancelled"), QStringLiteral("重命名已取消并回滚"), steps, staged, committed);
            result.cancelled = true;
            return result;
        }
        if (!QFile::rename(step.stage, step.item.targetPath) || hashFile(step.item.targetPath) != step.item.sha256) {
            return failure(QStringLiteral("commit-failed"), QStringLiteral("重命名提交或完整性校验失败"), steps, staged, committed);
        }
        ++committed;
    }
    RenameJournalStore::markCompleted(journalPath, true, QStringLiteral("Committed"));
    return {true, false, {}, {}, journalPath, {plan}};
}

RenameTransactionResult RenameTransaction::undo(const RenameUndoRecord& record) const
{
    RenamePlan reverse = record.plan;
    for (RenamePlanItem& item : reverse.items) {
        if (item.action != RenameAction::Rename) continue;
        if (!QFileInfo::exists(item.targetPath) || QFileInfo(item.targetPath).size() != item.size
            || hashFile(item.targetPath) != item.sha256) {
            return {false, false, QStringLiteral("undo-target-changed"), QStringLiteral("无法安全撤销：目标文件已被外部替换或修改"), {}};
        }
        std::swap(item.sourcePath, item.targetPath);
        item.proposedFileName = QFileInfo(item.targetPath).fileName();
    }
    return execute(reverse);
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
