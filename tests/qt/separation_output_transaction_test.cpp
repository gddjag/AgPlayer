#include "output_transaction.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace agplayer::separation;

namespace {

bool writePayload(const QString& path, const QByteArray& bytes = QByteArrayLiteral("audio"))
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

class FailingRenameOps final : public NativeOutputFileOps {
public:
    int renameCalls = 0;
    int failAt = 2;

    bool renameFile(const QString& source, const QString& destination) override
    {
        ++renameCalls;
        return renameCalls == failAt ? false
                                     : NativeOutputFileOps::renameFile(source, destination);
    }
};

class FailingCleanupOps final : public NativeOutputFileOps {
public:
    int renameCalls = 0;
    bool allowCleanup = false;

    bool renameFile(const QString& source, const QString& destination) override
    {
        return ++renameCalls == 2 ? false
                                  : NativeOutputFileOps::renameFile(source, destination);
    }

    bool removeFile(const QString& path) override
    {
        return allowCleanup && NativeOutputFileOps::removeFile(path);
    }
};

} // namespace

class SeparationOutputTransactionTest final : public QObject {
    Q_OBJECT

private slots:
    void commitsAllVerifiedFilesAfterPreflight();
    void noOverwritePreservesExistingOutputs();
    void renameFailureRollsBackAllCommittedAndTemporaryFiles();
    void rollbackDeleteFailureIsReportedAndCanBeRetried();
    void destructorReportsRollbackFailureInsteadOfDiscardingIt();
    void recoveryNeverTreatsALiveTransactionAsStale();
    void cancellationAfterVerificationNeverPublishesFinalFiles();
    void cancelAndVerificationFailureCleanTemporaryFiles();
    void unicodeAndLongPathsStayOnTheOutputVolume();
};

void SeparationOutputTransactionTest::commitsAllVerifiedFilesAfterPreflight()
{
    QTemporaryDir temporary;
    OutputTransaction transaction({temporary.path(), QStringLiteral("song"),
                                   QStringLiteral("wav"),
                                   {QStringLiteral("vocals"), QStringLiteral("instrumental")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));

    std::atomic_bool cancelled{false};
    const TransactionResult committed = transaction.commit(
        [](const QString& path) { return QFileInfo(path).size() == 5; }, cancelled);
    QVERIFY2(committed.ok, qPrintable(committed.message));
    QCOMPARE(committed.outputs.size(), 2);
    QVERIFY(QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("song-vocals.wav"))));
    QVERIFY(QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("song-instrumental.wav"))));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::noOverwritePreservesExistingOutputs()
{
    QTemporaryDir temporary;
    const QString existing = QDir(temporary.path()).filePath(QStringLiteral("song-vocals.wav"));
    QVERIFY(writePayload(existing, QByteArrayLiteral("keep")));
    OutputTransaction transaction({temporary.path(), QStringLiteral("song"),
                                   QStringLiteral("wav"), {QStringLiteral("vocals")}});
    const TransactionResult begun = transaction.begin();
    QVERIFY(!begun.ok);
    QCOMPARE(begun.code, QStringLiteral("output_exists"));
    QFile file(existing);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("keep"));
}

void SeparationOutputTransactionTest::renameFailureRollsBackAllCommittedAndTemporaryFiles()
{
    QTemporaryDir temporary;
    auto operations = std::make_shared<FailingRenameOps>();
    OutputTransaction transaction({temporary.path(), QStringLiteral("disk-error"),
                                   QStringLiteral("wav"),
                                   {QStringLiteral("vocals"), QStringLiteral("instrumental")}},
                                  operations);
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));
    std::atomic_bool cancelled{false};
    const TransactionResult committed = transaction.commit(
        [](const QString&) { return true; }, cancelled);
    QVERIFY(!committed.ok);
    QCOMPARE(committed.code, QStringLiteral("commit_failed"));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("disk-error-vocals.wav"))));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("disk-error-instrumental.wav"))));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::rollbackDeleteFailureIsReportedAndCanBeRetried()
{
    QTemporaryDir temporary;
    auto operations = std::make_shared<FailingCleanupOps>();
    OutputTransaction transaction({temporary.path(), QStringLiteral("locked"),
                                   QStringLiteral("wav"),
                                   {QStringLiteral("vocals"), QStringLiteral("instrumental")}},
                                  operations);
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));
    std::atomic_bool cancelled{false};

    const TransactionResult committed = transaction.commit(
        [](const QString&) { return true; }, cancelled);
    QVERIFY(!committed.ok);
    QCOMPARE(committed.code, QStringLiteral("rollback_failed"));
    const QString stranded = QDir(temporary.path())
        .filePath(QStringLiteral("locked-vocals.wav"));
    QVERIFY(QFileInfo::exists(stranded));

    const TransactionResult stillLocked = transaction.cancel();
    QVERIFY(!stillLocked.ok);
    QCOMPARE(stillLocked.code, QStringLiteral("rollback_failed"));
    QVERIFY(stillLocked.outputs.contains(stranded));
    QVERIFY(QFileInfo::exists(transaction.temporaryDirectory()));

    operations->allowCleanup = true;
    QVERIFY(transaction.cancel().ok);
    QVERIFY(!QFileInfo::exists(stranded));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::destructorReportsRollbackFailureInsteadOfDiscardingIt()
{
    QTemporaryDir temporary;
    auto operations = std::make_shared<FailingCleanupOps>();
    QString stranded;
    QString stagingDirectory;
    QTest::ignoreMessage(
        QtCriticalMsg,
        QRegularExpression(QStringLiteral(
            "^Separation output rollback failed; remaining paths:.*")));
    {
        OutputTransaction transaction(
            {temporary.path(), QStringLiteral("destructor"), QStringLiteral("wav"),
             {QStringLiteral("vocals"), QStringLiteral("instrumental")}},
            operations);
        QVERIFY(transaction.begin().ok);
        stagingDirectory = transaction.temporaryDirectory();
        QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
        QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));
        std::atomic_bool cancelled{false};
        const TransactionResult committed = transaction.commit(
            [](const QString&) { return true; }, cancelled);
        QCOMPARE(committed.code, QStringLiteral("rollback_failed"));
        stranded = temporary.filePath(QStringLiteral("destructor-vocals.wav"));
    }
    QVERIFY(QFileInfo::exists(stranded));
    operations->allowCleanup = true;
    QVERIFY(operations->removeFile(stranded));
    QVERIFY(QDir(stagingDirectory).removeRecursively());
}

void SeparationOutputTransactionTest::recoveryNeverTreatsALiveTransactionAsStale()
{
    QTemporaryDir temporary;
    OutputTransaction active(
        {temporary.path(), QStringLiteral("active"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    QVERIFY(active.begin().ok);
    const QString activeDirectory = active.temporaryDirectory();
    QVERIFY(writePayload(active.temporaryPath(QStringLiteral("vocals"))));

    OutputTransaction contender(
        {temporary.path(), QStringLiteral("contender"), QStringLiteral("wav"),
         {QStringLiteral("vocals")}});
    const TransactionResult blocked = contender.begin();
    QVERIFY(!blocked.ok);
    QCOMPARE(blocked.code, QStringLiteral("recovery_in_use"));
    QVERIFY(QFileInfo::exists(activeDirectory));

    QVERIFY(active.cancel().ok);
    QVERIFY(contender.begin().ok);
    QVERIFY(contender.cancel().ok);
}

void SeparationOutputTransactionTest::cancellationAfterVerificationNeverPublishesFinalFiles()
{
    QTemporaryDir temporary;
    OutputTransaction transaction({temporary.path(), QStringLiteral("cancel-window"),
                                   QStringLiteral("wav"),
                                   {QStringLiteral("vocals"), QStringLiteral("instrumental")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));

    std::atomic_bool cancelled{false};
    int verified = 0;
    const TransactionResult committed = transaction.commit(
        [&](const QString&) {
            if (++verified == 2) cancelled.store(true);
            return true;
        },
        cancelled);

    QVERIFY(!committed.ok);
    QCOMPARE(committed.code, QStringLiteral("cancelled"));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path())
                                   .filePath(QStringLiteral("cancel-window-vocals.wav"))));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path())
                                   .filePath(QStringLiteral("cancel-window-instrumental.wav"))));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::cancelAndVerificationFailureCleanTemporaryFiles()
{
    QTemporaryDir temporary;
    OutputTransaction failed({temporary.path(), QStringLiteral("bad"), QStringLiteral("wav"),
                              {QStringLiteral("vocals")}});
    QVERIFY(failed.begin().ok);
    QVERIFY(writePayload(failed.temporaryPath(QStringLiteral("vocals"))));
    std::atomic_bool notCancelled{false};
    QCOMPARE(failed.commit([](const QString&) { return false; }, notCancelled).code,
             QStringLiteral("verification_failed"));
    QVERIFY(!QFileInfo::exists(failed.temporaryDirectory()));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("bad-vocals.wav"))));

    OutputTransaction cancelled({temporary.path(), QStringLiteral("cancelled"),
                                 QStringLiteral("wav"), {QStringLiteral("vocals")}});
    QVERIFY(cancelled.begin().ok);
    QVERIFY(writePayload(cancelled.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(cancelled.cancel().ok);
    QVERIFY(!QFileInfo::exists(cancelled.temporaryDirectory()));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("cancelled-vocals.wav"))));
}

void SeparationOutputTransactionTest::unicodeAndLongPathsStayOnTheOutputVolume()
{
    QTemporaryDir temporary;
    QString nested = temporary.path();
    for (int index = 0; index < 8; ++index) {
        nested = QDir(nested).filePath(QStringLiteral("很长的音频输出目录%1").arg(index));
    }
    QVERIFY(QDir().mkpath(nested));
    OutputTransaction transaction({nested, QStringLiteral("歌曲 文件"), QStringLiteral("flac"),
                                   {QStringLiteral("人声")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(QFileInfo(transaction.temporaryDirectory()).absolutePath().startsWith(
        QFileInfo(nested).absoluteFilePath(), Qt::CaseInsensitive));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("人声"))));
    std::atomic_bool cancelled{false};
    QVERIFY(transaction.commit([](const QString&) { return true; }, cancelled).ok);
    QVERIFY(QFileInfo::exists(QDir(nested).filePath(QStringLiteral("歌曲 文件-人声.flac"))));
}

QTEST_GUILESS_MAIN(SeparationOutputTransactionTest)
#include "separation_output_transaction_test.moc"
