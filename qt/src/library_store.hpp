#pragma once

#include "library_model.hpp"

#include <QObject>
#include <QTimer>

class LibraryStore final : public QObject {
    Q_OBJECT

public:
    explicit LibraryStore(QString filePath, QObject* parent = nullptr);

    bool save(const QList<TrackRecord>& tracks) const;
    QList<TrackRecord> load() const;
    void requestSave(QList<TrackRecord> tracks);

signals:
    void saveFinished(bool success);

private:
    QString filePath_;
    QTimer saveTimer_;
    QList<TrackRecord> pendingTracks_;
};
