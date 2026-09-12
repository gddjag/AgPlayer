#pragma once

#include "filename_transform_engine.hpp"
#include "filename_validator.hpp"

#include <QList>
#include <QString>

namespace agplayer::qt {

enum class ConflictPolicy { Skip, Overwrite, AutoNumber, StopBatch };
enum class RenameAction { Rename, Skip, NoOp, Overwrite };

struct RenameSource {
    int itemId = -1;
    QString sourcePath;
    QString sha256;
    QString fileIdentity;
    qint64 size = 0;
};

struct RenamePlanItem {
    int itemId = -1;
    QString sourcePath;
    QString targetPath;
    QString proposedFileName;
    QString normalizedCollisionKey;
    QString sha256;
    qint64 size = 0;
    RenameAction action = RenameAction::Rename;
    RenameSeverity severity = RenameSeverity::Ready;
    QString reasonCode;
    QString reasonText;
};

struct RenamePlan {
    QList<RenamePlanItem> items;
    FilenameRuleSet rules;
    ConflictPolicy conflictPolicy = ConflictPolicy::AutoNumber;
    bool executable = false;
};

class RenamePlanner final {
public:
    static RenamePlan build(const QList<RenameSource>& sources,
                            const FilenameRuleSet& rules,
                            ConflictPolicy conflictPolicy);
};

} // namespace agplayer::qt
