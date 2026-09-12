#include "terrain_reactor_state.hpp"
#include "immersive_theme_catalog.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace agplayer::terrain {
QVector3D MeteorMaterialColor::advance(QVector3D targetWarmLinear, float dt) noexcept
{
    const float blend = std::isfinite(dt) ? std::clamp(3.0F * dt, 0.0F, 1.0F) : 0;
    const QVector3D target = targetWarmLinear * .3F + QVector3D(.7F,.7F,.7F);
    color_ += (target - color_) * blend;
    return color_;
}

void MeteorParticlePool::spawn(QVector3D origin, float multiplier, const std::array<float,8>& samples) noexcept
{
    Particle& p = particles_[next_];
    p = {};
    p.active = true;
    p.position = origin + QVector3D((samples[0]-.5F)*1.5F,
        (samples[1]-.5F)*1.5F, (samples[2]-.5F)*1.5F);
    p.velocity = {(samples[3]-.5F)*2, samples[4]*2+multiplier*10, (samples[5]-.5F)*2};
    p.maxLife = .5F+samples[6]*.5F;
    p.baseScale = .2F+samples[7]*.6F;
    next_ = (next_+1)%particles_.size();
}
void MeteorParticlePool::advance(float dt) noexcept
{
    if (!std::isfinite(dt) || dt < 0) return;
    for (auto& p:particles_) if(p.active) {
        p.life += dt;
        if (p.life >= p.maxLife) p.active = false;
        else p.position += p.velocity * (dt*10);
    }
}
QVector4D advanceFloatingBlocks(float previousPulse, float kick, float dt,
                               float minSize, float maxSize, float speed, float intensity) noexcept
{
    const float seconds = std::isfinite(dt) ? std::max(0.0F, dt) : 0.0F;
    const float amount = std::clamp(intensity / 100.0F, 0.0F, 1.0F);
    const float blend = 1.0F - std::exp(-(3.0F + 33.0F * std::clamp(speed / 100.0F, 0.0F, 1.0F)) * seconds);
    const float pulse = previousPulse + (std::clamp(kick, 0.0F, 1.0F) - previousPulse) * blend;
    const float low = 0.12F + 0.63F * std::clamp(minSize / 100.0F, 0.0F, 1.0F);
    const float high = std::max(low + 0.05F, 0.45F + 2.75F * std::clamp(maxSize / 100.0F, 0.0F, 1.0F));
    const float mix = std::clamp(pulse * (0.5F + amount * 1.7F), 0.0F, 1.0F);
    return {pulse, mix, low + (high - low) * mix, amount};
}
ThemePalette advanceThemePalette(const ThemePalette& current,
                                 const ThemePalette& target,
                                 float deltaSeconds) noexcept
{
    if (current.colors == target.colors && current.glow == target.glow)
        return target;
    const float blend = std::isfinite(deltaSeconds)
        ? std::clamp(deltaSeconds * 3.0F, 0.0F, 1.0F) : 0.0F;
    if (blend <= 0.0F) return current;
    if (blend >= 1.0F) return target;
    ThemePalette result = target;
    for (std::size_t index = 0; index < result.colors.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel) {
            if (current.colors[index][channel] == target.colors[index][channel])
                continue;
            const float from = immersive::srgbChannelToLinear(current.colors[index][channel]);
            const float to = immersive::srgbChannelToLinear(target.colors[index][channel]);
            result.colors[index][channel] = immersive::workingLinearChannelToSrgb(
                from + (to - from) * blend);
        }
        // Alpha stores semantic flags, not visual opacity.
        result.colors[index].setW(target.colors[index].w());
    }
    result.glow = current.glow + (target.glow - current.glow) * blend;
    return result;
}

float smoothReactorFeature(float current, float target, float elapsedSeconds) noexcept
{
    const float safeCurrent = std::isfinite(current) ? std::clamp(current, 0.0F, 1.0F) : 0.0F;
    const float safeTarget = std::isfinite(target) ? std::clamp(target, 0.0F, 1.0F) : 0.0F;
    const float elapsed = std::isfinite(elapsedSeconds) ? std::clamp(elapsedSeconds, 0.0F, 0.25F) : 0.0F;
    const float timeConstant = safeTarget > safeCurrent ? 0.045F : 0.105F;
    return safeCurrent + (safeTarget - safeCurrent) * (1.0F - std::exp(-elapsed / timeConstant));
}

bool TravelingWaveGate::consume(float nowSeconds, float strength) noexcept
{
    if (!std::isfinite(nowSeconds) || nowSeconds < nextSeconds_) return false;
    anchor(nowSeconds, strength);
    return true;
}

void TravelingWaveGate::anchor(float nowSeconds, float strength) noexcept
{
    if (!std::isfinite(nowSeconds)) return;
    const float energy = std::isfinite(strength) ? std::clamp(strength, 0.0F, 1.0F) : 0.0F;
    // Keep the user's deliberately sparse 3--6 second cadence, but avoid the
    // long six-second dead zone that made ordinary musical waves appear lost.
    // Strong beats arrive after 3 s; quieter accepted events after at most 5 s.
    nextSeconds_ = nowSeconds + 5.0F - energy * 2.0F;
}

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
    safeFallback.pitch = clampRange(fallback.pitch, 0.1F, 1.5707953F,
                                    defaults.pitch);
    safeFallback.distance = clampRange(fallback.distance, 5.0F, 120.0F,
                                       defaults.distance);
    safeFallback.punch = clampUnit(fallback.punch, defaults.punch);

    CameraSnapshot result;
    result.yaw = finiteOr(candidate.yaw, safeFallback.yaw);
    result.pitch = clampRange(candidate.pitch, 0.1F, 1.5707953F,
                              safeFallback.pitch);
    result.distance = clampRange(candidate.distance, 5.0F, 120.0F,
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
    // The playback spectrum already has a logarithmic scale and a decay.
    // Lift quiet musical detail without normalizing silence up to full height.
    const float bounded = clampUnit((clampUnit(value) - 0.002F) / 0.998F);
    const float exponent = 0.55F + dynamics.inputCompression * 1.1F;
    const float compressed = 1.0F - std::pow(1.0F - bounded, exponent);
    const float lifted = std::pow(compressed, 0.70F);
    // Bounded gain retains the ordering of loud bands. Multiplication followed
    // by clipping used to turn ordinary ~0.55 input into the same full height.
    const float gain = dynamics.audioResponse;
    return clampUnit(lifted * gain / (1.0F + lifted * (gain - 1.0F)));
}

} // namespace

void MeteorParticlePool::frame(float dt, QVector4D trajectory, float age, bool airborne, bool landed) noexcept
{
    if (!std::isfinite(dt) || dt < 0) return;
    seed_ = seed_*1664525U+1013904223U;
    DeterministicRandom random(seed_);
    const float y = trajectory.z()-trajectory.w()*60*age;
    const int count = landed ? 10 : (airborne && y > 0 && random.unit() > .3F ? 1 : 0);
    for (int i=0;i<count;++i) {
        std::array<float,8> samples;
        for (auto& sample:samples) sample = random.unit();
        spawn({trajectory.x(), landed ? .5F : y, trajectory.y()},
            trajectory.w()*(landed ? 1.5F : .2F), samples);
    }
    // MapScene updates newly spawned particles in this same frame.
    advance(dt);
}

QVector4D consumeSnareWave(TravelingWaveGate& gate, float now, double strength,
    quint32 seed, bool enabled, bool meteorLanded) noexcept
{
    if (!enabled || meteorLanded || !std::isfinite(strength) || strength <= 0
        || !gate.consume(now, float(strength))) return {};
    DeterministicRandom random(seed);
    const float angle = random.unit() * float(2.0 * M_PI);
    const float radius = 10.0F + random.unit() * 35.0F;
    // Same distribution/strength as the original Snare callback. The shared
    // 3--6s presentation gate above is the explicit native product exception.
    return QVector4D(std::cos(angle) * radius, std::sin(angle) * radius, now,
        -float((std::min)(strength * 3.0, 3.0)));
}

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
    static const std::array<TrackPalette, 6> coordinatedPalettes{{
        TrackPalette{QVector4D(0.018F, 0.026F, 0.075F, 1.0F),
                     QVector4D(0.14F, 0.52F, 0.98F, 1.0F),
                     QVector4D(0.96F, 0.32F, 0.34F, 1.0F),
                     QVector4D(0.28F, 0.86F, 0.95F, 1.0F),
                     QVector4D(1.0F, 0.84F, 0.80F, 1.0F)},
        TrackPalette{QVector4D(0.020F, 0.040F, 0.085F, 1.0F),
                     QVector4D(0.10F, 0.78F, 0.94F, 1.0F),
                     QVector4D(0.98F, 0.40F, 0.32F, 1.0F),
                     QVector4D(0.42F, 0.72F, 1.0F, 1.0F),
                     QVector4D(0.84F, 0.86F, 1.0F, 1.0F)},
        TrackPalette{QVector4D(0.032F, 0.022F, 0.072F, 1.0F),
                     QVector4D(0.20F, 0.46F, 0.96F, 1.0F),
                     QVector4D(0.98F, 0.32F, 0.43F, 1.0F),
                     QVector4D(0.34F, 0.90F, 0.88F, 1.0F),
                     QVector4D(0.96F, 0.78F, 0.82F, 1.0F)},
        TrackPalette{QVector4D(0.014F, 0.046F, 0.070F, 1.0F),
                     QVector4D(0.08F, 0.83F, 0.91F, 1.0F),
                     QVector4D(0.94F, 0.34F, 0.42F, 1.0F),
                     QVector4D(0.24F, 0.65F, 1.0F, 1.0F),
                     QVector4D(0.86F, 0.94F, 1.0F, 1.0F)},
        TrackPalette{QVector4D(0.030F, 0.030F, 0.080F, 1.0F),
                     QVector4D(0.18F, 0.62F, 1.0F, 1.0F),
                     QVector4D(1.0F, 0.48F, 0.34F, 1.0F),
                     QVector4D(0.34F, 0.72F, 1.0F, 1.0F),
                     QVector4D(1.0F, 0.82F, 0.88F, 1.0F)},
        TrackPalette{QVector4D(0.016F, 0.038F, 0.088F, 1.0F),
                     QVector4D(0.12F, 0.70F, 0.98F, 1.0F),
                     QVector4D(0.98F, 0.38F, 0.32F, 1.0F),
                     QVector4D(0.18F, 0.80F, 0.76F, 1.0F),
                     QVector4D(0.82F, 0.87F, 1.0F, 1.0F)},
    }};
    const std::size_t family = std::size_t(seed)
        % coordinatedPalettes.size();
    const TrackPalette& from = coordinatedPalettes[family];
    const TrackPalette& to = coordinatedPalettes[
        (family + 1U) % coordinatedPalettes.size()];
    return blendTrackPalettes(from, to, random.unit() * 0.22F);
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

bool MeteorFlight::launch(float now, int count, float strength) noexcept
{
    if (!std::isfinite(now) || count <= 0 || !std::isfinite(strength)
        || strength <= 0.0F || pending_ || now - start_ < 3.0F)
        return false;
    group_ = int(sequence_++ % unsigned(count));
    start_ = now;
    strength_ = strength;
    DeterministicRandom random(sequence_ * 0x9e3779b9U);
    const float angle = random.unit() * float(2.0 * M_PI);
    const float radius = random.unit() * 25.0F;
    const float height = 30.0F + random.unit() * 10.0F;
    const float speed = 1.0F + random.unit() * 0.5F + strength * 1.5F;
    trajectory_ = QVector4D(std::cos(angle) * radius, std::sin(angle) * radius, height, speed);
    pending_ = true;
    return true;
}

bool MeteorFlight::landed(float now) noexcept
{
    if (!pending_ || !std::isfinite(now) || now - start_ < duration())
        return false;
    pending_ = false;
    return true;
}

MultiWaveSources multiWaveSources(quint32 seed) noexcept
{
    DeterministicRandom random(seed == 0U ? 1U : seed);
    MultiWaveSources sources{};
    // Keep the first ridge near the musical core; the remaining sources are
    // distributed over the circular terrain so the field does not read as a
    // mechanical stack of concentric rings.
    sources[0] = QVector4D(0.0F, 0.0F, random.unit(), 1.0F);
    for (std::size_t index = 1; index < sources.size(); ++index) {
        const float angle = random.unit() * 2.0F * float(M_PI);
        const float radius = 14.0F + random.unit() * 48.0F;
        sources[index] = QVector4D(std::cos(angle) * radius,
                                   std::sin(angle) * radius,
                                   random.unit(),
                                   0.45F + random.unit() * 0.55F);
    }
    return sources;
}

BassEnvelopeSnapshot BassEnvelopeFollower::advance(
    float bass, float elapsedSeconds) noexcept
{
    const float input = clampUnit(bass);
    const float elapsed = std::clamp(finiteOr(elapsedSeconds, 0.0F),
                                     0.0F, 0.25F);
    const auto follow = [elapsed](float current, float target,
                                  float attackSeconds,
                                  float releaseSeconds) noexcept {
        const float timeConstant = target > current
            ? attackSeconds : releaseSeconds;
        const float amount = timeConstant <= 0.0F
            ? 1.0F : 1.0F - std::exp(-elapsed / timeConstant);
        return clampUnit(current + (target - current) * amount);
    };
    snapshot_.fast = follow(snapshot_.fast, input, 0.030F, 0.100F);
    snapshot_.slow = follow(snapshot_.slow, input, 0.075F, 0.220F);
    return snapshot_;
}

BassEnvelopeSnapshot BassEnvelopeFollower::snapshot() const noexcept
{
    return snapshot_;
}

SceneLayout makeSceneLayout(quint32 seed, int gridSize, int floatingCount,
                            int meteorCount, int particleCount)
{
    SceneLayout result;
    const int boundedGrid = std::max(1, gridSize);
    const int boundedParticleCount = std::clamp(particleCount, 0, 1600);
    const int terrainCount = boundedGrid * boundedGrid;
    result.terrain.reserve(terrainCount);
    result.floating.reserve(std::max(0, floatingCount));
    result.meteors.reserve(std::max(0, meteorCount));
    result.particles.reserve(boundedParticleCount);

    DeterministicRandom random(seed);
    constexpr float extent = kTerrainStageExtent;
    // JS computes in double, then InstancedMesh stores Float32. Preserve both
    // that rounding and x-outer/z-inner submission order for transparency.
    const double spacing = double(extent) / boundedGrid;
    for (int x = 0; x < boundedGrid; ++x) {
        for (int z = 0; z < boundedGrid; ++z) {
            SceneInstance instance;
            const float worldX = float(-double(extent) * .5 + double(x) * spacing);
            const float worldZ = float(-double(extent) * .5 + double(z) * spacing);
            instance.position = QVector3D(worldX, 0.0F, worldZ);
            // Instances carry physical box dimensions. The shader must not
            // apply another hidden gutter to this reference width ratio.
            const float columnWidth = float(spacing * (0.9 / 1.05));
            instance.scale = QVector3D(columnWidth, 1.0F, columnWidth);
            instance.random = random.unit();
            // Submit the complete Cartesian grid. The vertex shader owns the
            // radial stage fade, so the CPU count remains exactly N * N.
            const float radius = std::hypot(worldX, worldZ) / (extent * 0.5F);
            instance.zone = zoneFor(worldX, worldZ, radius, instance.random);
            result.terrain.append(instance);
        }
    }

    for (int index = 0; index < floatingCount; ++index) {
        const ColorZone zone = index % 3 == 0 ? ColorZone::Cool
            : index % 3 == 1 ? ColorZone::Warm : ColorZone::Accent;
        const float angle = float(index) / float(floatingCount) * float(M_PI * 10.0)
                            + std::sin(float(index) * 12.9898F) * 0.7F;
        const float radius = 14.0F + float((index * 37) % 62);
        SceneInstance floating;
        floating.position = {std::cos(angle)*radius, 6.0F+float((index*17)%19), std::sin(angle)*radius};
        floating.scale = QVector3D(1,1,1) * (0.75F + float((index*11)%9)*0.05F);
        floating.aux = float(index);
        floating.zone = zone;
        result.floating.append(floating);
    }
    for (int index = 0; index < meteorCount; ++index) {
        SceneInstance meteor = makeExtra(random, 18.0F, 74.0F,
                                         32.0F, 46.0F, ColorZone::Peak);
        meteor.scale = QVector3D(0.36F, 1.20F, 0.36F);
        meteor.aux = float(index);
        result.meteors.append(meteor);
    }
    constexpr float goldenRatioConjugate = 0.61803398875F;
    constexpr float plasticRatioConjugate = 0.75487766625F;
    const float verticalOffset = random.unit();
    const float azimuthOffset = random.unit();
    for (int index = 0; index < boundedParticleCount; ++index) {
        const ColorZone starZone = index % 3 == 0 ? ColorZone::Cool
            : index % 3 == 1 ? ColorZone::Warm : ColorZone::Peak;
        SceneInstance particle;
        const float radialDepth = (float(index) + random.unit())
            / float(boundedParticleCount);
        const float normalizedRadius = std::sqrt(radialDepth);
        const float radius = 240.0F + normalizedRadius * 320.0F;
        const float verticalCycle = std::fmod(
            float(index) * goldenRatioConjugate + verticalOffset, 1.0F);
        const float vertical = verticalCycle * 2.0F - 1.0F;
        const float horizontal = std::sqrt(std::max(
            0.0F, 1.0F - vertical * vertical));
        const float azimuthCycle = std::fmod(
            float(index) * plasticRatioConjugate + azimuthOffset, 1.0F);
        const float azimuth = azimuthCycle * 2.0F * float(M_PI);
        particle.position = QVector3D(horizontal * std::cos(azimuth), vertical,
                                      horizontal * std::sin(azimuth)) * radius;
        particle.random = random.unit();
        particle.zone = starZone;
        const float starSize = 0.075F + (1.0F - normalizedRadius) * 0.32F
            + random.unit() * 0.035F;
        particle.scale = QVector3D(starSize, starSize, starSize);
        result.particles.append(particle);
    }
    return result;
}

int referenceTerrainGridSize(int density) noexcept
{
    return int(std::lround(96.0 + 128.0 * std::clamp(density, 0, 100) / 100.0));
}

int terrainGridSizeForDensity(int baseGridSize, int densityPercent,
                              int gridCeiling) noexcept
{
    const int safeCeiling = std::max(32, gridCeiling);
    const int safeBase = std::clamp(baseGridSize, 32, safeCeiling);
    const float density = float(std::clamp(densityPercent, 50, 200)) / 125.0F;
    return std::clamp(qRound(float(safeBase) * std::sqrt(density)),
                      32, safeCeiling);
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
    parameters.beatStrength = clampUnit(parameters.beatStrength);
    parameters.beatAge = clampUnit(parameters.beatAge);
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
    const float ridgeCoordinateA = instance.position.x() * 0.052F
        + instance.position.z() * 0.024F;
    const float ridgeCoordinateB = instance.position.z() * 0.061F
        - instance.position.x() * 0.019F;
    const float ridgeMaskA = std::pow(0.5F + 0.5F * std::sin(
        ridgeCoordinateA + timeSeconds * 0.24F), 2.4F);
    const float ridgeMaskB = std::pow(0.5F + 0.5F * std::cos(
        ridgeCoordinateB - timeSeconds * 0.19F), 2.7F);
    const float midRegionalField = std::clamp(0.36F + (1.0F - center) * 0.38F
        + ridgeMaskA * 0.45F + ridgeMaskB * 0.12F, 0.0F, 1.35F);
    const float mids = (parameters.bands[2] * (0.58F + wideRidge * 2.30F)
        + parameters.bands[3] * (0.62F + (1.0F - wideRidge) * 2.05F))
        * midRegionalField;
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
    const float localizedHigh = 0.28F
        + 0.92F * std::pow(clampUnit(instance.random), 1.45F);
    const float peak = highEnergy * (0.16F + coherentDetail * 1.55F)
        * center * peakControl * localizedHigh;
    const float idlePhase = std::sin(instance.position.x() * 0.032F
                                     + instance.position.z() * 0.041F) * 0.72F;
    const float reliefA = 0.5F + 0.5F * std::sin(
        instance.position.x() * 0.055F + instance.position.z() * 0.032F);
    const float reliefB = 0.5F + 0.5F * std::cos(
        instance.position.z() * 0.070F - instance.position.x() * 0.018F);
    const float baseRelief = (0.24F + 0.48F
        * (reliefA * 0.55F + reliefB * 0.45F)) * terrainField
        + core * 1.08F;
    const float idle = style.idleBreathingEnabled
        ? baseRelief + 0.06F + 0.10F * std::sin(
              distance * 0.067F + idlePhase)
        : 0.0F;
    const float rippleRadius = std::fmod(std::max(0.0F, timeSeconds) * 13.5F,
                                        96.0F);
    const float ringDistance = std::abs(distance - rippleRadius);
    const float cellModulation = 0.55F + clampUnit(instance.random) * 0.45F;
    const float rippleToggle = style.ripplesEnabled ? 1.0F : 0.0F;
    const float ripple = parameters.rippleStrength * rippleToggle
        * std::exp(-(ringDistance * ringDistance) / 30.25F) * 3.35F
        * cellModulation;
    const float impactAge = clampUnit(parameters.impactAge);
    const float impact = clampUnit(parameters.impactStrength);
    const float centerPulse = impact * dynamics.centerHighlight
        * (1.0F - impactAge) * std::exp(-(distance * distance) / 1150.0F)
        * 7.2F;
    const float domeRadius = std::max(12.0F, dynamics.responseRadius * 0.36F);
    const float centerDome = std::exp(-(distance * distance)
                                      / (domeRadius * domeRadius));
    const float beatEnvelope = parameters.beatStrength
        * std::pow(1.0F - parameters.beatAge, 2.0F);
    const float steadyCenter = parameters.energy * dynamics.centerHighlight
        * (0.16F + parameters.bands[0] * 0.32F) * centerDome * 8.0F;
    const float beatLift = beatEnvelope * dynamics.centerHighlight
        * centerDome * 3.8F;
    const float centerShoulders = parameters.energy * dynamics.centerHighlight
        * terrainField * (0.035F + core * 0.14F + wideRidge * 0.26F);
    const float impactRadius = impactAge * dynamics.responseRadius * 1.15F;
    const float impactDistance = std::abs(distance - impactRadius);
    const float impactRing = impact * dynamics.rhythmStrength
        * std::exp(-(impactDistance * impactDistance) / 18.0F) * 5.8F;
    const float amplitude = 0.16F + clampUnit(style.terrainAmplitude) * 2.14F;
    const float maximumHeight = impact > 0.0F ? 36.0F : 32.0F;
    const float rawHeight = std::max(0.0F,
        idle + ((bass + mids + peak) * terrainField + ripple) * amplitude
        + steadyCenter + beatLift + centerShoulders
        + centerPulse + impactRing);
    return std::max(0.035F, maximumHeight
        * (1.0F - std::exp(-rawHeight / maximumHeight)));
}

void AutomaticQualityController::observeWorkSample(
    double workMilliseconds) noexcept
{
    observeFrameSample(workMilliseconds, frameBudgetMilliseconds_,
                       frameBudgetMilliseconds_);
}

void AutomaticQualityController::observeFrameSample(
    double workMilliseconds, double frameElapsedMilliseconds,
    double targetFrameMilliseconds) noexcept
{
    const double target = std::max(1.0, targetFrameMilliseconds);
    const double work = std::max(0.0, workMilliseconds);
    const double elapsed = std::max(0.0, frameElapsedMilliseconds);
    if (lastTargetFrameMilliseconds_ <= 0.0
        || std::abs(lastTargetFrameMilliseconds_ - target) > target * 0.10) {
        smoothedFrameElapsedMilliseconds_ = target;
    }
    lastTargetFrameMilliseconds_ = target;
    constexpr double cadenceSmoothing = 0.18;
    smoothedFrameElapsedMilliseconds_ +=
        (elapsed - smoothedFrameElapsedMilliseconds_) * cadenceSmoothing;

    // 45 FPS on a 60 Hz display naturally produces a 33/16/16 ms cadence.
    // The smoothed cadence accepts that quantization while still detecting a
    // sustained 33/33/33 ms (30 FPS) stream. A severe single miss remains an
    // immediate over-budget sample.
    const bool severeCadenceMiss = elapsed > target * 1.55;
    const bool sustainedCadenceMiss =
        smoothedFrameElapsedMilliseconds_ > target * 1.25;
    const bool healthyCadence =
        smoothedFrameElapsedMilliseconds_ <= target * 1.22;
    if (work > target * 1.05 || severeCadenceMiss || sustainedCadenceMiss) {
        loadSample_ = LoadSample::OverBudget;
    } else if (work < target * 0.80 && healthyCadence) {
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

QualityConfiguration AutomaticQualityController::configuration(bool eco) const noexcept
{
    QualityConfiguration result;
    switch (stage_) {
    case DegradationStage::Full:
        break;
    case DegradationStage::ReducedParticles:
        result.floatingCount = 52;
        result.particleCount = 240;
        break;
    case DegradationStage::ReducedMeteors:
        result.floatingCount = 52;
        result.particleCount = 240;
        result.meteorCount = 6;
        break;
    case DegradationStage::ReducedRipples:
        result.floatingCount = 52;
        result.particleCount = 240;
        result.meteorCount = 6;
        result.rippleCount = 2;
        break;
    case DegradationStage::ReducedGrid:
        result.floatingCount = 52;
        result.particleCount = 240;
        result.meteorCount = 6;
        result.rippleCount = 2;
        result.gridSize = 96;
        result.internalScale = 0.82F;
        result.sampleCount = 1;
        break;
    case DegradationStage::ReducedResolution:
        result.floatingCount = 52;
        result.particleCount = 120;
        result.meteorCount = 4;
        result.rippleCount = 2;
        result.gridSize = 96;
        result.internalScale = 0.70F;
        result.sampleCount = 1;
        break;
    }
    if (eco) {
        // Compose the desktop budget with adaptive stages here: capping the
        // full-size presets afterward swallowed Eco's first four reductions.
        const int stage = stageIndex(stage_);
        result.gridSize = std::min(result.gridSize,
            stage >= stageIndex(DegradationStage::ReducedGrid) ? 80 : 96);
        result.floatingCount = std::min(result.floatingCount, 36);
        result.particleCount = std::min(result.particleCount,
            stage >= stageIndex(DegradationStage::ReducedParticles) ? 64 : 160);
        result.meteorCount = std::min(result.meteorCount,
            stage >= stageIndex(DegradationStage::ReducedMeteors) ? 3 : 5);
        result.rippleCount = std::min(result.rippleCount,
            stage >= stageIndex(DegradationStage::ReducedRipples) ? 2 : 4);
        const float scale = stage >= stageIndex(DegradationStage::ReducedResolution)
            ? 0.60F : stage >= stageIndex(DegradationStage::ReducedGrid)
                ? 0.70F : 0.75F;
        result.internalScale = std::min(result.internalScale, scale);
        result.sampleCount = 1;
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

bool RendererResourceState::claimBeatRevision(quint64 revision) noexcept
{
    if (revision == 0) return false;
    quint64 consumed = consumedBeatRevision_.load(std::memory_order_acquire);
    while (revision > consumed) {
        if (consumedBeatRevision_.compare_exchange_weak(
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
    if (!initialized_) {
        initialized_ = true;
        nextFrameSeconds_ = now + interval;
        return true;
    }
    if (now + 0.000001 < nextFrameSeconds_) return false;

    // Advance from the ideal schedule instead of from the current v-sync.
    // Otherwise a 45 FPS target on a 60 Hz display renders every other
    // refresh and silently collapses to 30 FPS.
    const double missedIntervals = std::floor(
        std::max(0.0, now - nextFrameSeconds_) / interval);
    nextFrameSeconds_ += (missedIntervals + 1.0) * interval;
    return true;
}

void CameraMotion::orbitBy(float yawDelta, float pitchDelta,
                           double nowSeconds) noexcept
{
    snapshot_ = sanitizedCameraSnapshot(snapshot_);
    if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta)
        || !std::isfinite(nowSeconds)) return;
    snapshot_.yaw += yawDelta;
    snapshot_.pitch = std::clamp(snapshot_.pitch + pitchDelta, 0.1F, 1.5707953F);
    markManual(nowSeconds);
}
void CameraMotion::zoomBy(float wheelDelta, double nowSeconds) noexcept
{
    snapshot_ = sanitizedCameraSnapshot(snapshot_);
    if (!std::isfinite(wheelDelta) || !std::isfinite(nowSeconds)) return;
    snapshot_.distance = std::clamp(snapshot_.distance
        * std::pow(.95F, std::clamp(wheelDelta * .01F, -100.0F, 100.0F)), 5.0F, 120.0F);
    markManual(nowSeconds);
}
void CameraMotion::applyBeatPunch(float strength) noexcept
{
    snapshot_.punch = clampUnit(snapshot_.punch + clampUnit(strength));
}

void CameraMotion::clearBeatPunch() noexcept
{
    snapshot_.punch = 0.0F;
}
void CameraMotion::advance(double nowSeconds, float elapsedSeconds,
                           float autoRotateSpeed) noexcept
{
    const float elapsed = std::isfinite(elapsedSeconds)
        ? std::clamp(elapsedSeconds, 0.0F, 1.0F) : 0.0F;
    const float speed = std::isfinite(autoRotateSpeed)
        ? std::clamp(autoRotateSpeed, 0.0F, 2.0F) : 0.0F;
    if (speed <= 0.0F || !std::isfinite(nowSeconds)
        || nowSeconds < manualUntilSeconds_) {
        automaticInitialized_ = false;
    } else {
        // Bounded camera compositions, not an accumulating 360-degree orbit.
        // A paused/manual camera becomes the new anchor without a position jump.
        if (!automaticInitialized_) {
            automaticAnchor_ = snapshot_;
            automaticFrom_ = snapshot_;
            automaticPhase_ = 0.0F;
            automaticView_ = 0;
            automaticInitialized_ = true;
        }
        constexpr std::array<float, 4> yawOffsets{0.46F, -0.34F, 0.18F, 0.0F};
        constexpr std::array<float, 4> pitchOffsets{0.04F, 0.07F, -0.02F, 0.0F};
        const auto targetPitch = [&](int index) {
            return std::clamp(automaticAnchor_.pitch + pitchOffsets[index], 0.12F, 1.15F);
        };
        constexpr float moveSeconds = 8.0F;
        constexpr float viewSeconds = 11.0F;
        const float activeElapsed = manualUntilSeconds_ > 0.0
            ? std::min(elapsed, float(std::max(0.0, nowSeconds - manualUntilSeconds_)))
            : elapsed;
        automaticPhase_ += activeElapsed * speed;
        if (automaticPhase_ >= viewSeconds) {
            automaticFrom_.yaw = automaticAnchor_.yaw + yawOffsets[automaticView_];
            automaticFrom_.pitch = targetPitch(automaticView_);
            automaticPhase_ -= viewSeconds;
            automaticView_ = (automaticView_ + 1) % int(yawOffsets.size());
        }
        const float progress = std::min(automaticPhase_ / moveSeconds, 1.0F);
        const float ease = progress * progress * (3.0F - 2.0F * progress);
        snapshot_.yaw = automaticFrom_.yaw
            + (automaticAnchor_.yaw + yawOffsets[automaticView_] - automaticFrom_.yaw) * ease;
        snapshot_.pitch = automaticFrom_.pitch
            + (targetPitch(automaticView_) - automaticFrom_.pitch) * ease;
        // Distance belongs to the user's wheel/zoom, not the automatic tour.
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
    automaticInitialized_ = false;
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
    snapshot_.pitch = std::clamp(snapshot_.pitch + pitchDelta, 0.1F, 1.5707953F);
    snapshot_.distance = std::clamp(next.distance, 5.0F, 120.0F);
    if (manuallyMoved) markManual(nowSeconds);
}
void CameraMotion::markManual(double nowSeconds) noexcept
{
    if (!std::isfinite(nowSeconds)) return;
    manualUntilSeconds_ = std::max(0.0, nowSeconds) + 4.0;
    automaticInitialized_ = false;
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

bool PunchEventConsumer::discard(const PunchEvent& event,
                                 CameraMotion& camera) noexcept
{
    const bool discarded = lifecycle_.claimPunchRevision(event.revision);
    camera.clearBeatPunch();
    return discarded;
}

BeatEventConsumer::BeatEventConsumer(
    RendererResourceState& lifecycle) noexcept
    : lifecycle_(lifecycle)
{
}

bool BeatEventConsumer::consume(const BeatEvent& event,
                                float nowSeconds) noexcept
{
    if (!lifecycle_.claimBeatRevision(event.revision)) return false;
    startSeconds_ = std::max(0.0F, nowSeconds);
    baseStrength_ = clampUnit(event.strength);
    active_ = baseStrength_ > 0.0F;
    return true;
}

bool BeatEventConsumer::discard(const BeatEvent& event) noexcept
{
    const bool discarded = lifecycle_.claimBeatRevision(event.revision);
    baseStrength_ = 0.0F;
    active_ = false;
    return discarded;
}

BeatPulseSnapshot BeatEventConsumer::snapshot(float nowSeconds) const noexcept
{
    BeatPulseSnapshot result;
    if (!active_) return result;
    const float elapsed = std::max(0.0F, nowSeconds - startSeconds_);
    const float age = std::clamp(elapsed / durationSeconds_, 0.0F, 1.0F);
    if (age >= 1.0F) return result;
    result.active = true;
    result.age = age;
    const float envelope = 1.0F - age;
    result.strength = clampUnit(baseStrength_ * envelope * envelope);
    return result;
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

bool ImpactEventConsumer::discard(const ImpactEvent& event) noexcept
{
    const bool discarded = lifecycle_.claimImpactRevision(event.revision);
    baseStrength_ = 0.0F;
    active_ = false;
    return discarded;
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
    // Reach zero before expiration instead of dropping the remaining 45%
    // in one frame. Preserve the event's initial strength and lifetime.
    result.strength = clampUnit(baseStrength_ * (1.0F - age));
    return result;
}

} // namespace agplayer::terrain
