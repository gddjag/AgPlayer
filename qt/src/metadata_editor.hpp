#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <atomic>

struct MetadataEntry {
    QString path;
    QString fileName;
    QString title;
    QString artist;
    QString album;
    QString year;
    QString genre;
    QString format;
    qint64 durationMs = 0;
    qint64 fileSize = 0;
    bool hasError = false;
    QString error;
};

class MetadataEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)

public:
    explicit MetadataEditor(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QVariantMap entryAt(int index) const;
    Q_INVOKABLE void applyMetadata(const QVariantMap& fields,
                                   const QList<int>& indices);
    Q_INVOKABLE QStringList previewRename(const QString& prefix,
                                          const QString& suffix,
                                          bool autoNumber,
                                          int numberStart,
                                          int numberDigits) const;
    Q_INVOKABLE void applyRename(const QString& prefix,
                                 const QString& suffix,
                                 bool autoNumber,
                                 int numberStart,
                                 int numberDigits);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void fileCountChanged();
    void entriesLoaded();
    void metadataApplied(int successCount, int failureCount);
    void renameApplied(int successCount, int failureCount);
    void errorOccurred(const QString& message);

private:
    QList<MetadataEntry> entries_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};

    void setBusy(bool value);
    void setProgress(double value);
    QString computeNewName(const QString& original, const QString& prefix,
                           const QString& suffix, bool autoNumber,
                           int number, int numberDigits) const;
};
