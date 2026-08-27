#include "output_transaction.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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

} // namespace

class SeparationOutputTransactionTest final : public QObject {
    Q_OBJECT

private slots:
    void commitsAllVerifiedFilesAtomicallyAfterPreflight();
    void noOverwritePreservesExistingOutputs();
    void renameFailureRollsBackAllCommittedAndTemporaryFiles();
    void cancelAndVerificationFailureCleanTemporaryFiles();
    void unicodeAndLongPathsStayOnTheOutputVolume();
};

void SeparationOutputTransactionTest::commitsAllVerifiedFilesAtomicallyAfterPreflight()
{
    QTemporaryDir temporary;
    OutputTransaction transaction({temporary.path(), QStringLiteral("song"),
                                   QStringLiteral("wav"),
                                   {QStringLiteral("vocals"), QStringLiteral("instrumental")}});
    QVERIFY(transaction.begin().ok);
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("vocals"))));
    QVERIFY(writePayload(transaction.temporaryPath(QStringLiteral("instrumental"))));

    const TransactionResult committed = transaction.commit(
        [](const QString& path) { return QFileInfo(path).size() == 5; });
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
    const TransactionResult committed = transaction.commit(
        [](const QString&) { return true; });
    QVERIFY(!committed.ok);
    QCOMPARE(committed.code, QStringLiteral("commit_failed"));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("disk-error-vocals.wav"))));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("disk-error-instrumental.wav"))));
    QVERIFY(!QFileInfo::exists(transaction.temporaryDirectory()));
}

void SeparationOutputTransactionTest::cancelAndVerificationFailureCleanTemporaryFiles()
{
    QTemporaryDir temporary;
    OutputTransaction failed({temporary.path(), QStringLiteral("bad"), QStringLiteral("wav"),
                              {QStringLiteral("vocals")}});
    QVERIFY(failed.begin().ok);
    QVERIFY(writePayload(failed.temporaryPath(QStringLiteral("vocals"))));
    QCOMPARE(failed.commit([](const QString&) { return false; }).code,
             QStringLiteral("verification_failed"));
    QVERIFY(!QFileInfo::exists(failed.temporaryDirectory()));
    QVERIFY(!QFileInfo::exists(QDir(temporary.path()).filePath(QStringLiteral("bad-vocals.wav"))));

    OutputTransaction cancelled({temporary.path(), QStringLiteral("cancelled"),
                                 QStringLiteral("wav"), {QStringLiteral("vocals")}});
    QVERIFY(cancelled.begin().ok);
    QVERIFY(writePayload(cancelled.temporaryPath(QStringLiteral("vocals"))));
    cancelled.cancel();
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
    QVERIFY(transaction.commit([](const QString&) { return true; }).ok);
    QVERIFY(QFileInfo::exists(QDir(nested).filePath(QStringLiteral("歌曲 文件-人声.flac"))));
}

QTEST_GUILESS_MAIN(SeparationOutputTransactionTest)
#include "separation_output_transaction_test.moc"
