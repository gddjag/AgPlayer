#include "rename_plan.hpp"
#include "rename_journal_store.hpp"
#include "rename_transaction.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace agplayer::qt;

namespace {
QString hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(64 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}
void writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
}
}

class RenameTransactionTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void renamesSwapAndKeepsSha256();
    void refusesUndoWhenTargetWasExternallyChanged();
    void overwriteBacksUpTargetAndUndoRestoresBothFiles();
    void discardingUndoRemovesOverwriteBackupWithoutChangingCommittedFile();
    void persistsPreparedAndCommittedJournal();
    void recoversPreparedTransactionFromStageFile();
    void recoversPreparedTransactionAfterTargetCommit();
};

void RenameTransactionTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void RenameTransactionTest::renamesSwapAndKeepsSha256()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(u"first.mp3"_s);
    const QString second = directory.filePath(u"second.mp3"_s);
    writeFile(first, QByteArrayLiteral("first"));
    writeFile(second, QByteArrayLiteral("second"));
    const RenamePlan plan = RenameTransaction::makePlanForTests({
        {0, first, hashFile(first), {}, 5}, {1, second, hashFile(second), {}, 6}},
        {second, first});
    RenameTransaction executor;
    const RenameTransactionResult result = executor.execute(plan);
    QVERIFY(result.committed);
    QCOMPARE(hashFile(first), QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("second"), QCryptographicHash::Sha256).toHex()));
    QCOMPARE(hashFile(second), QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("first"), QCryptographicHash::Sha256).toHex()));
}

void RenameTransactionTest::refusesUndoWhenTargetWasExternallyChanged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString original = directory.filePath(u"original.flac"_s);
    const QString renamed = directory.filePath(u"renamed.flac"_s);
    writeFile(original, QByteArrayLiteral("original"));
    RenameTransaction executor;
    const RenameTransactionResult committed = executor.execute(
        RenameTransaction::makePlanForTests({{0, original, hashFile(original), {}, 8}}, {renamed}));
    QVERIFY(committed.committed);
    writeFile(renamed, QByteArrayLiteral("external"));
    const RenameTransactionResult undo = executor.undo(committed.undoRecord);
    QVERIFY(!undo.committed);
    QCOMPARE(undo.errorCode, u"undo-target-changed"_s);
}

void RenameTransactionTest::overwriteBacksUpTargetAndUndoRestoresBothFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.flac"_s);
    const QString target = directory.filePath(u"target.flac"_s);
    writeFile(source, QByteArrayLiteral("source-audio"));
    writeFile(target, QByteArrayLiteral("existing-audio"));
    const QString sourceHash = hashFile(source);
    const QString targetHash = hashFile(target);

    RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, sourceHash, {}, 12}}, {target});
    plan.items[0].action = RenameAction::Overwrite;
    RenameTransaction executor;
    const RenameTransactionResult committed = executor.execute(plan);
    QVERIFY2(committed.committed, qPrintable(committed.errorText));
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(hashFile(target), sourceHash);

    const RenameTransactionResult undone = executor.undo(committed.undoRecord);
    QVERIFY2(undone.committed, qPrintable(undone.errorText));
    QCOMPARE(hashFile(source), sourceHash);
    QCOMPARE(hashFile(target), targetHash);
}

void RenameTransactionTest::
    discardingUndoRemovesOverwriteBackupWithoutChangingCommittedFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.wav"_s);
    const QString target = directory.filePath(u"target.wav"_s);
    writeFile(source, QByteArrayLiteral("new-audio"));
    writeFile(target, QByteArrayLiteral("old-audio"));
    const QString sourceHash = hashFile(source);

    RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, sourceHash, {}, 9}}, {target});
    plan.items[0].action = RenameAction::Overwrite;
    const RenameTransactionResult committed = RenameTransaction().execute(plan);
    QVERIFY(committed.committed);
    QCOMPARE(committed.undoRecord.overwriteBackups.size(), 1);
    const QString backup = committed.undoRecord.overwriteBackups.first().backupPath;
    QVERIFY(QFileInfo::exists(backup));

    QVERIFY(RenameTransaction::discardUndo(committed.undoRecord));
    QVERIFY(!QFileInfo::exists(backup));
    QCOMPARE(hashFile(target), sourceHash);
}

void RenameTransactionTest::persistsPreparedAndCommittedJournal()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.ogg"_s);
    const QString target = directory.filePath(u"target.ogg"_s);
    writeFile(source, QByteArrayLiteral("journal"));
    RenameTransaction executor;
    const RenameTransactionResult result = executor.execute(
        RenameTransaction::makePlanForTests({{0, source, hashFile(source), {}, 7}}, {target}));
    QVERIFY(result.committed);
    QVERIFY(QFileInfo::exists(result.journalPath));
    QFile journal(result.journalPath);
    QVERIFY(journal.open(QIODevice::ReadOnly));
    QVERIFY(journal.readAll().contains("Committed"));
}

void RenameTransactionTest::recoversPreparedTransactionFromStageFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.aac"_s);
    const QString target = directory.filePath(u"target.aac"_s);
    writeFile(source, QByteArrayLiteral("recover-stage"));
    const QString transaction = u"recover-stage-test"_s;
    const RenamePlan plan = RenameTransaction::makePlanForTests(
        {{7, source, hashFile(source), {}, 13}}, {target});
    const QString journal = RenameJournalStore::writePrepared(transaction, plan);
    QVERIFY(!journal.isEmpty());
    const QString stage = directory.filePath(
        u".agplayer-rename-%1-7.tmp"_s.arg(transaction));
    QVERIFY(QFile::rename(source, stage));

    const RenameRecoveryReport report = RenameJournalStore::recoverIncomplete();
    QVERIFY(report.recovered >= 1);
    QCOMPARE(report.failed, 0);
    QCOMPARE(hashFile(source), plan.items.first().sha256);
    QVERIFY(!QFileInfo::exists(stage));
    QVERIFY(!QFileInfo::exists(target));
    QFile journalFile(journal);
    QVERIFY(journalFile.open(QIODevice::ReadOnly));
    QVERIFY(journalFile.readAll().contains("RecoveredAtStartup"));
}

void RenameTransactionTest::recoversPreparedTransactionAfterTargetCommit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"before.opus"_s);
    const QString target = directory.filePath(u"after.opus"_s);
    writeFile(source, QByteArrayLiteral("recover-commit"));
    const QString transaction = u"recover-commit-test"_s;
    const RenamePlan plan = RenameTransaction::makePlanForTests(
        {{9, source, hashFile(source), {}, 14}}, {target});
    const QString journal = RenameJournalStore::writePrepared(transaction, plan);
    QVERIFY(!journal.isEmpty());
    QVERIFY(QFile::rename(source, target));

    const RenameRecoveryReport report = RenameJournalStore::recoverIncomplete();
    QVERIFY(report.recovered >= 1);
    QCOMPARE(report.failed, 0);
    QCOMPARE(hashFile(source), plan.items.first().sha256);
    QVERIFY(!QFileInfo::exists(target));
}

QTEST_GUILESS_MAIN(RenameTransactionTest)

#include "rename_transaction_test.moc"
