#include "output_transaction.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace agplayer::separation {
namespace {

constexpr auto kReservationPattern = ".agplayer-separation-reservation-*.json";
constexpr auto kReservationPrefix = ".agplayer-separation-reservation-";
constexpr auto kReservationSuffix = ".json";
constexpr auto kTemporaryPrefix = ".agplayer-separation-job-";
constexpr auto kOutputLockName = ".agplayer-separation.lock";
constexpr qint64 kMaximumReservationBytes = 4096;
constexpr int kMaximumNumberedDirectories = 10'000;

enum class PathKind { Missing, RegularFile, Directory, Unsafe };

struct Reservation {
    QString path;
    QString temporaryPath;
};

bool safeName(const QString& value)
{
    if (value.isEmpty() || value != value.trimmed()
        || value.endsWith(QLatin1Char('.'))
        || value == QStringLiteral(".") || value == QStringLiteral("..")) {
        return false;
    }
    for (const QChar character : value) {
        if (character.unicode() < 0x20
            || QStringView(u"<>:\"/\\|?*").contains(character)) {
            return false;
        }
    }
    const QString stem = value.section(QLatin1Char('.'), 0, 0).toCaseFolded();
    static const QSet<QString> reserved{
        QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"),
        QStringLiteral("nul"), QStringLiteral("com1"), QStringLiteral("com2"),
        QStringLiteral("com3"), QStringLiteral("com4"), QStringLiteral("com5"),
        QStringLiteral("com6"), QStringLiteral("com7"), QStringLiteral("com8"),
        QStringLiteral("com9"), QStringLiteral("lpt1"), QStringLiteral("lpt2"),
        QStringLiteral("lpt3"), QStringLiteral("lpt4"), QStringLiteral("lpt5"),
        QStringLiteral("lpt6"), QStringLiteral("lpt7"), QStringLiteral("lpt8"),
        QStringLiteral("lpt9")};
    return !reserved.contains(stem);
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

#ifdef Q_OS_WIN
QString extendedNativePath(const QString& path)
{
    QString native = QDir::toNativeSeparators(absoluteCleanPath(path));
    if (native.startsWith(QStringLiteral("\\\\?\\"))) return native;
    if (native.startsWith(QStringLiteral("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    }
    return QStringLiteral("\\\\?\\") + native;
}

PathKind pathKind(const QString& path)
{
    const QString native = extendedNativePath(path);
    const DWORD attributes = GetFileAttributesW(
        reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
            ? PathKind::Missing
            : PathKind::Unsafe;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return PathKind::Unsafe;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        ? PathKind::Directory
        : PathKind::RegularFile;
}
#else
PathKind pathKind(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymbolicLink()) return PathKind::Unsafe;
    if (!info.exists()) return PathKind::Missing;
    if (info.isFile()) return PathKind::RegularFile;
    if (info.isDir()) return PathKind::Directory;
    return PathKind::Unsafe;
}
#endif

QString reservationPath(const QString& outputDirectory, const QString& token)
{
    return QDir(outputDirectory).filePath(
        QString::fromLatin1(kReservationPrefix) + token
        + QString::fromLatin1(kReservationSuffix));
}

QString temporaryName(const QString& token)
{
    return QString::fromLatin1(kTemporaryPrefix) + token;
}

bool validToken(const QString& token)
{
    if (token.size() != 32) return false;
    for (const QChar character : token) {
        if (!character.isDigit()
            && (character < QLatin1Char('a') || character > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

bool writeReservation(const QString& path, const QString& token,
                      const QString& temporaryDirectoryName)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("token"), token},
        {QStringLiteral("temporaryName"), temporaryDirectoryName}})
                                 .toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.flush() && file.commit();
}

bool parseReservation(const QString& path, const QString& outputDirectory,
                      Reservation* reservation)
{
    if (pathKind(path) != PathKind::RegularFile
        || !directChildOf(path, outputDirectory)) {
        return false;
    }
    const QString fileName = QFileInfo(path).fileName();
    const QString prefix = QString::fromLatin1(kReservationPrefix);
    const QString suffix = QString::fromLatin1(kReservationSuffix);
    if (!fileName.startsWith(prefix) || !fileName.endsWith(suffix)) return false;
    const QString fileToken = fileName.mid(
        prefix.size(), fileName.size() - prefix.size() - suffix.size());
    if (!validToken(fileToken)) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0
        || file.size() > kMaximumReservationBytes) {
        return false;
    }
    const QByteArray bytes = file.read(kMaximumReservationBytes + 1);
    if (bytes.size() != file.size()) return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    const QString token = object.value(QStringLiteral("token")).toString();
    const QString name = object.value(QStringLiteral("temporaryName")).toString();
    if (object.value(QStringLiteral("version")).toInt(-1) != 1
        || token != fileToken || name != temporaryName(token)) {
        return false;
    }
    const QString temporaryPath = QDir(outputDirectory).filePath(name);
    if (!safeName(name) || !directChildOf(temporaryPath, outputDirectory)) {
        return false;
    }
    *reservation = {path, temporaryPath};
    return true;
}

QFileInfoList flatEntries(const QString& directory)
{
    return QDir(directory).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
}

void appendExisting(QStringList* paths, const QString& path)
{
    if (pathKind(path) != PathKind::Missing && !paths->contains(path)) {
        paths->push_back(path);
    }
}

QStringList flatRemainingPaths(const Reservation& reservation,
                               const QFileInfoList& entries = {})
{
    QStringList remaining;
    for (const QFileInfo& entry : entries) {
        appendExisting(&remaining, entry.absoluteFilePath());
    }
    appendExisting(&remaining, reservation.temporaryPath);
    appendExisting(&remaining, reservation.path);
    return remaining;
}

QString numberedFinalDirectory(const OutputPlan& plan, int number)
{
    const QString& directoryName = plan.directoryName.isEmpty()
        ? plan.baseName : plan.directoryName;
    const QString name = number == 1
        ? directoryName
        : QStringLiteral("%1-%2").arg(directoryName).arg(number);
    return QDir(plan.outputDirectory).filePath(name);
}

} // namespace

bool NativeOutputFileOps::renameDirectory(const QString& source,
                                          const QString& destination)
{
    return QDir().rename(source, destination);
}

bool NativeOutputFileOps::removeFile(const QString& path)
{
    return !QFileInfo::exists(path) || QFile::remove(path);
}

bool NativeOutputFileOps::removeEmptyDirectory(const QString& path)
{
    if (!QFileInfo::exists(path)) return true;
    const QFileInfo info(path);
    return QDir(info.absolutePath()).rmdir(info.fileName());
}

OutputTransaction::OutputTransaction(
    OutputPlan plan, std::shared_ptr<NativeOutputFileOps> operations)
    : plan_(std::move(plan)), operations_(std::move(operations))
{
}

OutputTransaction::~OutputTransaction()
{
    if (!active_) return;
    const TransactionResult cleanup = rollback();
    if (!cleanup.ok) {
        qCritical().noquote()
            << "Separation output rollback failed; remaining paths:"
            << cleanup.outputs.join(QStringLiteral(", "));
    }
}

TransactionResult OutputTransaction::reject(const QString& code,
                                             const QString& message)
{
    return {false, code, message, {}};
}

TransactionResult OutputTransaction::cleanupReservedJob(
    const QString& reservationPathValue)
{
    if (pathKind(plan_.outputDirectory) != PathKind::Directory) {
        return {false, QStringLiteral("recovery_failed"),
                QStringLiteral("Output root is unsafe for cleanup"),
                {plan_.outputDirectory}};
    }

    Reservation reservation;
    if (!parseReservation(reservationPathValue, plan_.outputDirectory,
                          &reservation)) {
        QStringList remaining;
        appendExisting(&remaining, reservationPathValue);
        return {false, QStringLiteral("recovery_failed"),
                QStringLiteral("Output reservation is invalid or unsafe"),
                remaining};
    }

    const PathKind temporaryKind = pathKind(reservation.temporaryPath);
    if (temporaryKind == PathKind::Missing) {
        (void)operations_->removeFile(reservation.path);
        QStringList remaining;
        appendExisting(&remaining, reservation.path);
        return remaining.isEmpty()
            ? TransactionResult{true, {}, {}, {}}
            : TransactionResult{false, QStringLiteral("recovery_failed"),
                                QStringLiteral("Stale reservation cleanup failed"),
                                remaining};
    }
    if (temporaryKind != PathKind::Directory) {
        return {false, QStringLiteral("recovery_failed"),
                QStringLiteral("Reserved temporary path is unsafe"),
                flatRemainingPaths(reservation)};
    }

    const QFileInfoList entries = flatEntries(reservation.temporaryPath);
    for (const QFileInfo& entry : entries) {
        const QString path = entry.absoluteFilePath();
        if (!directChildOf(path, reservation.temporaryPath)
            || pathKind(path) != PathKind::RegularFile) {
            return {false, QStringLiteral("recovery_failed"),
                    QStringLiteral("Reserved temporary directory is not flat"),
                    flatRemainingPaths(reservation, entries)};
        }
    }

    for (const QFileInfo& entry : entries) {
        const QString path = entry.absoluteFilePath();
        if (pathKind(path) != PathKind::RegularFile) {
            return {false, QStringLiteral("recovery_failed"),
                    QStringLiteral("Reserved temporary entry became unsafe"),
                    flatRemainingPaths(reservation, flatEntries(
                        reservation.temporaryPath))};
        }
        (void)operations_->removeFile(path);
    }

    const QFileInfoList remainingEntries = flatEntries(reservation.temporaryPath);
    if (!remainingEntries.isEmpty()
        || pathKind(reservation.temporaryPath) != PathKind::Directory) {
        return {false, QStringLiteral("recovery_failed"),
                QStringLiteral("Temporary file cleanup left artifacts"),
                flatRemainingPaths(reservation, remainingEntries)};
    }
    (void)operations_->removeEmptyDirectory(reservation.temporaryPath);
    if (pathKind(reservation.temporaryPath) != PathKind::Missing) {
        return {false, QStringLiteral("recovery_failed"),
                QStringLiteral("Temporary directory cleanup failed"),
                flatRemainingPaths(reservation)};
    }

    (void)operations_->removeFile(reservation.path);
    QStringList remaining;
    appendExisting(&remaining, reservation.path);
    return remaining.isEmpty()
        ? TransactionResult{true, {}, {}, {}}
        : TransactionResult{false, QStringLiteral("recovery_failed"),
                            QStringLiteral("Reservation cleanup failed"),
                            remaining};
}

TransactionResult OutputTransaction::recoverReservedJobs()
{
    const QFileInfoList reservations = QDir(plan_.outputDirectory).entryInfoList(
        {QString::fromLatin1(kReservationPattern)},
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QFileInfo& reservation : reservations) {
        const TransactionResult recovered = cleanupReservedJob(
            reservation.absoluteFilePath());
        if (!recovered.ok) return recovered;
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
    if (!outputInfo.exists() || !outputInfo.isDir() || !outputInfo.isWritable()) {
        return reject(QStringLiteral("output_unwritable"),
                      QStringLiteral("Output directory does not exist or is not writable"));
    }
    plan_.outputDirectory = outputInfo.absoluteFilePath();
    if (pathKind(plan_.outputDirectory) != PathKind::Directory) {
        return reject(QStringLiteral("unsafe_output_root"),
                      QStringLiteral("Output directory is a link or reparse point"));
    }
    const bool hasLocalizedNames = !plan_.modelName.isEmpty()
        || !plan_.stemLabels.isEmpty();
    if (!safeName(plan_.baseName) || !safeName(plan_.extension)
        || plan_.stems.isEmpty()
        || (!plan_.directoryName.isEmpty() && !safeName(plan_.directoryName))
        || (hasLocalizedNames && (!safeName(plan_.modelName)
                                  || plan_.stemLabels.size() != plan_.stems.size()))
        || (plan_.directoryName.isEmpty() ? plan_.baseName : plan_.directoryName)
               .startsWith(QStringLiteral(".agplayer-separation-"),
                           Qt::CaseInsensitive)) {
        return reject(QStringLiteral("invalid_output_plan"),
                      QStringLiteral("Output plan contains an unsafe name"));
    }
    QSet<QString> stems;
    QSet<QString> labels;
    for (qsizetype index = 0; index < plan_.stems.size(); ++index) {
        const QString& stem = plan_.stems.at(index);
        const QString key = stem.toCaseFolded();
        if (!safeName(stem) || stems.contains(key)) {
            return reject(QStringLiteral("invalid_output_plan"),
                          QStringLiteral("Output plan contains an unsafe or duplicate stem"));
        }
        stems.insert(key);
        if (hasLocalizedNames) {
            const QString& label = plan_.stemLabels.at(index);
            const QString labelKey = label.toCaseFolded();
            const QString fileName = QStringLiteral("%1-%2-%3.%4").arg(
                plan_.baseName, label, plan_.modelName, plan_.extension);
            if (!safeName(label) || labels.contains(labelKey)
                || fileName.size() > 255) {
                return reject(QStringLiteral("invalid_output_plan"),
                              QStringLiteral("Output plan contains an unsafe localized name"));
            }
            labels.insert(labelKey);
        }
    }

    lock_ = std::make_unique<QLockFile>(QDir(plan_.outputDirectory).filePath(
        QString::fromLatin1(kOutputLockName)));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock(0)) {
        lock_.reset();
        return reject(QStringLiteral("transaction_lock_failed"),
                      QStringLiteral("Another output transaction is active"));
    }

    const TransactionResult recovered = recoverReservedJobs();
    if (!recovered.ok) {
        lock_.reset();
        return recovered;
    }

    const QString token = QUuid::createUuid().toString(QUuid::Id128).toLower();
    const QString name = temporaryName(token);
    reservationPath_ = reservationPath(plan_.outputDirectory, token);
    temporaryDirectory_ = QDir(plan_.outputDirectory).filePath(name);
    if (pathKind(reservationPath_) != PathKind::Missing
        || !writeReservation(reservationPath_, token, name)) {
        reservationPath_.clear();
        temporaryDirectory_.clear();
        lock_.reset();
        return reject(QStringLiteral("reservation_failed"),
                      QStringLiteral("Could not atomically reserve a temporary job"));
    }

    active_ = true;
    if (!QDir(plan_.outputDirectory).mkdir(name)
        || pathKind(temporaryDirectory_) != PathKind::Directory) {
        TransactionResult cleanup = rollback();
        if (!cleanup.ok) {
            cleanup.causeCode = QStringLiteral("temporary_directory_failed");
            cleanup.causeMessage = QStringLiteral(
                "Could not create a safe sibling temporary job directory");
            return cleanup;
        }
        return reject(QStringLiteral("temporary_directory_failed"),
                      QStringLiteral("Could not create a safe sibling temporary job directory"));
    }

    for (qsizetype index = 0; index < plan_.stems.size(); ++index) {
        const QString& stem = plan_.stems.at(index);
        const QString fileName = hasLocalizedNames
            ? QStringLiteral("%1-%2-%3.%4").arg(plan_.baseName,
                                                   plan_.stemLabels.at(index),
                                                   plan_.modelName,
                                                   plan_.extension)
            : QStringLiteral("%1-%2.%3").arg(plan_.baseName, stem,
                                                plan_.extension);
        temporaryPaths_.insert(
            stem, QDir(temporaryDirectory_).filePath(
                fileName));
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
    if (!operations_->removeFile(reservationPath_)) {
        // The outputs are already atomically published. Keep the reservation and
        // lock alive so cancel() or the destructor can retry only the stale
        // marker cleanup without touching the published directory.
        active_ = true;
        return {true, QStringLiteral("cleanup_pending"),
                QStringLiteral("Outputs were published, but reservation cleanup is pending"),
                outputs};
    }
    active_ = false;
    reservationPath_.clear();
    lock_.reset();
    return {true, {}, {}, outputs};
}

TransactionResult OutputTransaction::rollback()
{
    if (reservationPath_.isEmpty()) {
        active_ = false;
        lock_.reset();
        return {true, {}, {}, {}};
    }
    TransactionResult cleanup = cleanupReservedJob(reservationPath_);
    if (cleanup.ok) {
        active_ = false;
        reservationPath_.clear();
        lock_.reset();
        return cleanup;
    }
    active_ = true;
    cleanup.code = QStringLiteral("rollback_failed");
    cleanup.message = QStringLiteral("Output rollback left artifacts");
    return cleanup;
}

TransactionResult OutputTransaction::cancel()
{
    if (!active_) return {true, {}, {}, {}};
    return rollback();
}

} // namespace agplayer::separation
