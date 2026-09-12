#include "filename_validator.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace agplayer::qt {
namespace {

FilenameValidationIssue error(QString code, QString message)
{
    return {RenameSeverity::Error, std::move(code), std::move(message)};
}

} // namespace

FilenameValidationIssue FilenameValidator::validateFileName(const QString& fileName)
{
    if (fileName.isEmpty() || fileName == QLatin1String(".")
        || fileName == QLatin1String("..")) {
        return error(QStringLiteral("empty-name"), QStringLiteral("文件名不能为空"));
    }
    if (fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))) {
        return error(QStringLiteral("path-separator"), QStringLiteral("文件名不能包含路径分隔符"));
    }
    if (fileName.endsWith(QLatin1Char(' ')) || fileName.endsWith(QLatin1Char('.'))) {
        return error(QStringLiteral("trailing-space-or-dot"), QStringLiteral("Windows 文件名不能以空格或句点结尾"));
    }
    static const QString invalid = QStringLiteral("<>:\"|?*");
    for (const QChar character : fileName) {
        if (character.unicode() < 0x20) {
            return error(QStringLiteral("control-character"), QStringLiteral("文件名包含控制字符"));
        }
        if (invalid.contains(character)) {
            return error(QStringLiteral("windows-invalid-character"), QStringLiteral("文件名包含 Windows 非法字符"));
        }
    }
    const QString base = fileName.section(QLatin1Char('.'), 0, 0);
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(base).hasMatch()) {
        return error(QStringLiteral("windows-reserved-name"), QStringLiteral("文件名使用 Windows 保留设备名称"));
    }
    if (fileName.size() > 255) {
        return error(QStringLiteral("component-too-long"), QStringLiteral("文件名超过文件系统组件长度限制"));
    }
    return {};
}

FilenameValidationIssue FilenameValidator::validateTarget(const QString& sourcePath,
                                                          const QString& targetPath)
{
    const QFileInfo source(sourcePath);
    const QFileInfo target(targetPath);
    if (source.absolutePath() != target.absolutePath()) {
        return error(QStringLiteral("cross-directory-target"), QStringLiteral("目标必须位于源文件所在目录"));
    }
    if (source.isSymLink() || target.isSymLink()) {
        return error(QStringLiteral("symbolic-link"), QStringLiteral("不支持安全重命名符号链接"));
    }
    return validateFileName(target.fileName());
}

QString FilenameValidator::collisionKey(const QString& absolutePath)
{
    const QString normalized = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath())
        .normalized(QString::NormalizationForm_C);
#ifdef Q_OS_WIN
    return normalized.toCaseFolded();
#else
    return normalized;
#endif
}

} // namespace agplayer::qt
