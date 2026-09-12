#pragma once

#include <QColor>
#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QString>

#include <functional>

struct TagEntry {
    QString key;
    QString displayName;
    int trackCount = 0;
    QColor color;
};

class TagStore final {
public:
    using WriteFunction =
        std::function<qint64(QIODevice& device, const QByteArray& payload)>;

    explicit TagStore(QString filePath = {}, WriteFunction writer = {});

    QList<TagEntry> load() const;
    bool save(const QList<TagEntry>& entries) const;

private:
    QString filePath_;
    WriteFunction writer_;
};
