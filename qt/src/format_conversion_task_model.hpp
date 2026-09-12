#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QVariantMap>
#include <QVector>

class FormatConversionTaskModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Role {
        TaskIdRole = Qt::UserRole + 1,
        CheckedRole,
        FileNameRole,
        PathRole,
        SourceFormatRole,
        DurationMsRole,
        SampleRateRole,
        BitRateRole,
        ChannelLayoutRole,
        SampleFormatRole,
        OutputFormatRole,
        OutputPathRole,
        StatusRole,
        StageRole,
        ProgressRole,
        ErrorSummaryRole,
        ErrorDetailRole,
        ResolvedProfileRole,
        ImportRootRole,
        AudioStreamsRole
    };
    Q_ENUM(Role)

    explicit FormatConversionTaskModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    void appendTask(const QVariantMap& task);
    bool updateTask(const QString& taskId, const QVariantMap& changes);
    QString taskIdAt(int row) const;
    QVariantMap taskAt(int row) const;
    QStringList taskIds() const;
    bool containsTask(const QString& taskId) const;
    int checkedCount() const;
    double durationWeightedProgress() const;

    Q_INVOKABLE void setChecked(const QString& taskId, bool checked);
    Q_INVOKABLE void setAllVisibleChecked(bool checked);
    Q_INVOKABLE void removeTasks(const QStringList& taskIds);
    Q_INVOKABLE void clearTasks();

private:
    void rebuildRows();
    static int roleForKey(const QString& key);

    QVector<QVariantMap> tasks_;
    QHash<QString, int> rowById_;
};
