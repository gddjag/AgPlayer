#include "metadata_editor.hpp"

#include "audio_file_discovery.hpp"
#include "metadata_writer.hpp"

#include "agplayer/c_api.h"

#include <QDir>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace {

bool validEditPayload(const QVariantMap& fields, QString& error)
{
    bool editsYear = false;
    bool editsDate = false;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (it.key() == QLatin1String("coverMode")) continue;
        const QVariantMap descriptor = it.value().toMap();
        const QString mode = descriptor.value(QStringLiteral("mode"),
                                              QStringLiteral("keep")).toString();
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
        editsYear = editsYear || (it.key() == QLatin1String("year") && mode != QLatin1String("keep"));
        editsDate = editsDate || (it.key() == QLatin1String("date") && mode != QLatin1String("keep"));
    }
    if (editsYear && editsDate) {
        error = QObject::tr("年份和日期映射到同一标签，请只编辑其中一项。");
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
    if (key == QLatin1String("year")) return agplayer::CanonicalField::Year;
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
    static const QStringList supportedKeys{
        QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"),
        QStringLiteral("albumArtist"), QStringLiteral("genre"), QStringLiteral("year"),
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
    const QString mime = mimeTypeForImage(path);
    if (mime != QLatin1String("image/png") && mime != QLatin1String("image/jpeg")
        && mime != QLatin1String("image/bmp")) {
        emit errorOccurred(tr("封面仅支持 PNG、JPEG 或 BMP：%1").arg(path));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("无法打开封面图片：%1").arg(path));
        return;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        emit errorOccurred(tr("封面图片内容为空：%1").arg(path));
        return;
    }
    constexpr qsizetype maxCoverBytes = 20 * 1024 * 1024;
    if (data.size() > maxCoverBytes) {
        emit errorOccurred(tr("封面图片不能超过 20 MB：%1").arg(path));
        return;
    }
    QImageReader reader(path);
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || dimensions.width() > 4096 || dimensions.height() > 4096) {
        emit errorOccurred(tr("封面尺寸必须在 4096 × 4096 以内：%1").arg(path));
        return;
    }

    coverPath_ = path;
    coverData_ = data;
    coverMime_ = mime;
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
    if (!results_.isEmpty()) {
        results_.clear();
        emit resultsChanged();
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
            const QList<QUrl> expandedUrls = discovery->result();
            discovery->deleteLater();
            if (expandedUrls.isEmpty()) {
                entries_.clear();
                setBusy(false);
                setProgress(1.0);
                emit fileCountChanged();
                emit entriesLoaded();
                emit errorOccurred(tr("未找到支持的音频文件"));
                return;
            }
            startMetadataLoad(expandedUrls);
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
            entries_ = watcher->result();
            setBusy(false);
            setProgress(1.0);
            emit fileCountChanged();
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
                entry.title = QString::fromUtf8(ag_metadata_title(md));
                entry.artist = QString::fromUtf8(ag_metadata_artist(md));
                entry.album = QString::fromUtf8(ag_metadata_album(md));
                entry.albumArtist = QString::fromUtf8(ag_metadata_album_artist(md));
                entry.year = QString::fromUtf8(ag_metadata_year(md));
                entry.genre = QString::fromUtf8(ag_metadata_genre(md));
                entry.track = QString::fromUtf8(ag_metadata_track(md));
                entry.disc = QString::fromUtf8(ag_metadata_disc(md));
                entry.composer = QString::fromUtf8(ag_metadata_composer(md));
                entry.comment = QString::fromUtf8(ag_metadata_comment(md));
                entry.bpm = QString::fromUtf8(ag_metadata_bpm_tag(md));
                entry.copyright = QString::fromUtf8(ag_metadata_copyright(md));
                entry.encoder = QString::fromUtf8(ag_metadata_encoder(md));
                entry.lyrics = QString::fromUtf8(ag_metadata_lyrics(md));
                entry.format = QString::fromUtf8(ag_metadata_format(md));
                entry.durationMs = ag_metadata_duration_ms(md);
                size_t coverSize = 0;
                const char* coverMime = nullptr;
                const unsigned char* cover = ag_metadata_cover(md, &coverSize, &coverMime);
                entry.hasCover = cover != nullptr && coverSize > 0;
                if (entry.hasCover) {
                    const QImage image = QImage::fromData(
                        reinterpret_cast<const uchar*>(cover),
                        static_cast<int>(coverSize));
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
    map["year"] = e.year;
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
    map["hasError"] = e.hasError;
    map["error"] = e.error;
    return map;
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

void MetadataEditor::applyMetadata(const QVariantMap& fields,
                                   const QList<int>& indices)
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
    operationWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<MetadataApplySummary>::finished, this,
        [this, watcher]() {
            operationWatcher_.clear();
            const auto result = watcher->result();
            entries_ = result.entries;
            results_ = result.results;
            setBusy(false);
            setProgress(1.0);
            emit entriesChanged();
            emit resultsChanged();
            emit metadataApplied(result.successCount, result.failureCount);
            watcher->deleteLater();
        });

    const QByteArray coverData = coverData_;
    const QByteArray coverMime = coverMime_.toUtf8();
    const QList<MetadataEntry> snapshot = entries_;

    watcher->setFuture(QtConcurrent::run(
        [fields, targets, coverData, coverMime, coverMode, snapshot,
         this]() mutable {
            const agplayer::MetadataEditPlan plan =
                planForPayload(fields, coverData, coverMime, coverMode);
            MetadataApplySummary summary;
            summary.entries = snapshot;
            const int total = targets.size();
            for (int i = 0; i < total; ++i) {
                if (cancelFlag_.load(std::memory_order_acquire)) {
                    break;
                }
                const int idx = targets[i];
                if (idx < 0 || idx >= summary.entries.size()) {
                    ++summary.failureCount;
                    continue;
                }
                MetadataEntry& e = summary.entries[idx];
                agplayer::MetadataFileResult writeResult;
                const ag_result result = agplayer::write_metadata_plan(
                    e.path.toUtf8().toStdString(), plan, writeResult);
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
                        case agplayer::CanonicalField::Year:
                        case agplayer::CanonicalField::Date: e.year = actual; break;
                        case agplayer::CanonicalField::Composer: e.composer = actual; break;
                        case agplayer::CanonicalField::Bpm: e.bpm = actual; break;
                        }
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
                        e.coverInfo = tr("新封面 · %1 KB")
                            .arg(static_cast<qlonglong>(coverData.size() / 1024));
                    } else if (plan.cover_action == agplayer::CoverAction::Clear) {
                        e.hasCover = false;
                        e.coverPreview.clear();
                        e.coverInfo.clear();
                    }
                } else {
                    ++summary.failureCount;
                }
                QVariantMap fileResult;
                fileResult.insert(QStringLiteral("path"), e.path);
                fileResult.insert(QStringLiteral("fileName"), e.fileName);
                fileResult.insert(QStringLiteral("success"), result == AG_OK);
                fileResult.insert(QStringLiteral("stage"),
                                  result == AG_OK ? QStringLiteral("verified")
                                                  : QStringLiteral("write"));
                fileResult.insert(QStringLiteral("message"),
                                  result == AG_OK
                                      ? tr("已验证元数据与音频流，已完成替换")
                                      : QString::fromStdString(writeResult.message));
                fileResult.insert(QStringLiteral("errorCode"),
                                  static_cast<int>(result));
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

void MetadataEditor::preflightMetadata(const QVariantMap& fields,
                                       const QList<int>& indices)
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
    const agplayer::MetadataEditPlan plan = planForPayload(fields, coverData_,
                                                            coverMime_.toUtf8(), coverMode);
    QList<int> targets = indices;
    if (targets.isEmpty()) {
        for (int i = 0; i < entries_.size(); ++i) targets.append(i);
    }
    QVariantList preview;
    for (const int index : targets) {
        if (index < 0 || index >= entries_.size()) continue;
        const MetadataEntry& entry = entries_.at(index);
        QVariantMap item;
        item.insert(QStringLiteral("path"), entry.path);
        item.insert(QStringLiteral("fileName"), entry.fileName);
        const QFileInfo info(entry.path);
        std::string preflightError;
        const ag_result preflight = agplayer::preflight_metadata_edit(
            entry.path.toUtf8().toStdString(), plan, preflightError);
        const bool readable = preflight == AG_OK;
        const QString probePath = info.dir().filePath(
            QStringLiteral(".agplayer-metadata-write-probe-%1.tmp")
                .arg(QCoreApplication::applicationPid()));
        QFile probe(probePath);
        const bool writable = probe.open(QIODevice::WriteOnly | QIODevice::NewOnly);
        if (writable) {
            probe.close();
            QFile::remove(probePath);
        }
        item.insert(QStringLiteral("success"), readable && writable && !entry.hasError);
        item.insert(QStringLiteral("stage"), QStringLiteral("preflight"));
        item.insert(QStringLiteral("errorCode"), readable
                    ? QString() : QString::fromStdString(preflightError));
        item.insert(QStringLiteral("preflightReason"), readable
                    ? QString() : QString::fromStdString(preflightError));
        item.insert(QStringLiteral("message"), !readable ? tr("文件不可读")
                   : !writable ? tr("目标目录不可用")
                   : entry.hasError ? entry.error : tr("可使用流复制处理"));
        preview.push_back(item);
    }
    results_ = preview;
    emit resultsChanged();
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
