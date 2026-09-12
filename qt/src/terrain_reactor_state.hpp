#pragma once

#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QStringView>

#include <array>
#include <atomic>

namespace agplayer::terrain {

inline constexpr float kTerrainStageExtent = 168.0F;

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
    float aux = 0.0F;
    ColorZone zone = ColorZone::Dark;

    friend bool operator==(const SceneInstance& lhs,
                           const SceneInstance& rhs) noexcept
    {
        return lhs.position == rhs.position && lhs.scale == rhs.scale
            && lhs.random == rhs.random && lhs.aux == rhs.aux
            && lhs.zone == rhs.zone;
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
int referenceTerrainGridSize(int density) noexcept;
int terrainGridSizeForDensity(int baseGridSize, int densityPercent,
                              int gridCeiling) noexcept;

struct MeteorPhase {
    float normalizedAge = 0.0F;
    float fallDistance = 0.0F;
    float collisionProgress = 0.0F;
    bool flightActive = false;
    bool collisionActive = false;

    friend bool operator==(const MeteorPhase& lhs,
                           const MeteorPhase& rhs) noexcept
    {
        return lhs.normalizedAge == rhs.normalizedAge
            && lhs.fallDistance == rhs.fallDistance
            && lhs.collisionProgress == rhs.collisionProgress
            && lhs.flightActive == rhs.flightActive
            && lhs.collisionActive == rhs.collisionActive;
    }
};

MeteorPhase meteorPhase(float random, float timeSeconds) noexcept;

enum class RenderColorMode : quint8 { MultiRegion, Custom, RgbSweep, RainbowColumn = 3 };

using TrackPalette = std::array<QVector4D, 5>;

struct ThemePalette {
    // First five match TrackPalette, then body, fog, ripple; encoded sRGB.
    std::array<QVector4D, 8> colors{};
    float glow = 1.0F;
};

class MeteorMaterialColor final {
public:
    QVector3D advance(QVector3D targetWarmLinear, float deltaSeconds) noexcept;
    QVector3D color() const noexcept { return color_; }
private:
    QVector3D color_{1,1,1};
};

class MeteorParticlePool final {
public:
    struct Particle {
        QVector3D position, velocity;
        float life = 0, maxLife = 1, baseScale = 0;
        bool active = false;
        float scale() const noexcept { return active ? baseScale * (1-life/maxLife) : 0; }
    };
    void reset() noexcept { particles_ = {}; next_ = 0; seed_ = 1; }
    void spawn(QVector3D position, float speedMultiplier, const std::array<float,8>& samples) noexcept;
    void advance(float dt) noexcept;
    void frame(float dt, QVector4D trajectory, float flightAge, bool airborne, bool landed) noexcept;
    const std::array<Particle,200>& particles() const noexcept { return particles_; }
private:
    std::array<Particle,200> particles_{};
    std::size_t next_ = 0;
    quint32 seed_ = 1;
};
// pulse, sizeMix, uniform scale, normalized intensity.
QVector4D advanceFloatingBlocks(float previousPulse, float kick, float dt,
                               float minSize, float maxSize, float speed, float intensity) noexcept;
ThemePalette advanceThemePalette(const ThemePalette& current,
                                 const ThemePalette& target,
                                 float deltaSeconds) noexcept;

quint32 stableTrackPaletteSeed(QStringView trackIdentity) noexcept;
TrackPalette trackPalette(quint32 seed) noexcept;
TrackPalette blendTrackPalettes(const TrackPalette& from,
                                const TrackPalette& to,
                                float progress) noexcept;

struct RenderStyleSnapshot {
    // Encoded sRGB, like colors; alpha zero selects the legacy fallback.
    QVector4D bodyColor{};
    QVector4D atmosphereColor{};
    // Encoded theme ripple color; alpha one selects reference event semantics.
    // Zero preserves the existing custom palette/event encoding.
    QVector4D rippleColor{};
    TrackPalette colors{
        QVector4D(0.031F, 0.024F, 0.086F, 1.0F),
        QVector4D(0.31F, 0.435F, 1.0F, 1.0F),
        QVector4D(1.0F, 0.278F, 0.471F, 1.0F),
        QVector4D(0.467F, 0.918F, 1.0F, 1.0F),
        QVector4D(0.843F, 1.0F, 0.345F, 1.0F),
    };
    std::array<bool, 8> visualEqEnabled{true,true,true,true,true,true,true,true};
    std::array<float, 8> visualEqGains{0.9F, 0.92F, 0.5F, 0.5F,
                                       0.5F, 0.5F, 0.5F, 0.48F};
    RenderColorMode colorMode = RenderColorMode::MultiRegion;
    int materialMode = 0;
    float columnSize = 1.0F;
    int columnDensity = 125;
    int topographyDensity = -1; // Negative selects legacy percentage density.
    float columnOpacity = 1.0F;
    float reactorBrightness = 1.0F;
    float columnInnerLight = 1.0F;
    float columnLightSpill = 0.2F;
    float columnLightRadius = 1.0F;
    float materialSoftness = 0.45F;
    float jellyElasticity = 0.35F;
    float inkDensity = 0.60F;
    float rippleStrength = 1.0F;
    float rippleWidth = 1.0F;
    float rippleDecay = 1.0F;
    float terrainAmplitude = 0.42F;
    float motionResponse = 0.34F;
    float gradientLayers = 0.74F;
    float glowIntensity = 0.38F;
    float cinemaShake = 0.4F;
    float autoRotate = 0.54F;
    float peakBoost = 0.58F;
    float inputCompression = 0.82F;
    float audioResponse = 1.28F;
    float responseRange = 1.0F;
    float centerHighlight = 0.58F;
    float rhythmStrength = 0.30F;
    float depthOfField = 0.86F;
    float subjectClarity = 1.10F;
    float autoRotateSpeed = 0.42F;
    float rhythmSensitivity = 0.78F;
    bool ripplesEnabled = true;
    bool burstEnabled = true;
    bool floatingCubesEnabled = true;
    float floatingBlockMinSize = 9;
    float floatingBlockMaxSize = 26;
    float floatingBlockSpeed = 77;
    float floatingBlockIntensity = 55;
    bool meteorsEnabled = true;
    bool idleBreathingEnabled = true;
    bool themeCycleEnabled = false;
    bool streamHighlightEnabled = true;
};

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
    float beatStrength = 0.0F;
    float beatAge = 0.0F;
    float impactStrength = 0.0F;
    float impactAge = 0.0F;
    float timeSeconds = 0.0F;
};

struct BassEnvelopeSnapshot {
    float fast = 0.0F;
    float slow = 0.0F;
};

class BassEnvelopeFollower final {
public:
    BassEnvelopeSnapshot advance(float bass, float elapsedSeconds) noexcept;
    BassEnvelopeSnapshot snapshot() const noexcept;

private:
    BassEnvelopeSnapshot snapshot_;
};

using MultiWaveSources = std::array<QVector4D, 8>;

float smoothReactorFeature(float current, float target, float elapsedSeconds) noexcept;

// Beat-driven accents, not a free-running timer. Silence never emits a wave.
class TravelingWaveGate final {
public:
    bool consume(float nowSeconds, float strength) noexcept;
    void anchor(float nowSeconds, float strength) noexcept;
private:
    float nextSeconds_ = 0.0F;
};

QVector4D consumeSnareWave(TravelingWaveGate& gate, float now, double strength,
    quint32 seed, bool enabled, bool meteorLanded) noexcept;

class MeteorFlight final {
public:
    bool launch(float now, int count, float strength) noexcept;
    bool landed(float now) noexcept;
    int group() const noexcept { return group_; }
    float age(float now) const noexcept { return now - start_; }
    float strength() const noexcept { return strength_; }
    QVector4D trajectory() const noexcept { return trajectory_; } // x,z,height,speed
    float duration() const noexcept { return trajectory_.z() / (trajectory_.w() * 60.0F); }
    void cancel() noexcept { group_ = -1; pending_ = false; }
private:
    int group_ = -1;
    unsigned sequence_ = 0;
    float start_ = -100.0F;
    float strength_ = 0.0F;
    QVector4D trajectory_{0, 0, 30, 1};
    bool pending_ = false;
};

// x/y are stage coordinates, z is the normalized cycle phase and w is strength.
MultiWaveSources multiWaveSources(quint32 seed) noexcept;

struct RenderDynamics {
    float inputCompression = 0.82F;
    float audioResponse = 1.28F;
    float responseRadius = 72.0F;
    float centerHighlight = 0.58F;
    float rhythmStrength = 0.30F;
    float depthOfField = 0.86F;
    float subjectClarity = 1.10F;
    float autoRotateSpeed = 0.0F;
    float rhythmSensitivity = 0.78F;
};

RenderDynamics mapRenderDynamics(const RenderStyleSnapshot& style) noexcept;
VisualParameters mapVisualParameters(const AudioFeatures& features,
                                     float timeSeconds,
                                     const RenderStyleSnapshot& style = {}) noexcept;
float terrainHeight(const SceneInstance& instance,
                    const VisualParameters& parameters,
                    float timeSeconds,
                    const RenderStyleSnapshot& style = {}) noexcept;

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
    int floatingCount = 80;
    int particleCount = 1600;
    int meteorCount = 10;
    int rippleCount = 4;
    float internalScale = 0.90F;
    int sampleCount = 4;
};

class AutomaticQualityController final {
public:
    void observeWorkSample(double workMilliseconds) noexcept;
    void observeFrameSample(double workMilliseconds,
                            double frameElapsedMilliseconds,
                            double targetFrameMilliseconds) noexcept;
    void advanceWallClock(double elapsedSeconds) noexcept;
    DegradationStage stage() const noexcept;
    QualityConfiguration configuration(bool eco = false) const noexcept;

private:
    static constexpr double frameBudgetMilliseconds_ = 33.333;
    static constexpr double downgradeSeconds_ = 2.0;
    static constexpr double upgradeSeconds_ = 8.0;
    static constexpr double cooldownSeconds_ = 5.0;

    DegradationStage stage_ = DegradationStage::Full;
    enum class LoadSample : quint8 { Neutral, OverBudget, UnderBudget };
    LoadSample loadSample_ = LoadSample::Neutral;
    double overBudgetSeconds_ = 0.0;
    double underBudgetSeconds_ = 0.0;
    double cooldownRemainingSeconds_ = 0.0;
    double smoothedFrameElapsedMilliseconds_ = 0.0;
    double lastTargetFrameMilliseconds_ = 0.0;
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
    bool claimPunchRevision(quint64 revision) noexcept;
    bool claimBeatRevision(quint64 revision) noexcept;
    bool claimImpactRevision(quint64 revision) noexcept;
    quint64 consumedBeatRevision() const noexcept {
        return consumedBeatRevision_.load(std::memory_order_acquire);
    }
    quint64 consumedImpactRevision() const noexcept {
        return consumedImpactRevision_.load(std::memory_order_acquire);
    }

private:
    static std::atomic<quint64> globalGeneration_;
    std::atomic<quint64> rendererId_{0};
    std::atomic<quint64> generation_{0};
    std::atomic<quint64> consumedPunchRevision_{0};
    std::atomic<quint64> consumedBeatRevision_{0};
    std::atomic<quint64> consumedImpactRevision_{0};
    std::atomic_bool resourcesReady_{false};
};

class FramePacer final {
public:
    bool shouldRender(double nowSeconds, double targetFramesPerSecond) noexcept;

private:
    double nextFrameSeconds_ = 0.0;
    bool initialized_ = false;
};

struct CameraSnapshot {
    float yaw = -0.386852433896339F;
    float pitch = 0.25265687598048F;
    float distance = 102.885F;
    float punch = 0.0F;
};

class CameraMotion final {
public:
    void orbitBy(float yawDelta, float pitchDelta,
                 double nowSeconds) noexcept;
    void zoomBy(float wheelDelta, double nowSeconds) noexcept;
    void applyBeatPunch(float strength) noexcept;
    void clearBeatPunch() noexcept;
    void advance(double nowSeconds, float elapsedSeconds,
                 float autoRotateSpeed) noexcept;
    CameraSnapshot snapshot() const noexcept;
    double manualUntilSeconds() const noexcept;
    void synchronize(CameraSnapshot snapshot,
                     double manualUntilSeconds) noexcept;
    void applyManualDelta(const CameraSnapshot& previous,
                          const CameraSnapshot& next,
                          double nowSeconds) noexcept;

private:
    void markManual(double nowSeconds) noexcept;

    CameraSnapshot snapshot_;
    double manualUntilSeconds_ = 0.0;
    CameraSnapshot automaticAnchor_;
    CameraSnapshot automaticFrom_;
    float automaticPhase_ = 0.0F;
    int automaticView_ = 0;
    bool automaticInitialized_ = false;
};

struct PunchEvent {
    float strength = 0.0F;
    quint64 revision = 0;
};

class PunchEventConsumer final {
public:
    explicit PunchEventConsumer(RendererResourceState& lifecycle) noexcept;
    bool consume(const PunchEvent& event, CameraMotion& camera) noexcept;
    bool discard(const PunchEvent& event, CameraMotion& camera) noexcept;

private:
    RendererResourceState& lifecycle_;
};

struct BeatEvent {
    float strength = 0.0F;
    quint64 revision = 0;
};

struct BeatPulseSnapshot {
    float strength = 0.0F;
    float age = 0.0F;
    bool active = false;
};

class BeatEventConsumer final {
public:
    explicit BeatEventConsumer(RendererResourceState& lifecycle) noexcept;
    bool consume(const BeatEvent& event, float nowSeconds) noexcept;
    bool discard(const BeatEvent& event) noexcept;
    BeatPulseSnapshot snapshot(float nowSeconds) const noexcept;

private:
    static constexpr float durationSeconds_ = 0.34F;
    RendererResourceState& lifecycle_;
    float startSeconds_ = 0.0F;
    float baseStrength_ = 0.0F;
    bool active_ = false;
};

struct ImpactEvent {
    float strength = 0.0F;
    quint64 revision = 0;
};

struct ImpactPulseSnapshot {
    float strength = 0.0F;
    float age = 0.0F;
    bool active = false;
};

class ImpactEventConsumer final {
public:
    explicit ImpactEventConsumer(RendererResourceState& lifecycle) noexcept;
    bool consume(const ImpactEvent& event, float nowSeconds) noexcept;
    bool discard(const ImpactEvent& event) noexcept;
    ImpactPulseSnapshot snapshot(float nowSeconds) const noexcept;

private:
    static constexpr float durationSeconds_ = 1.2F;
    RendererResourceState& lifecycle_;
    float startSeconds_ = 0.0F;
    float baseStrength_ = 0.0F;
    bool active_ = false;
};

} // namespace agplayer::terrain
