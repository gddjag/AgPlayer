#include "vocal_separation_history.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class VocalSeparationHistoryTest final : public QObject {
    Q_OBJECT

private slots:
    void corruptStorageRecoversWithoutDeletingOutputs();
    void retainsOnlyTheNewestFiveHundredRecords();
    void marksMissingUnicodeOutputsUnavailable();
    void appendAtomicallyReplacesValidJson();
    void rejectsOversizedOrNonFileHistoryInput();
    void marksAReparseResultUnavailable();
};

namespace {

QVariantMap record(const QString& id, const QString& outputPath)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("inputPath"), QStringLiteral("C:/input.wav")},
            {QStringLiteral("stems"), QVariantList{QVariantMap{
                 {QStringLiteral("kind"), 1},
                 {QStringLiteral("path"), outputPath}}}}};
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

} // namespace

void VocalSeparationHistoryTest::corruptStorageRecoversWithoutDeletingOutputs()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString output = temporary.filePath(QStringLiteral("人声.wav"));
    QVERIFY(writeBytes(output, QByteArrayLiteral("audio")));
    const QString historyPath = temporary.filePath(QStringLiteral("history.json"));
    QVERIFY(writeBytes(historyPath, QByteArrayLiteral("{broken")));

    VocalSeparationHistoryStore store(historyPath);
    QCOMPARE(store.load().size(), 0);
    QVERIFY(QFileInfo::exists(output));
    QVERIFY(store.append(record(QStringLiteral("after-corrupt"), output)));
    QCOMPARE(store.load().size(), 1);
}

void VocalSeparationHistoryTest::retainsOnlyTheNewestFiveHundredRecords()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("history.json"));
    QJsonArray fixture;
    for (int index = 0; index < 501; ++index) {
        fixture.push_back(QJsonObject::fromVariantMap(record(
            QStringLiteral("record-%1").arg(index),
            temporary.filePath(QStringLiteral("output-%1.wav").arg(index)))));
    }
    QVERIFY(writeBytes(path, QJsonDocument(fixture).toJson(QJsonDocument::Compact)));
    VocalSeparationHistoryStore store(path);
    QVariantList loaded = store.load();
    QCOMPARE(loaded.size(), 500);
    QCOMPARE(loaded.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("record-1"));
    QCOMPARE(loaded.last().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("record-500"));
    QVERIFY(store.append(record(QStringLiteral("record-501"),
                                QStringLiteral("C:/missing.wav"))));
    loaded = store.load();
    QCOMPARE(loaded.size(), 500);
    QCOMPARE(loaded.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("record-2"));
}

void VocalSeparationHistoryTest::marksMissingUnicodeOutputsUnavailable()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString output = QDir(temporary.path()).filePath(
        QStringLiteral("很长的目录名-测试/伴奏-歌曲.wav"));
    QVERIFY(QDir().mkpath(QFileInfo(output).absolutePath()));
    QVERIFY(writeBytes(output, QByteArrayLiteral("audio")));
    VocalSeparationHistoryStore store(
        temporary.filePath(QStringLiteral("历史.json")));
    QVERIFY(store.append(record(QStringLiteral("unicode"), output)));
    QVariantMap stem = store.load().first().toMap()
                           .value(QStringLiteral("stems")).toList().first().toMap();
    QCOMPARE(stem.value(QStringLiteral("path")).toString(), output);
    QVERIFY(stem.value(QStringLiteral("available")).toBool());

    QVERIFY(QFile::remove(output));
    stem = store.load().first().toMap()
               .value(QStringLiteral("stems")).toList().first().toMap();
    QVERIFY(!stem.value(QStringLiteral("available")).toBool());
}

void VocalSeparationHistoryTest::appendAtomicallyReplacesValidJson()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString historyPath = temporary.filePath(QStringLiteral("history.json"));
    VocalSeparationHistoryStore store(historyPath);
    QVERIFY(store.append(record(QStringLiteral("first"), QStringLiteral("C:/missing.wav"))));
    QVERIFY(store.append(record(QStringLiteral("second"), QStringLiteral("C:/missing.wav"))));

    QFile file(historyPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(document.array().size(), 2);
    QCOMPARE(QDir(temporary.path()).entryList(
                 {QStringLiteral("history.json.*")}, QDir::Files),
             QStringList{});
}

void VocalSeparationHistoryTest::rejectsOversizedOrNonFileHistoryInput()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString oversized = temporary.filePath(QStringLiteral("oversized.json"));
    QByteArray oversizedJson("[");
    while (oversizedJson.size() < 2 * 1024 * 1024) {
        oversizedJson += QByteArrayLiteral("{\"id\":\"record\"},");
    }
    oversizedJson += QByteArrayLiteral("{}]");
    QVERIFY(writeBytes(oversized, oversizedJson));
    QCOMPARE(VocalSeparationHistoryStore(oversized).load().size(), 0);

    const QString directory = temporary.filePath(QStringLiteral("history-directory"));
    QVERIFY(QDir().mkpath(directory));
    QCOMPARE(VocalSeparationHistoryStore(directory).load().size(), 0);
}

void VocalSeparationHistoryTest::marksAReparseResultUnavailable()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reparse-point coverage");
#else
    QTemporaryDir temporary;
    QTemporaryDir external;
    QVERIFY(temporary.isValid());
    QVERIFY(external.isValid());
    const QString outside = external.filePath(QStringLiteral("outside.wav"));
    QVERIFY(writeBytes(outside, QByteArrayLiteral("audio")));
    const QString junction = temporary.filePath(QStringLiteral("result-link"));
    QProcess process;
    process.start(QStringLiteral("cmd.exe"),
                  {QStringLiteral("/d"), QStringLiteral("/c"),
                   QStringLiteral("mklink"), QStringLiteral("/J"),
                   QDir::toNativeSeparators(junction),
                   QDir::toNativeSeparators(external.path())});
    if (!process.waitForFinished(5'000) || process.exitCode() != 0)
        QSKIP("This Windows environment cannot create an NTFS junction");
    const QString history = temporary.filePath(QStringLiteral("history.json"));
    VocalSeparationHistoryStore store(history);
    QVERIFY(store.append(record(
        QStringLiteral("linked"), QDir(junction).filePath(QStringLiteral("outside.wav")))));
    const QVariantList loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    QVERIFY(!loaded.first().toMap().value(QStringLiteral("stems")).toList()
                 .first().toMap().value(QStringLiteral("available")).toBool());
    const QString native = QDir::toNativeSeparators(junction);
    RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()));
#endif
}

QTEST_GUILESS_MAIN(VocalSeparationHistoryTest)
#include "vocal_separation_history_test.moc"
