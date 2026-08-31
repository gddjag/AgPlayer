#include "terrain_reactor_state.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace agplayer::terrain {
namespace {

float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float clampRange(float value, float minimum, float maximum,
                 float fallback) noexcept
{
    return std::clamp(finiteOr(value, fallback), minimum, maximum);
}

float clampUnit(float value, float fallback = 0.0F) noexcept
{
    return clampRange(value, 0.0F, 1.0F, fallback);
}

CameraSnapshot sanitizedCameraSnapshot(
    const CameraSnapshot& candidate,
    const CameraSnapshot& fallback = CameraSnapshot{}) noexcept
{
    const CameraSnapshot defaults;
    CameraSnapshot safeFallback;
    safeFallback.yaw = finiteOr(fallback.yaw, defaults.yaw);
    safeFallback.pitch = clampRange(fallback.pitch, 0.12F, 1.15F,
                                    defaults.pitch);
    safeFallback.distance = clampRange(fallback.distance, 42.0F, 220.0F,
                                       defaults.distance);
    safeFallback.punch = clampUnit(fallback.punch, defaults.punch);

    CameraSnapshot result;
    result.yaw = finiteOr(candidate.yaw, safeFallback.yaw);
    result.pitch = clampRange(candidate.pitch, 0.12F, 1.15F,
                              safeFallback.pitch);
    result.distance = clampRange(candidate.distance, 42.0F, 220.0F,
                                 safeFallback.distance);
    result.punch = clampUnit(candidate.punch, safeFallback.punch);
    return result;
}

class DeterministicRandom final {
public:
    explicit DeterministicRandom(quint32 seed) : state_(seed == 0 ? 1U : seed) {}

    float unit() noexcept
    {
        quint32 value = state_;
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        state_ = value;
        return static_cast<float>(value & 0x00ffffffU)
            / static_cast<float>(0x01000000U);
    }

private:
    quint32 state_;
};

ColorZone zoneFor(float x, float z, float radius, float random) noexcept
{
    if (radius < 0.12F) return ColorZone::Peak;
    if (radius > 0.86F) return ColorZone::Dark;
    const float angle = std::atan2(z, x) + random * 0.45F;
    const float sector = angle / (2.0F * static_cast<float>(M_PI)) + 1.0F;
    const int index = static_cast<int>(std::floor(sector * 6.0F)) % 3;
    if (index == 0) return ColorZone::Cool;
    if (index == 1) return ColorZone::Warm;
    return ColorZone::Accent;
}

SceneInstance makeExtra(DeterministicRandom& random, float radiusMin,
                        float radiusMax, float heightMin,
                        float heightMax, ColorZone zone)
{
    const float angle = random.unit() * 2.0F * static_cast<float>(M_PI);
    const float radius = radiusMin + random.unit() * (radiusMax - radiusMin);
    SceneInstance result;
    result.position = QVector3D(std::cos(angle) * radius,
                                heightMin + random.unit() * (heightMax - heightMin),
                                std::sin(angle) * radius);
    const float size = 0.45F + random.unit() * 0.55F;
    result.scale = QVector3D(size, size, size);
    result.random = random.unit();
    result.zone = zone;
    return result;
}

int stageIndex(DegradationStage stage) noexcept
{
    return static_cast<int>(stage);
}

float mapAudioValue(float value, const RenderDynamics& dynamics) noexcept
{
    const float bounded = clampUnit(value);
    const float exponent = 0.55F + dynamics.inputCompression * 1.1F;
    const float compressed = 1.0F - std::pow(1.0F - bounded, exponent);
    return clampUnit(compressed * dynamics.audioResponse);
}

QVector4D hsv(float hue, float saturation, float value) noexcept
{
    hue -= std::floor(hue);
    const float sector = hue * 6.0F;
    const int index = int(std::floor(sector)) % 6;
    const float fraction = sector - std::floor(sector);
    const float p = value * (1.0F - saturation);
    const float q = value * (1.0F - saturation * fraction);
    const float t = value * (1.0F - saturation * (1.0F - fraction));
    switch (index) {
    case 0: return {value, t, p, 1.0F};
    case 1: return {q, value, p, 1.0F};
    case 2: return {p, value, t, 1.0F};
    case 3: return {p, q, value, 1.0F};
    case 4: return {t, p, value, 1.0F};
    default: return {value, p, q, 1.0F};
    }
}

} // namespace

quint32 stableTrackPaletteSeed(QStringView trackIdentity) noexcept
{
    if (trackIdentity.isEmpty()) return 0U;
    quint32 hash = 2166136261U;
    for (const QChar character : trackIdentity) {
        const ushort value = character.unicode();
        hash = (hash ^ quint32(value & 0xffU)) * 16777619U;
        hash = (hash ^ quint32(value >> 8U)) * 16777619U;
    }
    return hash == 0U ? 1U : hash;
}

TrackPalette trackPalette(quint32 seed) noexcept
{
    DeterministicRandom random(seed == 0U ? 1U : seed);
    const float hue = random.unit();
    const float split = 0.31F + random.unit() * 0.18F;
    const float accentOffset = 0.58F + random.unit() * 0.16F;
    const QVector4D cool = hsv(hue, 0.64F, 0.94F);
    const QVector4D warm = hsv(hue + split, 0.66F, 0.98F);
    const QVector4D accent = hsv(hue + accentOffset, 0.52F, 1.0F);
    const QVector4D peak = hsv(hue + split * 0.45F, 0.18F, 1.0F);
    const QVector4D dark = hsv(hue + 0.06F, 0.72F, 0.085F);
    return {dark, cool, warm, accent, peak};
}

TrackPalette blendTrackPalettes(const TrackPalette& from,
                                const TrackPalette& to,
                                float progress) noexcept
{
    const float amount = clampUnit(progress);
    TrackPalette result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = from[index] * (1.0F - amount) + to[index] * amount;
        result[index].setW(1.0F);
    }
    return result;
}

SceneLayout makeSceneLayout(quint32 seed, int gridSize, int floatingCount,
                            int meteorCount, int particleCount)
{
    SceneLayout result;
    const int boundedGrid = std::max(1, gridSize);
    const int terrainCount = boundedGrid * boundedGrid;
    result.terrain.reserve(terrainCount);
    result.floating.reserve(std::max(0, floatingCount));
    result.meteors.reserve(std::max(0, meteorCount));
    result.meteorTrails.reserve(std::max(0, meteorCount) * 3);
    result.collisionRipples.reserve(std::max(0, meteorCount) * 16);
    result.collisionParticles.reserve(std::max(0, meteorCount) * 12);
    result.particles.reserve(std::max(0, particleCount));

    DeterministicRandom random(seed);
    constexpr float extent = 168.0F;
    constexpr float stageRadius = extent * 0.5F;
    const float spacing = extent / static_cast<float>(boundedGrid);
    const float center = static_cast<float>(boundedGrid - 1) * 0.5F;
    for (int z = 0; z < boundedGrid; ++z) {
        for (int x = 0; x < boundedGrid; ++x) {
            SceneInstance instance;
            const float worldX = (static_cast<float>(x) - center) * spacing;
            const float worldZ = (static_cast<float>(z) - center) * spacing;
            const float distance = std::hypot(worldX, worldZ);
            if (distance > stageRadius) {
                continue;
            }
            instance.position = QVector3D(worldX, 0.0F, worldZ);
            instance.scale = QVector3D(spacing * 0.997F, 1.0F,
                                       spacing * 0.997F);
            instance.random = random.unit();
            const float radius = distance / stageRadius;
            instance.zone = zoneFor(worldX, worldZ, radius, instance.random);
            result.terrain.append(instance);
        }
    }

    for (int index = 0; index < floatingCount; ++index) {
        const ColorZone zone = index % 3 == 0 ? ColorZone::Cool
            : index % 3 == 1 ? ColorZone::Warm : ColorZone::Accent;
        SceneInstance floating = makeExtra(random, 12.0F, 78.0F,
                                           6.0F, 25.0F, zone);
        floating.scale *= 0.72F;
        result.floating.append(floating);
    }
    for (int index = 0; index < meteorCount; ++index) {
        SceneInstance meteor = makeExtra(random, 18.0F, 74.0F,
                                         32.0F, 46.0F, ColorZone::Peak);
        meteor.scale = QVector3D(0.46F, 1.05F, 0.46F);
        meteor.aux = float(index);
        result.meteors.append(meteor);
        for (int segment = 0; segment < 3; ++segment) {
            SceneInstance trail = meteor;
            trail.aux = float(index) + float(segment + 1) / 4.0F;
            trail.scale = QVector3D(0.30F, 0.72F, 0.30F);
            result.meteorTrails.append(trail);
        }
        for (int segment = 0; segment < 16; ++segment) {
            SceneInstance ripple = meteor;
            ripple.position.setY(0.08F);
            ripple.scale = QVector3D(0.8F, 0.08F, 0.22F);
            ripple.aux = float(index) + float(segment) / 16.0F;
            result.collisionRipples.append(ripple);
        }
        for (int segment = 0; segment < 12; ++segment) {
            SceneInstance burst = meteor;
            burst.position.setY(0.08F);
            burst.scale = QVector3D(0.15F, 0.15F, 0.15F);
            burst.aux = float(index) + float(segment) / 12.0F;
            result.collisionParticles.append(burst);
        }
    }
    for (int index = 0; index < particleCount; ++index) {
        SceneInstance particle = makeExtra(random, 72.0F, 168.0F,
                                           10.0F, 82.0F, ColorZone::Accent);
        const float starSize = 0.10F + random.unit() * 0.18F;
        particle.scale = QVector3D(starSize, starSize, starSize);
        result.particles.append(particle);
    }
    return result;
}

MeteorPhase meteorPhase(float random, float timeSeconds) noexcept
{
    const float boundedRandom = clampUnit(random);
    const float cycleSeconds = 4.5F + boundedRandom * 2.0F;
    const float time = std::max(0.0F, timeSeconds)
        + boundedRandom * cycleSeconds;
    const float age = std::fmod(time, cycleSeconds) / cycleSeconds;
    constexpr float impactAge = 0.72F;
    MeteorPhase result;
    result.normalizedAge = age;
    result.flightActive = age < impactAge;
    result.collisionActive = !result.flightActive;
    result.fallDistance = std::clamp(age / impactAge, 0.0F, 1.0F);
    result.collisionProgress = result.collisionActive
        ? std::clamp((age - impactAge) / (1.0F - impactAge), 0.0F, 1.0F)
        : 0.0F;
    return result;
}

RenderDynamics mapRenderDynamics(const RenderStyleSnapshot& style) noexcept
{
    const RenderStyleSnapshot defaults;
    RenderDynamics result;
    result.inputCompression = clampRange(style.inputCompression, 0.2F, 1.5F,
                                         defaults.inputCompression);
    result.audioResponse = clampRange(style.audioResponse, 0.2F, 2.0F,
                                      defaults.audioResponse);
    result.responseRadius = 56.0F * clampRange(style.responseRange, 0.5F, 2.2F,
                                               defaults.responseRange);
    result.centerHighlight = clampUnit(style.centerHighlight,
                                       defaults.centerHighlight);
    result.rhythmStrength = clampRange(style.rhythmStrength, 0.0F, 1.4F,
                                       defaults.rhythmStrength);
    result.depthOfField = clampRange(style.depthOfField, 0.0F, 1.5F,
                                     defaults.depthOfField);
    result.subjectClarity = clampRange(style.subjectClarity, 0.2F, 1.4F,
                                       defaults.subjectClarity);
    result.autoRotateSpeed = clampUnit(style.autoRotate, defaults.autoRotate)
        * clampUnit(style.autoRotateSpeed, defaults.autoRotateSpeed) * 2.0F;
    result.rhythmSensitivity = clampUnit(style.rhythmSensitivity,
                                         defaults.rhythmSensitivity);
    return result;
}

VisualParameters mapVisualParameters(const AudioFeatures& features,
                                     float timeSeconds,
                                     const RenderStyleSnapshot& style) noexcept
{
    const RenderDynamics dynamics = mapRenderDynamics(style);
    VisualParameters result;
    for (std::size_t index = 0; index < result.bands.size(); ++index) {
        result.bands[index] = mapAudioValue(features.bands[index], dynamics);
    }
    result.energy = mapAudioValue(features.energy, dynamics);
    result.spectralFlux = mapAudioValue(features.spectralFlux, dynamics);
    const float rhythmSensitivity = dynamics.rhythmSensitivity;
    const float rhythmStrength = dynamics.rhythmStrength;
    const float kick = clampUnit(features.kick) * rhythmSensitivity;
    const float snare = clampUnit(features.snare) * rhythmSensitivity;
    result.rippleStrength = clampUnit((kick * 0.9F + snare * 0.45F)
                                      * rhythmStrength);
    result.particleActivity = clampUnit(result.bands[7] * 0.45F
                                        + result.spectralFlux * 0.45F
                                        + snare * 0.25F * rhythmStrength);
    result.meteorActivity = clampUnit(result.bands[6] * 0.35F
                                      + result.bands[7] * 0.25F
                                      + result.spectralFlux * 0.55F
                                          * rhythmStrength);
    result.cameraPunch = clampUnit((kick * 0.78F + snare * 0.32F)
                                   * rhythmStrength);
    result.timeSeconds = std::max(0.0F, finiteOr(timeSeconds, 0.0F));
    return result;
}

float terrainHeight(const SceneInstance& unsafeInstance,
                    const VisualParameters& unsafeParameters,
                    float unsafeTimeSeconds,
                    const RenderStyleSnapshot& unsafeStyle) noexcept
{
    const RenderStyleSnapshot defaults;
    RenderStyleSnapshot style = unsafeStyle;
    style.terrainAmplitude = clampUnit(style.terrainAmplitude,
                                       defaults.terrainAmplitude);
    style.peakBoost = clampUnit(style.peakBoost, defaults.peakBoost);
    VisualParameters parameters = unsafeParameters;
    for (float& band : parameters.bands) band = clampUnit(band);
    parameters.energy = clampUnit(parameters.energy);
    parameters.spectralFlux = clampUnit(parameters.spectralFlux);
    parameters.rippleStrength = clampUnit(parameters.rippleStrength);
    parameters.particleActivity = clampUnit(parameters.particleActivity);
    parameters.meteorActivity = clampUnit(parameters.meteorActivity);
    parameters.cameraPunch = clampUnit(parameters.cameraPunch);
    parameters.impactStrength = clampUnit(parameters.impactStrength);
    parameters.impactAge = clampUnit(parameters.impactAge);
    parameters.timeSeconds = std::max(
        0.0F, finiteOr(parameters.timeSeconds, 0.0F));
    const float timeSeconds = std::max(
        0.0F, finiteOr(unsafeTimeSeconds, parameters.timeSeconds));
    SceneInstance instance = unsafeInstance;
    instance.position.setX(finiteOr(instance.position.x(), 0.0F));
    instance.position.setY(finiteOr(instance.position.y(), 0.0F));
    instance.position.setZ(finiteOr(instance.position.z(), 0.0F));
    instance.random = clampUnit(instance.random);
    const RenderDynamics dynamics = mapRenderDynamics(style);
    const float distance = std::hypot(instance.position.x(),
                                      instance.position.z());
    const float center = clampUnit(1.0F - distance / dynamics.responseRadius);
    const float core = std::pow(center, 1.18F);
    const float fieldStart = dynamics.responseRadius * 0.45F;
    const float fieldEnd = dynamics.responseRadius * 1.15F;
    const float fieldPosition = clampUnit(
        (fieldEnd - distance) / std::max(1.0F, fieldEnd - fieldStart));
    const float terrainField = fieldPosition * fieldPosition
        * (3.0F - 2.0F * fieldPosition);
    const float bassField = 0.78F + 0.22F * std::sin(
        instance.position.x() * 0.038F - instance.position.z() * 0.029F
        + timeSeconds * 0.20F);
    const float ridgeA = 0.5F + 0.5F * std::sin(
        instance.position.z() * 0.052F + instance.position.x() * 0.027F
        + timeSeconds * 0.28F);
    const float ridgeB = 0.5F + 0.5F * std::cos(
        instance.position.x() * 0.041F - instance.position.z() * 0.036F
        - timeSeconds * 0.22F);
    const float wideRidge = ridgeA * 0.56F + ridgeB * 0.44F;
    const float bass = parameters.bands[0] * (1.65F + core * 2.75F)
        + parameters.bands[1] * (1.35F + bassField * 1.70F) * center;
    const float mids = parameters.bands[2] * (0.75F + wideRidge * 1.95F)
        + parameters.bands[3] * (0.80F + (1.0F - wideRidge) * 1.65F);
    const float detailA = 0.5F + 0.5F * std::sin(
        instance.position.x() * 0.18F + instance.position.z() * 0.11F);
    const float detailB = 0.5F + 0.5F * std::cos(
        instance.position.z() * 0.16F - instance.position.x() * 0.09F);
    const float coherentDetail = detailA * 0.58F + detailB * 0.42F;
    const float highEnergy = parameters.bands[4] * 0.38F
        + parameters.bands[5] * 0.28F
        + parameters.bands[6] * 0.20F
        + parameters.bands[7] * 0.14F;
    const float peakControl = 0.42F + clampUnit(style.peakBoost) * 0.58F;
    const float peak = highEnergy * (0.20F + coherentDetail * 1.45F)
        * center * peakControl;
    const float idlePhase = std::sin(instance.position.x() * 0.032F
                                     + instance.position.z() * 0.041F) * 0.72F;
    const float reliefA = 0.5F + 0.5F * std::sin(
        instance.position.x() * 0.055F + instance.position.z() * 0.032F
        + timeSeconds * 0.18F);
    const float reliefB = 0.5F + 0.5F * std::cos(
        instance.position.z() * 0.070F - instance.position.x() * 0.018F
        - timeSeconds * 0.14F);
    const float baseRelief = (0.24F + 0.48F
        * (reliefA * 0.55F + reliefB * 0.45F)) * terrainField
        + core * 1.08F;
    const float idle = style.idleBreathingEnabled
        ? baseRelief + 0.06F + 0.10F * std::sin(
              distance * 0.067F - timeSeconds * 0.36F + idlePhase)
        : 0.0F;
    const float rippleRadius = std::fmod(std::max(0.0F, timeSeconds) * 13.5F,
                                        96.0F);
    const float ringDistance = std::abs(distance - rippleRadius);
    const float cellModulation = 0.55F + clampUnit(instance.random) * 0.45F;
    const float rippleToggle = style.ripplesEnabled ? 1.0F : 0.0F;
    const float ripple = parameters.rippleStrength * rippleToggle
        * std::exp(-(ringDistance * ringDistance) / 30.25F) * 3.35F
        * cellModulation;
    const float ringPhase = 0.5F + 0.5F * std::cos(
        distance * 0.29F - timeSeconds * 1.15F);
    const float structuralRing = std::pow(ringPhase, 6.0F) * rippleToggle
        * (0.20F + parameters.energy * 0.95F) * 0.88F * terrainField
        * cellModulation;
    const float impactAge = clampUnit(parameters.impactAge);
    const float impact = clampUnit(parameters.impactStrength);
    const float centerPulse = impact * dynamics.centerHighlight
        * (1.0F - impactAge) * std::exp(-(distance * distance) / 1150.0F)
        * 7.2F;
    const float domeRadius = std::max(12.0F, dynamics.responseRadius * 0.36F);
    const float centerDome = std::exp(-(distance * distance)
                                      / (domeRadius * domeRadius));
    const float steadyCenter = parameters.energy * dynamics.centerHighlight
        * (0.35F + parameters.bands[0] * 0.65F) * centerDome * 10.8F;
    const float centerShoulders = parameters.energy * dynamics.centerHighlight
        * terrainField * (0.42F + core * 1.55F + wideRidge * 0.38F);
    const float impactRadius = impactAge * dynamics.responseRadius * 1.15F;
    const float impactDistance = std::abs(distance - impactRadius);
    const float impactRing = impact * dynamics.rhythmStrength
        * std::exp(-(impactDistance * impactDistance) / 18.0F) * 5.8F;
    const float amplitude = 0.2F + clampUnit(style.terrainAmplitude) * 1.25F;
    const float maximumHeight = impact > 0.0F ? 36.0F : 32.0F;
    const float rawHeight = std::max(0.0F,
        idle + ((bass + mids + peak) * terrainField + ripple
                + structuralRing) * amplitude
        + steadyCenter + centerShoulders
        + centerPulse + impactRing);
    return std::max(0.035F, maximumHeight
        * (1.0F - std::exp(-rawHeight / maximumHeight)));
}

void AutomaticQualityController::observeWorkSample(
    double workMilliseconds) noexcept
{
    const double work = std::max(0.0, workMilliseconds);
    if (work > frameBudgetMilliseconds_ * 1.05) {
        loadSample_ = LoadSample::OverBudget;
    } else if (work < frameBudgetMilliseconds_ * 0.80) {
        loadSample_ = LoadSample::UnderBudget;
    } else {
        loadSample_ = LoadSample::Neutral;
    }
}

void AutomaticQualityController::advanceWallClock(
    double elapsedSeconds) noexcept
{
    const double elapsed = std::max(0.0, elapsedSeconds);
    cooldownRemainingSeconds_ = std::max(0.0,
        cooldownRemainingSeconds_ - elapsed);

    if (loadSample_ == LoadSample::OverBudget) {
        overBudgetSeconds_ += elapsed;
        underBudgetSeconds_ = 0.0;
    } else if (loadSample_ == LoadSample::UnderBudget) {
        underBudgetSeconds_ += elapsed;
        overBudgetSeconds_ = 0.0;
    } else {
        overBudgetSeconds_ = 0.0;
        underBudgetSeconds_ = 0.0;
    }

    const int current = stageIndex(stage_);
    const int last = stageIndex(DegradationStage::ReducedResolution);
    if (cooldownRemainingSeconds_ <= 0.0
        && overBudgetSeconds_ >= downgradeSeconds_ && current < last) {
        stage_ = static_cast<DegradationStage>(current + 1);
        overBudgetSeconds_ = 0.0;
        underBudgetSeconds_ = 0.0;
        cooldownRemainingSeconds_ = cooldownSeconds_;
    } else if (cooldownRemainingSeconds_ <= 0.0
               && underBudgetSeconds_ >= upgradeSeconds_ && current > 0) {
        stage_ = static_cast<DegradationStage>(current - 1);
        overBudgetSeconds_ = 0.0;
        underBudgetSeconds_ = 0.0;
        cooldownRemainingSeconds_ = cooldownSeconds_;
    }
}

DegradationStage AutomaticQualityController::stage() const noexcept
{
    return stage_;
}

QualityConfiguration AutomaticQualityController::configuration() const noexcept
{
    QualityConfiguration result;
    switch (stage_) {
    case DegradationStage::Full:
        break;
    case DegradationStage::ReducedParticles:
        result.particleCount = 80;
        break;
    case DegradationStage::ReducedMeteors:
        result.particleCount = 80;
        result.meteorCount = 10;
        break;
    case DegradationStage::ReducedRipples:
        result.particleCount = 80;
        result.meteorCount = 10;
        result.rippleCount = 5;
        break;
    case DegradationStage::ReducedGrid:
        result.particleCount = 80;
        result.meteorCount = 10;
        result.rippleCount = 5;
        result.gridSize = 112;
        break;
    case DegradationStage::ReducedResolution:
        result.particleCount = 80;
        result.meteorCount = 10;
        result.rippleCount = 5;
        result.gridSize = 112;
        result.internalScale = 0.75F;
        break;
    }
    return result;
}

void RenderWorkGate::setActive(bool active) noexcept { active_ = active; }
void RenderWorkGate::setVisible(bool visible) noexcept { visible_ = visible; }
void RenderWorkGate::setExposed(bool exposed) noexcept { exposed_ = exposed; }
bool RenderWorkGate::canRun() const noexcept
{
    return active_ && visible_ && exposed_;
}
bool RenderWorkGate::advance(bool uploaded) noexcept
{
    if (!canRun()) return false;
    ++counters_.frames;
    ++counters_.animations;
    if (uploaded) ++counters_.uploads;
    return true;
}
WorkCounters RenderWorkGate::counters() const noexcept { return counters_; }

std::atomic<quint64> RendererResourceState::globalGeneration_{0};

bool RendererResourceState::acquireRenderer(quint64 rendererId) noexcept
{
    if (rendererId == 0) return false;
    quint64 expected = 0;
    return rendererId_.compare_exchange_strong(expected, rendererId,
                                                std::memory_order_acq_rel);
}
void RendererResourceState::releaseRenderer(quint64 rendererId) noexcept
{
    quint64 expected = rendererId;
    if (rendererId_.compare_exchange_strong(expected, 0,
                                             std::memory_order_acq_rel)) {
        resourcesReady_.store(false, std::memory_order_release);
    }
}
quint64 RendererResourceState::initializeResources() noexcept
{
    if (rendererId_.load(std::memory_order_acquire) == 0) {
        return generation_.load(std::memory_order_acquire);
    }
    bool expected = false;
    if (resourcesReady_.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel)) {
        generation_.store(globalGeneration_.fetch_add(1,
            std::memory_order_acq_rel) + 1, std::memory_order_release);
    }
    return generation_.load(std::memory_order_acquire);
}
void RendererResourceState::invalidateResources() noexcept
{
    resourcesReady_.store(false, std::memory_order_release);
}
bool RendererResourceState::resourcesReady() const noexcept
{
    return resourcesReady_.load(std::memory_order_acquire);
}
int RendererResourceState::liveRendererCount() const noexcept
{
    return rendererId_.load(std::memory_order_acquire) == 0 ? 0 : 1;
}
quint64 RendererResourceState::generation() const noexcept
{
    return generation_.load(std::memory_order_acquire);
}

bool RendererResourceState::claimPunchRevision(quint64 revision) noexcept
{
    if (revision == 0) return false;
    quint64 consumed = consumedPunchRevision_.load(std::memory_order_acquire);
    while (revision > consumed) {
        if (consumedPunchRevision_.compare_exchange_weak(
                consumed, revision, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

bool RendererResourceState::claimImpactRevision(quint64 revision) noexcept
{
    if (revision == 0) return false;
    quint64 consumed = consumedImpactRevision_.load(std::memory_order_acquire);
    while (revision > consumed) {
        if (consumedImpactRevision_.compare_exchange_weak(
                consumed, revision, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

bool FramePacer::shouldRender(double nowSeconds,
                              double targetFramesPerSecond) noexcept
{
    const double now = std::max(0.0, nowSeconds);
    const double framesPerSecond = std::clamp(targetFramesPerSecond, 1.0, 240.0);
    const double interval = 1.0 / framesPerSecond;
    if (!initialized_ || now + 0.000001 >= nextFrameSeconds_) {
        initialized_ = true;
        nextFrameSeconds_ = now + interval;
        return true;
    }
    return false;
}

void CameraMotion::orbitBy(float yawDelta, float pitchDelta,
                           double nowSeconds) noexcept
{
    snapshot_ = sanitizedCameraSnapshot(snapshot_);
    if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta)
        || !std::isfinite(nowSeconds)) return;
    snapshot_.yaw += yawDelta;
    snapshot_.pitch = std::clamp(snapshot_.pitch + pitchDelta, 0.12F, 1.15F);
    markManual(nowSeconds);
}
void CameraMotion::zoomBy(float wheelDelta, double nowSeconds) noexcept
{
    snapshot_ = sanitizedCameraSnapshot(snapshot_);
    if (!std::isfinite(wheelDelta) || !std::isfinite(nowSeconds)) return;
    snapshot_.distance = std::clamp(snapshot_.distance + wheelDelta * 0.04F,
                                     42.0F, 220.0F);
    markManual(nowSeconds);
}
void CameraMotion::applyBeatPunch(float strength) noexcept
{
    snapshot_.punch = clampUnit(snapshot_.punch + clampUnit(strength));
}
void CameraMotion::advance(double nowSeconds, float elapsedSeconds,
                           float autoRotateSpeed) noexcept
{
    const float elapsed = std::max(0.0F, elapsedSeconds);
    if (nowSeconds >= manualUntilSeconds_) {
        snapshot_.yaw += elapsed * std::clamp(autoRotateSpeed, 0.0F, 2.0F)
            * 0.098F;
    }
    snapshot_.punch *= std::exp(-elapsed * 5.0F);
}
CameraSnapshot CameraMotion::snapshot() const noexcept { return snapshot_; }
double CameraMotion::manualUntilSeconds() const noexcept
{
    return manualUntilSeconds_;
}
void CameraMotion::synchronize(CameraSnapshot snapshot,
                               double manualUntilSeconds) noexcept
{
    const CameraSnapshot fallback = sanitizedCameraSnapshot(snapshot_);
    snapshot_ = sanitizedCameraSnapshot(snapshot, fallback);
    const double safeManualUntil = std::isfinite(manualUntilSeconds_)
        ? manualUntilSeconds_ : 0.0;
    manualUntilSeconds_ = std::max(
        0.0, std::isfinite(manualUntilSeconds)
            ? manualUntilSeconds : safeManualUntil);
}
void CameraMotion::applyManualDelta(const CameraSnapshot& previous,
                                    const CameraSnapshot& next,
                                    double nowSeconds) noexcept
{
    snapshot_ = sanitizedCameraSnapshot(snapshot_);
    if (!std::isfinite(previous.yaw) || !std::isfinite(previous.pitch)
        || !std::isfinite(previous.distance) || !std::isfinite(next.yaw)
        || !std::isfinite(next.pitch) || !std::isfinite(next.distance)
        || !std::isfinite(nowSeconds)) return;
    const float yawDelta = next.yaw - previous.yaw;
    const float pitchDelta = next.pitch - previous.pitch;
    const float distanceDelta = next.distance - previous.distance;
    if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta)
        || !std::isfinite(distanceDelta)) return;
    const bool manuallyMoved = std::abs(yawDelta) > 0.000001F
        || std::abs(pitchDelta) > 0.000001F
        || std::abs(distanceDelta) > 0.000001F;
    snapshot_.yaw += yawDelta;
    snapshot_.pitch = std::clamp(snapshot_.pitch + pitchDelta, 0.12F, 1.15F);
    snapshot_.distance = std::clamp(next.distance, 42.0F, 220.0F);
    if (manuallyMoved) markManual(nowSeconds);
}
void CameraMotion::markManual(double nowSeconds) noexcept
{
    if (!std::isfinite(nowSeconds)) return;
    manualUntilSeconds_ = std::max(0.0, nowSeconds) + 4.0;
}

PunchEventConsumer::PunchEventConsumer(
    RendererResourceState& lifecycle) noexcept
    : lifecycle_(lifecycle)
{
}

bool PunchEventConsumer::consume(const PunchEvent& event,
                                 CameraMotion& camera) noexcept
{
    if (!lifecycle_.claimPunchRevision(event.revision)) {
        return false;
    }
    camera.applyBeatPunch(event.strength);
    return true;
}

ImpactEventConsumer::ImpactEventConsumer(
    RendererResourceState& lifecycle) noexcept
    : lifecycle_(lifecycle)
{
}

bool ImpactEventConsumer::consume(const ImpactEvent& event,
                                  float nowSeconds) noexcept
{
    if (!lifecycle_.claimImpactRevision(event.revision)) return false;
    startSeconds_ = std::max(0.0F, nowSeconds);
    baseStrength_ = clampUnit(event.strength);
    active_ = baseStrength_ > 0.0F;
    return true;
}

ImpactPulseSnapshot ImpactEventConsumer::snapshot(float nowSeconds) const noexcept
{
    ImpactPulseSnapshot result;
    if (!active_) return result;
    const float elapsed = std::max(0.0F, nowSeconds - startSeconds_);
    const float age = std::clamp(elapsed / durationSeconds_, 0.0F, 1.0F);
    if (age >= 1.0F) return result;
    result.active = true;
    result.age = age;
    result.strength = clampUnit(baseStrength_ * (1.0F - age * 0.55F));
    return result;
}

} // namespace agplayer::terrain
