#pragma once

#include "library_model.hpp"

#include <QObject>
#include <QTimer>

class LibraryStore final : public QObject {
    Q_OBJECT

public:
    explicit LibraryStore(QString filePath, QObject* parent = nullptr);
    ~LibraryStore() override;

    bool save(const QList<TrackRecord>& tracks) const;
    QList<TrackRecord> load() const;
    void requestSave(QList<TrackRecord> tracks);
    bool flush();

signals:
    void saveFinished(bool success);

private:
    QString filePath_;
    QTimer saveTimer_;
    QList<TrackRecord> pendingTracks_;
    bool hasPendingSave_ = false;
};
