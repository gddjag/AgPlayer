#pragma once

#include "rename_plan.hpp"

#include <atomic>
#include <functional>
#include <utility>

class RenameTransactionTest;

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
    RenameTransaction() = default;
    RenameTransactionResult execute(const RenamePlan& plan,
                                    const std::atomic_bool* cancel = nullptr) const;
    RenameTransactionResult undo(const RenameUndoRecord& record) const;
    static bool discardUndo(const RenameUndoRecord& record);
    static RenamePlan makePlanForTests(const QList<RenameSource>& sources,
                                       const QList<QString>& targets);

private:
    using TestHook = std::function<void(QStringView, const QString&)>;

    explicit RenameTransaction(TestHook testHook)
        : testHook_(std::move(testHook))
    {
    }

    void notifyTestHook(QStringView point, const QString& path) const;

    TestHook testHook_;

    friend class ::RenameTransactionTest;
};

} // namespace agplayer::qt
