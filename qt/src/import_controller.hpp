#pragma once

#include "agplayer/c_api.h"
#include "library_model.hpp"

#include <QFuture>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>

struct ImportCallbackState;

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
    ~ImportController() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QStringList errors() const;
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls);
    void cancel();

signals:
    void progressChanged();
    void busyChanged();
    void errorsChanged();
    void finished();

private:
    void finishWithoutImport(const QString& error);
    void handleResult(const QString& path, ProbeResult result, int completed, int total);

    QPointer<LibraryModel> model_;
    ProbeFunction probe_;
    std::shared_ptr<ImportCallbackState> callbackState_;
    QFuture<void> future_;
    std::atomic_bool cancelled_{false};
    double progress_ = 0.0;
    bool busy_ = false;
    QStringList errors_;
};
