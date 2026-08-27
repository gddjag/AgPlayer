#include "output_transaction.hpp"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace agplayer::separation {
namespace {

constexpr auto kManifestName = ".agplayer-separation-transaction.json";
constexpr auto kLockSuffix = ".lock";
constexpr qint64 kMaximumManifestBytes = 64 * 1024;

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

bool flushFilePath(const QString& path)
{
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool flushed = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    return flushed;
#else
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return ::fsync(file.handle()) == 0;
#endif
}

bool writeDurableJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) return false;
    return flushFilePath(path);
}

struct RecoveryManifest {
    QString phase;
    QString temporaryDirectory;
    QStringList finalPaths;
};

bool readRecoveryManifest(const QString& manifestPath,
                          const QString& outputDirectory,
                          const QString& temporaryDirectory,
                          RecoveryManifest* manifest)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0
        || file.size() > kMaximumManifestBytes) {
        return false;
    }
    QJsonParseError parseError;
    const QByteArray manifestBytes = file.read(kMaximumManifestBytes + 1);
    if (manifestBytes.size() != file.size()) return false;
    const QJsonDocument document = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1
        || !samePath(object.value(QStringLiteral("outputDirectory")).toString(),
                     outputDirectory)
        || !samePath(object.value(QStringLiteral("temporaryDirectory")).toString(),
                     temporaryDirectory)) {
        return false;
    }
    const QString phase = object.value(QStringLiteral("phase")).toString();
    if (phase != QStringLiteral("staging")
        && phase != QStringLiteral("committing")) {
        return false;
    }
    const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty() || entries.size() > 5) return false;
    QSet<QString> finals;
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString temporary = entry.value(QStringLiteral("temporary")).toString();
        const QString final = entry.value(QStringLiteral("final")).toString();
        const QString finalKey = absoluteCleanPath(final).toCaseFolded();
        if (temporary.isEmpty() || final.isEmpty()
            || !directChildOf(temporary, temporaryDirectory)
            || !directChildOf(final, outputDirectory)
            || finals.contains(finalKey)) {
            return false;
        }
        finals.insert(finalKey);
        manifest->finalPaths.push_back(final);
    }
    manifest->phase = phase;
    manifest->temporaryDirectory = temporaryDirectory;
    return true;
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
        || plan_.stems.isEmpty()) {
        return reject(QStringLiteral("invalid_output_plan"),
                      QStringLiteral("Output plan contains an unsafe name"));
    }

    const TransactionResult recovered = recoverStaleTransactions();
    if (!recovered.ok) return recovered;

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
    lock_ = std::make_unique<QLockFile>(
        temporaryDirectory_ + QString::fromLatin1(kLockSuffix));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock(0)) {
        lock_.reset();
        (void)QDir(temporaryDirectory_).removeRecursively();
        temporaryDirectory_.clear();
        return reject(QStringLiteral("transaction_lock_failed"),
                      QStringLiteral("Could not acquire the output transaction lock"));
    }
    manifestPath_ = QDir(temporaryDirectory_).filePath(
        QString::fromLatin1(kManifestName));
    finalPaths_ = candidateFinals;
    for (const QString& stem : plan_.stems) {
        temporaryPaths_.insert(
            stem, QDir(temporaryDirectory_)
                      .filePath(QStringLiteral("%1.%2").arg(stem, plan_.extension)));
    }
    active_ = true;
    const TransactionResult manifest = writeManifest(QStringLiteral("staging"));
    if (!manifest.ok) {
        const TransactionResult cleanup = rollback();
        if (!cleanup.ok) return cleanup;
        return manifest;
    }
    return {true, {}, {}, {}};
}

TransactionResult OutputTransaction::recoverStaleTransactions()
{
    QDir output(plan_.outputDirectory);
    const QFileInfoList staleDirectories = output.entryInfoList(
        {QStringLiteral(".agplayer-separation-*")},
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo& staleInfo : staleDirectories) {
        const QString staleDirectory = staleInfo.absoluteFilePath();
        QLockFile recoveryLock(
            staleDirectory + QString::fromLatin1(kLockSuffix));
        recoveryLock.setStaleLockTime(0);
        if (!recoveryLock.tryLock(0)) {
            TransactionResult result = reject(
                recoveryLock.error() == QLockFile::LockFailedError
                    ? QStringLiteral("recovery_in_use")
                    : QStringLiteral("recovery_failed"),
                QStringLiteral("An output transaction is still active"));
            result.outputs = {staleDirectory};
            return result;
        }
        const QString staleManifest = QDir(staleDirectory).filePath(
            QString::fromLatin1(kManifestName));
        RecoveryManifest manifest;
        if (!readRecoveryManifest(staleManifest, plan_.outputDirectory,
                                  staleDirectory, &manifest)) {
            TransactionResult result = reject(
                QStringLiteral("recovery_failed"),
                QStringLiteral("A stale output transaction has an invalid recovery manifest"));
            result.outputs = {staleDirectory};
            return result;
        }

        QStringList remaining;
        if (manifest.phase == QStringLiteral("committing")) {
            for (const QString& finalPath : manifest.finalPaths) {
                (void)operations_->removeFile(finalPath);
                if (QFileInfo::exists(finalPath)) remaining.push_back(finalPath);
            }
        }
        if (!QDir(staleDirectory).removeRecursively()
            || QFileInfo::exists(staleDirectory)) {
            remaining.push_back(staleDirectory);
        }
        if (!remaining.isEmpty()) {
            return {false, QStringLiteral("recovery_failed"),
                    QStringLiteral("Stale output transaction recovery left artifacts"),
                    remaining};
        }
    }
    return {true, {}, {}, {}};
}

TransactionResult OutputTransaction::writeManifest(const QString& phase)
{
    QJsonArray entries;
    for (const QString& stem : plan_.stems) {
        entries.push_back(QJsonObject{
            {QStringLiteral("temporary"), temporaryPaths_.value(stem)},
            {QStringLiteral("final"), finalPaths_.value(stem)}});
    }
    const QJsonObject manifest{
        {QStringLiteral("version"), 1},
        {QStringLiteral("phase"), phase},
        {QStringLiteral("outputDirectory"), plan_.outputDirectory},
        {QStringLiteral("temporaryDirectory"), temporaryDirectory_},
        {QStringLiteral("entries"), entries}};
    if (!writeDurableJson(manifestPath_, manifest)) {
        TransactionResult result = reject(
            QStringLiteral("manifest_write_failed"),
            QStringLiteral("Could not durably write the output transaction manifest"));
        if (QFileInfo::exists(temporaryDirectory_)) {
            result.outputs.push_back(temporaryDirectory_);
        }
        return result;
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
    const std::atomic_bool& cancelled)
{
    if (!active_) {
        return reject(QStringLiteral("transaction_inactive"),
                      QStringLiteral("Output transaction has not begun"));
    }
    const auto cleanupOr = [this](const QString& message,
                                  const TransactionResult& fallback) {
        TransactionResult cleanup = rollback();
        if (!cleanup.ok) {
            cleanup.message = message;
            return cleanup;
        }
        return fallback;
    };
    const auto rejectCancelled = [this, &cleanupOr] {
        return cleanupOr(
            QStringLiteral("Cancellation cleanup left output artifacts"),
            reject(QStringLiteral("cancelled"),
                   QStringLiteral("Output commit was cancelled")));
    };
    if (cancelled.load()) return rejectCancelled();
    for (const QString& stem : plan_.stems) {
        if (cancelled.load()) return rejectCancelled();
        const QString path = temporaryPaths_.value(stem);
        if (!QFileInfo(path).isFile() || !verifier(path)) {
            return cleanupOr(
                QStringLiteral("Verification cleanup left output artifacts"),
                reject(QStringLiteral("verification_failed"),
                       QStringLiteral("A temporary output failed reopen verification")));
        }
        if (cancelled.load()) return rejectCancelled();
    }

    const TransactionResult committing = writeManifest(QStringLiteral("committing"));
    if (!committing.ok) {
        return cleanupOr(
            QStringLiteral("Manifest failure cleanup left output artifacts"),
            committing);
    }

    for (const QString& stem : plan_.stems) {
        if (cancelled.load()) return rejectCancelled();
        const QString finalPath = finalPaths_.value(stem);
        if (QFileInfo::exists(finalPath)
            || !operations_->renameFile(temporaryPaths_.value(stem), finalPath)) {
            return cleanupOr(
                QStringLiteral("Commit rollback left output artifacts"),
                reject(QStringLiteral("commit_failed"),
                       QStringLiteral("Output commit rename failed")));
        }
        committedPaths_.push_back(finalPath);
        if (cancelled.load()) return rejectCancelled();
    }

    const QStringList outputs = committedPaths_;
    if (!QDir(temporaryDirectory_).removeRecursively()
        || QFileInfo::exists(temporaryDirectory_)) {
        return cleanupOr(
            QStringLiteral("Temporary-directory cleanup left output artifacts"),
            reject(QStringLiteral("commit_failed"),
                   QStringLiteral("Could not remove the temporary output directory")));
    }
    if (cancelled.load()) return rejectCancelled();
    committedPaths_.clear();
    active_ = false;
    lock_.reset();
    return {true, {}, {}, outputs};
}

TransactionResult OutputTransaction::rollback()
{
    QStringList remaining;
    for (const QString& path : std::as_const(committedPaths_)) {
        (void)operations_->removeFile(path);
        if (QFileInfo::exists(path)) remaining.push_back(path);
    }
    committedPaths_ = remaining;
    // Keep the durable manifest while any published final remains. If this
    // process exits before a retry succeeds, the next transaction can still
    // identify and recover the partial commit.
    if (committedPaths_.isEmpty() && !temporaryDirectory_.isEmpty()) {
        (void)QDir(temporaryDirectory_).removeRecursively();
    }
    const bool clean = committedPaths_.isEmpty()
        && !QFileInfo::exists(temporaryDirectory_);
    active_ = !clean;
    if (clean) {
        lock_.reset();
        return {true, {}, {}, {}};
    }
    if (QFileInfo::exists(temporaryDirectory_)) {
        remaining.push_back(temporaryDirectory_);
    }
    return {false, QStringLiteral("rollback_failed"),
            QStringLiteral("Output rollback left artifacts"), remaining};
}

TransactionResult OutputTransaction::cancel()
{
    return active_ ? rollback() : TransactionResult{true, {}, {}, {}};
}

} // namespace agplayer::separation
