#include "output_transaction.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSet>
#include <QTemporaryDir>

namespace agplayer::separation {
namespace {

constexpr auto kTemporaryPattern = ".agplayer-separation-job-*";
constexpr auto kTemporaryTemplate = ".agplayer-separation-job-XXXXXX";
constexpr auto kOwnedMarkerName = ".agplayer-separation-owned";
constexpr auto kOwnedMarkerValue = "agplayer-separation-v1";
constexpr auto kOutputLockName = ".agplayer-separation.lock";
constexpr int kMaximumNumberedDirectories = 10'000;

bool safeName(const QString& value)
{
    return !value.isEmpty() && value == value.trimmed()
        && !value.contains(QLatin1Char('/'))
        && !value.contains(QLatin1Char('\\'))
        && value != QStringLiteral(".") && value != QStringLiteral("..");
}

QString absoluteCleanPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return absoluteCleanPath(left).compare(absoluteCleanPath(right),
                                           Qt::CaseInsensitive) == 0;
#else
    return absoluteCleanPath(left) == absoluteCleanPath(right);
#endif
}

bool directChildOf(const QString& path, const QString& directory)
{
    return samePath(QFileInfo(path).absolutePath(), directory);
}

QString markerPath(const QString& directory)
{
    return QDir(directory).filePath(QString::fromLatin1(kOwnedMarkerName));
}

bool createOwnedMarker(const QString& directory)
{
    QFile marker(markerPath(directory));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    const QByteArray value = QByteArrayLiteral(kOwnedMarkerValue);
    return marker.write(value) == value.size() && marker.flush();
}

bool hasOwnedMarker(const QString& directory)
{
    QFile marker(markerPath(directory));
    const QByteArray expected = QByteArrayLiteral(kOwnedMarkerValue);
    return marker.open(QIODevice::ReadOnly)
        && marker.size() == expected.size()
        && marker.read(expected.size() + 1) == expected;
}

QString numberedFinalDirectory(const OutputPlan& plan, int number)
{
    const QString name = number == 1
        ? plan.baseName
        : QStringLiteral("%1-%2").arg(plan.baseName).arg(number);
    return QDir(plan.outputDirectory).filePath(name);
}

} // namespace

bool NativeOutputFileOps::renameDirectory(const QString& source,
                                          const QString& destination)
{
    return QDir().rename(source, destination);
}

bool NativeOutputFileOps::removeDirectory(const QString& path)
{
    return !QFileInfo::exists(path) || QDir(path).removeRecursively();
}

OutputTransaction::OutputTransaction(
    OutputPlan plan, std::shared_ptr<NativeOutputFileOps> operations)
    : plan_(std::move(plan)), operations_(std::move(operations))
{
}

OutputTransaction::~OutputTransaction()
{
    if (!active_) return;
    const TransactionResult result = rollback();
    if (!result.ok) {
        qCritical().noquote()
            << "Separation output rollback failed; remaining paths:"
            << result.outputs.join(QStringLiteral(", "));
    }
}

TransactionResult OutputTransaction::reject(const QString& code,
                                             const QString& message)
{
    return {false, code, message, {}};
}

TransactionResult OutputTransaction::recoverOwnedTemporaryDirectories()
{
    QDir output(plan_.outputDirectory);
    const QFileInfoList candidates = output.entryInfoList(
        {QString::fromLatin1(kTemporaryPattern)},
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo& candidate : candidates) {
        const QString path = candidate.absoluteFilePath();
        if (!directChildOf(path, plan_.outputDirectory)
            || !hasOwnedMarker(path)) {
            continue;
        }
        (void)operations_->removeDirectory(path);
        if (QFileInfo::exists(path)) {
            return {false, QStringLiteral("recovery_failed"),
                    QStringLiteral("Stale temporary job cleanup left artifacts"),
                    {path}};
        }
    }
    return {true, {}, {}, {}};
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
    plan_.outputDirectory = outputInfo.absoluteFilePath();
    if (!safeName(plan_.baseName) || !safeName(plan_.extension)
        || plan_.stems.isEmpty()
        || plan_.baseName.startsWith(QStringLiteral(".agplayer-separation-"),
                                     Qt::CaseInsensitive)) {
        return reject(QStringLiteral("invalid_output_plan"),
                      QStringLiteral("Output plan contains an unsafe name"));
    }
    QSet<QString> stems;
    for (const QString& stem : plan_.stems) {
        const QString key = stem.toCaseFolded();
        if (!safeName(stem) || stems.contains(key)) {
            return reject(QStringLiteral("invalid_output_plan"),
                          QStringLiteral("Output plan contains an unsafe or duplicate stem"));
        }
        stems.insert(key);
    }

    lock_ = std::make_unique<QLockFile>(QDir(plan_.outputDirectory).filePath(
        QString::fromLatin1(kOutputLockName)));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock(0)) {
        lock_.reset();
        return reject(QStringLiteral("transaction_lock_failed"),
                      QStringLiteral("Another output transaction is active"));
    }

    const TransactionResult recovered = recoverOwnedTemporaryDirectories();
    if (!recovered.ok) {
        lock_.reset();
        return recovered;
    }

    QTemporaryDir temporary(QDir(plan_.outputDirectory).filePath(
        QString::fromLatin1(kTemporaryTemplate)));
    if (!temporary.isValid()) {
        lock_.reset();
        return reject(QStringLiteral("temporary_directory_failed"),
                      QStringLiteral("Could not create a sibling temporary job directory"));
    }
    temporary.setAutoRemove(false);
    temporaryDirectory_ = temporary.path();
    active_ = true;
    if (!createOwnedMarker(temporaryDirectory_)) {
        TransactionResult cleanup = rollback();
        if (!cleanup.ok) {
            cleanup.causeCode = QStringLiteral("marker_write_failed");
            cleanup.causeMessage = QStringLiteral(
                "Could not create the temporary job ownership marker");
            return cleanup;
        }
        return reject(QStringLiteral("marker_write_failed"),
                      QStringLiteral("Could not create the temporary job ownership marker"));
    }
    for (const QString& stem : plan_.stems) {
        temporaryPaths_.insert(
            stem, QDir(temporaryDirectory_).filePath(
                QStringLiteral("%1-%2.%3").arg(plan_.baseName, stem,
                                                plan_.extension)));
    }
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
    const std::function<bool(const QString&)>& verifier,
    const CancellationToken& cancellation)
{
    if (!active_) {
        return reject(QStringLiteral("transaction_inactive"),
                      QStringLiteral("Output transaction has not begun"));
    }
    const auto failAndRollback = [this](const QString& code,
                                        const QString& message,
                                        const QString& cleanupMessage) {
        const TransactionResult cause = reject(code, message);
        TransactionResult cleanup = rollback();
        if (!cleanup.ok) {
            cleanup.message = cleanupMessage;
            cleanup.causeCode = cause.code;
            cleanup.causeMessage = cause.message;
            return cleanup;
        }
        return cause;
    };
    const auto cancelled = [&] {
        return failAndRollback(
            QStringLiteral("cancelled"),
            QStringLiteral("Output commit was cancelled"),
            QStringLiteral("Cancellation cleanup left output artifacts"));
    };

    if (cancellation.isCancelled()) return cancelled();
    for (const QString& stem : plan_.stems) {
        if (cancellation.isCancelled()) return cancelled();
        const QString path = temporaryPaths_.value(stem);
        if (!QFileInfo(path).isFile() || !verifier(path)) {
            return failAndRollback(
                QStringLiteral("verification_failed"),
                QStringLiteral("A temporary output failed reopen verification"),
                QStringLiteral("Verification cleanup left output artifacts"));
        }
        if (cancellation.isCancelled()) return cancelled();
    }

    QString finalDirectory;
    const bool published = cancellation.tryCommit([&] {
        for (int number = 1; number <= kMaximumNumberedDirectories; ++number) {
            const QString candidate = numberedFinalDirectory(plan_, number);
            if (QFileInfo::exists(candidate)) continue;
            if (operations_->renameDirectory(temporaryDirectory_, candidate)) {
                finalDirectory = candidate;
                return true;
            }
            if (!QFileInfo::exists(candidate)) return false;
        }
        return false;
    });
    if (!published) {
        return cancellation.isCancelled()
            ? cancelled()
            : failAndRollback(
                QStringLiteral("commit_failed"),
                QStringLiteral("Output job directory rename failed"),
                QStringLiteral("Commit cleanup left output artifacts"));
    }

    QStringList outputs;
    outputs.reserve(plan_.stems.size());
    for (const QString& stem : plan_.stems) {
        outputs.push_back(QDir(finalDirectory).filePath(
            QFileInfo(temporaryPaths_.value(stem)).fileName()));
    }
    active_ = false;
    lock_.reset();
    return {true, {}, {}, outputs};
}

TransactionResult OutputTransaction::rollback()
{
    if (!temporaryDirectory_.isEmpty()) {
        (void)operations_->removeDirectory(temporaryDirectory_);
    }
    QStringList remaining;
    if (QFileInfo::exists(temporaryDirectory_)) {
        remaining.push_back(temporaryDirectory_);
    }
    active_ = !remaining.isEmpty();
    if (!active_) {
        lock_.reset();
        return {true, {}, {}, {}};
    }
    return {false, QStringLiteral("rollback_failed"),
            QStringLiteral("Output rollback left artifacts"), remaining};
}

TransactionResult OutputTransaction::cancel()
{
    return active_ ? rollback() : TransactionResult{true, {}, {}, {}};
}

} // namespace agplayer::separation
