#include "library_filter_model.hpp"
#include "playlist_model.hpp"

#include <QAbstractItemModel>
#include <QDateTime>

#include <algorithm>

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

int LibraryFilterModel::exactRating() const noexcept
{
    return exactRating_;
}

void LibraryFilterModel::setExactRating(int rating)
{
    rating = std::clamp(rating, 0, 5);
    if (exactRating_ == rating) {
        return;
    }
    exactRating_ = rating;
    emit exactRatingChanged();
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
    if (category_ == QStringLiteral("history")) {
        setSortRole(LibraryModel::LastPlayedAtRole);
        sort(0, Qt::DescendingOrder);
    } else if (category_ == QStringLiteral("recentAdded")) {
        setSortRole(LibraryModel::AddedAtRole);
        sort(0, Qt::DescendingOrder);
    } else if (category_ == QStringLiteral("all")
               || category_ == QStringLiteral("favorites")
               || category_ == QStringLiteral("neverPlayed")) {
        sort(-1);
    } else {
        sort(0, Qt::AscendingOrder);
    }
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

PlaylistModel* LibraryFilterModel::playlistModel() const noexcept
{
    return playlistModel_;
}

void LibraryFilterModel::setPlaylistModel(PlaylistModel* playlistModel)
{
    if (playlistModel_ == playlistModel) {
        return;
    }
    if (playlistModel_ != nullptr) {
        playlistModel_->disconnect(this);
    }
    playlistModel_ = playlistModel;
    if (playlistModel_ != nullptr) {
        connect(playlistModel_, &PlaylistModel::membershipChanged, this,
                [this] {
                    invalidateFilter();
                    invalidate();
                });
        connect(playlistModel_, &QAbstractItemModel::rowsRemoved, this,
                [this] { invalidateFilter(); });
        connect(playlistModel_, &QAbstractItemModel::modelReset, this,
                [this] { invalidateFilter(); });
    }
    emit playlistModelChanged();
    invalidateFilter();
}

bool LibraryFilterModel::setRating(int proxyRow, int rating)
{
    const int srcRow = sourceRow(proxyRow);
    if (srcRow < 0) {
        return false;
    }
    auto* library = qobject_cast<LibraryModel*>(sourceModel());
    return library != nullptr && library->setRating(srcRow, rating);
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
    if (category_ == QStringLiteral("history")) {
        QAbstractItemModel* model = sourceModel();
        const QModelIndex idx = model->index(sourceRow, 0);
        return model->data(idx, LibraryModel::PlayCountRole).toInt() > 0;
    }
    if (category_ == QStringLiteral("recentAdded")) {
        const QModelIndex idx = sourceModel()->index(sourceRow, 0);
        const qint64 addedAt = sourceModel()
                                   ->data(idx, LibraryModel::AddedAtRole)
                                   .toLongLong();
        constexpr qint64 dayMs = 24LL * 60 * 60 * 1000;
        return addedAt > 0
            && addedAt >= QDateTime::currentMSecsSinceEpoch() - 30 * dayMs;
    }
    if (category_ == QStringLiteral("neverPlayed")) {
        const QModelIndex idx = sourceModel()->index(sourceRow, 0);
        return sourceModel()->data(idx, LibraryModel::PlayCountRole).toInt() == 0;
    }
    if (category_ == QStringLiteral("all")) {
        return true;
    }
    if (playlistModel_ == nullptr) {
        return false;
    }
    QAbstractItemModel* model = sourceModel();
    const QModelIndex idx = model->index(sourceRow, 0);
    return playlistModel_->containsTrack(
        category_, model->data(idx, LibraryModel::TrackIdRole).toString());
}

bool LibraryFilterModel::lessThan(const QModelIndex& sourceLeft,
                                  const QModelIndex& sourceRight) const
{
    if (category_ == QStringLiteral("recentAdded")) {
        return sourceModel()
            ->data(sourceLeft, LibraryModel::AddedAtRole).toLongLong()
            < sourceModel()
                  ->data(sourceRight, LibraryModel::AddedAtRole).toLongLong();
    }
    if (category_ == QStringLiteral("history")) {
        return sourceModel()
            ->data(sourceLeft, LibraryModel::LastPlayedAtRole).toLongLong()
            < sourceModel()
                  ->data(sourceRight, LibraryModel::LastPlayedAtRole).toLongLong();
    }
    if (playlistModel_ != nullptr && category_ != QStringLiteral("all")
        && category_ != QStringLiteral("favorites")
        && category_ != QStringLiteral("recentAdded")
        && category_ != QStringLiteral("neverPlayed")) {
        const QString leftId = sourceModel()
                                   ->data(sourceLeft, LibraryModel::TrackIdRole)
                                   .toString();
        const QString rightId = sourceModel()
                                    ->data(sourceRight, LibraryModel::TrackIdRole)
                                    .toString();
        const QStringList ids = playlistModel_->trackIdsForPlaylist(category_);
        return ids.indexOf(leftId) < ids.indexOf(rightId);
    }
    return sourceLeft.row() < sourceRight.row();
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
    if (album.contains(text)) {
        return true;
    }
    const QStringList tags = model->data(idx, LibraryModel::TagsRole).toStringList();
    return std::any_of(tags.cbegin(), tags.cend(), [&text](const QString& tag) {
        return tag.toCaseFolded().contains(text);
    });
}

bool LibraryFilterModel::rowMatchesRating(int sourceRow) const
{
    if (exactRating_ <= 0) {
        return true;
    }
    QAbstractItemModel* model = sourceModel();
    const QModelIndex idx = model->index(sourceRow, 0);
    const int rating = model->data(idx, LibraryModel::RatingRole).toInt();
    return rating == exactRating_;
}

bool LibraryFilterModel::rowMatchesBpm(int sourceRow) const
{
    // Default range means "BPM filter disabled" so tracks with unknown BPM still show.
    if (qFuzzyCompare(minBpm_, 60.0) && qFuzzyCompare(maxBpm_, 160.0)) {
        return true;
    }
    QAbstractItemModel* model = sourceModel();
    const QModelIndex idx = model->index(sourceRow, 0);
    bool ok = false;
    const double bpm = model->data(idx, LibraryModel::BpmRole).toDouble(&ok);
    if (!ok || bpm <= 0.0) {
        return false;
    }
    return bpm >= minBpm_ && bpm <= maxBpm_;
}
