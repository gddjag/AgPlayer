#include "rename_plan.hpp"
#include "rename_journal_store.hpp"
#include "rename_transaction.hpp"
#include "filename_processor.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>

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

QString overwriteBackup(const QTemporaryDir& directory)
{
    const QStringList backups = QDir(directory.path()).entryList(
        {u".agplayer-overwrite-*.bak"_s}, QDir::Files | QDir::Hidden);
    return backups.size() == 1
        ? directory.filePath(backups.first())
        : QString();
}
}

class RenameTransactionTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void renamesSwapAndKeepsSha256();
    void refusesUndoWhenTargetWasExternallyChanged();
    void overwriteBacksUpTargetAndUndoRestoresBothFiles();
    void rejectsOverwriteTargetChangedWhileBeingBackedUp();
    void rejectsSourceChangedWhileBeingStaged();
    void restoresSourceWhenPostMoveIntegrityCheckFails();
    void rollsBackWhenCommittedJournalCannotBePersisted();
    void discardingUndoRemovesOverwriteBackupWithoutChangingCommittedFile();
    void refusesToDiscardChangedOverwriteBackup();
    void filenameProcessorKeepsUndoWhenBackupCleanupFails();
    void filenameProcessorDestructorRemovesLastOverwriteBackup();
    void filenameProcessorRunsUndoOutsideTheGuiThread();
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

void RenameTransactionTest::rejectsOverwriteTargetChangedWhileBeingBackedUp()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.flac"_s);
    const QString target = directory.filePath(u"target.flac"_s);
    writeFile(source, QByteArrayLiteral("source-audio"));
    writeFile(target, QByteArrayLiteral("existing-audio"));

    RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, hashFile(source), {}, 12}}, {target});
    plan.items[0].action = RenameAction::Overwrite;
    RenameTransaction executor(
        [](QStringView point, const QString& path) {
            if (point == u"after-overwrite-backup") {
                writeFile(path, QByteArrayLiteral("external-change"));
            }
        });

    const RenameTransactionResult result = executor.execute(plan);

    QVERIFY(!result.committed);
    QCOMPARE(result.errorCode, u"overwrite-target-changed"_s);
    QCOMPARE(hashFile(source), plan.items[0].sha256);
    QCOMPARE(hashFile(target), QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("external-change"), QCryptographicHash::Sha256).toHex()));
}

void RenameTransactionTest::rejectsSourceChangedWhileBeingStaged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.flac"_s);
    const QString target = directory.filePath(u"target.flac"_s);
    writeFile(source, QByteArrayLiteral("source-audio"));

    const RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, hashFile(source), {}, 12}}, {target});
    RenameTransaction executor(
        [](QStringView point, const QString& path) {
            if (point == u"after-source-stage") {
                writeFile(path, QByteArrayLiteral("external-change"));
            }
        });

    const RenameTransactionResult result = executor.execute(plan);

    QVERIFY(!result.committed);
    QCOMPARE(result.errorCode, u"source-changed"_s);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(target));
}

void RenameTransactionTest::restoresSourceWhenPostMoveIntegrityCheckFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.wav"_s);
    const QString target = directory.filePath(u"target.wav"_s);
    writeFile(source, QByteArrayLiteral("source-audio"));

    const RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, hashFile(source), {}, 12}}, {target});
    RenameTransaction executor(
        [](QStringView point, const QString& path) {
            if (point == u"after-target-move") {
                writeFile(path, QByteArrayLiteral("external-change"));
            }
        });

    const RenameTransactionResult result = executor.execute(plan);

    QVERIFY(!result.committed);
    QCOMPARE(result.errorCode, u"commit-failed"_s);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(target));
}

void RenameTransactionTest::rollsBackWhenCommittedJournalCannotBePersisted()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.ogg"_s);
    const QString target = directory.filePath(u"target.ogg"_s);
    writeFile(source, QByteArrayLiteral("source-audio"));
    const QString sourceHash = hashFile(source);

    const RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, sourceHash, {}, 12}}, {target});
    RenameTransaction executor(
        [](QStringView point, const QString& path) {
            if (point == u"before-journal-finalize") {
                QVERIFY(QFile::remove(path));
            }
        });

    const RenameTransactionResult result = executor.execute(plan);

    QVERIFY(!result.committed);
    QCOMPARE(result.errorCode, u"journal-finalize-failed"_s);
    QCOMPARE(hashFile(source), sourceHash);
    QVERIFY(!QFileInfo::exists(target));
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

void RenameTransactionTest::refusesToDiscardChangedOverwriteBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"source.wav"_s);
    const QString target = directory.filePath(u"target.wav"_s);
    writeFile(source, QByteArrayLiteral("new-audio"));
    writeFile(target, QByteArrayLiteral("old-audio"));

    RenamePlan plan = RenameTransaction::makePlanForTests(
        {{0, source, hashFile(source), {}, 9}}, {target});
    plan.items[0].action = RenameAction::Overwrite;
    const RenameTransactionResult committed = RenameTransaction().execute(plan);
    QVERIFY(committed.committed);
    const QString backup = committed.undoRecord.overwriteBackups.first().backupPath;
    writeFile(backup, QByteArrayLiteral("external-change"));

    QVERIFY(!RenameTransaction::discardUndo(committed.undoRecord));
    QVERIFY(QFileInfo::exists(backup));
}

void RenameTransactionTest::filenameProcessorKeepsUndoWhenBackupCleanupFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"track.wav"_s);
    const QString target = directory.filePath(u"P-track.wav"_s);
    writeFile(source, QByteArrayLiteral("new-audio"));
    writeFile(target, QByteArrayLiteral("old-audio"));

    FilenameProcessor processor;
    QSignalSpy loaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(source)});
    QVERIFY(loaded.wait(30'000));
    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply({{u"prefix"_s, u"P-"_s}}, {}, u"overwrite"_s);
    QVERIFY(renamed.wait(30'000));
    QVERIFY(processor.canUndo());

    const QString backup = overwriteBackup(directory);
    QVERIFY(!backup.isEmpty());
    writeFile(backup, QByteArrayLiteral("external-change"));
    QSignalSpy errors(&processor, &FilenameProcessor::errorOccurred);

    processor.clear();

    QCOMPARE(processor.fileCount(), 1);
    QVERIFY(processor.canUndo());
    QCOMPARE(errors.size(), 1);
    QVERIFY(QFileInfo::exists(backup));
}

void RenameTransactionTest::filenameProcessorDestructorRemovesLastOverwriteBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"track.wav"_s);
    const QString target = directory.filePath(u"P-track.wav"_s);
    writeFile(source, QByteArrayLiteral("new-audio"));
    writeFile(target, QByteArrayLiteral("old-audio"));
    QString backup;
    {
        FilenameProcessor processor;
        QSignalSpy loaded(&processor, &FilenameProcessor::entriesLoaded);
        processor.loadFiles({QUrl::fromLocalFile(source)});
        QVERIFY(loaded.wait(30'000));
        QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
        processor.apply({{u"prefix"_s, u"P-"_s}}, {}, u"overwrite"_s);
        QVERIFY(renamed.wait(30'000));
        backup = overwriteBackup(directory);
        QVERIFY(!backup.isEmpty());
        QVERIFY(QFileInfo::exists(backup));
    }

    QVERIFY(!QFileInfo::exists(backup));
}

void RenameTransactionTest::filenameProcessorRunsUndoOutsideTheGuiThread()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(u"track.wav"_s);
    const QString target = directory.filePath(u"P-track.wav"_s);
    writeFile(source, QByteArrayLiteral("audio-data"));

    FilenameProcessor processor;
    QSignalSpy loaded(&processor, &FilenameProcessor::entriesLoaded);
    processor.loadFiles({QUrl::fromLocalFile(source)});
    QVERIFY(loaded.wait(30'000));
    QSignalSpy renamed(&processor, &FilenameProcessor::renameApplied);
    processor.apply({{u"prefix"_s, u"P-"_s}});
    QVERIFY(renamed.wait(30'000));
    QVERIFY(processor.canUndo());

    QSignalSpy undone(&processor, &FilenameProcessor::undoCompleted);
    processor.undoLast();

    QVERIFY(processor.busy());
    QVERIFY(undone.wait(30'000));
    QCOMPARE(undone.first().at(0).toInt(), 1);
    QCOMPARE(undone.first().at(1).toInt(), 0);
    QVERIFY(!processor.busy());
    QVERIFY(!processor.canUndo());
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(target));
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
