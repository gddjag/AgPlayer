#include "output_transaction.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace agplayer::separation;

namespace {

constexpr auto kReservationPrefix = ".agplayer-separation-reservation-";
constexpr auto kTemporaryPrefix = ".agplayer-separation-job-";

bool writePayload(const QString& path,
                  const QByteArray& bytes = QByteArrayLiteral("audio"))
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

struct ReservedJob {
    QString reservationPath;
    QString temporaryName;
    QString temporaryPath;
};

ReservedJob createReservedJob(const QString& outputDirectory,
                              const QString& token,
                              bool createDirectory)
{
    ReservedJob job;
    job.temporaryName = QString::fromLatin1(kTemporaryPrefix) + token;
    job.temporaryPath = QDir(outputDirectory).filePath(job.temporaryName);
    job.reservationPath = QDir(outputDirectory).filePath(
        QString::fromLatin1(kReservationPrefix) + token
        + QStringLiteral(".json"));
    QSaveFile reservation(job.reservationPath);
    if (!reservation.open(QIODevice::WriteOnly)) return {};
    const QByteArray payload = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("token"), token},
        {QStringLiteral("temporaryName"), job.temporaryName}})
                                   .toJson(QJsonDocument::Compact);
    if (reservation.write(payload) != payload.size()
        || !reservation.flush() || !reservation.commit()) {
        return {};
    }
    if (createDirectory && !QDir(outputDirectory).mkdir(job.temporaryName)) {
        return {};
    }
    return job;
}

#ifdef Q_OS_WIN
bool createJunction(const QString& junctionPath, const QString& targetPath)
{
    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setArguments(
        {QStringLiteral("/d"), QStringLiteral("/c"), QStringLiteral("mklink"),
         QStringLiteral("/J"), QDir::toNativeSeparators(junctionPath),
         QDir::toNativeSeparators(targetPath)});
    process.start();
    return process.waitForFinished(5000) && process.exitCode() == 0
        && QFileInfo(junctionPath).isDir();
}

bool markHiddenAndSystem(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    return SetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()),
                              FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)
        != FALSE;
}

class JunctionGuard final {
public:
    explicit JunctionGuard(QString path) : path_(std::move(path)) {}
    ~JunctionGuard()
    {
        if (!path_.isEmpty()) {
            RemoveDirectoryW(reinterpret_cast<LPCWSTR>(path_.utf16()));
        }
    }

private:
    QString path_;
};
#endif

class ControlledDirectoryOps final : public NativeOutputFileOps {
public:
    int renameCalls = 0;
    bool allowRename = true;
    bool allowCleanup = true;
    bool reportCleanupFailureAfterRemoval = false;

    bool renameDirectory(const QString& source,
                         const QString& destination) override
    {
        ++renameCalls;
        return allowRename
            && NativeOutputFileOps::renameDirectory(source, destination);
    }

    bool removeFile(const QString& path) override
    {
        if (!allowCleanup) return false;
        const bool removed = NativeOutputFileOps::removeFile(path);
        return reportCleanupFailureAfterRemoval ? false : removed;
    }

    bool removeEmptyDirectory(const QString& path) override
    {
        if (!allowCleanup) return false;
        const bool removed = NativeOutputFileOps::removeEmptyDirectory(path);
        return reportCleanupFailureAfterRemoval ? false : removed;
    }
};

} // namespace

class SeparationOutputTransactionTest final : public QObject {
    Q_OBJECT

private slots:
    void commitPublishesAllStemsWithOneDirectoryRename();
    void commitUsesLocalizedStemLabelsAndVisibleModelName();
    void localizedNamesRejectUnsafeWindowsComponents();
    void successiveJobsUseAutoNumberedFinalDirectories();
    void publishedOutputReportsPendingReservationCleanupAndCanRetry();
    void renameFailureLeavesNoPublishedDirectory();
    void rollbackFailurePreservesCauseAndOnlyExistingPaths();
    void cleanupRescanNeverReportsAPathThatWasActuallyRemoved();
    void destructorReportsRollbackFailureInsteadOfDiscardingIt();
    void recoveryCleansOnlyReservedFlatTemporaryJobDirectories();
    void rootJunctionIsRejectedWithoutTouchingItsTarget();
    void nestedJunctionFailsClosedWithoutTouchingItsTarget();
    void nestedDirectoryFailsClosedWithoutTraversal();
    void reservationWithoutTemporaryDirectoryIsRecovered();
    void reservedDirectoryWithoutLegacyMarkerIsRecovered();
    void staleReservationAfterCommitNeverDeletesTheFinalDirectory();
    void liveTransactionPreventsRecovery();
    void cancellationAfterVerificationNeverPublishesAJobDirectory();
    void verificationFailureCleansTheTemporaryJobDirectory();
    void unicodeAndLongPathsStayOnTheOutputVolume();
};

void SeparationOutputTransactionTest::commitPublishesAllStemsWithOneDirectoryRename()
{
    QTemporaryDir output;
    auto operations = std::make_shared<ControlledDirectoryOps>();
    OutputTransaction transaction(
        {output.path(), QStringLiteral("song"), QStringLiteral("wav"),
         {QStringLiteral("vocals"), QStringLiteral("instrumental")}},
        operations);
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));

    CancellationToken cancellation;
    const TransactionResult committed = transaction.commit(
        [](const QString& path) { return QFileInfo(path).size() == 5; },
        cancellation);

    QVERIFY2(committed.ok, qPrintable(committed.message));
    QCOMPARE(operations->renameCalls, 1);
    QCOMPARE(committed.outputs.size(), 2);
    const QString finalDirectory = QFileInfo(committed.outputs.front()).absolutePath();
    QCOMPARE(finalDirectory, output.filePath(QStringLiteral("song")));
    for (const QString& path : committed.outputs) {
        QCOMPARE(QFileInfo(path).absolutePath(), finalDirectory);
        QVERIFY(QFileInfo::exists(path));
    }
    QCOMPARE(QDir(output.path()).entryList(
                 {QStringLiteral(".agplayer-separation-reservation-*.json")},
                 QDir::Files | QDir::Hidden | QDir::System),
             QStringList{});
    QCOMPARE(QDir(finalDirectory).entryList(
                 QDir::Files | QDir::Hidden | QDir::System, QDir::Name),
             QStringList({QStringLiteral("song-instrumental.wav"),
                          QStringLiteral("song-vocals.wav")}));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::commitUsesLocalizedStemLabelsAndVisibleModelName()
{
    QTemporaryDir output;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("陈百强  偏偏喜欢你"), QStringLiteral("flac"),
         {QStringLiteral("vocals"), QStringLiteral("instrumental"),
          QStringLiteral("drums"), QStringLiteral("bass"), QStringLiteral("other")},
         QStringLiteral("HTDemucs FT FP16"),
         {QStringLiteral("人声"), QStringLiteral("伴奏"), QStringLiteral("鼓组"),
          QStringLiteral("贝斯"), QStringLiteral("其他")},
         QStringLiteral("陈百强  偏偏喜欢你-five-stem")});
    QVERIFY(transaction.begin().ok);
    for (const QString& stem : {QStringLiteral("vocals"),
                                QStringLiteral("instrumental"),
                                QStringLiteral("drums"), QStringLiteral("bass"),
                                QStringLiteral("other")}) {
        QVERIFY(writePayload(transaction.temporaryPath(stem)));
    }

    CancellationToken cancellation;
    const TransactionResult committed = transaction.commit(
        [](const QString&) { return true; }, cancellation);

    QVERIFY2(committed.ok, qPrintable(committed.message));
    QStringList expected{
        QStringLiteral("陈百强  偏偏喜欢你-人声-HTDemucs FT FP16.flac"),
        QStringLiteral("陈百强  偏偏喜欢你-伴奏-HTDemucs FT FP16.flac"),
        QStringLiteral("陈百强  偏偏喜欢你-鼓组-HTDemucs FT FP16.flac"),
        QStringLiteral("陈百强  偏偏喜欢你-贝斯-HTDemucs FT FP16.flac"),
        QStringLiteral("陈百强  偏偏喜欢你-其他-HTDemucs FT FP16.flac")};
    expected.sort();
    QCOMPARE(QFileInfo(committed.outputs.front()).absolutePath(),
             output.filePath(QStringLiteral("陈百强  偏偏喜欢你-five-stem")));
    QCOMPARE(QDir(QFileInfo(committed.outputs.front()).absolutePath()).entryList(
                 QDir::Files | QDir::NoDotAndDotDot, QDir::Name),
             expected);
}

void SeparationOutputTransactionTest::localizedNamesRejectUnsafeWindowsComponents()
{
    QTemporaryDir output;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("song"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}, QStringLiteral("MDX: unsafe"),
         {QStringLiteral("Vocals")}});
    const TransactionResult begun = transaction.begin();
    QVERIFY(!begun.ok);
    QCOMPARE(begun.code, QStringLiteral("invalid_output_plan"));
}

void SeparationOutputTransactionTest::successiveJobsUseAutoNumberedFinalDirectories()
{
    QTemporaryDir output;
    const auto run = [&](const QByteArray& payload) {
        OutputTransaction transaction(
            {output.path(), QStringLiteral("song"), QStringLiteral("wav"),
             {QStringLiteral("vocals")}, QStringLiteral("MDX Inst HQ 3"),
             {QStringLiteral("Vocals")}, QStringLiteral("song-two-stem")});
        const TransactionResult begun = transaction.begin();
        if (!begun.ok) return begun;
        if (!writePayload(transaction.temporaryPath(QStringLiteral("vocals")),
                          payload)) {
            return TransactionResult{false, QStringLiteral("test_write_failed"),
                                     QStringLiteral("Could not stage test output")};
        }
        CancellationToken cancellation;
        return transaction.commit([](const QString&) { return true; },
                                  cancellation);
    };

    const TransactionResult first = run(QByteArrayLiteral("first"));
    QVERIFY2(first.ok, qPrintable(first.message));
    QCOMPARE(QFileInfo(first.outputs.front()).absolutePath(),
             output.filePath(QStringLiteral("song-two-stem")));
    const TransactionResult second = run(QByteArrayLiteral("second"));
    QVERIFY2(second.ok, qPrintable(second.message));
    QCOMPARE(QFileInfo(second.outputs.front()).absolutePath(),
             output.filePath(QStringLiteral("song-two-stem-2")));
    QVERIFY(QFileInfo::exists(first.outputs.front()));
    QVERIFY(QFileInfo::exists(second.outputs.front()));
}

void SeparationOutputTransactionTest::renameFailureLeavesNoPublishedDirectory()
{
    QTemporaryDir output;
    auto operations = std::make_shared<ControlledDirectoryOps>();
    operations->allowRename = false;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("failed"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}},
        operations);
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    CancellationToken cancellation;

    const TransactionResult result = transaction.commit(
        [](const QString&) { return true; }, cancellation);

    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("commit_failed"));
    QVERIFY(result.outputs.isEmpty());
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
    QVERIFY(!QFileInfo::exists(output.filePath(QStringLiteral("failed"))));
}

void SeparationOutputTransactionTest::rollbackFailurePreservesCauseAndOnlyExistingPaths()
{
    QTemporaryDir output;
    auto operations = std::make_shared<ControlledDirectoryOps>();
    operations->allowRename = false;
    operations->allowCleanup = false;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("locked"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}},
        operations);
    QVERIFY(transaction.begin().ok);
    const QString temporaryDirectory = transaction.temporaryDirectory();
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    CancellationToken cancellation;

    const TransactionResult result = transaction.commit(
        [](const QString&) { return true; }, cancellation);

    QVERIFY(!result.ok);
    QCOMPARE(result.code, QStringLiteral("rollback_failed"));
    QCOMPARE(result.causeCode, QStringLiteral("commit_failed"));
    QCOMPARE(result.causeMessage, QStringLiteral("Output job directory rename failed"));
    QVERIFY(result.outputs.contains(temporaryDirectory));
    for (const QString& path : result.outputs) QVERIFY(QFileInfo::exists(path));

    operations->allowCleanup = true;
    QVERIFY(transaction.cancel().ok);
}

void SeparationOutputTransactionTest::publishedOutputReportsPendingReservationCleanupAndCanRetry()
{
    QTemporaryDir output;
    auto operations = std::make_shared<ControlledDirectoryOps>();
    OutputTransaction transaction(
        {output.path(), QStringLiteral("published"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}},
        operations);
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    operations->allowCleanup = false;
    CancellationToken cancellation;

    const TransactionResult result = transaction.commit(
        [](const QString&) { return true; }, cancellation);

    QVERIFY(result.ok);
    QCOMPARE(result.code, QStringLiteral("cleanup_pending"));
    QCOMPARE(result.outputs.size(), 1);
    QVERIFY(QFileInfo::exists(result.outputs.front()));
    QCOMPARE(QDir(output.path()).entryList(
                 {QStringLiteral(".agplayer-separation-reservation-*.json")},
                 QDir::Files | QDir::Hidden | QDir::System).size(),
             1);

    operations->allowCleanup = true;
    QVERIFY(transaction.cancel().ok);
    QCOMPARE(QDir(output.path()).entryList(
                 {QStringLiteral(".agplayer-separation-reservation-*.json")},
                 QDir::Files | QDir::Hidden | QDir::System).size(),
             0);
    QVERIFY(QFileInfo::exists(result.outputs.front()));
}

void SeparationOutputTransactionTest::cleanupRescanNeverReportsAPathThatWasActuallyRemoved()
{
    QTemporaryDir output;
    auto operations = std::make_shared<ControlledDirectoryOps>();
    operations->allowRename = false;
    operations->reportCleanupFailureAfterRemoval = true;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("rescan"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}},
        operations);
    QVERIFY(transaction.begin().ok);
    const QString temporaryDirectory = transaction.temporaryDirectory();
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    CancellationToken cancellation;

    const TransactionResult result = transaction.commit(
        [](const QString&) { return true; }, cancellation);

    QCOMPARE(result.code, QStringLiteral("commit_failed"));
    QVERIFY(result.outputs.isEmpty());
    QVERIFY(!QFileInfo::exists(temporaryDirectory));
}

void SeparationOutputTransactionTest::destructorReportsRollbackFailureInsteadOfDiscardingIt()
{
    QTemporaryDir output;
    auto operations = std::make_shared<ControlledDirectoryOps>();
    operations->allowCleanup = false;
    QString temporaryDirectory;
    QTest::ignoreMessage(
        QtCriticalMsg,
        QRegularExpression(QStringLiteral(
            "^Separation output rollback failed; remaining paths:.*")));
    {
        OutputTransaction transaction(
            {output.path(), QStringLiteral("destructor"), QStringLiteral("wav"),
             {QStringLiteral("vocals")}},
            operations);
        QVERIFY(transaction.begin().ok);
        temporaryDirectory = transaction.temporaryDirectory();
        QCOMPARE(transaction.cancel().code, QStringLiteral("rollback_failed"));
    }
    QVERIFY(QFileInfo::exists(temporaryDirectory));
    operations->allowCleanup = true;
    QVERIFY(operations->removeEmptyDirectory(temporaryDirectory));
    const QStringList reservations = QDir(output.path()).entryList(
        {QStringLiteral(".agplayer-separation-reservation-*.json")},
        QDir::Files | QDir::Hidden | QDir::System);
    QCOMPARE(reservations.size(), 1);
    QVERIFY(operations->removeFile(output.filePath(reservations.front())));
}

void SeparationOutputTransactionTest::recoveryCleansOnlyReservedFlatTemporaryJobDirectories()
{
    QTemporaryDir output;
    const ReservedJob owned = createReservedJob(
        output.path(), QStringLiteral("11111111111111111111111111111111"), true);
    QVERIFY(!owned.reservationPath.isEmpty());
    const QString foreign = output.filePath(
        QStringLiteral(".agplayer-separation-job-foreign"));
    const QString finalDirectory = output.filePath(QStringLiteral("song"));
    QVERIFY(QDir().mkpath(foreign));
    QVERIFY(QDir().mkpath(finalDirectory));
    QVERIFY(writePayload(QDir(owned.temporaryPath).filePath(
        QStringLiteral("partial.wav"))));
#ifdef Q_OS_WIN
    const QString hiddenSystem = QDir(owned.temporaryPath).filePath(
        QStringLiteral("hidden-system.tmp"));
    QVERIFY(writePayload(hiddenSystem));
    QVERIFY(markHiddenAndSystem(hiddenSystem));
#endif
    QVERIFY(writePayload(QDir(foreign).filePath(QStringLiteral("keep.wav"))));
    QVERIFY(writePayload(QDir(finalDirectory).filePath(QStringLiteral("keep.wav"))));

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY2(begun.ok, qPrintable(begun.message));
    QVERIFY(!QFileInfo::exists(owned.temporaryPath));
    QVERIFY(!QFileInfo::exists(owned.reservationPath));
    QVERIFY(QFileInfo::exists(foreign));
    QVERIFY(QFileInfo::exists(QDir(finalDirectory).filePath(
        QStringLiteral("keep.wav"))));
    QVERIFY(transaction.cancel().ok);
}

void SeparationOutputTransactionTest::rootJunctionIsRejectedWithoutTouchingItsTarget()
{
#ifndef Q_OS_WIN
    QSKIP("NTFS junction coverage is Windows-only", "");
#else
    QTemporaryDir holder;
    QTemporaryDir external;
    QVERIFY(holder.isValid());
    QVERIFY(external.isValid());
    const QString sentinel = external.filePath(QStringLiteral("sentinel.txt"));
    QVERIFY(writePayload(sentinel, QByteArrayLiteral("outside")));
    const QString junction = holder.filePath(QStringLiteral("output-junction"));
    if (!createJunction(junction, external.path())) {
        QSKIP("This environment cannot create an NTFS directory junction", "");
    }
    JunctionGuard guard(junction);

    OutputTransaction transaction(
        {junction, QStringLiteral("unsafe"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY(!begun.ok);
    QCOMPARE(begun.code, QStringLiteral("unsafe_output_root"));
    QVERIFY(QFileInfo::exists(sentinel));
#endif
}

void SeparationOutputTransactionTest::nestedJunctionFailsClosedWithoutTouchingItsTarget()
{
#ifndef Q_OS_WIN
    QSKIP("NTFS junction coverage is Windows-only", "");
#else
    QTemporaryDir output;
    QTemporaryDir external;
    const ReservedJob stale = createReservedJob(
        output.path(), QStringLiteral("22222222222222222222222222222222"), true);
    QVERIFY(!stale.reservationPath.isEmpty());
    const QString sentinel = external.filePath(QStringLiteral("sentinel.txt"));
    QVERIFY(writePayload(sentinel, QByteArrayLiteral("outside")));
    const QString junction = QDir(stale.temporaryPath).filePath(
        QStringLiteral("nested-junction"));
    if (!createJunction(junction, external.path())) {
        QSKIP("This environment cannot create an NTFS directory junction", "");
    }
    JunctionGuard guard(junction);

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY(!begun.ok);
    QCOMPARE(begun.code, QStringLiteral("recovery_failed"));
    QVERIFY(begun.outputs.contains(junction));
    QVERIFY(QFileInfo::exists(sentinel));
    QVERIFY(QFileInfo::exists(stale.temporaryPath));
    QVERIFY(QFileInfo::exists(stale.reservationPath));
#endif
}

void SeparationOutputTransactionTest::nestedDirectoryFailsClosedWithoutTraversal()
{
    QTemporaryDir output;
    const ReservedJob stale = createReservedJob(
        output.path(), QStringLiteral("33333333333333333333333333333333"), true);
    QVERIFY(!stale.reservationPath.isEmpty());
    const QString nested = QDir(stale.temporaryPath).filePath(
        QStringLiteral("unexpected-directory"));
    QVERIFY(QDir().mkdir(nested));
    const QString sentinel = QDir(nested).filePath(QStringLiteral("keep.txt"));
    QVERIFY(writePayload(sentinel));

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY(!begun.ok);
    QCOMPARE(begun.code, QStringLiteral("recovery_failed"));
    QVERIFY(begun.outputs.contains(nested));
    QVERIFY(QFileInfo::exists(sentinel));
}

void SeparationOutputTransactionTest::reservationWithoutTemporaryDirectoryIsRecovered()
{
    QTemporaryDir output;
    const ReservedJob stale = createReservedJob(
        output.path(), QStringLiteral("44444444444444444444444444444444"), false);
    QVERIFY(!stale.reservationPath.isEmpty());
    QVERIFY(!QFileInfo::exists(stale.temporaryPath));

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY2(begun.ok, qPrintable(begun.message));
    QVERIFY(!QFileInfo::exists(stale.reservationPath));
    QVERIFY(transaction.cancel().ok);
}

void SeparationOutputTransactionTest::reservedDirectoryWithoutLegacyMarkerIsRecovered()
{
    QTemporaryDir output;
    const ReservedJob stale = createReservedJob(
        output.path(), QStringLiteral("55555555555555555555555555555555"), true);
    QVERIFY(!stale.reservationPath.isEmpty());
    QVERIFY(QDir(stale.temporaryPath).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                .isEmpty());

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY2(begun.ok, qPrintable(begun.message));
    QVERIFY(!QFileInfo::exists(stale.temporaryPath));
    QVERIFY(!QFileInfo::exists(stale.reservationPath));
    QVERIFY(transaction.cancel().ok);
}

void SeparationOutputTransactionTest::staleReservationAfterCommitNeverDeletesTheFinalDirectory()
{
    QTemporaryDir output;
    const ReservedJob stale = createReservedJob(
        output.path(), QStringLiteral("66666666666666666666666666666666"), false);
    QVERIFY(!stale.reservationPath.isEmpty());
    const QString finalDirectory = output.filePath(QStringLiteral("committed"));
    QVERIFY(QDir().mkdir(finalDirectory));
    const QString sentinel = QDir(finalDirectory).filePath(
        QStringLiteral("committed-vocals.wav"));
    QVERIFY(writePayload(sentinel));

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY2(begun.ok, qPrintable(begun.message));
    QVERIFY(!QFileInfo::exists(stale.reservationPath));
    QVERIFY(QFileInfo::exists(sentinel));
    QVERIFY(transaction.cancel().ok);
}

void SeparationOutputTransactionTest::liveTransactionPreventsRecovery()
{
    QTemporaryDir output;
    OutputTransaction active(
        {output.path(), QStringLiteral("active"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    QVERIFY(active.begin().ok);
    const QString activeDirectory = active.temporaryDirectory();

    OutputTransaction contender(
        {output.path(), QStringLiteral("contender"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult blocked = contender.begin();
    QVERIFY(!blocked.ok);
    QCOMPARE(blocked.code, QStringLiteral("transaction_lock_failed"));
    QVERIFY(QFileInfo::exists(activeDirectory));

    QVERIFY(active.cancel().ok);
    QVERIFY(contender.begin().ok);
    QVERIFY(contender.cancel().ok);
}

void SeparationOutputTransactionTest::cancellationAfterVerificationNeverPublishesAJobDirectory()
{
    QTemporaryDir output;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("cancel-window"), QStringLiteral("wav"),
         {QStringLiteral("vocals"), QStringLiteral("instrumental")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));

    CancellationToken cancellation;
    int verified = 0;
    const TransactionResult committed = transaction.commit(
        [&](const QString&) {
            if (++verified == 2) cancellation.cancel();
            return true;
        },
        cancellation);

    QVERIFY(!committed.ok);
    QCOMPARE(committed.code, QStringLiteral("cancelled"));
    QVERIFY(!QFileInfo::exists(output.filePath(
        QStringLiteral("cancel-window"))));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::verificationFailureCleansTheTemporaryJobDirectory()
{
    QTemporaryDir output;
    OutputTransaction transaction(
        {output.path(), QStringLiteral("bad"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    CancellationToken cancellation;

    const TransactionResult failed = transaction.commit(
        [](const QString&) { return false; }, cancellation);

    QCOMPARE(failed.code, QStringLiteral("verification_failed"));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
    QVERIFY(!QFileInfo::exists(output.filePath(QStringLiteral("bad"))));
}

void SeparationOutputTransactionTest::unicodeAndLongPathsStayOnTheOutputVolume()
{
    QTemporaryDir output;
    QString nested = output.path();
    for (int index = 0; index < 8; ++index) {
        nested = QDir(nested).filePath(
            QStringLiteral("很长的音频输出目录%1").arg(index));
    }
    QVERIFY(QDir().mkpath(nested));
    OutputTransaction transaction(
        {nested, QStringLiteral("歌曲 文件"), QStringLiteral("flac"),
         {QStringLiteral("人声")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(QFileInfo(transaction.temporaryDirectory()).absolutePath().startsWith(
        QFileInfo(nested).absoluteFilePath(), Qt::CaseInsensitive));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("人声"))));
    CancellationToken cancellation;
    const TransactionResult committed = transaction.commit(
        [](const QString&) { return true; }, cancellation);
    QVERIFY(committed.ok);
    QCOMPARE(QFileInfo(committed.outputs.front()).absolutePath(),
             QDir(nested).filePath(QStringLiteral("歌曲 文件")));
}

QTEST_GUILESS_MAIN(SeparationOutputTransactionTest)
#include "separation_output_transaction_test.moc"
