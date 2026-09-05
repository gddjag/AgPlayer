#pragma once

#include "../../core/src/lossless/lossless_types.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

class LosslessAnalysisController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* tasks READ tasks CONSTANT)
    Q_PROPERTY(QVariantMap selectedResult READ selectedResult
               NOTIFY selectedResultChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool stopping READ stopping NOTIFY stoppingChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int completedCount READ completedCount
               NOTIFY completedCountChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int selectedCount READ selectedCount
               NOTIFY selectedCountChanged)
    Q_PROPERTY(int concurrency READ concurrency WRITE setConcurrency
               NOTIFY concurrencyChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText
               NOTIFY searchTextChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList counts READ counts NOTIFY countsChanged)

public:
    using AnalyzerFunction = std::function<agplayer::lossless::AnalysisResult(
        const std::string&,
        const agplayer::lossless::AnalysisOptions&,
        const std::atomic_bool&,
        agplayer::lossless::ProgressCallback)>;

    explicit LosslessAnalysisController(QObject* parent = nullptr);
    LosslessAnalysisController(AnalyzerFunction analyzer, QObject* parent);
    ~LosslessAnalysisController() override;

    QAbstractItemModel* tasks() const noexcept;
    QVariantMap selectedResult() const;
    bool running() const noexcept;
    bool stopping() const noexcept;
    double progress() const noexcept;
    int completedCount() const noexcept;
    int totalCount() const noexcept;
    int selectedCount() const noexcept;
    int concurrency() const noexcept;
    QString filter() const;
    QString searchText() const;
    QString statusText() const;
    QString error() const;
    QVariantList counts() const;

    void setConcurrency(int concurrency);
    void setFilter(const QString& filter);
    void setSearchText(const QString& searchText);

    Q_INVOKABLE void loadFiles(const QVariantList& urls);
    Q_INVOKABLE void addFolder(const QUrl& folder);
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retrySelected();
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectTask(const QString& id);
    Q_INVOKABLE void setChecked(const QString& id, bool checked);
    Q_INVOKABLE void selectAll(bool checked);
    Q_INVOKABLE void exportReport(const QUrl& destination,
                                  const QString& format);
    Q_INVOKABLE void requestSpectrogram();
    Q_INVOKABLE void notifyError(const QString& message);
    Q_INVOKABLE void refreshTranslations();

    qint64 cacheBytesForTesting() const noexcept;
    qint64 totalRetainedBytesForTesting() const noexcept;
    qint64 cacheLimitBytesForTesting() const noexcept;
    void setCacheLimitBytesForTesting(qint64 bytes);

signals:
    void selectedResultChanged();
    void runningChanged();
    void stoppingChanged();
    void progressChanged();
    void completedCountChanged();
    void totalCountChanged();
    void selectedCountChanged();
    void concurrencyChanged();
    void filterChanged();
    void searchTextChanged();
    void statusTextChanged();
    void errorChanged();
    void countsChanged();
    void taskFinished(const QString& taskId);
    void reportExported(const QUrl& destination);
    void reportExportFailed(const QString& message);

private:
    class Private;
    std::unique_ptr<Private> d_;
};
