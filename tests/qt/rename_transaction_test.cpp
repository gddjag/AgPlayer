#include "rename_plan.hpp"
#include "rename_transaction.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
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
    void renamesSwapAndKeepsSha256();
    void refusesUndoWhenTargetWasExternallyChanged();
    void persistsPreparedAndCommittedJournal();
};

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

QTEST_GUILESS_MAIN(RenameTransactionTest)

#include "rename_transaction_test.moc"
