#pragma once

#include <QSortFilterProxyModel>

class TagFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)

public:
    explicit TagFilterModel(QObject* parent = nullptr);

    QString query() const;
    void setQuery(const QString& query);

signals:
    void queryChanged();

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString query_;
};
