#pragma once

#include <QVector>
#include <QVector3D>

#include <array>

namespace agplayer::terrain {

enum class ColorZone : quint8 {
    Dark,
    Cool,
    Warm,
    Accent,
    Peak,
};

struct SceneInstance {
    QVector3D position;
    QVector3D scale{1.0F, 1.0F, 1.0F};
    float random = 0.0F;
    ColorZone zone = ColorZone::Dark;

    friend bool operator==(const SceneInstance& lhs,
                           const SceneInstance& rhs) noexcept
    {
        return lhs.position == rhs.position && lhs.scale == rhs.scale
            && lhs.random == rhs.random && lhs.zone == rhs.zone;
    }
};

struct SceneLayout {
    QVector<SceneInstance> terrain;
    QVector<SceneInstance> floating;
    QVector<SceneInstance> meteors;
    QVector<SceneInstance> particles;
};

SceneLayout makeSceneLayout(quint32 seed, int gridSize, int floatingCount,
                            int meteorCount, int particleCount);

struct AudioFeatures {
    std::array<float, 8> bands{};
    float energy = 0.0F;
    float spectralFlux = 0.0F;
    float kick = 0.0F;
    float snare = 0.0F;
};

struct VisualParameters {
    std::array<float, 8> bands{};
    float energy = 0.0F;
    float spectralFlux = 0.0F;
    float rippleStrength = 0.0F;
    float particleActivity = 0.0F;
    float meteorActivity = 0.0F;
    float cameraPunch = 0.0F;
    float timeSeconds = 0.0F;
};

VisualParameters mapVisualParameters(const AudioFeatures& features,
                                     float timeSeconds) noexcept;
float terrainHeight(const SceneInstance& instance,
                    const VisualParameters& parameters,
                    float timeSeconds) noexcept;

enum class DegradationStage : quint8 {
    Full,
    ReducedParticles,
    ReducedMeteors,
    ReducedRipples,
    ReducedGrid,
    ReducedResolution,
};

struct QualityConfiguration {
    int gridSize = 160;
    int floatingCount = 120;
    int particleCount = 180;
    int meteorCount = 28;
    int rippleCount = 10;
    float internalScale = 1.0F;
};

class AutomaticQualityController final {
public:
    void observe(double frameMilliseconds, double elapsedSeconds) noexcept;
    DegradationStage stage() const noexcept;
    QualityConfiguration configuration() const noexcept;

private:
    static constexpr double frameBudgetMilliseconds_ = 33.333;
    static constexpr double downgradeSeconds_ = 2.0;
    static constexpr double upgradeSeconds_ = 8.0;
    static constexpr double cooldownSeconds_ = 5.0;

    DegradationStage stage_ = DegradationStage::Full;
    double overBudgetSeconds_ = 0.0;
    double underBudgetSeconds_ = 0.0;
    double cooldownRemainingSeconds_ = 0.0;
};

struct WorkCounters {
    quint64 frames = 0;
    quint64 animations = 0;
    quint64 uploads = 0;

    friend bool operator==(const WorkCounters& lhs,
                           const WorkCounters& rhs) noexcept
    {
        return lhs.frames == rhs.frames && lhs.animations == rhs.animations
            && lhs.uploads == rhs.uploads;
    }
};

class RenderWorkGate final {
public:
    void setActive(bool active) noexcept;
    void setVisible(bool visible) noexcept;
    void setExposed(bool exposed) noexcept;
    bool canRun() const noexcept;
    bool advance(bool uploaded) noexcept;
    WorkCounters counters() const noexcept;

private:
    bool active_ = false;
    bool visible_ = false;
    bool exposed_ = false;
    WorkCounters counters_;
};

class RendererResourceState final {
public:
    bool acquireRenderer(quint64 rendererId) noexcept;
    void releaseRenderer(quint64 rendererId) noexcept;
    quint64 initializeResources() noexcept;
    void invalidateResources() noexcept;
    bool resourcesReady() const noexcept;
    int liveRendererCount() const noexcept;
    quint64 generation() const noexcept;

private:
    quint64 rendererId_ = 0;
    quint64 generation_ = 0;
    bool resourcesReady_ = false;
};

struct CameraSnapshot {
    float yaw = 2.6075219F;
    float pitch = 0.47F;
    float distance = 82.0F;
    float punch = 0.0F;
};

class CameraMotion final {
public:
    void orbitBy(float yawDelta, float pitchDelta,
                 double nowSeconds) noexcept;
    void zoomBy(float wheelDelta, double nowSeconds) noexcept;
    void applyBeatPunch(float strength) noexcept;
    void advance(double nowSeconds, float elapsedSeconds,
                 float autoRotateSpeed) noexcept;
    CameraSnapshot snapshot() const noexcept;
    double manualUntilSeconds() const noexcept;
    void synchronize(CameraSnapshot snapshot,
                     double manualUntilSeconds) noexcept;

private:
    void markManual(double nowSeconds) noexcept;

    CameraSnapshot snapshot_;
    double manualUntilSeconds_ = 0.0;
};

} // namespace agplayer::terrain
