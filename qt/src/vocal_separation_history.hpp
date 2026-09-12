#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class VocalSeparationHistoryStore final {
public:
    explicit VocalSeparationHistoryStore(QString filePath);

    QVariantList load() const;
    bool append(const QVariantMap& record);

private:
    bool save(const QVariantList& records) const;

    QString filePath_;
};
