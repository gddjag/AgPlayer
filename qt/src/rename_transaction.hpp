#pragma once

#include "rename_plan.hpp"

#include <atomic>

namespace agplayer::qt {

struct RenameOverwriteBackup {
    int itemId = -1;
    QString targetPath;
    QString backupPath;
    QString sha256;
    qint64 size = 0;
};

struct RenameUndoRecord {
    RenamePlan plan;
    QList<RenameOverwriteBackup> overwriteBackups;
};

struct RenameTransactionResult {
    bool committed = false;
    bool cancelled = false;
    QString errorCode;
    QString errorText;
    QString journalPath;
    RenameUndoRecord undoRecord;
};

class RenameTransaction final {
public:
    RenameTransactionResult execute(const RenamePlan& plan,
                                    const std::atomic_bool* cancel = nullptr) const;
    RenameTransactionResult undo(const RenameUndoRecord& record) const;
    static bool discardUndo(const RenameUndoRecord& record);
    static RenamePlan makePlanForTests(const QList<RenameSource>& sources,
                                       const QList<QString>& targets);
};

} // namespace agplayer::qt
