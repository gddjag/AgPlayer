#pragma once

#include <QString>

namespace agplayer::qt {

enum class CaseRule { Keep, Lower, Upper, Title };
enum class NumberPosition { Beginning, AfterPrefix, BeforeSuffix, AfterSuffix };

struct FilenameRuleSet {
    QString prefix;
    QString suffix;
    QString spaceReplacement = QStringLiteral("_");
    QString numberSeparator = QStringLiteral("_");
    bool replaceSpaces = false;
    bool autoNumber = false;
    bool preserveExtension = true;
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
