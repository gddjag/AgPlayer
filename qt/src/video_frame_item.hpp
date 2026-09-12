#pragma once

#include "video_playback_controller.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>

#include <atomic>
#include <memory>

struct VideoFrameItemRenderStats;

class VideoFrameItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(VideoPlaybackController* controller READ controller
                   WRITE setController NOTIFY controllerChanged)

public:
    explicit VideoFrameItem(QQuickItem* parent = nullptr);
    ~VideoFrameItem() override;

    VideoPlaybackController* controller() const noexcept
    {
        return controller_;
    }
    void setController(VideoPlaybackController* controller);

#if defined(AGPLAYER_VIDEO_FRAME_ITEM_TESTING)
    void presentFrameForTesting(
        std::shared_ptr<const VideoFrameSnapshot> frame);
    void requestFrameUpdateForTesting();
    int updateRequestCountForTesting() const noexcept;
    int textureUploadCountForTesting() const noexcept;
    int liveTextureNodeCountForTesting() const noexcept;
    quint64 renderedSerialForTesting() const noexcept;
    QRectF renderedRectForTesting() const;
#endif

signals:
    void controllerChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode,
                             UpdatePaintNodeData* data) override;
    void releaseResources() override;
    void geometryChange(const QRectF& newGeometry,
                        const QRectF& oldGeometry) override;

private:
    void handleFrameChanged();
    void acceptFrame(std::shared_ptr<const VideoFrameSnapshot> frame);
    void requestRenderUpdate();

    QPointer<VideoPlaybackController> controller_;
    QMetaObject::Connection frameConnection_;
    QMetaObject::Connection destroyedConnection_;
    std::shared_ptr<const VideoFrameSnapshot> frame_;
    std::atomic_bool releaseRequested_{false};
    std::shared_ptr<VideoFrameItemRenderStats> renderStats_;
};
