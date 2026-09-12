#include "filename_transform_engine.hpp"
#include "filename_validator.hpp"
#include "rename_plan.hpp"

#include <QFile>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace agplayer::qt;

class FilenameProcessingTest final : public QObject {
    Q_OBJECT

private slots:
    void transformsStemWithoutChangingExtension();
    void blankAffixesRemoveRecognizableAffixesAndSequence();
    void removesExplicitLiteralAffixesAndEdgeSequenceOnly();
    void appliesRemovalAdditionAndNumberingInSpecifiedOrder();
    void keepsHiddenFilesAndExtensionlessNamesWellDefined();
    void reportsUnsafeWindowsNames();
    void plansInternalCollisionsInImportOrder();
    void treatsNoOpSourceAsAnOccupiedTarget();
    void propagatesStationaryOccupancyFromSkippedSources();
    void autoNumbersTargetsOccupiedByStationarySources();
    void refusesToOverwriteStationaryBatchSources();
    void refusesDuplicateOverwriteTargets();
    void automaticNumberingAndSequenceRemovalAreMutuallyExclusive();
};

void FilenameProcessingTest::transformsStemWithoutChangingExtension()
{
    FilenameRuleSet rules;
    rules.prefix = u"[Live]_"_s;
    rules.suffix = u"_Remaster"_s;
    rules.replaceSpaces = true;
    rules.spaceReplacement = u"_"_s;
    rules.autoNumber = true;
    rules.numberStart = 1;
    rules.numberDigits = 2;
    rules.numberPosition = NumberPosition::AfterPrefix;
    rules.numberSeparator = u"_"_s;

    QCOMPARE(FilenameTransformEngine::transform(u"Neon City.flac"_s, rules, 0),
             u"[Live]_01_Neon_City_Remaster.flac"_s);
}

void FilenameProcessingTest::blankAffixesRemoveRecognizableAffixesAndSequence()
{
    FilenameRuleSet rules;
    rules.removePrefixWhenEmpty = true;
    rules.removeSuffixWhenEmpty = true;
    rules.removeSequenceWhenEmpty = true;

    QCOMPARE(FilenameTransformEngine::transform(
                 u"[Live]_01_Neon City_Remaster.flac"_s, rules, 0),
             u"Neon City.flac"_s);
    QCOMPARE(FilenameTransformEngine::transform(
                 u"003 - 东京之夜 - Demo.wav"_s, rules, 0),
             u"东京之夜.wav"_s);
}

void FilenameProcessingTest::removesExplicitLiteralAffixesAndEdgeSequenceOnly()
{
    FilenameRuleSet rules;
    rules.removePrefix = u"DJ-"_s;
    rules.removeSuffix = u"-Promo"_s;
    rules.removeSequenceAtStart = true;
    rules.removePrefixWhenEmpty = false;
    rules.removeSuffixWhenEmpty = false;
    rules.removeSequenceWhenEmpty = false;

    QCOMPARE(FilenameTransformEngine::transform(
                 u"007 - DJ-Sunrise-Promo.mp3"_s, rules, 0),
             u"Sunrise.mp3"_s);
    QCOMPARE(FilenameTransformEngine::transform(
                 u"DJ-007 Sunrise-Promo.mp3"_s, rules, 0),
             u"007 Sunrise.mp3"_s);

    rules.removeSequenceAtStart = false;
    rules.removeSequenceAtEnd = true;
    QCOMPARE(FilenameTransformEngine::transform(
                 u"DJ-Sunrise-Promo - 09.flac"_s, rules, 0),
             u"Sunrise.flac"_s);
}

void FilenameProcessingTest::automaticNumberingAndSequenceRemovalAreMutuallyExclusive()
{
    FilenameRuleSet rules;
    rules.autoNumber = true;
    rules.removeSequenceWhenEmpty = true;
    rules.numberPosition = NumberPosition::AfterSuffix;

    QCOMPARE(FilenameTransformEngine::transform(u"01 Sunrise.flac"_s, rules, 0),
             u"01 Sunrise_01.flac"_s);
}

void FilenameProcessingTest::appliesRemovalAdditionAndNumberingInSpecifiedOrder()
{
    FilenameRuleSet rules;
    rules.removeSequenceAtStart = true;
    rules.removeSequenceAtEnd = true;
    rules.removePrefix = u"OLD-"_s;
    rules.removeSuffix = u"-Tail"_s;
    rules.removePrefixWhenEmpty = false;
    rules.removeSuffixWhenEmpty = false;
    rules.removeSequenceWhenEmpty = false;
    rules.prefix = u"NEW-"_s;
    rules.suffix = u"-DONE"_s;
    rules.autoNumber = false;
    rules.numberStart = 5;
    rules.numberDigits = 2;
    rules.numberPosition = NumberPosition::AfterSuffix;
    rules.numberSeparator = u"_"_s;

    QCOMPARE(FilenameTransformEngine::transform(
                 u"007 - OLD-Song-Tail - 09.flac"_s, rules, 0),
             u"NEW-Song-DONE.flac"_s);
}

void FilenameProcessingTest::keepsHiddenFilesAndExtensionlessNamesWellDefined()
{
    FilenameRuleSet rules;
    rules.prefix = u"新_"_s;
    rules.suffix = u"_完成"_s;

    QCOMPARE(FilenameTransformEngine::transform(u".env"_s, rules, 0),
             u"新_.env_完成"_s);
    QCOMPARE(FilenameTransformEngine::transform(u"README"_s, rules, 0),
             u"新_README_完成"_s);
}

void FilenameProcessingTest::reportsUnsafeWindowsNames()
{
    const FilenameValidationIssue reserved =
        FilenameValidator::validateFileName(u"CON.mp3"_s);
    QCOMPARE(reserved.severity, RenameSeverity::Error);
    QCOMPARE(reserved.code, u"windows-reserved-name"_s);

    const FilenameValidationIssue separator =
        FilenameValidator::validateFileName(u"album/song.mp3"_s);
    QCOMPARE(separator.severity, RenameSeverity::Error);
    QCOMPARE(separator.code, u"path-separator"_s);
}

void FilenameProcessingTest::plansInternalCollisionsInImportOrder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QList<RenameSource> sources{
        {0, directory.filePath(u"A B.flac"_s), {}, {}, 0},
        {1, directory.filePath(u"A  B.flac"_s), {}, {}, 0}};
    FilenameRuleSet rules;
    rules.replaceSpaces = true;
    rules.spaceReplacement = u"_"_s;
    const RenamePlan plan = RenamePlanner::build(sources, rules,
                                                  ConflictPolicy::AutoNumber);
    QCOMPARE(plan.items.at(0).proposedFileName, u"A_B.flac"_s);
    QCOMPARE(plan.items.at(1).proposedFileName, u"A_B_2.flac"_s);
}

void FilenameProcessingTest::treatsNoOpSourceAsAnOccupiedTarget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString stationaryPath = directory.filePath(u"a.wav"_s);
    const QString changingPath = directory.filePath(u"[Live]_a.wav"_s);
    for (const QString& path : {stationaryPath, changingPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("audio") > 0);
    }

    const QList<RenameSource> sources{
        {0, stationaryPath, {}, {}, 5},
        {1, changingPath, {}, {}, 5}};
    FilenameRuleSet rules;
    const RenamePlan plan = RenamePlanner::build(
        sources, rules, ConflictPolicy::StopBatch);

    QCOMPARE(plan.items.at(0).action, RenameAction::NoOp);
    QCOMPARE(plan.items.at(1).action, RenameAction::Skip);
    QCOMPARE(plan.items.at(1).reasonCode, u"target-conflict"_s);
    QVERIFY(!plan.executable);
}

void FilenameProcessingTest::propagatesStationaryOccupancyFromSkippedSources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(u"a.wav"_s);
    const QString secondPath = directory.filePath(u"x-a.wav"_s);
    const QString externalPath = directory.filePath(u"x-x-a.wav"_s);
    for (const QString& path : {firstPath, secondPath, externalPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("audio") > 0);
    }

    const QList<RenameSource> sources{
        {0, firstPath, {}, {}, 5},
        {1, secondPath, {}, {}, 5}};
    FilenameRuleSet rules;
    rules.prefix = u"x-"_s;
    const RenamePlan plan = RenamePlanner::build(
        sources, rules, ConflictPolicy::Skip);

    QCOMPARE(plan.items.at(0).action, RenameAction::Skip);
    QCOMPARE(plan.items.at(1).action, RenameAction::Skip);
    QVERIFY(!plan.executable);
}

void FilenameProcessingTest::autoNumbersTargetsOccupiedByStationarySources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString stationaryPath = directory.filePath(u"a.wav"_s);
    const QString changingPath = directory.filePath(u"[Live]_a.wav"_s);
    for (const QString& path : {stationaryPath, changingPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("audio") > 0);
    }

    FilenameRuleSet rules;
    const RenamePlan plan = RenamePlanner::build(
        {{0, stationaryPath, {}, {}, 5},
         {1, changingPath, {}, {}, 5}},
        rules, ConflictPolicy::AutoNumber);

    QCOMPARE(plan.items.at(0).action, RenameAction::NoOp);
    QCOMPARE(plan.items.at(1).action, RenameAction::Rename);
    QCOMPARE(plan.items.at(1).proposedFileName, u"a_2.wav"_s);
    QCOMPARE(plan.items.at(1).reasonCode, u"auto-numbered"_s);
    QVERIFY(plan.executable);
}

void FilenameProcessingTest::refusesToOverwriteStationaryBatchSources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString stationaryPath = directory.filePath(u"a.wav"_s);
    const QString changingPath = directory.filePath(u"[Live]_a.wav"_s);
    for (const QString& path : {stationaryPath, changingPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("audio") > 0);
    }

    FilenameRuleSet rules;
    const RenamePlan plan = RenamePlanner::build(
        {{0, stationaryPath, {}, {}, 5},
         {1, changingPath, {}, {}, 5}},
        rules, ConflictPolicy::Overwrite);

    QCOMPARE(plan.items.at(0).action, RenameAction::NoOp);
    QCOMPARE(plan.items.at(1).action, RenameAction::Skip);
    QCOMPARE(plan.items.at(1).reasonCode, u"target-conflict"_s);
    QVERIFY(!plan.executable);
}

void FilenameProcessingTest::refusesDuplicateOverwriteTargets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(u"A B.flac"_s);
    const QString secondPath = directory.filePath(u"A  B.flac"_s);
    const QString targetPath = directory.filePath(u"A_B.flac"_s);
    for (const QString& path : {firstPath, secondPath, targetPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("audio") > 0);
    }

    FilenameRuleSet rules;
    rules.replaceSpaces = true;
    rules.spaceReplacement = u"_"_s;
    const RenamePlan plan = RenamePlanner::build(
        {{0, firstPath, {}, {}, 5},
         {1, secondPath, {}, {}, 5}},
        rules, ConflictPolicy::Overwrite);

    QCOMPARE(plan.items.at(0).action, RenameAction::Overwrite);
    QCOMPARE(plan.items.at(1).action, RenameAction::Skip);
    QCOMPARE(plan.items.at(1).reasonCode, u"target-conflict"_s);
    QVERIFY(plan.executable);
}

QTEST_GUILESS_MAIN(FilenameProcessingTest)

#include "filename_processing_test.moc"
