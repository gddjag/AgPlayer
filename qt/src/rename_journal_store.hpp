#pragma once

#include "rename_plan.hpp"

#include <QString>
#include <QStringList>

namespace agplayer::qt {

struct RenameRecoveryReport {
    int recovered = 0;
    int failed = 0;
    QStringList failedJournals;
};

class RenameJournalStore final {
public:
    static QString writePrepared(const QString& transactionId, const RenamePlan& plan);
    static bool markCompleted(const QString& journalPath, bool committed,
                              const QString& outcome, const QString& error = {});
    static RenameRecoveryReport recoverIncomplete();
};

} // namespace agplayer::qt
