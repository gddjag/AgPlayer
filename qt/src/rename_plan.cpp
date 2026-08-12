#include "rename_plan.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace agplayer::qt {
namespace {

QString collisionFreePath(const QString& desired, const QSet<QString>& reserved)
{
    const QFileInfo info(desired);
    const QString extension = info.suffix().isEmpty() ? QString()
        : QLatin1Char('.') + info.suffix();
    for (int suffix = 2; suffix <= 9999; ++suffix) {
        const QString candidate = info.dir().filePath(
            info.completeBaseName() + QLatin1Char('_') + QString::number(suffix)
            + extension);
        const QString key = FilenameValidator::collisionKey(candidate);
        if (!reserved.contains(key) && !QFileInfo::exists(candidate)) return candidate;
    }
    return {};
}

} // namespace

RenamePlan RenamePlanner::build(const QList<RenameSource>& sources,
                                const FilenameRuleSet& rules,
                                ConflictPolicy conflictPolicy)
{
    RenamePlan plan;
    plan.rules = rules;
    plan.conflictPolicy = conflictPolicy;
    QSet<QString> sourceKeys;
    for (const RenameSource& source : sources) {
        sourceKeys.insert(FilenameValidator::collisionKey(source.sourcePath));
    }
    QSet<QString> reserved;
    for (int ordinal = 0; ordinal < sources.size(); ++ordinal) {
        const RenameSource& source = sources.at(ordinal);
        RenamePlanItem item;
        item.itemId = source.itemId;
        item.sourcePath = source.sourcePath;
        item.sha256 = source.sha256;
        item.size = source.size;
        item.proposedFileName = FilenameTransformEngine::transform(
            QFileInfo(source.sourcePath).fileName(), rules, ordinal);
        item.targetPath = QFileInfo(source.sourcePath).dir().filePath(item.proposedFileName);
        item.normalizedCollisionKey = FilenameValidator::collisionKey(item.targetPath);
        const FilenameValidationIssue validation = FilenameValidator::validateTarget(
            source.sourcePath, item.targetPath);
        if (validation.severity == RenameSeverity::Error) {
            item.action = RenameAction::Skip;
            item.severity = validation.severity;
            item.reasonCode = validation.code;
            item.reasonText = validation.message;
        } else if (item.normalizedCollisionKey
                   == FilenameValidator::collisionKey(source.sourcePath)) {
            item.action = RenameAction::NoOp;
            item.reasonCode = QStringLiteral("no-change");
            item.reasonText = QStringLiteral("无需修改");
        } else {
            const bool external = QFileInfo::exists(item.targetPath)
                && !sourceKeys.contains(item.normalizedCollisionKey);
            const bool internal = reserved.contains(item.normalizedCollisionKey);
            if (external || internal) {
                if (conflictPolicy == ConflictPolicy::AutoNumber) {
                    item.targetPath = collisionFreePath(item.targetPath, reserved);
                    item.proposedFileName = QFileInfo(item.targetPath).fileName();
                    item.normalizedCollisionKey = FilenameValidator::collisionKey(item.targetPath);
                    if (item.targetPath.isEmpty()) {
                        item.action = RenameAction::Skip;
                        item.severity = RenameSeverity::Error;
                        item.reasonCode = QStringLiteral("unable-to-number");
                        item.reasonText = QStringLiteral("无法生成无冲突文件名");
                    } else {
                        item.severity = RenameSeverity::Warning;
                        item.reasonCode = QStringLiteral("auto-numbered");
                        item.reasonText = QStringLiteral("冲突已自动追加序号");
                    }
                } else if (conflictPolicy == ConflictPolicy::Overwrite && external) {
                    item.action = RenameAction::Overwrite;
                    item.severity = RenameSeverity::Warning;
                    item.reasonCode = QStringLiteral("overwrite-confirmation-required");
                    item.reasonText = QStringLiteral("目标已存在，需要二次确认覆盖");
                } else {
                    item.action = RenameAction::Skip;
                    item.severity = conflictPolicy == ConflictPolicy::StopBatch
                        ? RenameSeverity::Error : RenameSeverity::Warning;
                    item.reasonCode = QStringLiteral("target-conflict");
                    item.reasonText = QStringLiteral("目标文件已存在或批次内重名");
                }
            }
        }
        if (item.action == RenameAction::Rename || item.action == RenameAction::Overwrite) {
            reserved.insert(item.normalizedCollisionKey);
        }
        plan.items.append(std::move(item));
    }
    bool hasError = false;
    bool hasExecutable = false;
    for (const RenamePlanItem& item : plan.items) {
        hasError = hasError || item.severity == RenameSeverity::Error;
        hasExecutable = hasExecutable || item.action == RenameAction::Rename
            || item.action == RenameAction::Overwrite;
    }
    plan.executable = hasExecutable && !(conflictPolicy == ConflictPolicy::StopBatch && hasError);
    return plan;
}

} // namespace agplayer::qt
