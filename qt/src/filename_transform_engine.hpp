#pragma once

#include <QString>

namespace agplayer::qt {

enum class CaseRule { Keep, Lower, Upper, Title };
enum class NumberPosition { Beginning, AfterPrefix, BeforeSuffix, AfterSuffix };

struct FilenameRuleSet {
    QString prefix;
    QString suffix;
    QString removePrefix;
    QString removeSuffix;
    QString spaceReplacement = QStringLiteral("_");
    QString numberSeparator = QStringLiteral("_");
    bool replaceSpaces = false;
    bool autoNumber = false;
    bool preserveExtension = true;
    // A blank editable affix means "remove a recognizable existing affix".
    // This keeps batch editing reversible without guessing at file contents.
    bool removePrefixWhenEmpty = true;
    bool removeSuffixWhenEmpty = true;
    bool removeSequenceWhenEmpty = false;
    bool removeSequenceAtStart = false;
    bool removeSequenceAtEnd = false;
    int numberStart = 1;
    int numberDigits = 2;
    CaseRule caseRule = CaseRule::Keep;
    NumberPosition numberPosition = NumberPosition::AfterPrefix;
};

class FilenameTransformEngine final {
public:
    static QString transform(const QString& sourceFileName,
                             const FilenameRuleSet& rules, int ordinal);
};

} // namespace agplayer::qt
