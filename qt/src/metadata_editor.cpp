#include "metadata_editor.hpp"

#include "agplayer/c_api.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

MetadataEditor::MetadataEditor(QObject* parent)
    : QObject(parent)
{
}

double MetadataEditor::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool MetadataEditor::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

int MetadataEditor::fileCount() const noexcept
{
    return static_cast<int>(entries_.size());
}

QString MetadataEditor::coverImage() const
{
    return coverPath_;
}

void MetadataEditor::resetCover()
{
    if (!coverPath_.isEmpty() || !coverData_.isEmpty()) {
        coverPath_.clear();
        coverData_.clear();
        coverMime_.clear();
        emit coverImageChanged();
    }
}

QString MetadataEditor::mimeTypeForImage(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png")) {
        return QStringLiteral("image/png");
    }
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (suffix == QLatin1String("gif")) {
        return QStringLiteral("image/gif");
    }
    if (suffix == QLatin1String("bmp")) {
        return QStringLiteral("image/bmp");
    }
    if (suffix == QLatin1String("webp")) {
        return QStringLiteral("image/webp");
    }
    return QStringLiteral("application/octet-stream");
}

void MetadataEditor::setCoverImage(const QUrl& url)
{
    if (!url.isValid()) {
        return;
    }
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Failed to open cover image: %1").arg(path));
        return;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        emit errorOccurred(tr("Cover image is empty: %1").arg(path));
        return;
    }

    coverPath_ = path;
    coverData_ = data;
    coverMime_ = mimeTypeForImage(path);
    emit coverImageChanged();
}

void MetadataEditor::clearCoverImage()
{
    resetCover();
}

void MetadataEditor::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void MetadataEditor::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void MetadataEditor::loadFiles(const QList<QUrl>& urls)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    resetCover();
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<QList<MetadataEntry>>(this);
    connect(watcher, &QFutureWatcher<QList<MetadataEntry>>::finished, this,
        [this, watcher]() {
            entries_ = watcher->result();
            setBusy(false);
            setProgress(1.0);
            emit fileCountChanged();
            emit entriesLoaded();
            watcher->deleteLater();
        });

    watcher->setFuture(QtConcurrent::run([urls, this]() {
        QList<MetadataEntry> result;
        const int total = urls.size();
        for (int i = 0; i < total; ++i) {
            if (cancelFlag_.load(std::memory_order_acquire)) {
                break;
            }
            const QString path = urls[i].toLocalFile();
            MetadataEntry entry;
            entry.path = path;
            entry.fileName = QFileInfo(path).fileName();
            ag_metadata* md = nullptr;
            if (ag_metadata_open(path.toUtf8().constData(), &md) == AG_OK
                && md != nullptr) {
                entry.title = QString::fromUtf8(ag_metadata_title(md));
                entry.artist = QString::fromUtf8(ag_metadata_artist(md));
                entry.album = QString::fromUtf8(ag_metadata_album(md));
                entry.year = QString::fromUtf8(ag_metadata_year(md));
                entry.genre = QString::fromUtf8(ag_metadata_genre(md));
                entry.format = QString::fromUtf8(ag_metadata_format(md));
                entry.durationMs = ag_metadata_duration_ms(md);
                ag_metadata_destroy(md);
            } else {
                entry.hasError = true;
                entry.error = QStringLiteral("Failed to read metadata");
            }
            entry.fileSize = QFileInfo(path).size();
            result.append(entry);
            progress_.store(static_cast<double>(i + 1) / total,
                            std::memory_order_release);
        }
        return result;
    }));
}

QVariantMap MetadataEditor::entryAt(int index) const
{
    QVariantMap map;
    if (index < 0 || index >= entries_.size()) {
        return map;
    }
    const MetadataEntry& e = entries_[index];
    map["path"] = e.path;
    map["fileName"] = e.fileName;
    map["title"] = e.title;
    map["artist"] = e.artist;
    map["album"] = e.album;
    map["year"] = e.year;
    map["genre"] = e.genre;
    map["format"] = e.format;
    map["durationMs"] = e.durationMs;
    map["fileSize"] = e.fileSize;
    map["hasError"] = e.hasError;
    map["error"] = e.error;
    return map;
}

void MetadataEditor::applyMetadata(const QVariantMap& fields,
                                   const QList<int>& indices)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    QList<int> targets = indices;
    if (targets.isEmpty()) {
        for (int i = 0; i < entries_.size(); ++i) {
            targets.append(i);
        }
    }

    auto* watcher = new QFutureWatcher<QPair<int, int>>(this);
    connect(watcher, &QFutureWatcher<QPair<int, int>>::finished, this,
        [this, watcher]() {
            const auto result = watcher->result();
            setBusy(false);
            setProgress(1.0);
            emit metadataApplied(result.first, result.second);
            watcher->deleteLater();
        });

    const QByteArray coverData = coverData_;
    const QByteArray coverMime = coverMime_.toUtf8();

    watcher->setFuture(QtConcurrent::run(
        [fields, targets, coverData, coverMime, this]() {
            int success = 0;
            int failure = 0;
            const int total = targets.size();
            for (int i = 0; i < total; ++i) {
                if (cancelFlag_.load(std::memory_order_acquire)) {
                    break;
                }
                const int idx = targets[i];
                if (idx < 0 || idx >= entries_.size()) {
                    ++failure;
                    continue;
                }
                const MetadataEntry& e = entries_[idx];
                const QByteArray pathUtf8 = e.path.toUtf8();
                const std::string title =
                    fields.value("title").toString().toStdString();
                const std::string artist =
                    fields.value("artist").toString().toStdString();
                const std::string album =
                    fields.value("album").toString().toStdString();
                const std::string year =
                    fields.value("year").toString().toStdString();
                const std::string genre =
                    fields.value("genre").toString().toStdString();
                const unsigned char* coverPtr = nullptr;
                size_t coverSize = 0;
                const char* mimePtr = nullptr;
                if (!coverData.isEmpty()) {
                    coverPtr = reinterpret_cast<const unsigned char*>(
                        coverData.constData());
                    coverSize = static_cast<size_t>(coverData.size());
                    mimePtr = coverMime.constData();
                }
                const ag_result result = ag_metadata_write(
                    pathUtf8.constData(),
                    title.empty() ? nullptr : title.c_str(),
                    artist.empty() ? nullptr : artist.c_str(),
                    album.empty() ? nullptr : album.c_str(),
                    year.empty() ? nullptr : year.c_str(),
                    genre.empty() ? nullptr : genre.c_str(),
                    coverPtr, coverSize, mimePtr);
                if (result == AG_OK) {
                    ++success;
                } else {
                    ++failure;
                }
                progress_.store(static_cast<double>(i + 1) / total,
                                std::memory_order_release);
            }
            return QPair<int, int>{success, failure};
        }));
}

QString MetadataEditor::computeNewName(const QString& original,
                                       const QString& prefix,
                                       const QString& suffix,
                                       bool autoNumber,
                                       int number,
                                       int numberDigits) const
{
    const QFileInfo info(original);
    const QString stem = info.completeBaseName();
    const QString ext = info.suffix();
    QString name = prefix;
    if (autoNumber) {
        name += QString("%1").arg(number, numberDigits, 10, QChar('0'));
    } else {
        name += stem;
    }
    name += suffix;
    if (!ext.isEmpty()) {
        name += "." + ext;
    }
    return name;
}

QStringList MetadataEditor::previewRename(const QString& prefix,
                                          const QString& suffix,
                                          bool autoNumber,
                                          int numberStart,
                                          int numberDigits) const
{
    QStringList result;
    for (int i = 0; i < entries_.size(); ++i) {
        const QString newName = computeNewName(entries_[i].fileName, prefix,
                                               suffix, autoNumber,
                                               numberStart + i, numberDigits);
        result.append(entries_[i].fileName + " -> " + newName);
    }
    return result;
}

QVariantList MetadataEditor::renamePreviewEntries(const QString& prefix,
                                                  const QString& suffix,
                                                  bool autoNumber,
                                                  int numberStart,
                                                  int numberDigits) const
{
    QVariantList result;
    for (int i = 0; i < entries_.size(); ++i) {
        const QString newName = computeNewName(entries_[i].fileName, prefix,
                                               suffix, autoNumber,
                                               numberStart + i, numberDigits);
        QVariantMap map;
        map["original"] = entries_[i].fileName;
        map["preview"] = newName;
        result.append(map);
    }
    return result;
}

QString MetadataEditor::renameExample(const QString& prefix,
                                      const QString& suffix,
                                      bool autoNumber,
                                      int numberStart,
                                      int numberDigits) const
{
    const QString placeholder = QStringLiteral("Song.mp3");
    const QString original = entries_.isEmpty() ? placeholder : entries_[0].fileName;
    return computeNewName(original, prefix, suffix, autoNumber,
                          numberStart, numberDigits);
}

void MetadataEditor::applyRename(const QString& prefix,
                                 const QString& suffix,
                                 bool autoNumber,
                                 int numberStart,
                                 int numberDigits)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<QPair<int, int>>(this);
    connect(watcher, &QFutureWatcher<QPair<int, int>>::finished, this,
        [this, watcher]() {
            const auto result = watcher->result();
            setBusy(false);
            setProgress(1.0);
            emit renameApplied(result.first, result.second);
            watcher->deleteLater();
        });

    watcher->setFuture(QtConcurrent::run(
        [prefix, suffix, autoNumber, numberStart, numberDigits, this]() {
            int success = 0;
            int failure = 0;
            const int total = entries_.size();
            for (int i = 0; i < total; ++i) {
                if (cancelFlag_.load(std::memory_order_acquire)) {
                    break;
                }
                MetadataEntry& e = entries_[i];
                const QString newName = computeNewName(e.fileName, prefix, suffix,
                                                       autoNumber,
                                                       numberStart + i,
                                                       numberDigits);
                const QString newPath = QFileInfo(e.path).dir().filePath(newName);
                // Collision avoidance: append _2, _3, ... if target exists.
                QString finalPath = newPath;
                int attempt = 1;
                while (QFileInfo::exists(finalPath) && finalPath != e.path) {
                    const QFileInfo info(newName);
                    finalPath = QFileInfo(e.path).dir().filePath(
                        info.completeBaseName() + "_"
                        + QString::number(attempt + 1)
                        + (info.suffix().isEmpty() ? "" : "." + info.suffix()));
                    ++attempt;
                    if (attempt > 99) {
                        finalPath.clear();
                        break;
                    }
                }
                if (finalPath.isEmpty()) {
                    ++failure;
                    continue;
                }
                if (QFile::rename(e.path, finalPath)) {
                    e.path = finalPath;
                    e.fileName = newName;
                    ++success;
                } else {
                    ++failure;
                }
                progress_.store(static_cast<double>(i + 1) / total,
                                std::memory_order_release);
            }
            return QPair<int, int>{success, failure};
        }));
}

void MetadataEditor::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
}

void MetadataEditor::clear()
{
    entries_.clear();
    resetCover();
    emit fileCountChanged();
}
