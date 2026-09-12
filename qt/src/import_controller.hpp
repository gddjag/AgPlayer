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
using DiscoveryFunction = std::function<QStringList(const QList<QUrl>&)>;

class ImportController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QStringList errors READ errors NOTIFY errorsChanged)
    Q_PROPERTY(QStringList importedTrackIds READ importedTrackIds
                   NOTIFY importedTrackIdsChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY skippedCountChanged)

public:
    explicit ImportController(LibraryModel* model, QObject* parent = nullptr);
    ImportController(LibraryModel* model, ProbeFunction probe, QObject* parent = nullptr);
    ImportController(LibraryModel* model, ProbeFunction probe,
                     DiscoveryFunction discovery, QObject* parent = nullptr);
    ~ImportController() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QStringList errors() const;
    QStringList importedTrackIds() const;
    int skippedCount() const noexcept;
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls);
    Q_INVOKABLE void importPaths(const QStringList& paths);
    Q_INVOKABLE void importFolder(const QUrl& folder);
    Q_INVOKABLE void clearErrors();
    void cancel();

signals:
    void progressChanged();
    void busyChanged();
    void errorsChanged();
    void importedTrackIdsChanged();
    void skippedCountChanged();
    void finished();

private:
    void finishWithoutImport(const QString& error);
    struct Outcome {
        qsizetype ordinal = 0;
        QString path;
        ProbeResult result;
        bool cachedDuplicate = false;
    };
    void handleBatch(QList<Outcome> outcomes, int completed, int total);
    void completeImport();

    QPointer<LibraryModel> model_;
    ProbeFunction probe_;
    DiscoveryFunction discovery_;
    std::shared_ptr<ImportCallbackState> callbackState_;
    QFuture<void> future_;
    double progress_ = 0.0;
    bool busy_ = false;
    QStringList errors_;
    QStringList importedTrackIds_;
    QSet<QString> importedTrackIdSet_;
    int skippedCount_ = 0;
    QList<QUrl> pendingUrls_;
};

double readEmbeddedBpmTag(const QString& requestedPath);
ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm);
