#pragma once

#include "terrain_reactor_state.hpp"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QQuickRhiItem>
#include <QVariantList>

#include <array>
#include <atomic>
#include <memory>

class AudioVisualFeatureController;
class PlayerExperienceController;
class QQuickWindow;
class TerrainReactorRenderer;

class TerrainReactorItem : public QQuickRhiItem {
    Q_OBJECT

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool hostExposed READ hostExposed WRITE setHostExposed
                   NOTIFY hostExposedChanged)
    Q_PROPERTY(bool renderingRequested READ renderingRequested
                   NOTIFY renderingRequestedChanged)
    Q_PROPERTY(QObject* featureSource READ featureSource WRITE setFeatureSource
                   NOTIFY featureSourceChanged)
    Q_PROPERTY(QObject* styleSource READ styleSource WRITE setStyleSource
                   NOTIFY styleSourceChanged)
    Q_PROPERTY(bool useSyntheticFeatures READ useSyntheticFeatures
                   WRITE setUseSyntheticFeatures NOTIFY useSyntheticFeaturesChanged)
    Q_PROPERTY(quint32 deterministicSeed READ deterministicSeed
                   WRITE setDeterministicSeed NOTIFY deterministicSeedChanged)
    Q_PROPERTY(Quality quality READ quality WRITE setQuality NOTIFY qualityChanged)
    Q_PROPERTY(qreal cameraYaw READ cameraYaw NOTIFY cameraChanged)
    Q_PROPERTY(qreal cameraPitch READ cameraPitch NOTIFY cameraChanged)
    Q_PROPERTY(qreal cameraDistance READ cameraDistance NOTIFY cameraChanged)
    Q_PROPERTY(qreal cameraPunch READ cameraPunch NOTIFY cameraChanged)
    Q_PROPERTY(quint64 punchRevision READ punchRevision NOTIFY cameraChanged)
    Q_PROPERTY(quint64 featureRevision READ featureRevision
                   NOTIFY featureRevisionChanged)
    Q_PROPERTY(quint64 styleRevision READ styleRevision
                   NOTIFY styleRevisionChanged)
    Q_PROPERTY(quint64 frameCount READ frameCount NOTIFY countersChanged)
    Q_PROPERTY(quint64 animationCount READ animationCount NOTIFY countersChanged)
    Q_PROPERTY(quint64 uploadCount READ uploadCount NOTIFY countersChanged)
    Q_PROPERTY(quint64 resourceGeneration READ resourceGeneration
                   NOTIFY countersChanged)
    Q_PROPERTY(int liveRendererCount READ liveRendererCount
                   NOTIFY countersChanged)
    Q_PROPERTY(RenderStatus renderStatus READ renderStatus
                   NOTIFY renderStatusChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY renderStatusChanged)

public:
    enum class Quality {
        Eco,
        Balanced,
        High,
    };
    Q_ENUM(Quality)

    enum class RenderStatus {
        Inactive,
        Ready,
        SoftwareBackend,
        ResourceError,
    };
    Q_ENUM(RenderStatus)

    explicit TerrainReactorItem(QQuickItem* parent = nullptr);
    ~TerrainReactorItem() override;

    bool active() const noexcept;
    void setActive(bool active);
    bool hostExposed() const noexcept;
    void setHostExposed(bool exposed);
    bool renderingRequested() const noexcept;

    QObject* featureSource() const noexcept;
    void setFeatureSource(QObject* source);
    QObject* styleSource() const noexcept;
    void setStyleSource(QObject* source);
    bool useSyntheticFeatures() const noexcept;
    void setUseSyntheticFeatures(bool enabled);
    quint32 deterministicSeed() const noexcept;
    void setDeterministicSeed(quint32 seed);
    Quality quality() const noexcept;
    void setQuality(Quality quality);

    QVariantList featureBands() const;
    qreal featureEnergy() const noexcept;
    qreal featureSpectralFlux() const noexcept;
    bool featureKick() const noexcept;
    bool featureSnare() const noexcept;
    quint64 featureRevision() const noexcept;
    quint64 styleRevision() const noexcept;
    agplayer::terrain::RenderStyleSnapshot renderStyleSnapshot() const;

    qreal cameraYaw() const noexcept;
    qreal cameraPitch() const noexcept;
    qreal cameraDistance() const noexcept;
    qreal cameraPunch() const noexcept;
    quint64 punchRevision() const noexcept;

    quint64 frameCount() const noexcept;
    quint64 animationCount() const noexcept;
    quint64 uploadCount() const noexcept;
    quint64 resourceGeneration() const noexcept;
    int liveRendererCount() const noexcept;
    RenderStatus renderStatus() const noexcept;
    QString diagnostic() const;

    Q_INVOKABLE void setSyntheticFeatures(const QVariantList& bands,
                                          qreal energy, qreal spectralFlux,
                                          bool kick, bool snare);
    Q_INVOKABLE void orbitBy(qreal yawDelta, qreal pitchDelta,
                             qreal nowSeconds);
    Q_INVOKABLE void zoomBy(qreal wheelDelta, qreal nowSeconds);
    Q_INVOKABLE void triggerCameraPunch(qreal strength);

signals:
    void activeChanged();
    void hostExposedChanged();
    void renderingRequestedChanged();
    void featureSourceChanged();
    void styleSourceChanged();
    void useSyntheticFeaturesChanged();
    void deterministicSeedChanged();
    void qualityChanged();
    void featureRevisionChanged();
    void styleRevisionChanged();
    void cameraChanged();
    void countersChanged();
    void renderStatusChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void applyInternalScale(float scale);

private:
    struct Telemetry {
        std::atomic<quint64> frames{0};
        std::atomic<quint64> animations{0};
        std::atomic<quint64> uploads{0};
        std::atomic<quint64> resourceGeneration{0};
    };

    struct RenderSnapshot {
        agplayer::terrain::AudioFeatures features;
        agplayer::terrain::RenderStyleSnapshot style;
        agplayer::terrain::CameraSnapshot camera;
        agplayer::terrain::PunchEvent punchEvent;
        double cameraManualUntilSeconds = 0.0;
        quint64 cameraRevision = 0;
        quint32 seed = 0x5eedU;
        Quality quality = Quality::Eco;
        bool running = false;
        float timeSeconds = 0.0F;
        quint64 featureRevision = 0;
        quint64 styleRevision = 0;
    };

    RenderSnapshot snapshotForRenderer() const;
    void copyFeatureSource();
    void copyStyleSource();
    void applyCurrentFeatures(const agplayer::terrain::AudioFeatures& features);
    void scheduleIfRunnable();
    void updateColorBufferSize();
    void updateWindowState(QQuickWindow* window);
    void refreshWindowExposure();
    void reportRenderStatus(RenderStatus status, const QString& diagnostic);

    bool active_ = false;
    bool hostExposed_ = true;
    bool windowExposed_ = true;
    bool useSyntheticFeatures_ = false;
    quint32 deterministicSeed_ = 0x5eedU;
    Quality quality_ = Quality::Eco;
    QPointer<AudioVisualFeatureController> featureSource_;
    QPointer<PlayerExperienceController> styleSource_;
    QMetaObject::Connection featureConnection_;
    QMetaObject::Connection sourceDestroyedConnection_;
    QVector<QMetaObject::Connection> styleConnections_;
    QMetaObject::Connection windowVisibilityConnection_;
    QPointer<QQuickWindow> trackedWindow_;
    agplayer::terrain::AudioFeatures liveFeatures_;
    agplayer::terrain::AudioFeatures syntheticFeatures_;
    quint64 featureRevision_ = 0;
    quint64 styleRevision_ = 0;
    quint64 cameraRevision_ = 0;
    agplayer::terrain::PunchEvent pendingPunch_;
    float internalScale_ = 1.0F;
    agplayer::terrain::CameraMotion camera_;
    QElapsedTimer clock_;
    std::shared_ptr<Telemetry> telemetry_;
    std::shared_ptr<agplayer::terrain::RendererResourceState> resourceState_;
    agplayer::terrain::RenderStyleSnapshot renderStyle_;
    RenderStatus renderStatus_ = RenderStatus::Inactive;
    QString diagnostic_;

    friend class TerrainReactorRenderer;
};
