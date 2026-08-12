#pragma once

#include "rename_plan.hpp"

#include <atomic>

namespace agplayer::qt {

struct RenameUndoRecord { RenamePlan plan; };

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
    static RenamePlan makePlanForTests(const QList<RenameSource>& sources,
                                       const QList<QString>& targets);
};

} // namespace agplayer::qt
