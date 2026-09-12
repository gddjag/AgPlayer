#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "rename_transaction.hpp"

#include <atomic>

template <typename T>
class QFutureWatcher;
class LibraryModel;

class FilenameProcessor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY entriesChanged)

public:
    explicit FilenameProcessor(QObject* parent = nullptr);
    ~FilenameProcessor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;
    bool canUndo() const noexcept;
    QVariantList files() const;

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QVariantMap entryAt(int index) const;
    Q_INVOKABLE void removeFiles(const QList<int>& indices);
    Q_INVOKABLE QVariantList preview(const QVariantMap& rules,
                                     const QList<int>& indices = {},
                                     const QString& conflictPolicy = QStringLiteral("autoNumber")) const;
    Q_INVOKABLE void apply(const QVariantMap& rules,
                           const QList<int>& indices = {},
                           const QString& conflictPolicy = QStringLiteral("autoNumber"));
    Q_INVOKABLE void undoLast();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    void setLibraryModel(LibraryModel* library);

signals:
    void progressChanged();
    void busyChanged();
    void fileCountChanged();
    void entriesLoaded();
    void entriesChanged();
    void canUndoChanged();
    void renameApplied(int successCount, int skippedCount, int failureCount);
    void undoCompleted(int successCount, int failureCount);
    void errorOccurred(const QString& message);

private:
    struct Entry {
        QString path;
        QString fileName;
        QString sha256;
    };
    struct RenameResult {
        QList<Entry> entries;
        agplayer::qt::RenameTransactionResult transaction;
        int success = 0;
        int skipped = 0;
        int failure = 0;
        QString error;
    };

    QList<Entry> entries_;
    agplayer::qt::RenameUndoRecord lastUndoRecord_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};
    QPointer<QFutureWatcher<QList<QUrl>>> discoveryWatcher_;
    QPointer<QFutureWatcher<QList<Entry>>> loadWatcher_;
    QPointer<QFutureWatcher<RenameResult>> operationWatcher_;
    QPointer<QFutureWatcher<agplayer::qt::RenameTransactionResult>> undoWatcher_;
    QPointer<LibraryModel> library_;

    void setBusy(bool value);
    void setProgress(double value);
    bool discardLastUndo();
    void startLoad(QList<QUrl> expanded);
    static QString proposedName(const QString& original,
                                const QVariantMap& rules, int ordinal);
    static QList<int> normalizedTargets(int count, const QList<int>& indices);
};
