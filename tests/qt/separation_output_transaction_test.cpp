#include "output_transaction.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace agplayer::separation;

namespace {

constexpr auto kOwnedMarkerName = ".agplayer-separation-owned";
constexpr auto kOwnedMarkerValue = "agplayer-separation-v1";

bool writePayload(const QString& path,
                  const QByteArray& bytes = QByteArrayLiteral("audio"))
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool createOwnedMarker(const QString& directory)
{
    return writePayload(QDir(directory).filePath(
                            QString::fromLatin1(kOwnedMarkerName)),
                        QByteArrayLiteral(kOwnedMarkerValue));
}

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

    bool removeDirectory(const QString& path) override
    {
        if (!allowCleanup) return false;
        const bool removed = NativeOutputFileOps::removeDirectory(path);
        return reportCleanupFailureAfterRemoval ? false : removed;
    }
};

} // namespace

class SeparationOutputTransactionTest final : public QObject {
    Q_OBJECT

private slots:
    void commitPublishesAllStemsWithOneDirectoryRename();
    void successiveJobsUseAutoNumberedFinalDirectories();
    void renameFailureLeavesNoPublishedDirectory();
    void rollbackFailurePreservesCauseAndOnlyExistingPaths();
    void cleanupRescanNeverReportsAPathThatWasActuallyRemoved();
    void destructorReportsRollbackFailureInsteadOfDiscardingIt();
    void recoveryCleansOnlyMarkedTemporaryJobDirectories();
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
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::successiveJobsUseAutoNumberedFinalDirectories()
{
    QTemporaryDir output;
    const auto run = [&](const QByteArray& payload) {
        OutputTransaction transaction(
            {output.path(), QStringLiteral("song"), QStringLiteral("wav"),
             {QStringLiteral("vocals")}});
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
             output.filePath(QStringLiteral("song")));
    const TransactionResult second = run(QByteArrayLiteral("second"));
    QVERIFY2(second.ok, qPrintable(second.message));
    QCOMPARE(QFileInfo(second.outputs.front()).absolutePath(),
             output.filePath(QStringLiteral("song-2")));
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
    QCOMPARE(result.outputs, QStringList{temporaryDirectory});
    QVERIFY(QFileInfo::exists(temporaryDirectory));

    operations->allowCleanup = true;
    QVERIFY(transaction.cancel().ok);
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
    QVERIFY(operations->removeDirectory(temporaryDirectory));
}

void SeparationOutputTransactionTest::recoveryCleansOnlyMarkedTemporaryJobDirectories()
{
    QTemporaryDir output;
    const QString owned = output.filePath(
        QStringLiteral(".agplayer-separation-job-owned"));
    const QString foreign = output.filePath(
        QStringLiteral(".agplayer-separation-job-foreign"));
    const QString finalDirectory = output.filePath(QStringLiteral("song"));
    QVERIFY(QDir().mkpath(owned));
    QVERIFY(QDir().mkpath(foreign));
    QVERIFY(QDir().mkpath(finalDirectory));
    QVERIFY(createOwnedMarker(owned));
    QVERIFY(createOwnedMarker(finalDirectory));
    QVERIFY(writePayload(QDir(owned).filePath(QStringLiteral("partial.wav"))));
    QVERIFY(writePayload(QDir(foreign).filePath(QStringLiteral("keep.wav"))));
    QVERIFY(writePayload(QDir(finalDirectory).filePath(QStringLiteral("keep.wav"))));

    OutputTransaction transaction(
        {output.path(), QStringLiteral("new-job"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();

    QVERIFY2(begun.ok, qPrintable(begun.message));
    QVERIFY(!QFileInfo::exists(owned));
    QVERIFY(QFileInfo::exists(foreign));
    QVERIFY(QFileInfo::exists(QDir(finalDirectory).filePath(
        QStringLiteral("keep.wav"))));
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
