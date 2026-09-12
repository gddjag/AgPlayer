#pragma once

#include <QSortFilterProxyModel>

class FormatConversionFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter
                   NOTIFY statusFilterChanged)
    Q_PROPERTY(QString formatFilter READ formatFilter WRITE setFormatFilter
                   NOTIFY formatFilterChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountsChanged)
    Q_PROPERTY(int visibleCheckedCount READ visibleCheckedCount
                   NOTIFY visibleCountsChanged)

public:
    explicit FormatConversionFilterModel(QObject* parent = nullptr);

    QString query() const;
    QString statusFilter() const;
    QString formatFilter() const;
    int visibleCount() const;
    int visibleCheckedCount() const;

    void setQuery(const QString& value);
    void setStatusFilter(const QString& value);
    void setFormatFilter(const QString& value);

    Q_INVOKABLE QString sourceTaskId(int proxyRow) const;
    Q_INVOKABLE void setAllVisibleChecked(bool checked);

signals:
    void queryChanged();
    void statusFilterChanged();
    void formatFilterChanged();
    void visibleCountsChanged();

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;

private:
    QString query_;
    QString statusFilter_;
    QString formatFilter_;
};
