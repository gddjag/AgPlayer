#pragma once

#include <QString>

namespace agplayer::qt {

enum class RenameSeverity { Ready, Warning, Error };

struct FilenameValidationIssue {
    RenameSeverity severity = RenameSeverity::Ready;
    QString code;
    QString message;
};

class FilenameValidator final {
public:
    static FilenameValidationIssue validateFileName(const QString& fileName);
    static FilenameValidationIssue validateTarget(const QString& sourcePath,
                                                  const QString& targetPath);
    static QString collisionKey(const QString& absolutePath);
};

} // namespace agplayer::qt
