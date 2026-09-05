#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>

struct LosslessReportWriteResult final {
    bool success = false;
    QString error;
};

class LosslessReport final {
public:
    using CancelCheck = std::function<bool()>;
    static LosslessReportWriteResult write(
        const QString& destination,
        const QString& format,
        const QVariantList& results,
        const QStringList& sourcePaths,
        const CancelCheck& cancelled = {});
};
