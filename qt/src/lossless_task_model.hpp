#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class LosslessTaskModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Role {
        TaskIdRole = Qt::UserRole + 1,
        FileNameRole,
        FilePathRole,
        FormatNameRole,
        AudioFormatRole,
        VerdictCodeRole,
        VerdictTextRole,
        ConfidenceRole,
        StateRole,
        StateTextRole,
        CheckedRole,
        ProgressRole,
    };
    Q_ENUM(Role)

    explicit LosslessTaskModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    void sort(int column,
              Qt::SortOrder order = Qt::AscendingOrder) override;

    bool appendTask(const QVariantMap& task);
    bool updateTask(const QString& taskId, const QVariantMap& changes);
    QVariantMap task(const QString& taskId) const;
    QVariantMap taskAtVisibleRow(int row) const;
    QVector<QVariantMap> allTasks() const;
    QStringList taskIds() const;
    QStringList visibleTaskIds() const;
    int taskCount() const noexcept;
    bool containsIdentity(const QString& identity) const;
    QString taskIdForIdentity(const QString& identity) const;
    int checkedCount() const;
    void setChecked(const QString& taskId, bool checked);
    void selectAllVisible(bool checked);
    void removeTasks(const QStringList& taskIds);
    void clearTasks();
    void setFilter(const QString& filter);
    void setSearchText(const QString& searchText);

signals:
    void visibleRowsChanged();

private:
    static bool matchesFilter(const QVariantMap& task, const QString& filter);
    bool isVisible(const QVariantMap& task) const;
    void rebuildIndexes();
    void rebuildVisible();
    void sortVisibleRows();
    int visibleRowForSourceRow(int sourceRow) const;
    QString sortKeyForColumn(const QVariantMap& task, int column) const;

    QVector<QVariantMap> tasks_;
    QVector<int> visible_rows_;
    QHash<QString, int> row_by_id_;
    QHash<QString, QString> id_by_identity_;
    QString filter_{QStringLiteral("all")};
    QString search_text_;
    int sort_column_ = -1;
    Qt::SortOrder sort_order_ = Qt::AscendingOrder;
};
