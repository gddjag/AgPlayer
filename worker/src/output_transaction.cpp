#include "output_transaction.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace agplayer::separation {
namespace {

bool safeName(const QString& value)
{
    return !value.isEmpty() && value == value.trimmed()
        && !value.contains(QLatin1Char('/'))
        && !value.contains(QLatin1Char('\\'))
        && value != QStringLiteral(".") && value != QStringLiteral("..");
}

} // namespace

bool NativeOutputFileOps::renameFile(const QString& source,
                                     const QString& destination)
{
    return QFile::rename(source, destination);
}

bool NativeOutputFileOps::removeFile(const QString& path)
{
    return !QFileInfo::exists(path) || QFile::remove(path);
}

OutputTransaction::OutputTransaction(
    OutputPlan plan, std::shared_ptr<NativeOutputFileOps> operations)
    : plan_(std::move(plan)), operations_(std::move(operations))
{
}

OutputTransaction::~OutputTransaction()
{
    if (active_) rollback();
}

TransactionResult OutputTransaction::reject(const QString& code,
                                            const QString& message)
{
    return {false, code, message, {}};
}

TransactionResult OutputTransaction::begin()
{
    if (active_) {
        return reject(QStringLiteral("transaction_active"),
                      QStringLiteral("Output transaction is already active"));
    }
    const QFileInfo outputInfo(plan_.outputDirectory);
    if (!outputInfo.isDir() || !outputInfo.isWritable()) {
        return reject(QStringLiteral("output_unwritable"),
                      QStringLiteral("Output directory does not exist or is not writable"));
    }
    if (!safeName(plan_.baseName) || !safeName(plan_.extension)
        || plan_.stems.isEmpty()) {
        return reject(QStringLiteral("invalid_output_plan"),
                      QStringLiteral("Output plan contains an unsafe name"));
    }

    QHash<QString, QString> candidateFinals;
    for (const QString& stem : plan_.stems) {
        if (!safeName(stem) || candidateFinals.contains(stem)) {
            return reject(QStringLiteral("invalid_output_plan"),
                          QStringLiteral("Output plan contains an unsafe or duplicate stem"));
        }
        const QString finalPath = QDir(plan_.outputDirectory)
            .filePath(QStringLiteral("%1-%2.%3").arg(plan_.baseName, stem,
                                                     plan_.extension));
        if (QFileInfo::exists(finalPath)) {
            return reject(QStringLiteral("output_exists"),
                          QStringLiteral("An output file already exists"));
        }
        candidateFinals.insert(stem, finalPath);
    }

    QTemporaryDir temporary(QDir(plan_.outputDirectory)
                                .filePath(QStringLiteral(".agplayer-separation-XXXXXX")));
    if (!temporary.isValid()) {
        return reject(QStringLiteral("temporary_directory_failed"),
                      QStringLiteral("Could not create a sibling temporary directory"));
    }
    temporary.setAutoRemove(false);
    temporaryDirectory_ = temporary.path();
    finalPaths_ = candidateFinals;
    for (const QString& stem : plan_.stems) {
        temporaryPaths_.insert(
            stem, QDir(temporaryDirectory_)
                      .filePath(QStringLiteral("%1.%2").arg(stem, plan_.extension)));
    }
    active_ = true;
    return {true, {}, {}, {}};
}

QString OutputTransaction::temporaryPath(const QString& stem) const
{
    return temporaryPaths_.value(stem);
}

QString OutputTransaction::temporaryDirectory() const
{
    return temporaryDirectory_;
}

TransactionResult OutputTransaction::commit(
    const std::function<bool(const QString&)>& verifier)
{
    if (!active_) {
        return reject(QStringLiteral("transaction_inactive"),
                      QStringLiteral("Output transaction has not begun"));
    }
    for (const QString& stem : plan_.stems) {
        const QString path = temporaryPaths_.value(stem);
        if (!QFileInfo(path).isFile() || !verifier(path)) {
            rollback();
            return reject(QStringLiteral("verification_failed"),
                          QStringLiteral("A temporary output failed reopen verification"));
        }
    }

    for (const QString& stem : plan_.stems) {
        const QString finalPath = finalPaths_.value(stem);
        if (QFileInfo::exists(finalPath)
            || !operations_->renameFile(temporaryPaths_.value(stem), finalPath)) {
            rollback();
            return reject(QStringLiteral("commit_failed"),
                          QStringLiteral("Atomic output commit failed"));
        }
        committedPaths_.push_back(finalPath);
    }

    const QStringList outputs = committedPaths_;
    committedPaths_.clear();
    QDir(temporaryDirectory_).removeRecursively();
    active_ = false;
    return {true, {}, {}, outputs};
}

void OutputTransaction::rollback()
{
    for (const QString& path : std::as_const(committedPaths_)) {
        operations_->removeFile(path);
    }
    committedPaths_.clear();
    if (!temporaryDirectory_.isEmpty()) {
        QDir(temporaryDirectory_).removeRecursively();
    }
    active_ = false;
}

void OutputTransaction::cancel()
{
    if (active_) rollback();
}

} // namespace agplayer::separation
