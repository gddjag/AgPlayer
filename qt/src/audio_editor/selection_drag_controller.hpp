#pragma once

#include "../../../core/src/audio_editor/audio_document.hpp"

#include <QPointF>
#include <QFutureWatcher>
#include <QUrl>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

struct HandoffRenderState final {
    int sampleRate{};
    int channels{};
    float trackGain{1.0F};
    bool muted{};
    double speedPercent{100.0};
    int pitchCents{};
    bool keepPitch{true};
    bool formantPreservation{};
};

struct HandoffRequest final {
    agplayer::editor::TimelineSnapshot snapshot;
    agplayer::editor::Selection selection;
    QString sourceIdentity;
    quint64 timelineRevision{};
    HandoffRenderState renderState;
    QString outputFileStem;

    struct PlaybackClipSource final {
        QString path;
        QString title;
        qint64 startMs{};
        qint64 endMs{};
    };
    std::optional<PlaybackClipSource> playbackClip;
};

struct HandoffAssetResult final {
    bool success{};
    QString path;
    QUrl url;
    QString error;
};

class HandoffAssetManager final {
public:
    explicit HandoffAssetManager(QString directory = {});

    [[nodiscard]] HandoffAssetResult prepare(
        const HandoffRequest& request,
        const std::atomic_bool* cancelled = nullptr) const;

    [[nodiscard]] QString directory() const { return directory_; }

private:
    QString directory_;
};

class SelectionDragController final : public QObject {
    Q_OBJECT

public:
    using PrepareFunction = std::function<HandoffAssetResult(
        const HandoffRequest&, const std::atomic_bool*)>;
    using DragFunction = std::function<void(const QUrl&)>;

    SelectionDragController(PrepareFunction prepare, DragFunction drag,
                            QObject* parent = nullptr);
    explicit SelectionDragController(HandoffAssetManager* assets,
                                     QObject* parent = nullptr);
    ~SelectionDragController() override;

    void begin(QPointF scenePosition, HandoffRequest request);
    void update(QPointF scenePosition);
    void release();
    void cancel();

    [[nodiscard]] bool preparing() const noexcept { return preparing_; }

signals:
    void preparingChanged();
    void handoffReady(const QUrl& url);
    void errorOccurred(const QString& message);

private:
    void startPrepare();
    void launchDrag(const QUrl& url);

    PrepareFunction prepare_;
    DragFunction drag_;
    QPointF press_position_;
    HandoffRequest request_;
    std::shared_ptr<std::atomic_bool> cancel_token_;
    QFutureWatcher<HandoffAssetResult>* watcher_{};
    bool active_{};
    bool triggered_{};
    bool preparing_{};
    quint64 generation_{};
};
