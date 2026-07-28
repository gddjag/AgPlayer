#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <atomic>

template <typename T>
class QFutureWatcher;

struct MetadataEntry {
    QString path;
    QString fileName;
    QString title;
    QString artist;
    QString album;
    QString year;
    QString genre;
    QString lyrics;
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
    Q_PROPERTY(QString coverImage READ coverImage NOTIFY coverImageChanged)

public:
    explicit MetadataEditor(QObject* parent = nullptr);
    ~MetadataEditor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;
    QString coverImage() const;

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QVariantMap entryAt(int index) const;
    Q_INVOKABLE void applyMetadata(const QVariantMap& fields,
                                   const QList<int>& indices);
    Q_INVOKABLE void setCoverImage(const QUrl& url);
    Q_INVOKABLE void clearCoverImage();
    Q_INVOKABLE QStringList previewRename(const QString& prefix,
                                          const QString& suffix,
                                          bool autoNumber,
                                          int numberStart,
                                          int numberDigits) const;
    Q_INVOKABLE QVariantList renamePreviewEntries(const QString& prefix,
                                                  const QString& suffix,
                                                  bool autoNumber,
                                                  int numberStart,
                                                  int numberDigits) const;
    Q_INVOKABLE QString renameExample(const QString& prefix,
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
    void entriesChanged();
    void coverImageChanged();
    void metadataApplied(int successCount, int failureCount);
    void renameApplied(int successCount, int failureCount);
    void errorOccurred(const QString& message);

private:
    QList<MetadataEntry> entries_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};
    QPointer<QFutureWatcher<QList<MetadataEntry>>> loadWatcher_;
    QPointer<QFutureWatcher<QPair<int, int>>> operationWatcher_;

    QString coverPath_;
    QByteArray coverData_;
    QString coverMime_;

    void setBusy(bool value);
    void setProgress(double value);
    void resetCover();
    static QString mimeTypeForImage(const QString& path);
    QString computeNewName(const QString& original, const QString& prefix,
                           const QString& suffix, bool autoNumber,
                           int number, int numberDigits) const;
};
