#include "video_frame_item.hpp"

#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QTransform>

#include <algorithm>
#include <atomic>
#include <limits>

struct VideoFrameItemRenderStats final {
#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
    std::atomic<int> updateRequests{0};
    std::atomic<int> textureUploads{0};
    std::atomic<int> liveTextureNodes{0};
    std::atomic<quint64> renderedSerial{0};
    mutable QMutex geometryMutex;
    QRectF renderedRect;
#endif
};

namespace {

int normalizedRotation(const int degrees) noexcept
{
    switch (degrees) {
    case 90:
    case 180:
    case 270:
        return degrees;
    default:
        return 0;
    }
}

bool isValidFrame(const VideoFrameSnapshot& frame) noexcept
{
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return false;
    }
    if (frame.width > std::numeric_limits<int>::max() / 4
        || frame.stride < frame.width * 4
        || frame.height > std::numeric_limits<int>::max() / frame.stride) {
        return false;
    }
    return frame.pixels.size() >= frame.stride * frame.height;
}

QRectF aspectFitRect(const QSizeF& bounds,
                     const VideoFrameSnapshot& frame) noexcept
{
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0
        || !isValidFrame(frame)) {
        return {};
    }

    const qreal sarNum = static_cast<qreal>(std::max(1, frame.sarNum));
    const qreal sarDen = static_cast<qreal>(std::max(1, frame.sarDen));
    qreal displayWidth = static_cast<qreal>(frame.width) * sarNum / sarDen;
    qreal displayHeight = static_cast<qreal>(frame.height);
    const int rotation = normalizedRotation(frame.rotationDegrees);
    if (rotation == 90 || rotation == 270) {
        std::swap(displayWidth, displayHeight);
    }
    if (displayWidth <= 0.0 || displayHeight <= 0.0) return {};

    const qreal scale = std::min(bounds.width() / displayWidth,
                                 bounds.height() / displayHeight);
    const QSizeF fitted(displayWidth * scale, displayHeight * scale);
    return QRectF((bounds.width() - fitted.width()) / 2.0,
                  (bounds.height() - fitted.height()) / 2.0,
                  fitted.width(), fitted.height());
}

QImage imageForTexture(const VideoFrameSnapshot& frame)
{
    if (!isValidFrame(frame)) return {};
    const auto* pixels = reinterpret_cast<const uchar*>(
        frame.pixels.constData());
    const QImage source(pixels, frame.width, frame.height, frame.stride,
                        QImage::Format_ARGB32);
    QImage upload = source.copy();
    const int rotation = normalizedRotation(frame.rotationDegrees);
    if (rotation != 0) {
        QTransform transform;
        transform.rotate(rotation);
        upload = upload.transformed(transform, Qt::FastTransformation);
    }
    return upload;
}

class VideoTextureNode final : public QSGSimpleTextureNode {
public:
    explicit VideoTextureNode(
        std::shared_ptr<VideoFrameItemRenderStats> renderStats)
        : renderStats_(std::move(renderStats))
    {
        setOwnsTexture(true);
#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
        renderStats_->liveTextureNodes.fetch_add(1,
                                                  std::memory_order_relaxed);
#endif
    }

    ~VideoTextureNode() override
    {
#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
        renderStats_->liveTextureNodes.fetch_sub(1,
                                                  std::memory_order_relaxed);
#endif
    }

    void replaceTexture(QSGTexture* replacement)
    {
        QSGTexture* const previous = texture();
        setOwnsTexture(false);
        setTexture(replacement);
        delete previous;
        setOwnsTexture(true);
    }

    quint64 generation = 0;
    quint64 serial = 0;

private:
    std::shared_ptr<VideoFrameItemRenderStats> renderStats_;
};

} // namespace

VideoFrameItem::VideoFrameItem(QQuickItem* parent)
    : QQuickItem(parent),
      renderStats_(std::make_shared<VideoFrameItemRenderStats>())
{
    setFlag(ItemHasContents, true);
}

VideoFrameItem::~VideoFrameItem()
{
    if (frameConnection_) disconnect(frameConnection_);
    if (destroyedConnection_) disconnect(destroyedConnection_);
}

void VideoFrameItem::setController(VideoPlaybackController* controller)
{
    if (controller_ == controller) return;
    if (frameConnection_) disconnect(frameConnection_);
    if (destroyedConnection_) disconnect(destroyedConnection_);
    controller_ = controller;
    if (controller_ != nullptr) {
        frameConnection_ = connect(
            controller_, &VideoPlaybackController::frameChanged,
            this, &VideoFrameItem::handleFrameChanged);
        destroyedConnection_ = connect(
            controller_, &QObject::destroyed, this, [this] {
                controller_ = nullptr;
                acceptFrame({});
                emit controllerChanged();
            });
        acceptFrame(controller_->currentFrame());
    } else {
        acceptFrame({});
    }
    emit controllerChanged();
}

void VideoFrameItem::handleFrameChanged()
{
    acceptFrame(controller_ != nullptr ? controller_->currentFrame()
                                       : nullptr);
}

void VideoFrameItem::acceptFrame(
    std::shared_ptr<const VideoFrameSnapshot> frame)
{
    if (frame_ == frame) return;
    if (frame_ != nullptr && frame != nullptr
        && frame_->generation == frame->generation
        && frame_->serial == frame->serial) {
        return;
    }
    frame_ = std::move(frame);
    requestRenderUpdate();
}

void VideoFrameItem::requestRenderUpdate()
{
#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
    renderStats_->updateRequests.fetch_add(1, std::memory_order_relaxed);
#endif
    update();
}

QSGNode* VideoFrameItem::updatePaintNode(QSGNode* oldNode,
                                          UpdatePaintNodeData*)
{
    auto* node = static_cast<VideoTextureNode*>(oldNode);
    if (releaseRequested_.exchange(false, std::memory_order_acq_rel)) {
        delete node;
        node = nullptr;
    }

    const std::shared_ptr<const VideoFrameSnapshot> frame = frame_;
    const QRectF fitted = frame != nullptr
        ? aspectFitRect(size(), *frame) : QRectF{};
    if (frame == nullptr || fitted.isEmpty() || window() == nullptr) {
        delete node;
#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
        renderStats_->renderedSerial.store(0, std::memory_order_relaxed);
        const QMutexLocker lock(&renderStats_->geometryMutex);
        renderStats_->renderedRect = {};
#endif
        return nullptr;
    }

    if (node == nullptr) node = new VideoTextureNode(renderStats_);
    if (node->generation != frame->generation
        || node->serial != frame->serial || node->texture() == nullptr) {
        const QImage upload = imageForTexture(*frame);
        QSGTexture* const texture = upload.isNull()
            ? nullptr : window()->createTextureFromImage(upload);
        if (texture == nullptr) {
            delete node;
            return nullptr;
        }
        node->replaceTexture(texture);
        node->generation = frame->generation;
        node->serial = frame->serial;
        node->setFiltering(QSGTexture::Linear);
#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
        renderStats_->textureUploads.fetch_add(1,
                                                std::memory_order_relaxed);
#endif
    }
    node->setRect(fitted);

#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
    renderStats_->renderedSerial.store(frame->serial,
                                       std::memory_order_relaxed);
    const QMutexLocker lock(&renderStats_->geometryMutex);
    renderStats_->renderedRect = fitted;
#endif
    return node;
}

void VideoFrameItem::releaseResources()
{
    // QSG nodes and textures belong to the render thread. The synchronization
    // pass consumes this flag and performs deletion there; a Scene Graph
    // invalidation destroys the node itself before the next pass.
    releaseRequested_.store(true, std::memory_order_release);
    requestRenderUpdate();
}

void VideoFrameItem::geometryChange(const QRectF& newGeometry,
                                    const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) requestRenderUpdate();
}

#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
void VideoFrameItem::presentFrameForTesting(
    std::shared_ptr<const VideoFrameSnapshot> frame)
{
    acceptFrame(std::move(frame));
}

void VideoFrameItem::requestFrameUpdateForTesting()
{
    requestRenderUpdate();
}

int VideoFrameItem::updateRequestCountForTesting() const noexcept
{
    return renderStats_->updateRequests.load(std::memory_order_relaxed);
}

int VideoFrameItem::textureUploadCountForTesting() const noexcept
{
    return renderStats_->textureUploads.load(std::memory_order_relaxed);
}

int VideoFrameItem::liveTextureNodeCountForTesting() const noexcept
{
    return renderStats_->liveTextureNodes.load(std::memory_order_relaxed);
}

quint64 VideoFrameItem::renderedSerialForTesting() const noexcept
{
    return renderStats_->renderedSerial.load(std::memory_order_relaxed);
}

QRectF VideoFrameItem::renderedRectForTesting() const
{
    const QMutexLocker lock(&renderStats_->geometryMutex);
    return renderStats_->renderedRect;
}
#endif
