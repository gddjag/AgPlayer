#pragma once

#include "tag_store.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

class LibraryModel;

class TagModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString selectedKey READ selectedKey WRITE setSelectedKey NOTIFY selectedKeyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY persistenceStateChanged)
    Q_PROPERTY(QString persistenceError READ persistenceError
                   NOTIFY persistenceStateChanged)

public:
    enum Role { KeyRole = Qt::UserRole + 1, DisplayNameRole, TrackCountRole,
                ColorRole, SelectedRole };
    Q_ENUM(Role)

    explicit TagModel(LibraryModel* library, QString storagePath,
                      QObject* parent = nullptr,
                      TagStore::WriteFunction writer = {});

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const noexcept;
    QString selectedKey() const noexcept;
    void setSelectedKey(const QString& key);
    Q_INVOKABLE bool createTag(const QString& displayName);
    Q_INVOKABLE bool renameTag(const QString& key, const QString& displayName);
    Q_INVOKABLE int removeTag(const QString& key);
    Q_INVOKABLE bool setTagColor(const QString& key, const QString& color);
    Q_INVOKABLE int countForKey(const QString& key) const;
    QColor colorForKey(const QString& key) const;
    bool dirty() const noexcept;
    QString persistenceError() const;
    bool flush();

signals:
    void selectedKeyChanged();
    void countChanged();
    void persistenceStateChanged();

private:
    static QString keyFor(const QString& value);
    int rowForKey(const QString& key) const;
    QColor nextColorFor(const QString& key) const;
    void rebuildFromLibrary();
    void addTrackTags(const QString& trackId, const QStringList& tags, int delta);
    void applyTagChange(const QString& trackId, const QStringList& oldTags,
                        const QStringList& newTags);
    void rememberTrackTags(int firstRow, int lastRow, int delta);
    void scheduleFlush();

    QPointer<LibraryModel> library_;
    TagStore store_;
    QList<TagEntry> entries_;
    QHash<QString, int> rowsByKey_;
    QHash<QString, QStringList> trackTags_;
    QString selectedKey_;
    QTimer flushTimer_;
    QSet<QString> pendingRemovedKeys_;
    QString persistenceError_;
    int retryAttempts_ = 0;
    bool dirty_ = false;
    static constexpr int kMaxFlushRetries = 3;
    static constexpr int kFlushRetryIntervalMs = 100;
};
