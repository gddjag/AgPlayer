#include "filename_transform_engine.hpp"

#include <QRegularExpression>

namespace agplayer::qt {
namespace {

struct NameParts { QString stem; QString extension; };

NameParts splitName(const QString& name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1) return {name, {}};
    return {name.left(dot), name.mid(dot + 1)};
}

QString convertCase(QString value, CaseRule rule)
{
    if (rule == CaseRule::Lower) return value.toLower();
    if (rule == CaseRule::Upper) return value.toUpper();
    if (rule != CaseRule::Title) return value;
    bool upper = true;
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar current = value.at(index);
        if (current.isLetter()) {
            value[index] = upper ? current.toUpper() : current.toLower();
            upper = false;
        } else if (current.isSpace() || current == QLatin1Char('_')
                   || current == QLatin1Char('-')) {
            upper = true;
        }
    }
    return value;
}

QString ordinalText(const FilenameRuleSet& rules, int ordinal)
{
    const int digits = qBound(1, rules.numberDigits, 12);
    return QStringLiteral("%1").arg(rules.numberStart + ordinal, digits, 10,
                                     QLatin1Char('0'));
}

QString removeKnownPrefix(QString stem, const FilenameRuleSet& rules)
{
    if (!rules.removePrefixWhenEmpty) return stem;
    static const QRegularExpression tagAtStart(
        QStringLiteral("^\\s*(?:\\[[^\\]]+\\]|【[^】]+】|\\([^)]*\\)|（[^）]*）)\\s*[_\\- ]*"));
    stem.remove(tagAtStart);
    if (rules.removeSequenceWhenEmpty) {
        static const QRegularExpression numberAtStart(
            QStringLiteral("^\\s*\\d{1,6}\\s*(?:[_\\-. ]+)\\s*"));
        stem.remove(numberAtStart);
    }
    return stem;
}

QString removeKnownSuffix(QString stem, const FilenameRuleSet& rules)
{
    if (!rules.removeSuffixWhenEmpty) return stem;
    static const QRegularExpression tagAtEnd(
        QStringLiteral("\\s*[_\\- ]*(?:\\[[^\\]]+\\]|【[^】]+】|\\([^)]*\\)|（[^）]*）)\\s*$"));
    stem.remove(tagAtEnd);
    static const QRegularExpression commonTail(
        QStringLiteral("\\s*[_\\- ]+(?:remaster(?:ed)?|demo|live|mix|radio edit|extended|instrumental|acoustic|version)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    stem.remove(commonTail);
    if (rules.removeSequenceWhenEmpty) {
        static const QRegularExpression numberAtEnd(
            QStringLiteral("\\s*[_\\- .]+\\d{1,6}\\s*$"));
        stem.remove(numberAtEnd);
    }
    return stem;
}

} // namespace

QString FilenameTransformEngine::transform(const QString& sourceFileName,
                                           const FilenameRuleSet& rules,
                                           int ordinal)
{
    const NameParts parts = splitName(sourceFileName);
    QString stem = parts.stem;
    if (rules.prefix.isEmpty()) stem = removeKnownPrefix(stem, rules);
    if (rules.suffix.isEmpty()) stem = removeKnownSuffix(stem, rules);
    if (rules.replaceSpaces) {
        stem.replace(QRegularExpression(QStringLiteral(" +")),
                     rules.spaceReplacement);
    }
    stem = convertCase(stem, rules.caseRule);
    QString prefix = rules.prefix;
    QString suffix = rules.suffix;
    const QString number = rules.autoNumber ? ordinalText(rules, ordinal) : QString();
    switch (rules.numberPosition) {
    case NumberPosition::Beginning:
        stem = number.isEmpty() ? stem : number + rules.numberSeparator + stem;
        break;
    case NumberPosition::AfterPrefix:
        if (!number.isEmpty()) prefix += number + rules.numberSeparator;
        break;
    case NumberPosition::BeforeSuffix:
        if (!number.isEmpty()) suffix = rules.numberSeparator + number + suffix;
        break;
    case NumberPosition::AfterSuffix:
        if (!number.isEmpty()) suffix += rules.numberSeparator + number;
        break;
    }
    QString extension = parts.extension;
    if (!rules.preserveExtension && !extension.isEmpty()) {
        extension = convertCase(extension, rules.caseRule);
    }
    const QString transformed = prefix + stem + suffix;
    return extension.isEmpty() ? transformed
        : transformed + QLatin1Char('.') + extension;
}

} // namespace agplayer::qt
