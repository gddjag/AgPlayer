#include "library_filter_model.hpp"

#include <QAbstractItemModel>

LibraryFilterModel::LibraryFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &LibraryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &LibraryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &LibraryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &LibraryFilterModel::countChanged);
}

void LibraryFilterModel::setSourceModel(QAbstractItemModel* sourceModel)
{
    QSortFilterProxyModel::setSourceModel(sourceModel);
    emit sourceModelChanged();
    emit countChanged();
}

QString LibraryFilterModel::searchText() const noexcept
{
    return searchText_;
}

void LibraryFilterModel::setSearchText(const QString& text)
{
    if (searchText_ == text) {
        return;
    }
    searchText_ = text;
    emit searchTextChanged();
    invalidateFilter();
}

int LibraryFilterModel::minRating() const noexcept
{
    return minRating_;
}

void LibraryFilterModel::setMinRating(int rating)
{
    if (minRating_ == rating) {
        return;
    }
    minRating_ = rating;
    emit minRatingChanged();
    invalidateFilter();
}

double LibraryFilterModel::minBpm() const noexcept
{
    return minBpm_;
}

void LibraryFilterModel::setMinBpm(double bpm)
{
    if (qFuzzyCompare(minBpm_, bpm)) {
        return;
    }
    minBpm_ = bpm;
    emit minBpmChanged();
    invalidateFilter();
}

double LibraryFilterModel::maxBpm() const noexcept
{
    return maxBpm_;
}

void LibraryFilterModel::setMaxBpm(double bpm)
{
    if (qFuzzyCompare(maxBpm_, bpm)) {
        return;
    }
    maxBpm_ = bpm;
    emit maxBpmChanged();
    invalidateFilter();
}

QString LibraryFilterModel::category() const noexcept
{
    return category_;
}

void LibraryFilterModel::setCategory(const QString& category)
{
    if (category_ == category) {
        return;
    }
    category_ = category;
    emit categoryChanged();
    invalidateFilter();
}

int LibraryFilterModel::count() const
{
    return rowCount();
}

int LibraryFilterModel::sourceRow(int proxyRow) const
{
    const QModelIndex proxyIndex = index(proxyRow, 0);
    if (!proxyIndex.isValid()) {
        return -1;
    }
    const QModelIndex sourceIndex = mapToSource(proxyIndex);
    return sourceIndex.isValid() ? sourceIndex.row() : -1;
}

void LibraryFilterModel::playSourceRow(int proxyRow)
{
    const int srcRow = sourceRow(proxyRow);
    if (srcRow < 0) {
        return;
    }
    auto* library = qobject_cast<LibraryModel*>(sourceModel());
    if (library != nullptr) {
        library->playRow(srcRow);
    }
}

bool LibraryFilterModel::setFavorite(int proxyRow, bool favorite)
{
    const int srcRow = sourceRow(proxyRow);
    if (srcRow < 0) {
        return false;
    }
    auto* library = qobject_cast<LibraryModel*>(sourceModel());
    if (library != nullptr) {
        return library->setFavorite(srcRow, favorite);
    }
    return false;
}

bool LibraryFilterModel::filterAcceptsRow(int sourceRow,
                                          const QModelIndex& sourceParent) const
{
    if (sourceParent.isValid()) {
        return false;
    }
    QAbstractItemModel* model = sourceModel();
    if (model == nullptr) {
        return false;
    }
    if (sourceRow < 0 || sourceRow >= model->rowCount(sourceParent)) {
        return false;
    }

    return rowMatchesCategory(sourceRow) && rowMatchesSearch(sourceRow)
           && rowMatchesRating(sourceRow) && rowMatchesBpm(sourceRow);
}

bool LibraryFilterModel::rowMatchesCategory(int sourceRow) const
{
    if (category_ == QStringLiteral("favorites")) {
        QAbstractItemModel* model = sourceModel();
        const QModelIndex idx = model->index(sourceRow, 0);
        return model->data(idx, LibraryModel::FavoriteRole).toBool();
    }
    return true;
}

bool LibraryFilterModel::rowMatchesSearch(int sourceRow) const
{
    if (searchText_.isEmpty()) {
        return true;
    }
    QAbstractItemModel* model = sourceModel();
    const QModelIndex idx = model->index(sourceRow, 0);
    const QString text = searchText_.toCaseFolded();
    const QString title = model->data(idx, LibraryModel::TitleRole).toString().toCaseFolded();
    if (title.contains(text)) {
        return true;
    }
    const QString artist = model->data(idx, LibraryModel::ArtistRole).toString().toCaseFolded();
    if (artist.contains(text)) {
        return true;
    }
    const QString album = model->data(idx, LibraryModel::AlbumRole).toString().toCaseFolded();
    return album.contains(text);
}

bool LibraryFilterModel::rowMatchesRating(int sourceRow) const
{
    if (minRating_ <= 0) {
        return true;
    }
    QAbstractItemModel* model = sourceModel();
    const QModelIndex idx = model->index(sourceRow, 0);
    const int rating = model->data(idx, LibraryModel::RatingRole).toInt();
    return rating >= minRating_;
}

bool LibraryFilterModel::rowMatchesBpm(int sourceRow) const
{
    QAbstractItemModel* model = sourceModel();
    const QModelIndex idx = model->index(sourceRow, 0);
    bool ok = false;
    const double bpm = model->data(idx, LibraryModel::BpmRole).toDouble(&ok);
    if (!ok || bpm <= 0.0) {
        return false;
    }
    return bpm >= minBpm_ && bpm <= maxBpm_;
}
