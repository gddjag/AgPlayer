#include "selection_drag_controller.hpp"

#include "audio_editor/document_render_pipeline.hpp"
#include "audio_editor/audio_source_probe.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMimeData>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QtConcurrent>

#include <cmath>
#include <filesystem>
#include <limits>

namespace {

QString defaultHandoffDirectory()
{
    return QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("AudioEditor/Handoff"));
}

QString safeFileStem(QString value)
{
    value.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")),
                  QStringLiteral("_"));
    value = value.simplified();
    while (value.endsWith(QLatin1Char('.'))
           || value.endsWith(QLatin1Char(' '))) {
        value.chop(1);
    }
    if (value.isEmpty()) value = QStringLiteral("clip");
    return value.left(160);
}

qint64 frameForMilliseconds(const qint64 milliseconds,
                            const std::uint32_t sampleRate)
{
    return static_cast<qint64>(std::llround(
        static_cast<long double>(milliseconds)
        * static_cast<long double>(sampleRate) / 1000.0L));
}

bool materializePlaybackClip(HandoffRequest& request,
                             const std::atomic_bool* cancelled,
                             QString& error)
{
    if (!request.playbackClip) return true;
    const auto clip = *request.playbackClip;
    if (clip.path.isEmpty() || clip.startMs < 0
        || clip.endMs - clip.startMs < 100) {
        error = QStringLiteral("拖出音频选区无效");
        return false;
    }
    const auto probe = agplayer::editor::AudioSourceProbe::probe(
        std::filesystem::path(clip.path.toStdWString()),
        std::chrono::steady_clock::now() + std::chrono::seconds(10),
        cancelled);
    if (!probe.ok()) {
        error = QString::fromStdString(probe.message.empty()
            ? std::string("无法读取拖出音频源") : probe.message);
        return false;
    }
    const qint64 startFrame = std::clamp<qint64>(
        frameForMilliseconds(clip.startMs, probe.source.sample_rate), 0,
        probe.source.total_frames);
    const qint64 endFrame = std::clamp<qint64>(
        frameForMilliseconds(clip.endMs, probe.source.sample_rate), 0,
        probe.source.total_frames);
    if (endFrame <= startFrame) {
        error = QStringLiteral("拖出音频选区超出源文件范围");
        return false;
    }
    request.snapshot = agplayer::editor::AudioDocument::fromSource(probe.source)
        .timelineSnapshot();
    request.selection = {startFrame, endFrame};
    request.timelineRevision = request.snapshot.revision;
    request.renderState.sampleRate = static_cast<int>(probe.source.sample_rate);
    request.renderState.channels = static_cast<int>(probe.source.channels);
    request.renderState.trackGain = 1.0F;
    request.renderState.muted = false;
    request.renderState.speedPercent = 100.0;
    request.renderState.pitchCents = 0;
    request.renderState.keepPitch = true;
    request.renderState.formantPreservation = false;
    request.playbackClip.reset();
    return true;
}

QString cacheKey(const HandoffRequest& request)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add = [&hash](const QString& value) {
        const QByteArray bytes = value.toUtf8();
        hash.addData(QByteArrayView(bytes));
        hash.addData(QByteArrayView("\0", 1));
    };
    add(request.sourceIdentity);
    add(QString::number(request.snapshot.totalFrames));
    for (const auto& event : request.snapshot.events) {
        if (event.source) {
            add(QString::fromStdWString(event.source->path.wstring()));
            add(QString::number(event.source->sample_rate));
            add(QString::number(event.source->channels));
            add(QString::number(event.source->total_frames));
        } else {
            add(QStringLiteral("missing-source"));
        }
        add(QString::number(event.sourceStart));
        add(QString::number(event.sourceEnd));
        add(QString::number(event.timelineStart));
        add(QString::number(event.gain, 'g',
                            std::numeric_limits<float>::max_digits10));
        add(QString::number(event.fadeIn));
        add(QString::number(event.fadeOut));
        add(QString::number(static_cast<int>(event.fadeInCurve)));
        add(QString::number(static_cast<int>(event.fadeOutCurve)));
        add(QString::number(event.speedRatio, 'g',
                            std::numeric_limits<double>::max_digits10));
        add(QString::number(event.pitchSemitone));
        add(event.mute ? QStringLiteral("event-muted")
                       : QStringLiteral("event-audible"));
        for (const auto& point : event.envelope) {
            add(QString::number(point.offset));
            add(QString::number(point.gain, 'g',
                                std::numeric_limits<float>::max_digits10));
        }
        add(QStringLiteral("end-event"));
    }
    add(QString::number(request.selection.start));
    add(QString::number(request.selection.end));
    add(QString::number(request.timelineRevision));
    add(QString::number(request.renderState.sampleRate));
    add(QString::number(request.renderState.channels));
    add(QString::number(request.renderState.trackGain, 'g', 9));
    add(request.renderState.muted ? QStringLiteral("muted")
                                  : QStringLiteral("audible"));
    add(QString::number(request.renderState.speedPercent, 'g', 17));
    add(QString::number(request.renderState.pitchCents));
    add(request.renderState.keepPitch ? QStringLiteral("keep-pitch")
                                      : QStringLiteral("coupled-pitch"));
    add(request.renderState.formantPreservation
            ? QStringLiteral("preserve-formants")
            : QStringLiteral("shift-formants"));
    return QString::fromLatin1(hash.result().toHex());
}

HandoffAssetResult verifiedAsset(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, {}, {},
                QStringLiteral("拖出音频文件验证失败：无法打开")};
    }
    const qint64 size = file.size();
    const QByteArray header = file.read(4);
    if (size < 44 || header != QByteArray("RIFF", 4)) {
        return {false, {}, {},
                QStringLiteral("拖出音频文件验证失败：size=%1 header=%2")
                    .arg(size)
                    .arg(QString::fromLatin1(header.toHex()))};
    }
    file.close();
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {false, {}, {}, QStringLiteral("拖出音频文件验证失败")};
    }
    const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
    if (!url.isValid() || !url.isLocalFile()) {
        return {false, {}, {}, QStringLiteral("拖出音频 URI 无效")};
    }
    return {true, info.absoluteFilePath(), url, {}};
}

} // namespace

HandoffAssetManager::HandoffAssetManager(QString directory)
    : directory_(directory.isEmpty() ? defaultHandoffDirectory()
                                     : std::move(directory))
{
}

HandoffAssetResult HandoffAssetManager::prepare(
    const HandoffRequest& request, const std::atomic_bool* cancelled) const
{
    HandoffRequest renderRequest = request;
    QString materializeError;
    if (!materializePlaybackClip(renderRequest, cancelled, materializeError)) {
        return {false, {}, {}, materializeError};
    }
    if (renderRequest.snapshot.events.empty()
        || !renderRequest.selection.valid()
        || renderRequest.sourceIdentity.isEmpty()
        || renderRequest.renderState.sampleRate <= 0
        || renderRequest.renderState.channels <= 0) {
        return {false, {}, {}, QStringLiteral("拖出音频请求无效")};
    }
    if (cancelled && cancelled->load(std::memory_order_acquire)) {
        return {false, {}, {}, QStringLiteral("拖出音频已取消")};
    }
    QDir outputDirectory(directory_);
    if (!outputDirectory.mkpath(QStringLiteral("."))) {
        return {false, {}, {}, QStringLiteral("无法创建拖出音频目录")};
    }
    const QString key = cacheKey(renderRequest);
    const QString fileName = renderRequest.outputFileStem.isEmpty()
        ? QStringLiteral("handoff-%1.wav").arg(key)
        : QStringLiteral("%1_%2.wav")
              .arg(safeFileStem(renderRequest.outputFileStem), key.left(8));
    const QString path = outputDirectory.filePath(fileName);
    if (QFileInfo::exists(path)) {
        const auto cached = verifiedAsset(path);
        if (cached.success) return cached;
    }

    agplayer::editor::WriteRequest write;
    write.snapshot = renderRequest.snapshot;
    write.output_path = std::filesystem::path(path.toStdWString());
    write.codec_name = "pcm_s24le";
    write.sample_rate = renderRequest.renderState.sampleRate;
    write.channels = renderRequest.renderState.channels;
    write.keep_metadata = false;
    write.range = renderRequest.selection;
    agplayer::editor::TimePitchSession timePitch;
    if (!timePitch.setSpeedPercent(renderRequest.renderState.speedPercent)
        && std::abs(timePitch.speedPercent()
                    - renderRequest.renderState.speedPercent) > 0.001) {
        return {false, {}, {}, QStringLiteral("拖出音频变速参数无效")};
    }
    const int semitones = renderRequest.renderState.pitchCents / 100;
    const int cents = renderRequest.renderState.pitchCents - semitones * 100;
    if (!timePitch.setPitch(semitones, cents)
        && timePitch.pitchCents() != renderRequest.renderState.pitchCents) {
        return {false, {}, {}, QStringLiteral("拖出音频变调参数无效")};
    }
    timePitch.setKeepPitch(renderRequest.renderState.keepPitch);
    timePitch.setFormantPreservation(
        renderRequest.renderState.formantPreservation);
    const auto result = agplayer::editor::DocumentRenderPipeline{}.write(
        write, timePitch, cancelled);
    if (!result.ok()) {
        return {false, {}, {}, QString::fromStdString(result.message)};
    }
    return verifiedAsset(path);
}

SelectionDragController::SelectionDragController(
    PrepareFunction prepare, DragFunction drag, QObject* parent)
    : QObject(parent), prepare_(std::move(prepare)), drag_(std::move(drag))
{
}

SelectionDragController::SelectionDragController(
    HandoffAssetManager* assets, QObject* parent)
    : SelectionDragController(
          [assets](const HandoffRequest& request,
                   const std::atomic_bool* cancelled) {
              return assets ? assets->prepare(request, cancelled)
                            : HandoffAssetResult{
                                  false, {}, {},
                                  QStringLiteral("拖出音频服务不可用")};
          }, {}, parent)
{
}

SelectionDragController::~SelectionDragController()
{
    cancel();
    if (watcher_) watcher_->waitForFinished();
}

void SelectionDragController::begin(
    const QPointF scenePosition, HandoffRequest request)
{
    cancel();
    press_position_ = scenePosition;
    request_ = std::move(request);
    cancel_token_ = std::make_shared<std::atomic_bool>(false);
    active_ = true;
    triggered_ = false;
    preparing_ = false;
    ++generation_;
}

void SelectionDragController::update(const QPointF scenePosition)
{
    if (!active_ || triggered_) return;
    const QPointF delta = scenePosition - press_position_;
    const qreal distance = std::abs(delta.x()) + std::abs(delta.y());
    if (distance < QApplication::startDragDistance()) return;
    triggered_ = true;
    startPrepare();
}

void SelectionDragController::release()
{
    cancel();
}

void SelectionDragController::cancel()
{
    ++generation_;
    active_ = false;
    triggered_ = false;
    if (preparing_) {
        preparing_ = false;
        emit preparingChanged();
    }
    if (cancel_token_) {
        cancel_token_->store(true, std::memory_order_release);
    }
}

void SelectionDragController::startPrepare()
{
    if (!prepare_ || watcher_) return;
    preparing_ = true;
    emit preparingChanged();
    const HandoffRequest request = request_;
    const auto cancelToken = cancel_token_;
    const quint64 generation = generation_;
    auto* watcher = new QFutureWatcher<HandoffAssetResult>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<HandoffAssetResult>::finished,
            this, [this, watcher, cancelToken, generation] {
        watcher_ = nullptr;
        const HandoffAssetResult result = watcher->result();
        watcher->deleteLater();
        if (generation == generation_ && preparing_) {
            preparing_ = false;
            emit preparingChanged();
        }
        if (generation == generation_) {
            if (!cancelToken->load(std::memory_order_acquire)
                && result.success && triggered_ && active_) {
                emit handoffReady(result.url);
                launchDrag(result.url);
                active_ = false;
            } else if (!cancelToken->load(std::memory_order_acquire)
                       && !result.error.isEmpty()) {
                emit errorOccurred(result.error);
                active_ = false;
            }
        } else if (active_) {
            startPrepare();
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [prepare = prepare_, request, cancelToken] {
            return prepare(request, cancelToken.get());
        }));
}

void SelectionDragController::launchDrag(const QUrl& url)
{
    if (drag_) {
        drag_(url);
        return;
    }
    QDrag drag(this);
    auto* mime = new QMimeData;
    mime->setUrls({url});
    drag.setMimeData(mime);
    drag.exec(Qt::CopyAction);
}
