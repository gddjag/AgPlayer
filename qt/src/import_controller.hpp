#pragma once

#include "agplayer/c_api.h"
#include "library_model.hpp"

#include <QFuture>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

struct ProbeResult {
    ag_result result = AG_INTERNAL_ERROR;
    TrackRecord track;
    QString error;
};

using ProbeFunction = std::function<ProbeResult(const QString&)>;

class ImportController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QStringList errors READ errors NOTIFY errorsChanged)

public:
    explicit ImportController(LibraryModel* model, QObject* parent = nullptr);
    ImportController(LibraryModel* model, ProbeFunction probe, QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    QStringList errors() const;
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls);

signals:
    void progressChanged();
    void busyChanged();
    void errorsChanged();
    void finished();

private:
    void handleResult(const QString& path, ProbeResult result, int completed, int total);

    LibraryModel* model_ = nullptr;
    ProbeFunction probe_;
    QFuture<void> future_;
    double progress_ = 0.0;
    bool busy_ = false;
    QStringList errors_;
};
