#include "tag_model.hpp"

#include "library_model.hpp"

#include <QSet>

#include <algorithm>

namespace {
QStringList normalizedTags(const QStringList& values)
{
    QStringList tags;
    QSet<QString> keys;
    for (const QString& value : values) {
        const QString displayName = value.trimmed();
        const QString key = displayName.toCaseFolded();
        if (!displayName.isEmpty() && !keys.contains(key)) {
            tags.append(displayName);
            keys.insert(key);
        }
    }
    return tags;
}

const QList<QColor>& tagPalette()
{
    static const QList<QColor> palette{QColor("#EE0000"), QColor("#007BFF"),
        QColor("#28A745"), QColor("#FD7E14"), QColor("#6F42C1"),
        QColor("#D63384"), QColor("#17A2B8"), QColor("#809438"),
        QColor("#925B37"), QColor("#495057"), QColor("#9C27B0"),
        QColor("#008577"), QColor("#C44632"), QColor("#3F51B5"),
        QColor("#B87400"), QColor("#57762F"), QColor("#A12758"),
        QColor("#006A96"), QColor("#75613E"), QColor("#665A99")};
    return palette;
}

QColor nextColor(const QString& key, const QList<TagEntry>& entries)
{
    const QList<QColor>& palette = tagPalette();
    quint32 hash = 2166136261U;
    for (const QChar character : key) {
        hash ^= character.unicode();
        hash *= 16777619U;
    }
    // Keep the key-derived starting point, but reserve unused colours first.
    // Existing / explicitly edited colours never change when a tag is added.
    QSet<QRgb> usedColors;
    for (const TagEntry& entry : entries) usedColors.insert(entry.color.rgb());
    const int start = static_cast<int>(hash % static_cast<quint32>(palette.size()));
    for (int offset = 0; offset < palette.size(); ++offset) {
        const int candidate = (start + offset) % palette.size();
        if (!usedColors.contains(palette[candidate].rgb())) return palette[candidate];
    }
    // Extend the initial swatches deterministically, without recoloring stored
    // tags. RGB comparison also avoids duplicate rendered colors from HSV rounding.
    constexpr quint32 colorCount = 360U * 64U * 64U;
    for (quint32 attempt = 0; attempt < colorCount; ++attempt) {
        const quint32 slot = (hash % colorCount + attempt * 137U) % colorCount;
        const QColor candidate = QColor::fromRgb(QColor::fromHsv(static_cast<int>(slot % 360U),
            160 + static_cast<int>((slot / 360U) % 64U),
            192 + static_cast<int>(slot / (360U * 64U))).rgb());
        if (!usedColors.contains(candidate.rgb())) return candidate;
    }
    // The finite display-color space may eventually be exhausted.
    return palette[start];
}
}

TagModel::TagModel(LibraryModel* library, QString storagePath, QObject* parent,
                   TagStore::WriteFunction writer)
    : QAbstractListModel(parent)
    , library_(library)
    , store_(std::move(storagePath), std::move(writer))
{
    flushTimer_.setSingleShot(true);
    flushTimer_.setInterval(0);
    connect(&flushTimer_, &QTimer::timeout, this, [this] { flush(); });
    rebuildFromLibrary();
    if (library_ == nullptr) return;
    connect(library_, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int first, int last) {
                rememberTrackTags(first, last, 1);
            });
    connect(library_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex&, int first, int last) {
                rememberTrackTags(first, last, -1);
            });
    connect(library_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last,
                   const QList<int>& roles) {
                if (!roles.isEmpty() && !roles.contains(LibraryModel::TagsRole)) return;
                for (int row = first.row(); row <= last.row(); ++row) {
                    const QModelIndex index = library_->index(row, 0);
                    const QString trackId = library_->data(index, LibraryModel::TrackIdRole).toString();
                    applyTagChange(trackId, trackTags_.value(trackId),
                                   library_->data(index, LibraryModel::TagsRole).toStringList());
                }
            });
    connect(library_, &LibraryModel::tagsChanged, this,
            [this](const QString& trackId, const QStringList& oldTags,
                   const QStringList& newTags) {
                if (trackTags_.value(trackId) != normalizedTags(newTags)) {
                    applyTagChange(trackId, oldTags, newTags);
                }
            });
    connect(library_, &QAbstractItemModel::modelReset, this,
            [this] { rebuildFromLibrary(); });
}

int TagModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : entries_.size();
}

QVariant TagModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};
    const TagEntry& entry = entries_.at(index.row());
    switch (role) {
    case KeyRole: return entry.key;
    case DisplayNameRole: return entry.displayName;
    case TrackCountRole: return entry.trackCount;
    case ColorRole: return entry.color;
    case SelectedRole: return entry.key == selectedKey_;
    default: return {};
    }
}

QHash<int, QByteArray> TagModel::roleNames() const
{
    return {{KeyRole, "key"}, {DisplayNameRole, "displayName"},
            {TrackCountRole, "trackCount"}, {ColorRole, "color"},
            {SelectedRole, "selected"}};
}

int TagModel::count() const noexcept { return entries_.size(); }
QString TagModel::selectedKey() const noexcept { return selectedKey_; }

void TagModel::setSelectedKey(const QString& key)
{
    const QString normalized = keyFor(key);
    if (selectedKey_ == normalized) return;
    const int previous = rowForKey(selectedKey_);
    selectedKey_ = normalized;
    const int current = rowForKey(selectedKey_);
    if (previous >= 0) emit dataChanged(index(previous, 0), index(previous, 0), {SelectedRole});
    if (current >= 0) emit dataChanged(index(current, 0), index(current, 0), {SelectedRole});
    emit selectedKeyChanged();
}

bool TagModel::createTag(const QString& displayName)
{
    const QString cleanName = displayName.trimmed();
    const QString key = keyFor(cleanName);
    if (key.isEmpty() || rowForKey(key) >= 0) return false;
    pendingRemovedKeys_.remove(key);
    const int row = entries_.size();
    beginInsertRows({}, row, row);
    entries_.append({key, cleanName, 0, nextColorFor(key)});
    rowsByKey_.insert(key, row);
    endInsertRows();
    emit countChanged();
    scheduleFlush();
    return true;
}

bool TagModel::renameTag(const QString& key, const QString& displayName)
{
    const QString oldKey = keyFor(key);
    const QString newName = displayName.trimmed();
    const QString newKey = keyFor(newName);
    const int oldRow = rowForKey(oldKey);
    if (oldRow < 0 || newKey.isEmpty()) return false;
    if (oldKey != newKey && rowForKey(newKey) >= 0) return false;
    const QColor preservedColor = entries_.at(oldRow).color;
    const int changed = library_ == nullptr ? 0 : library_->renameTag(oldKey, newName);
    const int refreshedOldRow = rowForKey(oldKey);
    const int refreshedNewRow = rowForKey(newKey);
    if (refreshedNewRow < 0 && refreshedOldRow >= 0) {
        entries_[refreshedOldRow].key = newKey;
        entries_[refreshedOldRow].displayName = newName;
        rowsByKey_.remove(oldKey);
        rowsByKey_.insert(newKey, refreshedOldRow);
        emit dataChanged(index(refreshedOldRow, 0), index(refreshedOldRow, 0),
                         {KeyRole, DisplayNameRole});
    } else if (refreshedNewRow >= 0) {
        entries_[refreshedNewRow].displayName = newName;
        entries_[refreshedNewRow].color = preservedColor;
        emit dataChanged(index(refreshedNewRow, 0), index(refreshedNewRow, 0),
                         {DisplayNameRole, ColorRole});
    }
    if (oldKey != newKey && refreshedOldRow >= 0 && refreshedNewRow >= 0) {
        beginRemoveRows({}, refreshedOldRow, refreshedOldRow);
        entries_.removeAt(refreshedOldRow);
        endRemoveRows();
        rowsByKey_.clear();
        for (int row = 0; row < entries_.size(); ++row) rowsByKey_.insert(entries_.at(row).key, row);
        emit countChanged();
    }
    if (oldKey == selectedKey_) setSelectedKey(newKey);
    if (oldKey != newKey) {
        pendingRemovedKeys_.insert(oldKey);
        pendingRemovedKeys_.remove(newKey);
    }
    scheduleFlush();
    Q_UNUSED(changed)
    return true;
}

int TagModel::removeTag(const QString& key)
{
    const QString normalized = keyFor(key);
    const int row = rowForKey(normalized);
    if (row < 0) return 0;
    const int changed = library_ == nullptr ? 0 : library_->removeTag(normalized);
    const int currentRow = rowForKey(normalized);
    if (currentRow >= 0) {
        beginRemoveRows({}, currentRow, currentRow);
        entries_.removeAt(currentRow);
        endRemoveRows();
        rowsByKey_.clear();
        for (int index = 0; index < entries_.size(); ++index) rowsByKey_.insert(entries_.at(index).key, index);
        emit countChanged();
    }
    if (selectedKey_ == normalized) setSelectedKey({});
    pendingRemovedKeys_.insert(normalized);
    scheduleFlush();
    return changed;
}

bool TagModel::setTagColor(const QString& key, const QString& color)
{
    const int row = rowForKey(keyFor(key));
    const QColor parsed(color);
    if (row < 0 || !parsed.isValid() || entries_.at(row).color == parsed) return false;
    entries_[row].color = parsed;
    emit dataChanged(index(row, 0), index(row, 0), {ColorRole});
    scheduleFlush();
    return true;
}

int TagModel::countForKey(const QString& key) const
{
    const int row = rowForKey(keyFor(key));
    return row < 0 ? 0 : entries_.at(row).trackCount;
}

QColor TagModel::colorForKey(const QString& key) const
{
    const int row = rowForKey(keyFor(key));
    return row < 0 ? QColor{} : entries_.at(row).color;
}

bool TagModel::dirty() const noexcept { return dirty_; }
QString TagModel::persistenceError() const { return persistenceError_; }

bool TagModel::flush()
{
    flushTimer_.stop();
    if (!dirty_) return true;
    if (!store_.save(entries_)) {
        if (persistenceError_.isEmpty()) {
            persistenceError_ = tr("无法保存标签数据");
            emit persistenceStateChanged();
        }
        if (retryAttempts_ < kMaxFlushRetries) {
            ++retryAttempts_;
            flushTimer_.start(kFlushRetryIntervalMs);
        }
        return false;
    }
    dirty_ = false;
    retryAttempts_ = 0;
    pendingRemovedKeys_.clear();
    persistenceError_.clear();
    emit persistenceStateChanged();
    return true;
}

QString TagModel::keyFor(const QString& value) { return value.trimmed().toCaseFolded(); }
int TagModel::rowForKey(const QString& key) const { return rowsByKey_.value(keyFor(key), -1); }

QColor TagModel::nextColorFor(const QString& key) const
{
    return nextColor(key, entries_);
}

void TagModel::rebuildFromLibrary()
{
    // Overlay the local directory on the persisted one, but keep explicit
    // local removals as tombstones until QSaveFile commits. This preserves
    // unsaved colors/zero-count tags without dropping independent disk entries.
    QList<TagEntry> entries = store_.load();
    if (dirty_) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [this](const TagEntry& entry) {
            return pendingRemovedKeys_.contains(entry.key);
        }), entries.end());
        QHash<QString, int> storedRows;
        for (int row = 0; row < entries.size(); ++row) storedRows.insert(entries.at(row).key, row);
        for (const TagEntry& memoryEntry : entries_) {
            const int row = storedRows.value(memoryEntry.key, -1);
            if (row < 0) {
                storedRows.insert(memoryEntry.key, entries.size());
                entries.append(memoryEntry);
            } else {
                entries[row] = memoryEntry;
            }
        }
    }
    for (TagEntry& entry : entries) entry.trackCount = 0;
    bool metadataChanged = false;
    QHash<QString, int> rowByKey;
    for (int row = 0; row < entries.size(); ++row) rowByKey.insert(entries.at(row).key, row);
    if (library_ != nullptr) {
        for (const TrackRecord& track : library_->tracks()) {
            for (const QString& tag : normalizedTags(track.tags)) {
                const QString key = keyFor(tag);
                int row = rowByKey.value(key, -1);
                if (row < 0) {
                    const QColor color = nextColor(key, entries);
                    entries.append({key, tag, 0, color});
                    metadataChanged = true;
                    row = entries.size() - 1;
                    rowByKey.insert(key, row);
                }
                ++entries[row].trackCount;
            }
        }
    }
    beginResetModel();
    entries_ = std::move(entries);
    rowsByKey_ = std::move(rowByKey);
    trackTags_.clear();
    if (library_ != nullptr) {
        for (const TrackRecord& track : library_->tracks()) trackTags_.insert(track.trackId, normalizedTags(track.tags));
    }
    endResetModel();
    emit countChanged();
    if (metadataChanged) scheduleFlush();
}

void TagModel::addTrackTags(const QString&, const QStringList& tags, const int delta)
{
    for (const QString& tag : normalizedTags(tags)) {
        const QString key = keyFor(tag);
        int row = rowForKey(key);
        if (row < 0 && delta > 0) {
            const int inserted = entries_.size();
            beginInsertRows({}, inserted, inserted);
            entries_.append({key, tag, 0, nextColorFor(key)});
            rowsByKey_.insert(key, inserted);
            endInsertRows();
            emit countChanged();
            scheduleFlush();
            row = inserted;
        }
        if (row < 0) continue;
        const int next = qMax(0, entries_.at(row).trackCount + delta);
        if (entries_.at(row).trackCount == next) continue;
        entries_[row].trackCount = next;
        emit dataChanged(index(row, 0), index(row, 0), {TrackCountRole});
    }
}

void TagModel::applyTagChange(const QString& trackId, const QStringList& oldTags,
                              const QStringList& newTags)
{
    const QStringList oldNormalized = normalizedTags(oldTags);
    const QStringList newNormalized = normalizedTags(newTags);
    if (oldNormalized == newNormalized) {
        trackTags_.insert(trackId, newNormalized);
        return;
    }
    QSet<QString> oldKeys;
    QSet<QString> newKeys;
    for (const QString& tag : oldNormalized) oldKeys.insert(keyFor(tag));
    for (const QString& tag : newNormalized) newKeys.insert(keyFor(tag));
    for (const QString& tag : oldNormalized) if (!newKeys.contains(keyFor(tag))) addTrackTags(trackId, {tag}, -1);
    for (const QString& tag : newNormalized) if (!oldKeys.contains(keyFor(tag))) addTrackTags(trackId, {tag}, 1);
    trackTags_.insert(trackId, newNormalized);
}

void TagModel::rememberTrackTags(const int firstRow, const int lastRow, const int delta)
{
    if (library_ == nullptr) return;
    for (int row = firstRow; row <= lastRow; ++row) {
        const QModelIndex index = library_->index(row, 0);
        const QString trackId = library_->data(index, LibraryModel::TrackIdRole).toString();
        const QStringList tags = delta > 0 ? library_->data(index, LibraryModel::TagsRole).toStringList()
                                            : trackTags_.value(trackId);
        addTrackTags(trackId, tags, delta);
        if (delta > 0) trackTags_.insert(trackId, normalizedTags(tags));
        else trackTags_.remove(trackId);
    }
}

void TagModel::scheduleFlush()
{
    const bool wasDirty = dirty_;
    dirty_ = true;
    retryAttempts_ = 0;
    flushTimer_.start();
    if (!wasDirty) emit persistenceStateChanged();
}
