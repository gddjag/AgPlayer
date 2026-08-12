#pragma once

#include "rename_plan.hpp"

#include <QString>

namespace agplayer::qt {

class RenameJournalStore final {
public:
    static QString writePrepared(const QString& transactionId, const RenamePlan& plan);
    static bool markCompleted(const QString& journalPath, bool committed,
                              const QString& outcome, const QString& error = {});
};

} // namespace agplayer::qt
