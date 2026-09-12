#include "playback_clip_drag_adapter.hpp"

#include "library_model.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString defaultClipDirectory()
{
    QString root = QStandardPaths::writableLocation(
        QStandardPaths::MusicLocation);
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
    }
    return QDir(root).filePath(QStringLiteral("AgPlayer Drag Clips"));
}

QString safeTitle(QString title)
{
    title.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")),
                  QStringLiteral("_"));
    title = title.simplified();
    while (title.endsWith(QLatin1Char('.'))
           || title.endsWith(QLatin1Char(' '))) {
        title.chop(1);
    }
    return title.isEmpty() ? QStringLiteral("clip") : title.left(96);
}

QString timeStamp(const qint64 milliseconds)
{
    const qint64 minutes = milliseconds / 60'000;
    const qint64 seconds = (milliseconds / 1000) % 60;
    const qint64 millis = milliseconds % 1000;
    return QStringLiteral("%1m%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString sourceIdentity(const QFileInfo& file)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(file.absoluteFilePath().toUtf8());
    hash.addData(QByteArray::number(file.size()));
    hash.addData(QByteArray::number(
        file.lastModified().toUTC().toMSecsSinceEpoch()));
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

PlaybackClipDragAdapter::PlaybackClipDragAdapter(LibraryModel* library,
                                                 QObject* parent)
    : PlaybackClipDragAdapter(library, defaultClipDirectory(), {}, parent)
{
}

PlaybackClipDragAdapter::PlaybackClipDragAdapter(
    LibraryModel* library, QString outputDirectory,
    SelectionDragController::DragFunction drag, QObject* parent)
    : QObject(parent), library_(library),
      assets_(outputDirectory.isEmpty() ? defaultClipDirectory()
                                        : std::move(outputDirectory))
{
    drag_controller_ = std::make_unique<SelectionDragController>(
        [this](const HandoffRequest& request,
               const std::atomic_bool* cancelled) {
            return assets_.prepare(request, cancelled);
        }, std::move(drag), this);
    connect(drag_controller_.get(),
            &SelectionDragController::preparingChanged,
            this, &PlaybackClipDragAdapter::preparingChanged);
    connect(drag_controller_.get(), &SelectionDragController::handoffReady,
            this, &PlaybackClipDragAdapter::handoffReady);
    connect(drag_controller_.get(), &SelectionDragController::errorOccurred,
            this, [this](const QString& message) {
        setError(message);
        emit errorOccurred(message);
    });
}

PlaybackClipDragAdapter::~PlaybackClipDragAdapter() = default;

bool PlaybackClipDragAdapter::preparing() const noexcept
{
    return drag_controller_ && drag_controller_->preparing();
}

bool PlaybackClipDragAdapter::begin(
    const double sceneX, const double sceneY, const QString& trackId,
    const qint64 selectionStartMs, const qint64 selectionEndMs)
{
    if (!library_ || trackId.isEmpty()
        || selectionStartMs < 0
        || selectionEndMs - selectionStartMs < 100) {
        setError(QStringLiteral("拖出音频选区无效"));
        return false;
    }
    const TrackRecord* const track = library_->recordForId(trackId);
    const QFileInfo source(track ? track->path : QString{});
    if (!track || !track->available || !source.isFile()) {
        setError(QStringLiteral("当前歌曲文件不可用"));
        return false;
    }
    if (track->durationMs > 0 && selectionEndMs > track->durationMs + 1) {
        setError(QStringLiteral("拖出音频选区超出歌曲时长"));
        return false;
    }

    HandoffRequest request;
    request.sourceIdentity = sourceIdentity(source);
    request.outputFileStem = QStringLiteral("%1_片段_%2-%3")
        .arg(safeTitle(track->title), timeStamp(selectionStartMs),
             timeStamp(selectionEndMs));
    request.playbackClip = HandoffRequest::PlaybackClipSource{
        source.absoluteFilePath(), track->title,
        selectionStartMs, selectionEndMs};
    setError({});
    drag_controller_->begin(QPointF(sceneX, sceneY), std::move(request));
    return true;
}

void PlaybackClipDragAdapter::update(const double sceneX, const double sceneY)
{
    if (drag_controller_) drag_controller_->update(QPointF(sceneX, sceneY));
}

void PlaybackClipDragAdapter::cancel()
{
    if (drag_controller_) drag_controller_->cancel();
}

void PlaybackClipDragAdapter::setError(QString message)
{
    if (error_message_ == message) return;
    error_message_ = std::move(message);
    emit errorMessageChanged();
}
