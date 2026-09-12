#include "metadata_editor.hpp"

#include "audio_file_discovery.hpp"
#include "library_model.hpp"
#include "metadata_writer.hpp"
#include "metadata_text.hpp"

#include "agplayer/c_api.h"

#include <QDir>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QSet>
#include <QTextStream>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace {

QString normalizedLocalPath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath()
                                               : canonical);
}

QString normalizedPathKey(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(
        normalizedLocalPath(path));
#ifdef Q_OS_WIN
    return normalized.toCaseFolded();
#else
    return normalized;
#endif
}

QString storedMetadataPathKey(const QString& path)
{
    const QString key = QDir::fromNativeSeparators(path);
#ifdef Q_OS_WIN
    return key.toCaseFolded();
#else
    return key;
#endif
}

const QStringList& aggregateFieldKeys()
{
    static const QStringList keys{
        QStringLiteral("title"), QStringLiteral("artist"),
        QStringLiteral("album"), QStringLiteral("albumArtist"),
        QStringLiteral("genre"), QStringLiteral("customTag"),
        QStringLiteral("date"), QStringLiteral("composer"),
        QStringLiteral("bpm")};
    return keys;
}

QString entryFieldValue(const MetadataEntry& entry, const QString& key)
{
    if (key == QLatin1String("title")) return entry.title;
    if (key == QLatin1String("artist")) return entry.artist;
    if (key == QLatin1String("album")) return entry.album;
    if (key == QLatin1String("albumArtist")) return entry.albumArtist;
    if (key == QLatin1String("genre")) return entry.genre;
    if (key == QLatin1String("customTag")) return entry.customTag;
    if (key == QLatin1String("date")) return entry.date;
    if (key == QLatin1String("composer")) return entry.composer;
    return entry.bpm;
}

bool validEditPayload(const QVariantMap& fields, QString& error)
{
    bool hasEdit = fields.value(QStringLiteral("coverMode"),
                                QStringLiteral("keep")).toString()
        != QLatin1String("keep");
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (it.key() == QLatin1String("coverMode")) continue;
        const QVariantMap descriptor = it.value().toMap();
        const QString mode = descriptor.value(QStringLiteral("mode"),
                                              QStringLiteral("keep")).toString();
        hasEdit = hasEdit || mode != QLatin1String("keep")
            || (descriptor.isEmpty() && !it.value().toString().isEmpty());
        if (mode == QLatin1String("set")
            && descriptor.value(QStringLiteral("value")).toString().trimmed().isEmpty()) {
            error = QObject::tr("“设为”不能为空；请改用“清除”。");
            return false;
        }
        if (it.key() == QLatin1String("bpm") && mode == QLatin1String("set")) {
            bool ok = false;
            const double bpm = descriptor.value(QStringLiteral("value")).toDouble(&ok);
            if (!ok || !std::isfinite(bpm) || bpm <= 0.0) {
                error = QObject::tr("BPM 必须是大于 0 的数字。");
                return false;
            }
        }
    }
    if (!hasEdit) {
        error = QObject::tr("请至少设置、清除一个字段，或修改封面。");
        return false;
    }
    return true;
}

agplayer::CanonicalField canonicalFieldForKey(const QString& key)
{
    if (key == QLatin1String("title")) return agplayer::CanonicalField::Title;
    if (key == QLatin1String("artist")) return agplayer::CanonicalField::Artist;
    if (key == QLatin1String("album")) return agplayer::CanonicalField::Album;
    if (key == QLatin1String("albumArtist")) return agplayer::CanonicalField::AlbumArtist;
    if (key == QLatin1String("genre")) return agplayer::CanonicalField::Genre;
    if (key == QLatin1String("customTag")) return agplayer::CanonicalField::CustomTag;
    if (key == QLatin1String("date")) return agplayer::CanonicalField::Date;
    if (key == QLatin1String("composer")) return agplayer::CanonicalField::Composer;
    return agplayer::CanonicalField::Bpm;
}

std::optional<agplayer::FieldEdit> editForField(const QVariantMap& fields,
                                                const QString& key)
{
    const QVariant value = fields.value(key);
    if (!value.isValid()) return std::nullopt;
    const QVariantMap descriptor = value.toMap();
    if (descriptor.isEmpty()) {
        const QString legacy = value.toString();
        if (legacy.isEmpty()) return std::nullopt;
        return agplayer::FieldEdit{canonicalFieldForKey(key),
                                   agplayer::MetadataAction::Set,
                                   legacy.toUtf8().toStdString()};
    }
    const QString mode = descriptor.value(QStringLiteral("mode"),
                                          QStringLiteral("keep")).toString();
    if (mode == QLatin1String("keep")) return std::nullopt;
    const auto action = mode == QLatin1String("clear")
        ? agplayer::MetadataAction::Clear : agplayer::MetadataAction::Set;
    return agplayer::FieldEdit{canonicalFieldForKey(key), action,
                               action == agplayer::MetadataAction::Set
                                   ? std::optional<std::string>(descriptor.value(
                                         QStringLiteral("value")).toString().toUtf8().toStdString())
                                   : std::nullopt};
}

agplayer::MetadataEditPlan planForPayload(const QVariantMap& fields,
                                          const QByteArray& coverData,
                                          const QByteArray& coverMime,
                                          const QString& coverMode)
{
    agplayer::MetadataEditPlan plan;
    const QString audioPolicy = fields.value(
        QStringLiteral("audioPolicy"),
        QStringLiteral("forceVerifiedNormalization")).toString();
    plan.audio_policy = audioPolicy == QLatin1String("strict")
        ? agplayer::MetadataAudioPolicy::StrictPacketIdentity
        : agplayer::MetadataAudioPolicy::ForceVerifiedNormalization;
    static const QStringList supportedKeys{
        QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"),
        QStringLiteral("albumArtist"), QStringLiteral("genre"), QStringLiteral("customTag"),
        QStringLiteral("date"), QStringLiteral("composer"), QStringLiteral("bpm")};
    for (const QString& key : supportedKeys) {
        if (const auto edit = editForField(fields, key); edit.has_value()) {
            plan.fields.push_back(*edit);
        }
    }
    if (coverMode == QLatin1String("set")) {
        plan.cover_action = agplayer::CoverAction::Set;
        plan.cover_data = reinterpret_cast<const unsigned char*>(coverData.constData());
        plan.cover_size = static_cast<std::size_t>(coverData.size());
        plan.cover_mime_type = coverMime.toStdString();
    } else if (coverMode == QLatin1String("clear")) {
        plan.cover_action = agplayer::CoverAction::Clear;
    }
    return plan;
}

} // namespace

MetadataEditor::MetadataEditor(QObject* parent)
    : QObject(parent)
{
}

MetadataEditor::~MetadataEditor()
{
    cancel();
    if (discoveryWatcher_ != nullptr) {
        discoveryWatcher_->future().waitForFinished();
    }
    if (loadWatcher_ != nullptr) {
        loadWatcher_->future().waitForFinished();
    }
    if (operationWatcher_ != nullptr) {
        operationWatcher_->future().waitForFinished();
    }
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
    return coverPath_.isEmpty() ? QString() : QUrl::fromLocalFile(coverPath_).toString();
}

void MetadataEditor::resetOperationState()
{
    requiresPreflightDecision_ = false;
    successCount_ = 0;
    failedCount_ = 0;
    supportedCount_ = 0;
    unsupportedCount_ = 0;
    cancelledCount_ = 0;
    pendingFields_.clear();
    pendingTargets_.clear();
    pendingSupportedTargets_.clear();
    pendingUnsupportedResults_.clear();
    emit statisticsChanged();
    emit preflightDecisionChanged();
}

void MetadataEditor::resetCover()
{
    if (!coverPath_.isEmpty() || !coverData_.isEmpty()
        || !replacementCoverDetails_.isEmpty()) {
        coverPath_.clear();
        coverData_.clear();
        coverMime_.clear();
        replacementCoverDetails_.clear();
        emit coverImageChanged();
    }
}

QString MetadataEditor::mimeTypeForFormat(const QByteArray& format)
{
    const QByteArray normalized = format.toLower();
    if (normalized == "png") {
        return QStringLiteral("image/png");
    }
    if (normalized == "jpg" || normalized == "jpeg") {
        return QStringLiteral("image/jpeg");
    }
    if (normalized == "bmp") {
        return QStringLiteral("image/bmp");
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
        emit errorOccurred(tr("Could not open the cover image: %1").arg(path));
        return;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        emit errorOccurred(tr("The cover image is empty: %1").arg(path));
        return;
    }
    constexpr qsizetype maxCoverBytes = 20 * 1024 * 1024;
    if (data.size() > maxCoverBytes) {
        emit errorOccurred(tr("The cover image cannot exceed 20 MB: %1").arg(path));
        return;
    }
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead()) {
        emit errorOccurred(tr("The selected file is not a readable image: %1").arg(path));
        return;
    }
    const QByteArray format = reader.format().toLower();
    const QString mime = mimeTypeForFormat(format);
    if (mime == QLatin1String("application/octet-stream")) {
        emit errorOccurred(tr("Cover images must contain PNG, JPEG, or BMP data: %1")
                               .arg(path));
        return;
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        emit errorOccurred(tr("The cover image could not be decoded completely: %1")
                               .arg(path));
        return;
    }
    if (image.width() > 4096 || image.height() > 4096) {
        emit errorOccurred(tr("The cover dimensions must not exceed 4096 x 4096: %1")
                               .arg(path));
        return;
    }

    coverPath_ = path;
    coverData_ = data;
    coverMime_ = mime;
    replacementCoverDetails_ = {
        {QStringLiteral("fileName"), QFileInfo(path).fileName()},
        {QStringLiteral("width"), image.width()},
        {QStringLiteral("height"), image.height()},
        {QStringLiteral("format"), QString::fromLatin1(format).toUpper()},
        {QStringLiteral("mimeType"), mime},
        {QStringLiteral("sizeBytes"), data.size()}};
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
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto* discovery = new QFutureWatcher<QList<QUrl>>(this);
    discoveryWatcher_ = discovery;
    connect(discovery, &QFutureWatcher<QList<QUrl>>::finished, this,
        [this, discovery]() {
            if (discoveryWatcher_ == discovery) {
                discoveryWatcher_.clear();
            }
            QList<QUrl> expandedUrls;
            try {
                expandedUrls = discovery->result();
            } catch (...) {
                discovery->deleteLater();
                setBusy(false);
                setProgress(1.0);
                emit entriesLoaded();
                emit errorOccurred(tr("Could not scan the selected audio files."));
                return;
            }
            discovery->deleteLater();
            QSet<QString> knownPaths;
            for (const MetadataEntry& entry : std::as_const(entries_)) {
                knownPaths.insert(normalizedPathKey(entry.path));
            }
            QList<QUrl> additions;
            additions.reserve(expandedUrls.size());
            for (const QUrl& url : std::as_const(expandedUrls)) {
                const QString normalized = normalizedLocalPath(url.toLocalFile());
                if (normalized.isEmpty()) continue;
                const QString key = normalizedPathKey(normalized);
                if (knownPaths.contains(key)) continue;
                knownPaths.insert(key);
                additions.append(QUrl::fromLocalFile(normalized));
            }
            if (additions.isEmpty()) {
                setBusy(false);
                setProgress(1.0);
                emit entriesLoaded();
                emit errorOccurred(tr("No new supported audio files were found."));
                return;
            }
            if (!results_.isEmpty()) {
                results_.clear();
                emit resultsChanged();
            }
            startMetadataLoad(std::move(additions));
        });
    discovery->setFuture(agplayer::qt::expandAudioUrlsAsync(urls));
}

void MetadataEditor::startMetadataLoad(QList<QUrl> expandedUrls)
{
    auto* watcher = new QFutureWatcher<QList<MetadataEntry>>(this);
    loadWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<QList<MetadataEntry>>::finished, this,
        [this, watcher]() {
            loadWatcher_.clear();
            QList<MetadataEntry> additions;
            try {
                additions = watcher->result();
            } catch (...) {
                setBusy(false);
                setProgress(1.0);
                emit entriesLoaded();
                emit errorOccurred(tr("Could not read metadata from the selected files."));
                watcher->deleteLater();
                return;
            }
            entries_.append(additions);
            setBusy(false);
            setProgress(1.0);
            if (!additions.isEmpty()) {
                emit fileCountChanged();
                emit entriesChanged();
            }
            emit entriesLoaded();
            watcher->deleteLater();
        });

    watcher->setFuture(QtConcurrent::run([expandedUrls = std::move(expandedUrls), this]() {
        QList<MetadataEntry> result;
        const int total = expandedUrls.size();
        for (int i = 0; i < total; ++i) {
            if (cancelFlag_.load(std::memory_order_acquire)) {
                break;
            }
            const QString path = expandedUrls[i].toLocalFile();
            MetadataEntry entry;
            entry.path = path;
            entry.fileName = QFileInfo(path).fileName();
            ag_metadata* md = nullptr;
            if (ag_metadata_open(path.toUtf8().constData(), &md) == AG_OK
                && md != nullptr) {
                entry.title = agplayer::qt::decodeMetadataText(ag_metadata_title(md));
                entry.artist = agplayer::qt::decodeMetadataText(ag_metadata_artist(md));
                entry.album = agplayer::qt::decodeMetadataText(ag_metadata_album(md));
                entry.albumArtist = agplayer::qt::decodeMetadataText(ag_metadata_album_artist(md));
                entry.year = agplayer::qt::decodeMetadataText(ag_metadata_year(md));
                entry.customTag = agplayer::qt::decodeMetadataText(ag_metadata_custom_tag(md));
                entry.date = agplayer::qt::decodeMetadataText(ag_metadata_date(md));
                entry.genre = agplayer::qt::decodeMetadataText(ag_metadata_genre(md));
                entry.track = agplayer::qt::decodeMetadataText(ag_metadata_track(md));
                entry.disc = agplayer::qt::decodeMetadataText(ag_metadata_disc(md));
                entry.composer = agplayer::qt::decodeMetadataText(ag_metadata_composer(md));
                entry.comment = agplayer::qt::decodeMetadataText(ag_metadata_comment(md));
                entry.bpm = agplayer::qt::decodeMetadataText(ag_metadata_bpm_tag(md));
                entry.copyright = agplayer::qt::decodeMetadataText(ag_metadata_copyright(md));
                entry.encoder = agplayer::qt::decodeMetadataText(ag_metadata_encoder(md));
                entry.lyrics = agplayer::qt::decodeMetadataText(ag_metadata_lyrics(md));
                entry.format = agplayer::qt::decodeMetadataText(ag_metadata_format(md));
                entry.durationMs = ag_metadata_duration_ms(md);
                size_t coverSize = 0;
                const char* coverMime = nullptr;
                const unsigned char* cover = ag_metadata_cover(md, &coverSize, &coverMime);
                entry.hasCover = cover != nullptr && coverSize > 0;
                if (entry.hasCover) {
                    const QByteArray coverBytes(
                        reinterpret_cast<const char*>(cover),
                        static_cast<qsizetype>(coverSize));
                    const QImage image = QImage::fromData(coverBytes);
                    if (!image.isNull()) {
                        const QImage thumbnail = image.scaled(160, 160,
                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        entry.coverPreview = QStringLiteral("data:image/png;base64,")
                            + QString::fromLatin1([&thumbnail] {
                                QByteArray data;
                                QBuffer buffer(&data);
                                buffer.open(QIODevice::WriteOnly);
                                thumbnail.save(&buffer, "PNG");
                                return data.toBase64();
                            }());
                        entry.coverInfo = QStringLiteral("%1 × %2 · %3 · %4 KB")
                            .arg(image.width()).arg(image.height())
                            .arg(QString::fromUtf8(coverMime != nullptr ? coverMime : "image"))
                            .arg(static_cast<qlonglong>(coverSize / 1024));
                        entry.coverFingerprint = QString::fromLatin1(
                            QCryptographicHash::hash(coverBytes,
                                QCryptographicHash::Sha256).toHex());
                        entry.coverMimeType = QString::fromUtf8(
                            coverMime != nullptr ? coverMime : "application/octet-stream");
                        entry.coverWidth = image.width();
                        entry.coverHeight = image.height();
                        entry.coverSizeBytes = static_cast<qint64>(coverSize);
                    }
                }
                ag_metadata_destroy(md);
            } else {
                entry.hasError = true;
                entry.error = tr("无法读取元数据");
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
    map["albumArtist"] = e.albumArtist;
    map["customTag"] = e.customTag;
    map["date"] = e.date;
    map["genre"] = e.genre;
    map["track"] = e.track;
    map["disc"] = e.disc;
    map["composer"] = e.composer;
    map["comment"] = e.comment;
    map["bpm"] = e.bpm;
    map["copyright"] = e.copyright;
    map["encoder"] = e.encoder;
    map["lyrics"] = e.lyrics;
    map["format"] = e.format;
    map["durationMs"] = e.durationMs;
    map["fileSize"] = e.fileSize;
    map["hasCover"] = e.hasCover;
    map["coverPreview"] = e.coverPreview;
    map["coverInfo"] = e.coverInfo;
    map["coverFingerprint"] = e.coverFingerprint;
    map["coverFileName"] = e.coverFileName;
    map["coverMimeType"] = e.coverMimeType;
    map["coverWidth"] = e.coverWidth;
    map["coverHeight"] = e.coverHeight;
    map["coverSizeBytes"] = e.coverSizeBytes;
    map["hasError"] = e.hasError;
    map["error"] = e.error;
    return map;
}

QVariantMap aggregate_metadata_entries(const QList<MetadataEntry>& entries,
                                       const QList<int>& indices)
{
    QList<int> validIndices;
    QSet<int> seen;
    validIndices.reserve(indices.size());
    for (const int index : indices) {
        if (index < 0 || index >= entries.size() || seen.contains(index)) continue;
        seen.insert(index);
        validIndices.append(index);
    }

    QVariantMap aggregate;
    for (const QString& key : aggregateFieldKeys()) {
        QString value;
        bool initialized = false;
        bool multiple = false;
        for (const int index : std::as_const(validIndices)) {
            const QString candidate = entryFieldValue(entries.at(index), key);
            if (!initialized) {
                value = candidate;
                initialized = true;
            } else if (candidate != value) {
                multiple = true;
                break;
            }
        }
        aggregate.insert(key, QVariantMap{
            {QStringLiteral("value"), multiple ? QString() : value},
            {QStringLiteral("multiple"), multiple}});
    }

    QVariantMap cover{{QStringLiteral("state"), QStringLiteral("none")}};
    QString fingerprint;
    bool initialized = false;
    bool multiple = false;
    const MetadataEntry* representative = nullptr;
    for (const int index : std::as_const(validIndices)) {
        const MetadataEntry& entry = entries.at(index);
        const QString candidate = entry.hasCover
            ? QStringLiteral("cover:") + entry.coverFingerprint
            : QStringLiteral("none");
        if (!initialized) {
            fingerprint = candidate;
            representative = &entry;
            initialized = true;
        } else if (candidate != fingerprint) {
            multiple = true;
            break;
        }
    }
    if (multiple) {
        cover.insert(QStringLiteral("state"), QStringLiteral("multiple"));
    } else if (representative != nullptr && representative->hasCover) {
        cover = {
            {QStringLiteral("state"), QStringLiteral("single")},
            {QStringLiteral("preview"), representative->coverPreview},
            {QStringLiteral("fileName"), representative->coverFileName},
            {QStringLiteral("width"), representative->coverWidth},
            {QStringLiteral("height"), representative->coverHeight},
            {QStringLiteral("mimeType"), representative->coverMimeType},
            {QStringLiteral("sizeBytes"), representative->coverSizeBytes},
            {QStringLiteral("info"), representative->coverInfo}};
    }
    aggregate.insert(QStringLiteral("cover"), cover);
    return aggregate;
}

QVariantMap MetadataEditor::aggregateMetadata(const QList<int>& indices) const
{
    return aggregate_metadata_entries(entries_, indices);
}

void MetadataEditor::removeFiles(const QList<int>& indices)
{
    if (busy_.load(std::memory_order_acquire) || indices.isEmpty()) {
        return;
    }
    QList<int> ordered = indices;
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    bool changed = false;
    for (const int index : ordered) {
        if (index >= 0 && index < entries_.size()) {
            entries_.removeAt(index);
            changed = true;
        }
    }
    if (changed) {
        emit fileCountChanged();
        emit entriesChanged();
    }
}

void MetadataEditor::startApply(const QVariantMap& fields,
                                const QList<int>& indices,
                                const QList<MetadataEntry>& snapshot)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    QString validationError;
    if (!validEditPayload(fields, validationError)) {
        emit errorOccurred(validationError);
        return;
    }
    const QString coverMode =
        fields.value(QStringLiteral("coverMode"), QStringLiteral("keep"))
            .toString();
    if (coverMode == QLatin1String("set") && coverData_.isEmpty()) {
        emit errorOccurred(tr("请选择封面图片"));
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

    auto* watcher = new QFutureWatcher<MetadataApplySummary>(this);
    const QPointer<LibraryModel> refreshModel = libraryModel_;
    QHash<QString, LibraryMetadataRefresh> libraryTargets;
    if (refreshModel != nullptr && refreshModel->thread() == thread()) {
        QSet<QString> targetPaths;
        for (const int target : targets) {
            if (target >= 0 && target < snapshot.size())
                targetPaths.insert(storedMetadataPathKey(snapshot.at(target).path));
        }
        for (const TrackRecord& track : refreshModel->tracks()) {
            const QString key = storedMetadataPathKey(track.path);
            if (!targetPaths.contains(key)) continue;
            const auto claim = refreshModel->beginMetadataRefresh(track.trackId);
            if (claim) {
                TrackRecord previous;
                previous.coverUrl = track.coverUrl;
                libraryTargets.insert(key, {*claim, std::move(previous)});
            }
        }
        // Also runs when the editor is destroyed before delivery. Exact
        // generations prevent cleanup from abandoning a later request.
        connect(watcher, &QObject::destroyed, refreshModel.data(),
                [refreshModel, libraryTargets] {
            if (refreshModel == nullptr) return;
            for (const LibraryMetadataRefresh& target : libraryTargets)
                refreshModel->abandonMetadataProbe(target.claim);
        });
    }
    operationWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<MetadataApplySummary>::finished, this,
        [this, watcher, refreshModel]() {
            operationWatcher_.clear();
            const auto result = watcher->result();
            // Paths were canonicalized when entries were loaded; the worker
            // copies those paths into both its snapshot and result rows.
            // Merge by the stored identity without touching the filesystem.
            QSet<QString> updatedPaths;
            for (const QVariant& value : result.results) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("success")).toBool()) {
                    updatedPaths.insert(storedMetadataPathKey(
                        row.value(QStringLiteral("path")).toString()));
                }
            }
            QHash<QString, qsizetype> rowByPath;
            rowByPath.reserve(entries_.size());
            for (qsizetype row = 0; row < entries_.size(); ++row) {
                const QString key = storedMetadataPathKey(entries_.at(row).path);
                if (!rowByPath.contains(key)) rowByPath.insert(key, row);
            }
            for (const MetadataEntry& updated : result.entries) {
                const QString key = storedMetadataPathKey(updated.path);
                if (!updatedPaths.contains(key)) continue;
                const auto row = rowByPath.constFind(key);
                if (row != rowByPath.cend()) {
                    entries_[row.value()] = updated;
                }
            }
            results_ = pendingUnsupportedResults_;
            results_.append(result.results);
            successCount_ = result.successCount;
            failedCount_ = result.failureCount;
            for (const QVariant& value : std::as_const(pendingUnsupportedResults_)) {
                if (value.toMap().value(QStringLiteral("status")).toString()
                    == QLatin1String("failed")) {
                    ++failedCount_;
                }
            }
            cancelledCount_ = result.cancelledCount;
            if (refreshModel != nullptr && refreshModel == libraryModel_) {
                refreshModel->completeMetadataRefreshes(result.libraryRefreshes);
            }
            setBusy(false);
            setProgress(1.0);
            emit entriesChanged();
            emit resultsChanged();
            emit statisticsChanged();
            emit metadataApplied(successCount_, failedCount_);
            watcher->deleteLater();
        });

    const QByteArray coverData = coverData_;
    const QByteArray coverMime = coverMime_.toUtf8();
    const QVariantMap coverDetails = replacementCoverDetails_;
    watcher->setFuture(QtConcurrent::run(
        [fields, targets, coverData, coverMime, coverDetails, coverMode, snapshot, libraryTargets,
          this]() mutable {
            MetadataApplySummary summary;
            summary.entries = snapshot;
            const auto appendPreparationFailure = [&summary](
                                                  const MetadataEntry& entry,
                                                  const QString& message) {
                QVariantMap fileResult;
                fileResult.insert(QStringLiteral("path"), entry.path);
                fileResult.insert(QStringLiteral("fileName"), entry.fileName);
                fileResult.insert(QStringLiteral("success"), false);
                fileResult.insert(QStringLiteral("stage"), QStringLiteral("prepare"));
                fileResult.insert(QStringLiteral("message"), message);
                fileResult.insert(QStringLiteral("errorCode"),
                                  static_cast<int>(
                                      agplayer::MetadataErrorCode::InternalError));
                summary.results.push_back(fileResult);
            };
            agplayer::MetadataEditPlan plan;
            try {
                plan = planForPayload(fields, coverData, coverMime, coverMode);
            } catch (const std::exception& exception) {
                summary.failureCount = targets.size();
                for (const int idx : targets) {
                    if (idx < 0 || idx >= summary.entries.size()) continue;
                    const MetadataEntry& entry = summary.entries.at(idx);
                    appendPreparationFailure(
                        entry, tr("无法准备元数据修改：%1")
                                   .arg(QString::fromUtf8(exception.what())));
                }
                return summary;
            } catch (...) {
                summary.failureCount = targets.size();
                for (const int idx : targets) {
                    if (idx < 0 || idx >= summary.entries.size()) continue;
                    const MetadataEntry& entry = summary.entries.at(idx);
                    appendPreparationFailure(entry, tr("无法准备元数据修改"));
                }
                return summary;
            }
            const int total = targets.size();
            for (int i = 0; i < total; ++i) {
                if (cancelFlag_.load(std::memory_order_acquire)) {
                    QVariantMap cancelled;
                    const int cancelledIndex = targets[i];
                    if (cancelledIndex >= 0 && cancelledIndex < summary.entries.size()) {
                        const MetadataEntry& entry = summary.entries.at(cancelledIndex);
                        cancelled.insert(QStringLiteral("path"), entry.path);
                        cancelled.insert(QStringLiteral("fileName"), entry.fileName);
                    }
                    cancelled.insert(QStringLiteral("success"), false);
                    cancelled.insert(QStringLiteral("stage"), QStringLiteral("cancelled"));
                    cancelled.insert(QStringLiteral("message"), tr("已取消"));
                    cancelled.insert(QStringLiteral("errorCode"),
                                     static_cast<int>(agplayer::MetadataErrorCode::Cancelled));
                    summary.results.push_back(cancelled);
                    ++summary.cancelledCount;
                    continue;
                }
                const int idx = targets[i];
                if (idx < 0 || idx >= summary.entries.size()) {
                    ++summary.failureCount;
                    continue;
                }
                MetadataEntry& e = summary.entries[idx];
                agplayer::MetadataFileResult writeResult;
                ag_result result = AG_INTERNAL_ERROR;
                const QFileInfo currentSource(e.path);
                const QString currentCanonicalPath = normalizedLocalPath(e.path);
                const QString currentStableSourceId = QStringLiteral("%1|%2|%3")
                    .arg(currentCanonicalPath)
                    .arg(currentSource.size())
                    .arg(currentSource.lastModified().toMSecsSinceEpoch());
                if (!currentSource.isFile()
                    || currentCanonicalPath != e.canonicalPath
                    || currentStableSourceId != e.stableSourceId) {
                    writeResult.message = tr("源文件在预检后发生变化，请重新预检")
                        .toStdString();
                    writeResult.error_code =
                        agplayer::MetadataErrorCode::SourceChanged;
                    writeResult.final_status =
                        agplayer::FileResultStatus::Failed;
                    result = AG_IO_ERROR;
                } else {
                    try {
                        result = agplayer::write_metadata_plan(
                            e.path.toUtf8().toStdString(), plan, writeResult,
                            &cancelFlag_);
                    } catch (const std::exception& exception) {
                        writeResult.message = tr("元数据写入异常：%1")
                                                  .arg(QString::fromUtf8(exception.what()))
                                                  .toStdString();
                        writeResult.error_code =
                            agplayer::MetadataErrorCode::InternalError;
                        writeResult.final_status =
                            agplayer::FileResultStatus::Failed;
                    } catch (...) {
                        writeResult.message = tr("元数据写入发生未知异常").toStdString();
                        writeResult.error_code =
                            agplayer::MetadataErrorCode::InternalError;
                        writeResult.final_status =
                            agplayer::FileResultStatus::Failed;
                    }
                }
                if (result == AG_OK) {
                    ++summary.successCount;
                    for (const agplayer::FieldResult& field : writeResult.fields) {
                        const QString actual = QString::fromUtf8(field.actual_value);
                        switch (field.field) {
                        case agplayer::CanonicalField::Title: e.title = actual; break;
                        case agplayer::CanonicalField::Artist: e.artist = actual; break;
                        case agplayer::CanonicalField::Album: e.album = actual; break;
                        case agplayer::CanonicalField::AlbumArtist: e.albumArtist = actual; break;
                        case agplayer::CanonicalField::Genre: e.genre = actual; break;
                        case agplayer::CanonicalField::Year: e.year = actual; break;
                        case agplayer::CanonicalField::Date: e.date = actual; break;
                        case agplayer::CanonicalField::Composer: e.composer = actual; break;
                        case agplayer::CanonicalField::Bpm: e.bpm = actual; break;
                        case agplayer::CanonicalField::CustomTag: e.customTag = actual; break;
                        }
                    }
                    ag_metadata* refreshed = nullptr;
                    if (ag_metadata_open(e.path.toUtf8().constData(), &refreshed) == AG_OK
                        && refreshed != nullptr) {
                        e.title = agplayer::qt::decodeMetadataText(ag_metadata_title(refreshed));
                        e.artist = agplayer::qt::decodeMetadataText(ag_metadata_artist(refreshed));
                        e.album = agplayer::qt::decodeMetadataText(ag_metadata_album(refreshed));
                        e.albumArtist = agplayer::qt::decodeMetadataText(ag_metadata_album_artist(refreshed));
                        e.genre = agplayer::qt::decodeMetadataText(ag_metadata_genre(refreshed));
                        e.year = agplayer::qt::decodeMetadataText(ag_metadata_year(refreshed));
                        e.customTag = agplayer::qt::decodeMetadataText(ag_metadata_custom_tag(refreshed));
                        e.date = agplayer::qt::decodeMetadataText(ag_metadata_date(refreshed));
                        e.composer = agplayer::qt::decodeMetadataText(ag_metadata_composer(refreshed));
                        e.bpm = agplayer::qt::decodeMetadataText(ag_metadata_bpm_tag(refreshed));
                        const auto target = libraryTargets.constFind(
                            storedMetadataPathKey(e.path));
                        if (target != libraryTargets.cend()) {
                            summary.libraryRefreshes.append({target->claim,
                                readLibraryMetadata(e.path, refreshed,
                                                    target->record.coverUrl)});
                        }
                        ag_metadata_destroy(refreshed);
                    }
                    if (plan.cover_action == agplayer::CoverAction::Set) {
                        e.hasCover = true;
                        const QImage thumbnail = QImage::fromData(coverData)
                            .scaled(160, 160, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
                        QByteArray thumbnailData;
                        QBuffer thumbnailBuffer(&thumbnailData);
                        thumbnailBuffer.open(QIODevice::WriteOnly);
                        thumbnail.save(&thumbnailBuffer, "PNG");
                        e.coverPreview = QStringLiteral("data:image/png;base64,")
                            + QString::fromLatin1(thumbnailData.toBase64());
                        e.coverFingerprint = QString::fromLatin1(
                            QCryptographicHash::hash(coverData,
                                QCryptographicHash::Sha256).toHex());
                        e.coverFileName = coverDetails.value(
                            QStringLiteral("fileName")).toString();
                        e.coverMimeType = QString::fromUtf8(coverMime);
                        e.coverWidth = coverDetails.value(
                            QStringLiteral("width")).toInt();
                        e.coverHeight = coverDetails.value(
                            QStringLiteral("height")).toInt();
                        e.coverSizeBytes = coverData.size();
                        e.coverInfo = QStringLiteral("%1 × %2 · %3 · %4 KB")
                            .arg(e.coverWidth).arg(e.coverHeight)
                            .arg(e.coverMimeType)
                            .arg(static_cast<qlonglong>(coverData.size() / 1024));
                    } else if (plan.cover_action == agplayer::CoverAction::Clear) {
                        e.hasCover = false;
                        e.coverPreview.clear();
                        e.coverInfo.clear();
                        e.coverFingerprint.clear();
                        e.coverFileName.clear();
                        e.coverMimeType.clear();
                        e.coverWidth = 0;
                        e.coverHeight = 0;
                        e.coverSizeBytes = 0;
                    }
                } else if (result == AG_CANCELLED) {
                    ++summary.cancelledCount;
                } else {
                    ++summary.failureCount;
                }
                QVariantMap fileResult;
                fileResult.insert(QStringLiteral("path"), e.path);
                fileResult.insert(QStringLiteral("fileName"), e.fileName);
                fileResult.insert(QStringLiteral("success"), result == AG_OK);
                fileResult.insert(QStringLiteral("status"),
                                  result == AG_OK ? QStringLiteral("completed")
                                  : result == AG_CANCELLED
                                      ? QStringLiteral("cancelled")
                                      : result == AG_UNSUPPORTED_FORMAT
                                          ? QStringLiteral("unsupported")
                                          : QStringLiteral("failed"));
                fileResult.insert(QStringLiteral("stage"),
                                  result == AG_OK ? QStringLiteral("verified")
                                  : result == AG_CANCELLED
                                      ? QStringLiteral("cancelled")
                                      : QStringLiteral("write"));
                fileResult.insert(QStringLiteral("message"),
                                  QString::fromStdString(writeResult.message));
                fileResult.insert(QStringLiteral("errorCode"),
                                  static_cast<int>(writeResult.error_code));
                fileResult.insert(QStringLiteral("resultCode"),
                                  static_cast<int>(result));
                fileResult.insert(QStringLiteral("usedStreamCopy"),
                                  writeResult.used_stream_copy);
                fileResult.insert(QStringLiteral("audioVerifiedUnchanged"),
                                  writeResult.audio_verified_unchanged);
                fileResult.insert(QStringLiteral("audioEquivalence"),
                                  static_cast<int>(writeResult.audio_equivalence));
                fileResult.insert(QStringLiteral("usedForceFallback"),
                                  writeResult.used_force_fallback);
                fileResult.insert(QStringLiteral("packetsCopied"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.packets_copied));
                fileResult.insert(QStringLiteral("decoderOpenCount"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.decoder_open_count));
                fileResult.insert(QStringLiteral("encoderOpenCount"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.encoder_open_count));
                fileResult.insert(QStringLiteral("audioStreamsBefore"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.audio_streams_before));
                fileResult.insert(QStringLiteral("audioStreamsAfter"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.audio_streams_after));
                fileResult.insert(QStringLiteral("chaptersBefore"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.chapters_before));
                fileResult.insert(QStringLiteral("chaptersAfter"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.chapters_after));
                fileResult.insert(QStringLiteral("attachmentsBefore"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.attachments_before));
                fileResult.insert(QStringLiteral("attachmentsAfter"),
                                  static_cast<qulonglong>(
                                      writeResult.runtime.attachments_after));
                QVariantList fieldResults;
                for (const agplayer::FieldResult& field : writeResult.fields) {
                    QVariantMap fieldResult;
                    fieldResult.insert(QStringLiteral("field"),
                                       static_cast<int>(field.field));
                    fieldResult.insert(QStringLiteral("action"),
                                       static_cast<int>(field.requested_action));
                    fieldResult.insert(QStringLiteral("status"),
                                       static_cast<int>(field.status));
                    fieldResult.insert(QStringLiteral("beforeValue"),
                                       QString::fromUtf8(field.before_value));
                    fieldResult.insert(QStringLiteral("requestedValue"),
                                       QString::fromUtf8(field.requested_value));
                    fieldResult.insert(QStringLiteral("actualValue"),
                                       QString::fromUtf8(field.actual_value));
                    fieldResult.insert(QStringLiteral("reason"),
                                       QString::fromUtf8(field.reason));
                    fieldResults.append(fieldResult);
                }
                fileResult.insert(QStringLiteral("fields"), fieldResults);
                QVariantMap coverResult;
                coverResult.insert(QStringLiteral("action"),
                                   static_cast<int>(writeResult.cover.requested_action));
                coverResult.insert(QStringLiteral("status"),
                                   static_cast<int>(writeResult.cover.status));
                coverResult.insert(QStringLiteral("hadCover"),
                                   writeResult.cover.had_cover);
                coverResult.insert(QStringLiteral("hasCover"),
                                   writeResult.cover.has_cover);
                coverResult.insert(QStringLiteral("mimeType"),
                                   QString::fromStdString(
                                       writeResult.cover.mime_type));
                fileResult.insert(QStringLiteral("cover"), coverResult);
                summary.results.push_back(fileResult);
                const double fraction = total > 0
                    ? static_cast<double>(i + 1) / total : 1.0;
                QMetaObject::invokeMethod(
                    this, [this, fraction]() { setProgress(fraction); },
                    Qt::QueuedConnection);
            }
            return summary;
        }));
}

void MetadataEditor::applyMetadata(const QVariantMap& fields,
                                   const QList<int>& indices)
{
    resetOperationState();
    startPreflight(fields, indices, true);
}

void MetadataEditor::preflightMetadata(const QVariantMap& fields,
                                       const QList<int>& indices)
{
    resetOperationState();
    startPreflight(fields, indices, false);
}

void MetadataEditor::startPreflight(const QVariantMap& fields,
                                    const QList<int>& indices,
                                    bool applyWhenSupported)
{
    if (busy_.load(std::memory_order_acquire)) return;
    QString validationError;
    if (!validEditPayload(fields, validationError)) {
        emit errorOccurred(validationError);
        return;
    }
    const QString coverMode = fields.value(QStringLiteral("coverMode"),
                                           QStringLiteral("keep")).toString();
    if (coverMode == QLatin1String("set") && coverData_.isEmpty()) {
        emit errorOccurred(tr("请选择封面图片"));
        return;
    }
    QList<int> targets = indices;
    if (targets.isEmpty()) {
        for (int i = 0; i < entries_.size(); ++i) targets.append(i);
    }
    if (targets.isEmpty()) {
        emit errorOccurred(tr("没有可处理的文件"));
        return;
    }

    pendingFields_ = fields;
    pendingTargets_ = targets;
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<MetadataApplySummary>(this);
    operationWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<MetadataApplySummary>::finished, this,
        [this, watcher, applyWhenSupported]() {
            operationWatcher_.clear();
            MetadataApplySummary summary;
            try {
                summary = watcher->result();
            } catch (...) {
                for (const int index : std::as_const(pendingTargets_)) {
                    if (index < 0 || index >= entries_.size()) continue;
                    const MetadataEntry& entry = entries_.at(index);
                    summary.results.append(QVariantMap{
                        {QStringLiteral("path"), entry.path},
                        {QStringLiteral("fileName"), entry.fileName},
                        {QStringLiteral("stage"), QStringLiteral("preflight")},
                        {QStringLiteral("success"), false},
                        {QStringLiteral("status"), QStringLiteral("failed")},
                        {QStringLiteral("message"),
                         tr("Metadata preflight failed unexpectedly.")},
                        {QStringLiteral("errorCode"), static_cast<int>(
                             agplayer::MetadataErrorCode::InternalError)}});
                    ++summary.failureCount;
                }
            }
            watcher->deleteLater();
            results_ = summary.results;
            pendingEntrySnapshot_ = summary.entries;
            supportedCount_ = summary.supportedCount;
            unsupportedCount_ = summary.unsupportedCount;
            failedCount_ = summary.failureCount;
            cancelledCount_ = summary.cancelledCount;
            pendingSupportedTargets_ = summary.supportedTargets;
            pendingUnsupportedResults_.clear();
            for (const QVariant& value : summary.results) {
                if (!value.toMap().value(QStringLiteral("success")).toBool()) {
                    pendingUnsupportedResults_.append(value);
                }
            }
            setBusy(false);
            setProgress(1.0);
            emit resultsChanged();
            emit statisticsChanged();
            emit preflightCompleted(supportedCount_, unsupportedCount_);

            if (!applyWhenSupported || cancelledCount_ > 0) return;
            if (unsupportedCount_ > 0) {
                requiresPreflightDecision_ = true;
                emit preflightDecisionChanged();
                emit preflightDecisionRequired(supportedCount_, unsupportedCount_);
                return;
            }
            if (pendingSupportedTargets_.isEmpty()) return;
            startApply(pendingFields_, pendingSupportedTargets_,
                       pendingEntrySnapshot_);
        });

    QList<MetadataEntry> snapshot = entries_;
    const QByteArray coverData = coverData_;
    const QByteArray coverMime = coverMime_.toUtf8();
    watcher->setFuture(QtConcurrent::run(
        [this, fields, targets, snapshot, coverData, coverMime, coverMode]() mutable {
            MetadataApplySummary summary;
            // File identity is part of preflight, so gather it on the same
            // worker as validation rather than blocking the caller per file.
            for (MetadataEntry& entry : snapshot) {
                const QFileInfo source(entry.path);
                entry.canonicalPath = normalizedLocalPath(entry.path);
                entry.fileSize = source.size();
                entry.sourceLastModifiedMs = source.lastModified().toMSecsSinceEpoch();
                entry.stableSourceId = QStringLiteral("%1|%2|%3")
                    .arg(entry.canonicalPath)
                    .arg(entry.fileSize)
                    .arg(entry.sourceLastModifiedMs);
            }
            summary.entries = snapshot;
            const auto appendInternalFailure = [&summary, &snapshot](
                                                   int index,
                                                   const QString& message) {
                if (index < 0 || index >= snapshot.size()) return;
                const MetadataEntry& entry = snapshot.at(index);
                QVariantMap item{
                    {QStringLiteral("path"), entry.path},
                    {QStringLiteral("fileName"), entry.fileName},
                    {QStringLiteral("stage"), QStringLiteral("preflight")},
                    {QStringLiteral("success"), false},
                    {QStringLiteral("status"), QStringLiteral("failed")},
                    {QStringLiteral("message"), message},
                    {QStringLiteral("preflightReason"), message},
                    {QStringLiteral("errorCode"), static_cast<int>(
                         agplayer::MetadataErrorCode::InternalError)}};
                summary.results.append(item);
                ++summary.failureCount;
            };
            agplayer::MetadataEditPlan plan;
            try {
                plan = planForPayload(fields, coverData, coverMime, coverMode);
            } catch (const std::exception& exception) {
                const QString message = tr("Metadata preflight setup failed: %1")
                    .arg(QString::fromUtf8(exception.what()));
                for (const int index : targets) {
                    appendInternalFailure(index, message);
                }
                return summary;
            } catch (...) {
                const QString message = tr("Metadata preflight setup failed.");
                for (const int index : targets) {
                    appendInternalFailure(index, message);
                }
                return summary;
            }
            const int total = targets.size();
            for (int i = 0; i < total; ++i) {
                QVariantMap item;
                const int index = targets.at(i);
                if (index < 0 || index >= snapshot.size()) {
                    summary.results.append(QVariantMap{
                        {QStringLiteral("path"), QString()},
                        {QStringLiteral("fileName"), QString()},
                        {QStringLiteral("stage"), QStringLiteral("preflight")},
                        {QStringLiteral("success"), false},
                        {QStringLiteral("status"), QStringLiteral("failed")},
                        {QStringLiteral("message"), tr("Metadata target is no longer available.")},
                        {QStringLiteral("preflightReason"),
                         tr("Metadata target is no longer available.")},
                        {QStringLiteral("errorCode"), static_cast<int>(
                             agplayer::MetadataErrorCode::InternalError)}});
                    ++summary.failureCount;
                    continue;
                }
                const MetadataEntry& entry = snapshot.at(index);
                item.insert(QStringLiteral("path"), entry.path);
                item.insert(QStringLiteral("fileName"), entry.fileName);
                item.insert(QStringLiteral("stage"), QStringLiteral("preflight"));
                if (cancelFlag_.load(std::memory_order_acquire)) {
                    item.insert(QStringLiteral("success"), false);
                    item.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
                    item.insert(QStringLiteral("message"), tr("已取消"));
                    item.insert(QStringLiteral("errorCode"),
                                static_cast<int>(agplayer::MetadataErrorCode::Cancelled));
                    ++summary.cancelledCount;
                    summary.results.append(item);
                    continue;
                }

                try {
                    agplayer::MetadataPreflightReport report;
                    const ag_result preflight = agplayer::preflight_metadata_edit(
                        entry.path.toUtf8().toStdString(), plan, report);
                    QTemporaryFile probe(QFileInfo(entry.path).dir().filePath(
                        QStringLiteral(".agplayer-metadata-write-probe-XXXXXX.tmp")));
                    const bool writable = probe.open();
                    const bool supported = preflight == AG_OK && writable
                        && !entry.hasError;
                    item.insert(QStringLiteral("success"), supported);
                    item.insert(QStringLiteral("status"), supported
                                ? QStringLiteral("supported")
                                : QStringLiteral("unsupported"));
                    item.insert(QStringLiteral("container"),
                                QString::fromStdString(report.container));
                    item.insert(QStringLiteral("errorCode"), static_cast<int>(
                        !writable ? agplayer::MetadataErrorCode::PermissionDenied
                        : report.error_code));
                    QVariantList unsupportedFields;
                    for (const agplayer::CanonicalField field
                         : report.unsupported_fields) {
                        unsupportedFields.append(static_cast<int>(field));
                    }
                    item.insert(QStringLiteral("unsupportedFields"),
                                unsupportedFields);
                    const QString reason = !writable ? tr("Target directory is not writable.")
                        : entry.hasError ? entry.error
                        : QString::fromStdString(report.user_message);
                    item.insert(QStringLiteral("preflightReason"),
                                supported ? QString() : reason);
                    item.insert(QStringLiteral("message"), supported
                                ? tr("Supported; waiting to process without audio re-encoding.")
                                : reason);
                    summary.results.append(item);
                    if (supported) {
                        ++summary.supportedCount;
                        summary.supportedTargets.append(index);
                    } else {
                        ++summary.unsupportedCount;
                    }
                } catch (const std::exception& exception) {
                    appendInternalFailure(
                        index, tr("Metadata preflight failed: %1")
                                   .arg(QString::fromUtf8(exception.what())));
                } catch (...) {
                    appendInternalFailure(index, tr("Metadata preflight failed."));
                }
                const double fraction = static_cast<double>(i + 1) / total;
                QMetaObject::invokeMethod(
                    this, [this, fraction]() { setProgress(fraction); },
                    Qt::QueuedConnection);
            }
            return summary;
        }));
}

void MetadataEditor::applyPreflightDecision(const QString& policy)
{
    if (!requiresPreflightDecision_ || busy_.load(std::memory_order_acquire)) return;
    requiresPreflightDecision_ = false;
    emit preflightDecisionChanged();
    if (policy == QLatin1String("supportedOnly")
        || policy == QLatin1String("skipUnsupported")) {
        if (pendingSupportedTargets_.isEmpty()) {
            return;
        }
        startApply(pendingFields_, pendingSupportedTargets_,
                   pendingEntrySnapshot_);
    }
}

bool MetadataEditor::exportResults(const QUrl& destination)
{
    const QString path = destination.toLocalFile();
    if (path.isEmpty() || results_.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        emit errorOccurred(tr("无法导出结果：%1").arg(path));
        return false;
    }
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "fileName,path,status,message,errorCode\n";
        for (const QVariant& value : results_) {
            const QVariantMap row = value.toMap();
            const auto csv = [](const QString& text) {
                QString escaped = text;
                escaped.replace('"', QStringLiteral("\"\""));
                return QStringLiteral("\"") + escaped + QStringLiteral("\"");
            };
            out << csv(row.value(QStringLiteral("fileName")).toString()) << ','
                << csv(row.value(QStringLiteral("path")).toString()) << ','
                << csv(row.value(QStringLiteral("stage")).toString()) << ','
                << csv(row.value(QStringLiteral("message")).toString()) << ','
                << row.value(QStringLiteral("errorCode")).toString() << '\n';
        }
    } else {
        QJsonArray rows;
        for (const QVariant& value : results_) {
            rows.append(QJsonObject::fromVariantMap(value.toMap()));
        }
        file.write(QJsonDocument(rows).toJson(QJsonDocument::Indented));
    }
    return true;
}

bool MetadataEditor::exportCurrentList(const QUrl& destination,
                                       const QList<int>& indices) const
{
    const QString path = destination.toLocalFile();
    if (path.isEmpty() || indices.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const auto rowFor = [this](int index) {
        QVariantMap row = entryAt(index);
        row.insert(QStringLiteral("index"), index);
        return row;
    };
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "fileName,path,title,artist,album,albumArtist,genre,customTag,composer,bpm\n";
        const auto csv = [](QString text) {
            text.replace('"', QStringLiteral("\"\""));
            return QStringLiteral("\"") + text + QStringLiteral("\"");
        };
        for (const int index : indices) {
            const QVariantMap row = rowFor(index);
            if (row.isEmpty()) continue;
            out << csv(row.value(QStringLiteral("fileName")).toString()) << ','
                << csv(row.value(QStringLiteral("path")).toString()) << ','
                << csv(row.value(QStringLiteral("title")).toString()) << ','
                << csv(row.value(QStringLiteral("artist")).toString()) << ','
                << csv(row.value(QStringLiteral("album")).toString()) << ','
                << csv(row.value(QStringLiteral("albumArtist")).toString()) << ','
                << csv(row.value(QStringLiteral("genre")).toString()) << ','
                << csv(row.value(QStringLiteral("customTag")).toString()) << ','
                << csv(row.value(QStringLiteral("composer")).toString()) << ','
                << csv(row.value(QStringLiteral("bpm")).toString()) << '\n';
        }
    } else {
        QJsonArray rows;
        for (const int index : indices) {
            const QVariantMap row = rowFor(index);
            if (!row.isEmpty()) rows.append(QJsonObject::fromVariantMap(row));
        }
        file.write(QJsonDocument(rows).toJson(QJsonDocument::Indented));
    }
    return true;
}

void MetadataEditor::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
}

void MetadataEditor::clear()
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    entries_.clear();
    results_.clear();
    resetCover();
    emit fileCountChanged();
    emit entriesChanged();
    emit resultsChanged();
}
