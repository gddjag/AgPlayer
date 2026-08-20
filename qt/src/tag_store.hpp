#pragma once

#include <QColor>
#include <QList>
#include <QString>

struct TagEntry {
    QString key;
    QString displayName;
    int trackCount = 0;
    QColor color;
};

class TagStore final {
public:
    explicit TagStore(QString filePath = {});

    QList<TagEntry> load() const;
    bool save(const QList<TagEntry>& entries) const;

private:
    QString filePath_;
};
