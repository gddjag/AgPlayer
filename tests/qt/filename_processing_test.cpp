#include "filename_transform_engine.hpp"
#include "filename_validator.hpp"
#include "rename_plan.hpp"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace agplayer::qt;

class FilenameProcessingTest final : public QObject {
    Q_OBJECT

private slots:
    void transformsStemWithoutChangingExtension();
    void keepsHiddenFilesAndExtensionlessNamesWellDefined();
    void reportsUnsafeWindowsNames();
    void plansInternalCollisionsInImportOrder();
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

QTEST_GUILESS_MAIN(FilenameProcessingTest)

#include "filename_processing_test.moc"
