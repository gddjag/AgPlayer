#include "terrain_column_mesh.hpp"
#include "terrain_reactor_state.hpp"
#include "terrain_shadow_map.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QMatrix4x4>
#include <QVector2D>
#include <QQuickRhiItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>
#include <QTemporaryFile>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

using namespace agplayer::terrain::gpu;

namespace {
bool waitForStudyWindow(QQuickWindow& window)
{
    if (QTest::qWaitForWindowExposed(&window, 3000)) return true;
    // Windows can decline the first exposure while the previous native QRhi
    // fixture is being destroyed. Re-show the same surface; never recreate it
    // or change its DPI, size, render backend, or captured assertions.
    window.hide();
    QTest::qWait(50);
    window.show();
    return QTest::qWaitForWindowExposed(&window, 5000);
}

struct NativeReplay {
    UniformBlock uniform{};
    QMatrix4x4 projection, view;
    std::vector<GpuInstance> instances;
    QJsonObject manifest;
};
bool saveNativeReadbackPng(QImage image, const QString& path, bool premultiplied = true)
{
    // The transparent render target contains associated RGB after blending.
    // PNG stores straight RGB: label the bytes correctly so Qt converts once.
    if (premultiplied)
        image.reinterpretAsFormat(QImage::Format_RGBA8888_Premultiplied);
    return image.save(path);
}
std::shared_ptr<const NativeReplay> loadNativeReplay(const QString& path)
{
    const auto require = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open reference JSON");
    require(file.size() < 32 * 1024 * 1024, "Reference exceeds bounded input size");
    const QByteArray bytes = file.readAll();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid reference JSON");
    const auto root = document.object();
    const QString input = root.value("input").toString();
    const bool silenceInput = input == "reference-engine-silence";
    const bool descriptorFixture = input == "reference-uniform-fixture";
    require(root.value("schema").toInt() == 1 && (silenceInput || descriptorFixture),
            "Only schema 1 silence replay or synthetic descriptor fixture supported");
    const auto numbers = [&](QJsonValue value, int count) {
        require(value.isArray(), "Expected numeric array");
        auto array = value.toArray();
        require(array.size() == count, "Numeric array length mismatch");
        for (const auto number : array)
            require(number.isDouble() && std::isfinite(number.toDouble())
                && std::abs(number.toDouble()) < 1e8, "Nonfinite or out-of-range numeric input");
        return array;
    };
    auto result = std::make_shared<NativeReplay>();
    const auto matrices = root.value("matrices").toObject();
    for (const auto name : {"model", "world", "modelView", "view", "cameraWorld", "projection"}) {
        const auto a = numbers(matrices.value(name), 16);
        if (QString(name) == "model" || QString(name) == "world") {
            for (int i = 0; i < 16; ++i)
                require(std::abs(a[i].toDouble() - (i % 5 == 0 ? 1.0 : 0.0)) < 1e-7,
                        "Nonidentity object transform unsupported (must not ignore rotation/scale)");
        }
        if (QString(name) == "view" || QString(name) == "projection") {
            auto& matrix = QString(name) == "view" ? result->view : result->projection;
            for (int i = 0; i < 16; ++i) matrix.data()[i] = float(a[i].toDouble());
        }
    }
    numbers(matrices.value("normal"), 9);
    const auto eye = numbers(root.value("camera").toObject().value("position"), 3);
    auto& u = result->uniform;
    for (int i = 0; i < 3; ++i) u.cameraPosition[i] = float(eye[i].toDouble());
    const auto uniforms = root.value("uniforms").toObject();
    const QStringList descriptorFields = {"uSubBass", "uBass", "uLowMid", "uMid", "uHighMid", "uPresence",
        "uBrilliance", "uAir", "uWarmth", "uBrightness", "uSharpness", "uSmoothness", "uDensity",
        "uSpectralCentroid", "uEnergy"};
    for (const auto& key : descriptorFields) {
        const auto value = uniforms.value(key);
        require(value.isDouble() && std::isfinite(value.toDouble()) && std::abs(value.toDouble()) < 1e8,
                "Absent, nonfinite, or out-of-range audio descriptor unsupported");
        require(!silenceInput || value.toDouble() == 0, "Nonzero audio descriptor unsupported in silence replay");
    }
    const QStringList colorFields = {"uBaseColor1", "uBaseColor2", "uFogColor", "uCoolCore", "uCoolEdge",
        "uWarmCore", "uWarmEdge", "uRippleColor"};
    for (auto it = uniforms.begin(); it != uniforms.end(); ++it) {
        require(descriptorFields.contains(it.key()) || colorFields.contains(it.key())
            || QStringList{"uTime", "uAmplitude", "uGlowIntensity", "uRipples"}.contains(it.key()),
            "Unknown uniform: no silent generic conversion");
    }
    for (const auto& key : colorFields) numbers(uniforms.value(key).toObject().value("rgb"), 3);
    for (const auto key : {"uTime", "uAmplitude", "uGlowIntensity"})
        require(uniforms.value(key).isDouble() && std::isfinite(uniforms.value(key).toDouble())
            && std::abs(uniforms.value(key).toDouble()) < 1e8, "Missing or out-of-range numeric uniform");
    require(uniforms.value("uRipples").isArray(), "Missing ripple state");
    const auto ripples = uniforms.value("uRipples").toArray();
    require(ripples.size() == 10, "Expected exactly ten reference ripple slots");
    const auto rippleNumber = [&](const QJsonObject& r, const char* key) {
        const auto v = r.value(key);
        require(v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) < 1e8,
                "Missing or invalid ripple number");
        return v.toDouble();
    };
    for (int slot=0; slot<ripples.size(); ++slot) {
        require(ripples[slot].isObject(), "Invalid ripple object");
        const auto r = ripples[slot].toObject();
        const double active = rippleNumber(r, "isActive");
        const double strength = rippleNumber(r, "strength");
        const double time = rippleNumber(r, "time");
        const auto pos = numbers(r.value("pos"), 2);
        require(active == 0 || active == 1, "Invalid ripple active flag");
        require(strength >= 0, "Negative reference ripple strength");
        require(!silenceInput || (active == 0 && strength == 0), "Active ripple in silence input");
        if (active == 0) continue; // Old idle slots may omit rippleType.
        const double type = rippleNumber(r, "rippleType");
        require(type == 0 || type == 1, "Unknown reference ripple type");
        const double age = uniforms.value("uTime").toDouble() - time;
        require(std::isfinite(age) && age >= 0 && age < 1e8, "Future or out-of-range ripple age unsupported");
        u.waveSources[slot][0] = float(pos[0].toDouble());
        u.waveSources[slot][1] = float(pos[1].toDouble());
        u.waveSources[slot][2] = float(age);
        u.waveSources[slot][3] = float(type == 1 ? -strength : strength);
    }
    u.parameters[3] = float(uniforms.value("uTime").toDouble());
    const QStringList anchors = {"uBaseColor1", "uCoolCore", "uWarmCore", "uCoolEdge", "uWarmEdge"};
    const auto nativeSrgb = [](double value) {
        return float(value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
    };
    for (int c = 0; c < 5; ++c) {
        const auto rgb = uniforms.value(anchors[c]).toObject().value("rgb").toArray();
        for (int i = 0; i < 3; ++i) u.colors[c][i] = nativeSrgb(rgb[i].toDouble());
        u.colors[c][3] = 1;
    }
    const auto extraColor = [&](const char* name, float* target) {
        const auto rgb=uniforms.value(name).toObject().value("rgb").toArray();
        for(int i=0;i<3;++i)target[i]=nativeSrgb(rgb[i].toDouble());
        target[3]=1;
    };
    extraColor("uBaseColor2",u.bodyColor); extraColor("uFogColor",u.atmosphereColor);
    extraColor("uRippleColor",u.rippleColor);
    // Explicit adapter defaults, not equivalence to foreign material uniforms.
    for (int i = 0; i < 4; ++i) u.equalizerLow[i] = u.equalizerHigh[i] = 1;
    u.waveParameters[0] = u.waveParameters[1] = u.waveParameters[2] = 1;
    if (descriptorFixture) {
        u.bandsLow[0] = float(uniforms.value("uSubBass").toDouble());
        u.bandsLow[1] = float(uniforms.value("uBass").toDouble());
        u.bandsLow[2] = float(uniforms.value("uLowMid").toDouble());
        u.bandsLow[3] = float(uniforms.value("uMid").toDouble());
        u.bandsHigh[0] = float(uniforms.value("uHighMid").toDouble());
        u.bandsHigh[1] = float(uniforms.value("uPresence").toDouble());
        u.bandsHigh[2] = float(uniforms.value("uBrilliance").toDouble());
        u.bandsHigh[3] = float(uniforms.value("uAir").toDouble());
        u.timbre[0] = float(uniforms.value("uWarmth").toDouble());
        u.timbre[1] = float(uniforms.value("uBrightness").toDouble());
        u.timbre[2] = float(uniforms.value("uSharpness").toDouble());
        u.waveParameters[3] = float(uniforms.value("uSmoothness").toDouble());
        u.sceneLighting[3] = float(uniforms.value("uDensity").toDouble());
        u.parameters[0] = float(uniforms.value("uEnergy").toDouble());
        // uSpectralCentroid is declared by the reference but unused there;
        // do not invent a mapping to native spectralFlux or another lane.
    }
    u.styleParameters[0] = float(uniforms.value("uAmplitude").toDouble()) * 0.5F;
    u.styleParameters[1] = -0.125F; u.styleParameters[2] = float(uniforms.value("uGlowIntensity").toDouble());
    u.styleToggles[3] = 1; // Reference idle field is part of the zero-audio frame.
    u.styleToggles[0] = 1; // Active reference ripple slots are authoritative input.
    // MapShaderMaterial has no uStream uniform or conditional around its
    // presence/brilliance/air passes. This explicit adapter value enables the
    // native gate without claiming a nonexistent source descriptor.
    u.styleExtra[2] = 1;
    u.styleDynamics[1] = 0.5F; u.styleDynamics[2] = 1; u.styleDynamics[3] = 0.5F;
    u.styleAudio[0] = u.styleAudio[1] = u.styleAudio[3] = 1; u.styleAudio[2] = 56;
    u.stylePresentation[0] = u.stylePresentation[2] = 1;
    u.materialParameters[0] = 1; u.materialParameters[1] = 0.45F;
    u.materialParameters[2] = 1; u.materialParameters[3] = 0.6F;
    u.sceneControls[0] = u.sceneControls[1] = u.sceneControls[3] = 1; u.sceneControls[2] = 112;
    u.sceneLighting[0] = u.sceneLighting[2] = 1; u.sceneLighting[1] = 0.6F;
    const auto geometry = root.value("geometry").toObject();
    const auto grid = root.value("config").toObject().value("grid").toObject();
    require(grid.value("terrainSize").isDouble() && grid.value("terrainSize").toDouble() > 0
        && grid.value("terrainSize").toDouble() < 10000, "Invalid reference terrain size");
    u.sceneControls[2] = float(grid.value("terrainSize").toDouble() * 0.5);
    require(geometry.value("type").toString() == "BoxGeometry", "Only box geometry supported");
    const auto dimensions = geometry.value("parameters").toObject();
    for (const auto key : {"width", "height", "depth"})
        require(dimensions.value(key).isDouble() && dimensions.value(key).toDouble() > 0
            && dimensions.value(key).toDouble() < 1000, "Invalid box dimensions");
    const auto instanceObject = root.value("instances").toObject();
    const int count = instanceObject.value("count").toInt();
    require(count == 24025 && count <= 65536, "Baseline must contain exactly 24025 instances");
    const auto data = numbers(instanceObject.value("matrices"), count * 16);
    for (int n = 0; n < count; ++n)
        for (int i = 0; i < 16; ++i)
            if (i < 12 || i == 15)
                require(std::abs(data[n * 16 + i].toDouble() - (i % 5 == 0 ? 1.0 : 0.0)) < 1e-6,
                        "Instance rotation/scale unsupported");
    result->instances.reserve(count);
    for (int n = 0; n < count; ++n) result->instances.push_back({
        {float(data[n*16+12].toDouble()), float(data[n*16+13].toDouble() - dimensions.value("height").toDouble() * 0.5), float(data[n*16+14].toDouble())},
        {float(dimensions.value("width").toDouble()), float(dimensions.value("height").toDouble()),
         float(dimensions.value("depth").toDouble())}, {0, 0, 0.5F, 0}});
    const auto instanceFields = [](const GpuInstance& instance) {
        QJsonArray values;
        for (const auto value : instance.position) values.append(double(value));
        for (const auto value : instance.scale) values.append(double(value));
        for (const auto value : instance.data) values.append(double(value));
        return values;
    };
    const auto first = instanceFields(result->instances.front());
    QJsonArray minima = first, maxima = first;
    for (const auto& instance : result->instances) {
        const auto values = instanceFields(instance);
        for (int i = 0; i < values.size(); ++i) {
            minima[i] = std::min(minima[i].toDouble(), values[i].toDouble());
            maxima[i] = std::max(maxima[i].toDouble(), values[i].toDouble());
        }
    }
    const auto matrixSequence = QJsonDocument(data).toJson(QJsonDocument::Compact);
    result->manifest = {{"input", input}, {"referenceSha256", QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {"referenceAbsolutePath", QFileInfo(path).absoluteFilePath()},
        {"instanceAnchorConversion", "native baseY = reference matrix centerY - geometry.height/2; original centerY sequence at matrices[16*n+13], SHA256 below; fixed baseline centerY=.5,height=1 -> baseY=0"},
        {"referenceCameraPosition", eye}, {"referenceGeometryParameters", dimensions},
        {"stageHalfExtentSource", "config.grid.terrainSize / 2"}, {"referenceGrid", grid},
        {"referenceInstanceMatricesSha256", QString(QCryptographicHash::hash(matrixSequence, QCryptographicHash::Sha256).toHex())},
        {"referenceInstanceMatricesSerialization", "Qt 6.7 QJsonDocument(QJsonArray).toJson(Compact), UTF-8, parsed double values in original array order, no newline"},
        {"referenceStreamContract", "MapShaderMaterial declares no uStream uniform; its high-band top effects are unconditional, so native styleExtra.z is explicitly 1"},
        {"nativeInstanceSummary", QJsonObject{{"fieldOrder", "position.xyz,scale.xyz,data.type,zone,random,aux"},
            {"first", first}, {"last", instanceFields(result->instances.back())}, {"min", minima}, {"max", maxima}}},
        {"instanceCount", count}, {"referenceUniforms", uniforms}, {"referenceMatrices", matrices},
        {"nativeColorAnchors", QJsonArray::fromStringList(anchors)},
        {"colorContract", "Reference linear RGB converted once to native sRGB. body/fog/ripple tint consumed. Active rippleType 0/1 maps to positive/negative strength; age is uTime minus event time, unit wave controls. uGlowIntensity copied to styleParameters.z; shader gain remains native"},
        {"unusedReferenceUniforms", QJsonArray{"uSpectralCentroid"}},
        {"knownGaps", QJsonArray{"Not equal-U or shader parity; native defaults explicitly retained", "Native height deformation and analytic surface coordinates differ from reference BoxGeometry UVs",
            "uAmplitude copied through the explicit half-scale adapter; glow material semantics still differ", "Native custom-glint random=0.5 and zone=0; frequency height computes the position hash in shader",
            "Height field checked against72 reference GPU samples; this full-grid replay still needs geometry/alpha acceptance. Idle and explicit scene time enabled; rest/base/width preserved",
            "Native material/style/lighting defaults; no analyzer, style controller or smoothing", "Native shadow pass disabled; terrain direct-clamped UNORM follows reference GPU probe; material response and alpha blending still unverified"}}};
    return result;
}
struct StudyParameters {
    float time = 0;
    float motionControl = -0.125F;
    bool idleRelief = false;
    int material = 1;
    bool rainbow = false;
    bool stream = false;
    float beat = 0;
    float beatAge = 0;
    float audioLevel = 1;
    float heightControl = 0.60F;
    float restHeight = 1.0F;
    float lowAudioLevel = 1;
    float midAudioLevel = 0;
    int waveSlot = -1;
    int waveTint = 0;
    bool waveEnabled = true;
    bool waveSourceActive = true;
    QColor rippleTint;
    float rippleAge = 0;
    float rippleStrength = 0;
    bool rippleEnabled = true;
    QVector2D rippleCenter;
    QVector3D rippleParameters{1, 1, 1};
    float exposure = 1;
    float opacity = 1;
    float softness = 0.45F;
    float glow = 0.5F;
    float randomValue = 0.9F;
    QVector3D position;
    QVector4D highBands;
    bool explicitHighBands = false;
    bool array = false;
    bool shadows = false;
    float runtimeMode = 0.0F;
    float stageHalfExtent = 112.0F;
    QVector3D lighting{1, 0.6F, 1};
    QColor tint = QColor::fromRgbF(0.08F, 0.55F, 0.72F);
    QColor bodyTint; // Optional explicit base2; invalid retains the legacy fallback.
    QVector3D camera{15, 16, 24};
    QVector3D cameraTarget{0, 6, 0};
};

void frameColumnOptics(StudyParameters& parameters)
{
    // Inspect the shorter reference-driven column at material-study scale.
    // Keep the original viewing direction; change only the test camera, never
    // the production mesh, audio gain, or height. Geometry cases retain defaults.
    parameters.cameraTarget = {0, 3, 0};
    parameters.camera = {9.75F, 9.5F, 15.6F};
}
struct StudyCounters {
    QMutex replayMutex;
    QList<QByteArray> replayFrames;
    QList<QString> replayUniformHashes;
    int replayFrameLimit = 2;
    QJsonObject replayManifest;
    // Test-only rejection at the native material creation boundary.
    int rejectDepthStage = 0;
    std::atomic<int> materialAttempts{0};
    std::atomic<int> live{0}, generations{0}, frames{0};
    std::atomic<bool> failed{false};
    std::atomic<bool> shadowAvailable{false};
    std::atomic<float> depthMinimum{1}, depthMaximum{0};
    std::atomic<int> shadowSamples{0};
};
class ColumnItem;
class ColumnRenderer final : public QQuickRhiItemRenderer {
public:
    explicit ColumnRenderer(std::shared_ptr<StudyCounters> counters, std::shared_ptr<const NativeReplay> replay = {}) : counters_(std::move(counters)), replay_(std::move(replay))
    { ++counters_->live; }
    ~ColumnRenderer() override { release(); --counters_->live; }
    void initialize(QRhiCommandBuffer*) override;
    void synchronize(QQuickRhiItem*) override;
    void render(QRhiCommandBuffer*) override;
private:
    void release() {
        pipeline_.reset(); bindings_.reset(); shadow_.reset(); uniform_.reset();
        instances_.reset(); indices_.reset(); vertices_.reset();
        lastTarget_ = nullptr;
    }
    StudyParameters parameters_;
    std::shared_ptr<StudyCounters> counters_;
    std::shared_ptr<const NativeReplay> replay_;
    std::vector<std::unique_ptr<QRhiReadbackResult>> replayReadbacks_;
    std::unique_ptr<QRhiBuffer> vertices_, indices_, instances_, uniform_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    TerrainShadowMap shadow_;
    QRhiRenderTarget* lastTarget_ = nullptr;
    bool upload_ = true;
    QRhiReadbackResult shadowReadback_;
};
class ColumnItem final : public QQuickRhiItem {
public:
    explicit ColumnItem(QQuickItem* parent, std::shared_ptr<StudyCounters> counters)
        : QQuickRhiItem(parent), counters(std::move(counters)) {
        setSize(QSizeF(640, 640));
        setFixedColorBufferWidth(640); setFixedColorBufferHeight(640);
        setAlphaBlending(true);
    }
    StudyParameters parameters;
    std::shared_ptr<const NativeReplay> replay;
    std::shared_ptr<StudyCounters> counters;
protected:
    QQuickRhiItemRenderer* createRenderer() override { return new ColumnRenderer(counters, replay); }
};

void ColumnRenderer::initialize(QRhiCommandBuffer*)
{
    if (pipeline_ && lastTarget_ == renderTarget()) return;
    release();
    const auto shader = [this](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QShader{};
        const auto bytes = file.readAll();
        if (replay_) {
            QMutexLocker lock(&counters_->replayMutex);
            counters_->replayManifest[path] = QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        }
        return QShader::fromSerialized(bytes);
    };
    const QShader vertexShader = shader(QStringLiteral(":/terrain/shaders/terrain_reactor.vert.qsb"));
    const bool hashDiagnostic = qEnvironmentVariable("AGPLAYER_PARITY_HASH_DIAGNOSTIC") == "1";
    const bool diagnosticBlendOff = hashDiagnostic
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_BLEND_OFF") == "1";
    const bool diagnosticDepthOff = replay_ && hashDiagnostic
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_DEPTH_OFF") == "1";
    const bool diagnosticDepthWriteOff = replay_ && hashDiagnostic
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_DEPTH_WRITE_OFF") == "1";
    const QString diagnosticPath = qEnvironmentVariable("AGPLAYER_PARITY_HASH_DIAGNOSTIC_QSB");
    const QString fragmentPath = hashDiagnostic ? diagnosticPath
        : QStringLiteral(":/terrain/shaders/terrain_reactor.frag.qsb");
    const QShader fragmentShader = shader(fragmentPath);
    const QShader shadowShader = shader(QStringLiteral(":/terrain/shaders/terrain_shadow.frag.qsb"));
    const QShader shadowVertexShader = shader(QStringLiteral(":/terrain-shadow/shaders/terrain_shadow.vert.qsb"));
    if (!vertexShader.isValid() || !fragmentShader.isValid()
        || !shadowShader.isValid() || !shadowVertexShader.isValid()) {
        counters_->failed = true; return;
    }
    if (hashDiagnostic) {
        QMutexLocker lock(&counters_->replayMutex);
        counters_->replayManifest["hashDiagnostic"] = QJsonObject{
            {"nonAppearanceOnly", true},
            {"blendEnabled", !diagnosticBlendOff},
            {"depthTestEnabled", !diagnosticDepthOff},
            {"depthWriteEnabled", !diagnosticDepthWriteOff},
            {"fragmentShaderPath", fragmentPath},
            {"fragmentShaderSha256", counters_->replayManifest.value(fragmentPath)},
            {"contract", "R=flat columnRandom; G=clamp(flat reliefHeight/16); B=0; A=opacity"}};
    }
    vertices_.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                     sizeof(columnVertices)));
    indices_.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                    sizeof(columnIndices)));
    const bool immutableInstancesDiagnostic = replay_
        && qEnvironmentVariable("AGPLAYER_PARITY_IMMUTABLE_INSTANCES") == "1";
    instances_.reset(rhi()->newBuffer(immutableInstancesDiagnostic ? QRhiBuffer::Immutable : QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                      (replay_ ? int(replay_->instances.size()) : 25) * sizeof(GpuInstance)));
    uniform_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UniformBlock)));
    if (!vertices_->create() || !indices_->create() || !instances_->create() || !uniform_->create()) {
        counters_->failed = true; return;
    }
    const auto layout = terrainVertexLayout();
    if (!shadow_.create(rhi(), shadowVertexShader, shadowShader, layout,
                        qEnvironmentVariableIsSet("AGPLAYER_SHADOW_READBACK"))) {
        counters_->failed = true; return;
    }
    const auto createMaterial = [&]() {
        ++counters_->materialAttempts;
        bindings_.reset(rhi()->newShaderResourceBindings());
        bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            uniform_.get()), QRhiShaderResourceBinding::sampledTexture(1,
            QRhiShaderResourceBinding::FragmentStage, shadow_.texture(), shadow_.sampler())});
        if ((shadow_.available() && counters_->rejectDepthStage == 1)
            || !bindings_->create()) return false;
        pipeline_.reset(rhi()->newGraphicsPipeline());
        pipeline_->setShaderStages({
            {QRhiShaderStage::Vertex, vertexShader},
            {QRhiShaderStage::Fragment, fragmentShader}});
        pipeline_->setVertexInputLayout(layout);
        pipeline_->setShaderResourceBindings(bindings_.get());
        pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        pipeline_->setSampleCount(renderTarget()->sampleCount());
        pipeline_->setCullMode(QRhiGraphicsPipeline::Back);
        if (replay_ && qEnvironmentVariable("AGPLAYER_PARITY_RASTER_MIRROR") == "1")
            pipeline_->setFrontFace(QRhiGraphicsPipeline::CW);
        pipeline_->setDepthTest(!diagnosticDepthOff);
        pipeline_->setDepthWrite(!diagnosticDepthWriteOff);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = !diagnosticBlendOff; blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline_->setTargetBlends({blend});
        return !(shadow_.available() && counters_->rejectDepthStage == 2)
            && pipeline_->create();
    };
    if (!shadow_.createMaterialOrFallback(rhi(), createMaterial, [&]() {
            pipeline_.reset(); bindings_.reset();
        })) { counters_->failed = true; return; }
    counters_->shadowAvailable = shadow_.available();
    lastTarget_ = renderTarget();
    upload_ = true;
    ++counters_->generations;
}
void ColumnRenderer::synchronize(QQuickRhiItem* item)
{ parameters_ = static_cast<ColumnItem*>(item)->parameters; }

void ColumnRenderer::render(QRhiCommandBuffer* cb)
{
    if (counters_->failed || !pipeline_) return;
    UniformBlock u;
    QMatrix4x4 projection;
    projection.perspective(32, 1, 0.1F, 200);
    QMatrix4x4 view;
    view.lookAt(parameters_.camera, parameters_.cameraTarget, QVector3D(0, 1, 0));
    const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * projection * view;
    std::memcpy(u.mvp, mvp.constData(), sizeof(u.mvp));
    for (int i = 0; i < 4; ++i) {
        u.equalizerLow[i] = u.equalizerHigh[i] = 1;
        u.bandsHigh[i] = parameters_.explicitHighBands
            ? parameters_.highBands[i] : 0.9F * parameters_.audioLevel;
    }
    u.bandsLow[0] = 0.40F * parameters_.audioLevel * parameters_.lowAudioLevel;
    u.bandsLow[1] = 0.30F * parameters_.audioLevel * parameters_.lowAudioLevel;
    u.bandsLow[2] = u.bandsLow[3] = 0.70F * parameters_.midAudioLevel;
    u.parameters[3] = parameters_.time;
    for (int i = 0; i < 5; ++i) {
        u.colors[i][0] = float(parameters_.tint.redF());
        u.colors[i][1] = float(parameters_.tint.greenF());
        u.colors[i][2] = float(parameters_.tint.blueF()); u.colors[i][3] = 1;
    }
    if (parameters_.bodyTint.isValid()) {
        u.bodyColor[0] = float(parameters_.bodyTint.redF());
        u.bodyColor[1] = float(parameters_.bodyTint.greenF());
        u.bodyColor[2] = float(parameters_.bodyTint.blueF());
        u.bodyColor[3] = 1;
    }
    u.styleParameters[0] = parameters_.heightControl;
    // Material-only cases freeze affine motion by default; the fixed-audio
    // geometry regression explicitly supplies a supported nonzero motion.
    u.styleParameters[1] = parameters_.motionControl;
    u.styleParameters[2] = parameters_.glow;
    u.styleDynamics[1] = 0.5F;
    u.styleDynamics[2] = parameters_.rainbow ? 3.0F : 1.0F;
    u.styleDynamics[3] = 0.5F;
    u.styleExtra[2] = parameters_.stream ? 1.0F : 0.0F;
    u.styleToggles[3] = parameters_.idleRelief ? 1.0F : 0.0F;
    u.styleAudio[0] = u.styleAudio[1] = u.styleAudio[3] = 1;
    u.styleAudio[2] = 56;
    u.stylePresentation[0] = u.stylePresentation[2] = 1;
    u.audioEnvelope[2] = parameters_.beat;
    u.audioEnvelope[3] = parameters_.beatAge;
    u.cameraPosition[0] = parameters_.camera.x();
    u.cameraPosition[1] = parameters_.camera.y();
    u.cameraPosition[2] = parameters_.camera.z();
    u.materialParameters[0] = float(parameters_.material);
    u.materialParameters[1] = parameters_.softness;
    u.materialParameters[2] = 1.0F;
    u.materialParameters[3] = 0.6F;
    u.sceneControls[0] = parameters_.opacity; u.sceneControls[1] = parameters_.exposure;
    u.sceneControls[2] = parameters_.stageHalfExtent;
    u.sceneControls[3] = 1.0F;
    u.waveParameters[0] = u.waveParameters[1] = u.waveParameters[2] = 1;
    u.timbre[3] = parameters_.runtimeMode;
    if (parameters_.waveSlot >= 0) {
        const QVector3D anchors[] = {{0.1F,0.8F,1}, {1,0.2F,0.4F}, {1,0.7F,0.1F}};
        for (int i = 0; i < 3; ++i)
            for (int channel = 0; channel < 3; ++channel)
                u.colors[i + 1][channel] = anchors[i][channel];
        u.styleToggles[0] = parameters_.waveEnabled ? 1.0F : 0.0F;
        u.effects[3] = 8;
        u.waveSources[parameters_.waveSlot][3] = parameters_.waveSourceActive ? 0.8F : 0.0F;
        u.waveSources[parameters_.waveSlot][2] = 32.0F * parameters_.waveTint;
    }
    if (parameters_.rippleTint.isValid()) {
        u.rippleColor[0] = float(parameters_.rippleTint.redF());
        u.rippleColor[1] = float(parameters_.rippleTint.greenF());
        u.rippleColor[2] = float(parameters_.rippleTint.blueF());
        u.rippleColor[3] = 1.0F;
        u.styleToggles[0] = parameters_.rippleEnabled ? 1.0F : 0.0F;
        u.waveSources[0][2] = parameters_.rippleAge;
        u.waveSources[0][3] = parameters_.rippleStrength;
        u.waveSources[0][0] = parameters_.rippleCenter.x();
        u.waveSources[0][1] = parameters_.rippleCenter.y();
        u.waveParameters[0] = parameters_.rippleParameters.x();
        u.waveParameters[1] = parameters_.rippleParameters.y();
        u.waveParameters[2] = parameters_.rippleParameters.z();
    }
    u.sceneLighting[0] = parameters_.lighting.x();
    u.sceneLighting[1] = parameters_.lighting.y();
    u.sceneLighting[2] = parameters_.lighting.z();
    shadow_.configure(rhi(), u, parameters_.shadows);
    std::array<GpuInstance, 25> columns{};
    quint32 instanceCount = 1;
    if (parameters_.array) {
        instanceCount = quint32(columns.size());
        for (int z = 0; z < 5; ++z) {
            for (int x = 0; x < 5; ++x) {
                const int index = z * 5 + x;
                const float random = 0.08F + float((index * 37 + 11) % 89) / 100.0F;
                columns[std::size_t(index)] = {
                    {(x - 2) * 5.0F, 0, (z - 2) * 5.0F},
                    {3.7F, 1.0F, 3.7F}, {0, 0, random, 0}};
            }
        }
    } else {
        columns[0] = {{parameters_.position.x(), parameters_.position.y(), parameters_.position.z()},
                      {4, parameters_.restHeight, 4}, {0, 0, parameters_.randomValue, 0}};
    }
    if (replay_) {
        u = replay_->uniform;
        auto corrected = rhi()->clipSpaceCorrMatrix() * replay_->projection * replay_->view;
        if (qEnvironmentVariable("AGPLAYER_PARITY_RASTER_MIRROR") == "1")
            for (int column = 0; column < 4; ++column) corrected(1, column) *= -1;
        std::memcpy(u.mvp, corrected.constData(), sizeof(u.mvp));
        instanceCount = quint32(replay_->instances.size());
    }
    auto* updates = rhi()->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(uniform_.get(), 0, sizeof(u), &u);
    if (instances_->type() == QRhiBuffer::Dynamic) {
        updates->updateDynamicBuffer(instances_.get(), 0,
                                     instanceCount * sizeof(GpuInstance), replay_ ? replay_->instances.data() : columns.data());
    } else if (upload_) {
        updates->uploadStaticBuffer(instances_.get(), replay_->instances.data());
    }
    const bool uploadedStaticGeometry = upload_;
    if (upload_) {
        updates->uploadStaticBuffer(vertices_.get(), columnVertices.data());
        updates->uploadStaticBuffer(indices_.get(), columnIndices.data());
        upload_ = false;
    }
    cb->resourceUpdate(updates);
    shadow_.render(rhi(), cb, u, vertices_.get(), indices_.get(), instances_.get(),
                   quint32(columnIndices.size()), instanceCount);
    if (parameters_.shadows && counters_->shadowSamples == 0
        && qEnvironmentVariableIsSet("AGPLAYER_SHADOW_READBACK")) {
        counters_->shadowSamples = -1;
        const auto counters = counters_;
        shadowReadback_.completed = [this, counters] {
            float minimum = 1, maximum = 0;
            const auto& bytes = shadowReadback_.data;
            for (qsizetype i = 0; i + 4 <= bytes.size(); i += 4) {
                float depth; std::memcpy(&depth, bytes.constData() + i, 4);
                minimum = std::min(minimum, depth); maximum = std::max(maximum, depth);
            }
            counters->depthMinimum = minimum; counters->depthMaximum = maximum;
            counters->shadowSamples = int(bytes.size() / 4);
        };
        auto* readback = rhi()->nextResourceUpdateBatch();
        readback->readBackTexture(QRhiReadbackDescription(shadow_.texture()), &shadowReadback_);
        cb->resourceUpdate(readback);
        rhi()->finish();
    }
    // Isolate repeated readback from repeated rasterization/MSAA resolve.
    // Only the opt-in hash diagnostic may retain its first resolved texture.
    const bool retainResolvedDiagnostic = replay_
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_DIAGNOSTIC") == "1"
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_RETAIN_RESOLVE") == "1";
    const bool repeatResolveDiagnostic = replay_
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_DIAGNOSTIC") == "1"
        && qEnvironmentVariable("AGPLAYER_PARITY_HASH_REPEAT_RESOLVE") == "1";
    if (repeatResolveDiagnostic && !replayReadbacks_.empty()) {
        // QRhi D3D11 endPass resolves the attachment even without draws.
        // Preserve both attachments so this changes only repeated resolve.
        auto* target = static_cast<QRhiTextureRenderTarget*>(renderTarget());
        const auto flags = target->flags();
        target->setFlags(flags | QRhiTextureRenderTarget::PreserveColorContents
            | QRhiTextureRenderTarget::PreserveDepthStencilContents);
        cb->beginPass(target, QColor(0, 0, 0, 0), {1, 0});
        cb->endPass();
        target->setFlags(flags);
    } else if (!retainResolvedDiagnostic || replayReadbacks_.empty()) {
        cb->beginPass(renderTarget(), QColor(0, 0, 0, 0), {1, 0});
        cb->setGraphicsPipeline(pipeline_.get()); cb->setShaderResources(bindings_.get());
        const auto size = renderTarget()->pixelSize();
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        const QRhiCommandBuffer::VertexInput inputs[] = {{vertices_.get(), 0}, {instances_.get(), 0}};
        cb->setVertexInput(0, 2, inputs, indices_.get(), 0, QRhiCommandBuffer::IndexUInt16);
        cb->drawIndexed(quint32(columnIndices.size()), instanceCount);
        cb->endPass();
    }
    if (replay_ && int(replayReadbacks_.size()) < counters_->replayFrameLimit) {
        auto result = std::make_unique<QRhiReadbackResult>();
        auto* frameResult = result.get();
        const int submission = int(replayReadbacks_.size());
        replayReadbacks_.push_back(std::move(result));
        auto* texture = resolveTexture() ? resolveTexture() : colorTexture();
        const auto uniformHash = QString(QCryptographicHash::hash(
            QByteArray(reinterpret_cast<const char*>(&u), sizeof(u)), QCryptographicHash::Sha256).toHex());
        frameResult->completed = [this, uniformHash, frameResult, submission] {
            QMutexLocker lock(&counters_->replayMutex);
            if (counters_->replayFrames.size() < counters_->replayFrameLimit) {
                counters_->replayFrames.append(frameResult->data);
                counters_->replayUniformHashes.append(uniformHash);
            }
            auto order = counters_->replayManifest.value("callbackOrder").toArray();
            order.append(submission); counters_->replayManifest["callbackOrder"] = order;
            counters_->replayManifest["width"] = frameResult->pixelSize.width();
            counters_->replayManifest["height"] = frameResult->pixelSize.height();
            counters_->replayManifest["format"] = int(frameResult->format);
        };
        auto* readback = rhi()->nextResourceUpdateBatch();
        readback->readBackTexture(QRhiReadbackDescription(texture), frameResult);
        cb->resourceUpdate(readback);
        const auto finishResult = rhi()->finish();
        if (finishResult != QRhi::FrameOpSuccess) counters_->failed = true;
        QMutexLocker lock(&counters_->replayMutex);
        auto finishes = counters_->replayManifest.value("finishResults").toArray();
        finishes.append(int(finishResult)); counters_->replayManifest["finishResults"] = finishes;
        counters_->replayManifest["backend"] = int(rhi()->backend());
        counters_->replayManifest["immutableInstancesDiagnostic"] = instances_->type() == QRhiBuffer::Immutable;
        counters_->replayManifest["retainFirstResolvedTextureDiagnostic"] = retainResolvedDiagnostic;
        counters_->replayManifest["repeatResolveWithoutDrawDiagnostic"] = repeatResolveDiagnostic;
        counters_->replayManifest["sampleCount"] = renderTarget()->sampleCount();
        counters_->replayManifest["framebufferYUp"] = rhi()->isYUpInFramebuffer();
        counters_->replayManifest["actualUniformBytesHex"] = QString(QByteArray(reinterpret_cast<const char*>(&u), sizeof(u)).toHex());
        counters_->replayManifest["actualInstanceSha256"] = QString(QCryptographicHash::hash(
            QByteArray(reinterpret_cast<const char*>(replay_->instances.data()), instanceCount * sizeof(GpuInstance)), QCryptographicHash::Sha256).toHex());
        // Capture each submission, not just the final target state. These are
        // CPU upload-source hashes, not a claim to have read GPU buffers back.
        const auto sourceHash = [](const void* data, qsizetype bytes) {
            return QString(QCryptographicHash::hash(
                QByteArray(reinterpret_cast<const char*>(data), bytes),
                QCryptographicHash::Sha256).toHex());
        };
        auto geometryFrames = counters_->replayManifest.value("geometryFrames").toArray();
        geometryFrames.append(QJsonObject{
            {"submission", submission},
            {"resourceGeneration", counters_->generations.load()},
            {"targetId", QString::number(quintptr(renderTarget()), 16)},
            {"readbackTextureId", QString::number(quintptr(texture), 16)},
            {"width", renderTarget()->pixelSize().width()},
            {"height", renderTarget()->pixelSize().height()},
            {"sampleCount", renderTarget()->sampleCount()},
            {"uploadedStaticGeometry", uploadedStaticGeometry},
            {"vertexSourceSha256", sourceHash(columnVertices.data(), sizeof(columnVertices))},
            {"indexSourceSha256", sourceHash(columnIndices.data(), sizeof(columnIndices))},
            {"instanceSourceSha256", counters_->replayManifest.value("actualInstanceSha256")}});
        counters_->replayManifest["geometryFrames"] = geometryFrames;
    }
    ++counters_->frames;
}
struct FrontFace {
    int cap = 0;
    int foot = 0;
    double capContrast = 0;
    double horizontalContrast = 0;
    QVector3D upper, middle, lower;
    double coreLight = 0;
};

struct FrameComparison {
    QRect firstBounds;
    QRect secondBounds;
    int firstVisible = 0;
    int secondVisible = 0;
    int commonVisible = 0;
    int silhouetteMismatch = 0;
    int changedCommon = 0;
    quint64 totalRgbDifference = 0;
    qint64 firstMinusSecondLight = 0;
};

QImage studyFrame(QQuickWindow& window)
{
    // grabWindow uses the screen DPR, whereas this fixture deliberately renders
    // into a fixed 640px target. Mixed-DPI screens must share one image-space
    // coordinate system for the inspected front-face regions and PNG evidence.
    return window.grabWindow().scaled(640, 640, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QVector3D meanPatch(const QImage& frame, int xFirst, int xLast, int yFirst, int yLast)
{
    QVector3D sum;
    for (int y = yFirst; y < yLast; ++y)
        for (int x = xFirst; x < xLast; ++x) {
            const QColor c = frame.pixelColor(x, y);
            sum += QVector3D(float(c.red()), float(c.green()), float(c.blue()));
        }
    return sum / float((xLast - xFirst) * (yLast - yFirst));
}

double patchLight(QVector3D c)
{ return qGray(qRgb(qRound(c.x()), qRound(c.y()), qRound(c.z()))); }

FrameComparison compareFrames(const QImage& first, const QImage& second)
{
    FrameComparison result;
    const QColor firstBackground = first.pixelColor(20, 20);
    const QColor secondBackground = second.pixelColor(20, 20);
    const auto visible = [](QColor pixel, QColor background) {
        return std::abs(pixel.red() - background.red())
             + std::abs(pixel.green() - background.green())
             + std::abs(pixel.blue() - background.blue()) > 10;
    };
    const auto addPoint = [](QRect& bounds, int x, int y) {
        const QRect point(x, y, 1, 1);
        bounds = bounds.isNull() ? point : bounds.united(point);
    };
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            const QColor a = first.pixelColor(x, y);
            const QColor b = second.pixelColor(x, y);
            const bool aVisible = visible(a, firstBackground);
            const bool bVisible = visible(b, secondBackground);
            if (aVisible) { ++result.firstVisible; addPoint(result.firstBounds, x, y); }
            if (bVisible) { ++result.secondVisible; addPoint(result.secondBounds, x, y); }
            if (aVisible != bVisible) ++result.silhouetteMismatch;
            if (!aVisible || !bVisible) continue;
            ++result.commonVisible;
            const int difference = std::abs(a.red() - b.red())
                                 + std::abs(a.green() - b.green())
                                 + std::abs(a.blue() - b.blue());
            result.totalRgbDifference += quint64(difference);
            if (difference > 3) ++result.changedCommon;
            result.firstMinusSecondLight += qGray(a.rgb()) - qGray(b.rgb());
        }
    }
    return result;
}

bool nearlySameBounds(QRect first, QRect second, int tolerance = 1)
{
    return std::abs(first.left() - second.left()) <= tolerance
        && std::abs(first.top() - second.top()) <= tolerance
        && std::abs(first.right() - second.right()) <= tolerance
        && std::abs(first.bottom() - second.bottom()) <= tolerance;
}

FrontFace locateFrontFace(const QImage& frame)
{
    // The single axis-aligned column's inspected front face contains x=275..295
    // at both beat heights. Locate its silhouette from pixels, not shader math.
    const QColor background = frame.pixelColor(20, 20);
    int first = -1, last = -1;
    for (int y = 10; y < frame.height() - 10; ++y) {
        const QVector3D c = meanPatch(frame, 275, 295, y, y + 1);
        if (std::abs(c.x() - background.red()) + std::abs(c.y() - background.green())
                + std::abs(c.z() - background.blue()) > 10) {
            if (first < 0) first = y;
            last = y;
        }
    }
    FrontFace result;
    if (first < 10 || last - first < 100) return result;
    result.foot = last;
    double strongest = -1e9;
    // A low box has the same projected cap width as a tall one. Search the
    // upper half so the physical cap/side junction is not excluded by height.
    for (int y = first + 8; y < first + (last - first) / 2; ++y) {
        const double drop = patchLight(meanPatch(frame, 275, 295, y - 6, y - 2))
                          - patchLight(meanPatch(frame, 275, 295, y + 2, y + 6));
        if (drop > strongest) { strongest = drop; result.cap = y; }
    }
    result.capContrast = strongest;
    const int height = result.foot - result.cap;
    const auto atHeight = [&](double fraction) {
        const int y = result.cap + qRound(height * fraction);
        return meanPatch(frame, 265, 305, y - 5, y + 5);
    };
    result.upper = atHeight(0.20); result.middle = atHeight(0.50); result.lower = atHeight(0.85);
    result.coreLight = patchLight(meanPatch(frame, 265, 305,
        result.cap + qRound(height * 0.25), result.cap + qRound(height * 0.65)));
    double minimum = 255, maximum = 0;
    const int centerY = result.cap + height / 2;
    for (int x = 235; x < 325; ++x) {
        const double light = patchLight(meanPatch(frame, x, x + 1, centerY - 4, centerY + 4));
        minimum = std::min(minimum, light); maximum = std::max(maximum, light);
    }
    result.horizontalContrast = maximum - minimum;
    return result;
}
} // namespace

class TerrainColumnMaterialTest : public QObject {
    Q_OBJECT
private slots:
    void nativeReadbackPngPreservesStraightColorAndAlpha()
    {
        // SrcAlpha/OneMinusSrcAlpha into transparent stores premultiplied RGB.
        // Export must unpremultiply exactly once without changing alpha.
        const uchar rgba[]{64,32,16,85, 255,128,32,255, 0,0,0,0};
        const QImage raw(rgba,3,1,QImage::Format_RGBA8888);
        QTemporaryFile output(QDir::tempPath()+"/native-alpha-XXXXXX.png");
        QVERIFY(output.open()); output.close();
        QVERIFY(saveNativeReadbackPng(raw,output.fileName()));
        const QImage saved(output.fileName());
        QCOMPARE(saved.pixelColor(0,0),QColor(192,96,48,85));
        QCOMPARE(saved.pixelColor(1,0),QColor(255,128,32,255));
        QCOMPARE(saved.pixelColor(2,0),QColor(0,0,0,0));
        QCOMPARE(raw.pixelColor(0,0),QColor(64,32,16,85));
    }
    void terrainExposureUsesDirectLinearOutput()
    {
        double measured[2]{};
        for(int level=0;level<2;++level){
            auto replay=std::make_shared<NativeReplay>();auto& u=replay->uniform;
            u.sceneControls[0]=u.sceneControls[3]=1;u.sceneControls[1]=level?1:.5F;u.sceneControls[2]=84;
            u.styleAudio[2]=56;u.materialParameters[0]=1;
            for(auto& c:u.colors){c[0]=c[1]=c[2]=.7F;c[3]=1;}
            replay->instances.push_back({{0,0,0},{6,6,6},{0,0,.5F,0}});
            replay->projection.ortho(-5,5,-5,5,.1F,100);
            replay->view.lookAt({0,20,0},{0,0,0},{0,0,-1});u.cameraPosition[1]=20;
            auto counters=std::make_shared<StudyCounters>();QQuickWindow window;window.resize(640,640);
            auto* item=new ColumnItem(window.contentItem(),counters);item->replay=replay;
            window.show();QVERIFY(waitForStudyWindow(window));
            const auto ready=[&]{QMutexLocker lock(&counters->replayMutex);return !counters->replayFrames.isEmpty();};
            QTRY_VERIFY_WITH_TIMEOUT(ready(),10000);
            QMutexLocker lock(&counters->replayMutex);const auto bytes=counters->replayFrames.first();
            QCOMPARE(bytes.size(),640*640*4);
            QImage frame(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888);
            measured[level]=patchLight(meanPatch(frame,300,340,300,340));
        }
        qInfo()<<"Direct output half/full exposure bytes"<<measured[0]<<measured[1];
        QVERIFY(measured[0]>3 && measured[1]<240);
        QVERIFY2(std::abs(measured[1]-2*measured[0])<=1.1,"Below clipping, physical output bytes must double with exposure as reference GPU demonstrates");
    }
    void selfLuminousMaterialHasNoStudioSpot_data()
    {
        QTest::addColumn<bool>("top");
        QTest::newRow("flat-cap-with-luminous-border") << true;
        QTest::newRow("wall-upper-brighter-than-foot") << false;
    }
    void silentCapPreservesIndependentThemeBase_data()
    {
        QTest::addColumn<float>("restHeight");
        QTest::addColumn<float>("clarity");
        QTest::addColumn<bool>("raised");
        QTest::addColumn<float>("capWidth");
        QTest::addColumn<float>("runtime");
        QTest::newRow("one-unit-rest-low-clarity") << 1.0F << 0.0F << false << 6.0F << 0.0F;
        QTest::newRow("three-unit-rest-high-clarity") << 3.0F << 1.4F << false << 6.0F << 0.0F;
        QTest::newRow("raised-cap-follows-glow-palette") << 1.0F << 1.4F << true << 6.0F << 0.0F;
        QTest::newRow("two-pixel-cap-keeps-base-interior") << 1.0F << 1.0F << false << .03125F << 0.0F;
        QTest::newRow("runtime-cap-keeps-reference-base") << 1.0F << 1.14F << false << 6.0F << 1.0F;
    }
    void silentCapPreservesIndependentThemeBase()
    {
        QFETCH(float, restHeight);
        QFETCH(float, clarity);
        QFETCH(bool, raised);
        QFETCH(float, capWidth);
        QFETCH(float, runtime);
        auto replay = std::make_shared<NativeReplay>();
        auto& u = replay->uniform;
        u.timbre[3] = runtime;
        u.sceneControls[0]=u.sceneControls[1]=u.sceneControls[3]=1;
        u.sceneControls[2]=84; u.styleAudio[2]=56;
        u.materialParameters[0]=1; u.materialParameters[1]=.45F;
        u.stylePresentation[2]=clarity;
        u.styleParameters[2]=1;
        if (raised) {
            u.bandsLow[0]=u.equalizerLow[0]=1;
            u.styleParameters[0]=1;
        }
        for (auto& c : u.colors) { c[0]=.9F;c[1]=.2F;c[2]=.1F;c[3]=1; }
        for (int channel=0;channel<3;++channel) u.bodyColor[channel]=.5F;
        u.bodyColor[3]=1;
        replay->instances.push_back({{0,0,0},{capWidth,restHeight,capWidth},{0,0,.5F,0}});
        replay->projection.ortho(-5,5,-5,5,.1F,100);
        replay->view.lookAt({0,20,0},{0,0,0},{0,0,-1});
        u.cameraPosition[1]=20;
        auto counters=std::make_shared<StudyCounters>();
        QQuickWindow window; window.resize(640,640);
        auto* item=new ColumnItem(window.contentItem(),counters); item->replay=replay;
        window.show(); QVERIFY(waitForStudyWindow(window));
        const auto ready=[&]{QMutexLocker lock(&counters->replayMutex);return !counters->replayFrames.isEmpty();};
        QTRY_VERIFY_WITH_TIMEOUT(ready(),10000);
        QMutexLocker lock(&counters->replayMutex);
        const auto bytes=counters->replayFrames.first();
        QCOMPARE(bytes.size(),640*640*4);
        QImage frame(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888);
        const QColor center=frame.pixelColor(320,320);
        qInfo()<<"Independent silent cap raw RGB"<<center;
        if (raised) {
            QVERIFY2(center.red()>center.green()+15,
                     "Raised top must reveal the glow palette even when the optional inner volume is off");
            return;
        }
        // Reference r0, zero elevation/audio: cap center is base2. Encoded .5
        // decodes to linear .21404114, whose UNORM byte is55 (not sRGB128).
        for (const int channel : {center.red(),center.green(),center.blue()})
            QVERIFY2(std::abs(channel-55)<=1,"Silent top must retain theme base2, regardless of clarity or rest slab height");
    }
    void selfLuminousMaterialHasNoStudioSpot()
    {
        QFETCH(bool, top);
        auto replay = std::make_shared<NativeReplay>();
        auto& u = replay->uniform;
        u.sceneControls[0]=u.sceneControls[1]=u.sceneControls[3]=1; u.sceneControls[2]=84;
        u.styleAudio[2]=56; u.materialParameters[0]=1; u.materialParameters[1]=.45F;
        u.styleParameters[2]=1; // The border is theme glow, not an always-on external lamp.
        for (auto& c : u.colors) { c[0]=.35F;c[1]=.5F;c[2]=.65F;c[3]=1; }
        replay->instances.push_back({{0,0,0},{6,6,6},{0,0,.5F,0}});
        replay->projection.ortho(-5,5,-5,5,.1F,100);
        const QVector3D eye=top?QVector3D(0,20,0):QVector3D(0,3,20);
        replay->view.lookAt(eye,{0,3,0},top?QVector3D(0,0,-1):QVector3D(0,1,0));
        for(int i=0;i<3;++i)u.cameraPosition[i]=eye[i];
        auto counters=std::make_shared<StudyCounters>();
        QQuickWindow window;window.resize(640,640);
        auto* item=new ColumnItem(window.contentItem(),counters);item->replay=replay;
        window.show();QVERIFY(waitForStudyWindow(window));
        const auto ready=[&]{QMutexLocker lock(&counters->replayMutex);return !counters->replayFrames.isEmpty();};
        QTRY_VERIFY_WITH_TIMEOUT(ready(),10000);
        QMutexLocker lock(&counters->replayMutex);
        const auto bytes=counters->replayFrames.first();
        QCOMPARE(bytes.size(),640*640*4);
        QImage frame(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888);
        const auto patch=[&](int x,int y){ return patchLight(meanPatch(frame,x-2,x+3,y-2,y+3)); };
        const double center=patch(320,320), upper=patch(320,170),lower=patch(320,470),edge=patch(131,320);
        qInfo()<<"Self-luminous cap/wall center upper lower edge"<<top<<center<<upper<<lower<<edge;
        if(top){
            QVERIFY2(center>=3 && edge>center*1.15,"The actual top border must be brighter than the uninterrupted cap interior in direct UNORM output");
            QVERIFY2(std::abs(upper-center)<4 && std::abs(lower-center)<4,"Silent cap interior must not contain an external studio light spot");
        }else{
            QVERIFY2(upper>=3 && upper>lower*1.6,"Silent wall must retain a visible upper-to-foot tint ratio in direct UNORM output");
        }
    }
    void themeWallPreservesBasesAndRaisedGlow_data()
    {
        QTest::addColumn<bool>("raised");
        QTest::addColumn<bool>("distant");
        QTest::addColumn<float>("sharpness");
        QTest::newRow("silent-base-gradient") << false << false << 0.0F;
        QTest::newRow("raised-upper-wall-glow") << true << false << 0.0F;
        QTest::newRow("distant-base-atmosphere") << false << true << 0.0F;
        QTest::newRow("sharp-upper-wall-glow") << true << false << 1.0F;
    }
    void themeWallPreservesBasesAndRaisedGlow()
    {
        QFETCH(bool, raised);
        QFETCH(bool, distant);
        QFETCH(float, sharpness);
        // Catch the old fixed dark multiplier that discarded base1 and
        // turned even bright theme walls into an opaque-looking black shell.
        auto replay = std::make_shared<NativeReplay>();
        auto& u = replay->uniform;
        u.timbre[2]=sharpness;
        u.sceneControls[0]=u.sceneControls[1]=u.sceneControls[3]=1;
        u.sceneControls[2]=84; u.styleAudio[2]=56;
        u.materialParameters[0]=1; u.materialParameters[1]=.45F;
        u.styleParameters[2]=1;
        const auto encode=[](float value) { return value<=.0031308F
            ? value*12.92F : 1.055F*std::pow(value,1.0F/2.4F)-.055F; };
        const float foot[3]={.04F,.10F,.20F}, crown[3]={.24F,.30F,.40F};
        for(int channel=0;channel<3;++channel) {
            u.colors[0][channel]=encode(foot[channel]);
            u.bodyColor[channel]=encode(crown[channel]);
            u.atmosphereColor[channel]=encode(foot[channel]);
        }
        u.colors[0][3]=u.bodyColor[3]=u.atmosphereColor[3]=1;
        if (raised) {
            // At r0, sub .84 produces exactly four added units with amplitude1.
            // Other palette anchors are zero: cap glow is base2 * .5.
            u.bandsLow[0]=.84F; u.equalizerLow[0]=1; u.styleParameters[0]=.5F;
        }
        const float columnX=distant?60.0F:0.0F;
        replay->instances.push_back({{columnX,0,0},{6,raised?1.0F:6.0F,6},{0,0,.5F,0}});
        replay->projection.ortho(-5,5,-5,5,.1F,100);
        const float centerHeight=raised?2.5F:3.0F;
        replay->view.lookAt({columnX,centerHeight,20},{columnX,centerHeight,0},{0,1,0});
        u.cameraPosition[0]=columnX;
        u.cameraPosition[1]=centerHeight; u.cameraPosition[2]=20;
        auto counters=std::make_shared<StudyCounters>();
        QQuickWindow window; window.resize(640,640);
        auto* item=new ColumnItem(window.contentItem(),counters); item->replay=replay;
        window.show(); QVERIFY(waitForStudyWindow(window));
        const auto ready=[&]{QMutexLocker lock(&counters->replayMutex);return !counters->replayFrames.isEmpty();};
        QTRY_VERIFY_WITH_TIMEOUT(ready(),10000);
        QMutexLocker lock(&counters->replayMutex);
        const auto bytes=counters->replayFrames.first();
        QCOMPARE(bytes.size(),640*640*4);
        QImage frame(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888);
        // Orthographic column occupies y128..512: sample quarter, middle,
        // three-quarter height. Independent linear base interpolation, UNORM.
        const int rows[3]={raised?400:416,320,raised?240:224};
        // With zero sharpness and relief4, at 75% height the upper glow mixes
        // 37.5% of base2*.5 into the base gradient. Lower half stays unchanged.
        // r60: original aerial factor .33061224, alpha .87877044 and
        // backdrop mix .05455330; RGB includes blending over transparent black.
        const int expected[3][3]={{distant?17:23,distant?31:38,distant?53:64},
            {distant?20:36,distant?34:51,distant?56:77},
            {distant?23:(raised&&sharpness==0?42:48),
             distant?36:(raised&&sharpness==0?54:64),
             distant?59:(raised&&sharpness==0?75:89)}};
        for(int sample=0;sample<3;++sample) {
            const QColor pixel=frame.pixelColor(320,rows[sample]);
            qInfo()<<"Theme wall raw RGB at row"<<rows[sample]<<pixel;
            const int actual[3]={pixel.red(),pixel.green(),pixel.blue()};
            for(int channel=0;channel<3;++channel)
                QVERIFY2(std::abs(actual[channel]-expected[sample][channel])<=1,
                    "Theme wall must preserve both base colors and the relief-driven upper glow");
        }
        if (raised) {
            // Inside the side face, not the cap: 4.5 pixels below its top,
            // relative height .9859375. The last 3% carries a raised rim.
            const QColor rim=frame.pixelColor(320,164);
            qInfo()<<"Raised side rim raw RGB"<<rim;
            QVERIFY(std::abs(rim.red()-46)<=1);
            QVERIFY(std::abs(rim.green()-58)<=1);
            QVERIFY(std::abs(rim.blue()-78)<=1);
        }
    }
    void themePaletteUsesLinearRolesWithoutClockTint_data()
    {
        QTest::addColumn<float>("time");
        QTest::addColumn<float>("warmth");
        QTest::addColumn<float>("brightness");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("time-zero") << 0.0F << 0.0F << 0.0F << QColor(36,23,43);
        QTest::newRow("time-twenty") << 20.0F << 0.0F << 0.0F << QColor(36,23,43);
        QTest::newRow("warm-descriptor") << 0.0F << 1.0F << 0.0F << QColor(56,15,31);
        QTest::newRow("bright-descriptor") << 0.0F << 0.0F << 1.0F << QColor(41,43,63);
    }
    void themePaletteUsesLinearRolesWithoutClockTint()
    {
        QFETCH(float,time);
        QFETCH(float,warmth);
        QFETCH(float,brightness);
        QFETCH(QColor,expected);
        auto replay=std::make_shared<NativeReplay>(); auto& u=replay->uniform;
        u.timbre[0]=warmth; u.timbre[1]=brightness;
        u.sceneControls[0]=u.sceneControls[1]=u.sceneControls[3]=1;
        u.sceneControls[2]=84; u.styleAudio[2]=56;
        u.materialParameters[0]=1; u.materialParameters[1]=.45F;
        u.styleParameters[0]=.5F; u.styleParameters[2]=1;
        u.bandsLow[0]=.84F; u.equalizerLow[0]=1; u.parameters[3]=time;
        const auto encode=[](float value) { return value<=.0031308F
            ? value*12.92F : 1.055F*std::pow(value,1.0F/2.4F)-.055F; };
        const float roles[5][3]={{.01F,.01F,.01F},{.04F,.16F,.36F},
            {.36F,.04F,.16F},{.7F,.1F,.2F},{.1F,.8F,.2F}};
        for(int i=0;i<5;++i) for(int c=0;c<3;++c) u.colors[i][c]=encode(roles[i][c]);
        for(int c=0;c<3;++c) u.bodyColor[c]=encode(.08F);
        u.bodyColor[3]=1;
        replay->instances.push_back({{0,0,0},{6,1,6},{0,0,.5F,0}});
        replay->projection.ortho(-5,5,-5,5,.1F,100);
        replay->view.lookAt({0,20,0},{0,0,0},{0,0,-1}); u.cameraPosition[1]=20;
        auto counters=std::make_shared<StudyCounters>();
        QQuickWindow window;window.resize(640,640);
        auto* item=new ColumnItem(window.contentItem(),counters);item->replay=replay;
        window.show();QVERIFY(waitForStudyWindow(window));
        const auto ready=[&]{QMutexLocker lock(&counters->replayMutex);return !counters->replayFrames.isEmpty();};
        QTRY_VERIFY_WITH_TIMEOUT(ready(),10000);
        QMutexLocker lock(&counters->replayMutex);const auto bytes=counters->replayFrames.first();
        QCOMPARE(bytes.size(),640*640*4);
        QImage frame(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888);
        const QColor pixel=frame.pixelColor(320,320);
        qInfo()<<"Linear-role cap RGB"<<time<<pixel;
        // r0 hash0, warmth0 -> equal cool/warm CORE mix (.20,.10,.26).
        // Relief4/8 mixes this with base2 .08 -> (.14,.09,.17), raw UNORM.
        QVERIFY(std::abs(pixel.red()-expected.red())<=1);
        QVERIFY(std::abs(pixel.green()-expected.green())<=1);
        QVERIFY(std::abs(pixel.blue()-expected.blue())<=1);
    }
    void referenceHeightSamples_data()
    {
        QTest::addColumn<QJsonObject>("sample");
        QFile file(QFINDTESTDATA("../fixtures/terrain_reference_heights.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto samples = QJsonDocument::fromJson(file.readAll()).object().value("samples").toArray();
        QCOMPARE(samples.size(), 72);
        for (int i = 0; i < samples.size(); ++i) {
            const auto sample = samples[i].toObject();
            QTest::newRow(qPrintable(QString::number(i))) << sample;
        }
    }
    void referenceHeightSamples()
    {
        QFETCH(QJsonObject, sample);
        auto replay = std::make_shared<NativeReplay>();
        auto& u = replay->uniform;
        u.timbre[3] = 0.0F;
        const auto position = sample.value("position").toArray();
        const float x = float(position[0].toDouble()), z = float(position[1].toDouble());
        u.parameters[3] = float(sample.value("time").toDouble());
        const QStringList fields{"uSubBass", "uBass", "uLowMid", "uMid", "uHighMid"};
        const int band = fields.indexOf(sample.value("uniform").toString());
        if (band >= 0) (band < 4 ? u.bandsLow[band] : u.bandsHigh[0]) = float(sample.value("value").toDouble());
        for (float& gain : u.equalizerLow) gain = 1;
        for (float& gain : u.equalizerHigh) gain = 1;
        u.styleParameters[0] = .5F;
        u.styleToggles[3] = 1;
        u.styleAudio[2] = 56;
        u.sceneControls[0] = u.sceneControls[1] = u.sceneControls[3] = 1;
        u.sceneControls[2] = 84;
        u.materialParameters[0] = 1; u.materialParameters[1] = .45F;
        for (auto& color : u.colors) for (float& v : color) v = .7F;
        replay->instances.push_back({{x,0,z},{4,1,4},{0,0,.5F,0}});
        replay->projection.ortho(-5,5,-5,5,.1F,100);
        replay->view.lookAt({x,3,z+20},{x,3,z},{0,1,0});
        u.cameraPosition[0]=x; u.cameraPosition[1]=3; u.cameraPosition[2]=z+20;
        auto counters = std::make_shared<StudyCounters>();
        QQuickWindow window; window.resize(640,640);
        auto* item = new ColumnItem(window.contentItem(), counters);
        item->replay = replay;
        window.show();
        QVERIFY(waitForStudyWindow(window));
        const auto ready = [&] { QMutexLocker lock(&counters->replayMutex); return !counters->replayFrames.isEmpty(); };
        QTRY_VERIFY_WITH_TIMEOUT(ready() || counters->failed.load(), 10000);
        QVERIFY(!counters->failed);
        QMutexLocker lock(&counters->replayMutex);
        const auto bytes = counters->replayFrames.first();
        QCOMPARE(bytes.size(),640*640*4);
        const QImage image(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888);
        QRect bounds;
        for (int y=0;y<640;++y) for (int px=0;px<640;++px)
            if (image.pixelColor(px,y).alpha()>8) bounds |= QRect(px,y,1,1);
        // Expected is exported from the reference GPU, not a native CPU formula.
        // Runtime response has its own behavior tests because its deliberate
        // gain and idle-gating changes do not share this fixed oracle.
        const double expectedElevation = sample.value("elevation").toDouble();
        const double expectedPixels = 64.0 * (1.0 + std::max(0.0, expectedElevation));
        qInfo()<<"Reference/native height pixels"<<expectedPixels<<bounds.height()<<sample;
        QVERIFY2(std::abs(bounds.height()-expectedPixels)<=1.0,
                 "Column height must agree with the reference GPU within one raster pixel");
        QCOMPARE(bounds.bottom(),511); // baseY remains zero for every band/time.
    }
    void referenceRippleLiteralSamples_data()
    {
        QTest::addColumn<QJsonObject>("sample");
        const QString overridePath = qEnvironmentVariable("AGPLAYER_REFERENCE_RIPPLE_SAMPLES");
        const QString path = overridePath.isEmpty()
            ? QFINDTESTDATA("../fixtures/terrain_reference_ripples.json") : overridePath;
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const auto samples = QJsonDocument::fromJson(file.readAll()).object().value("samples").toArray();
        QCOMPARE(samples.size(), 8);
        for (const auto& value : samples)
            QTest::newRow(qPrintable(value.toObject().value("name").toString())) << value.toObject();
    }
    void referenceRippleLiteralSamples()
    {
        // Break caught: reference-mode ripples use legacy packed age/palette,
        // omit white/slot-9 events, or fail to preserve the original 0.4 idle
        // field. Expected elevation and top RGBA are read from the independent
        // original MapShaderMaterial GPU probe, never calculated by this shader.
        QFETCH(QJsonObject, sample);
        bool validSlots = true;
        auto render = [&](bool top) -> QImage {
            auto replay = std::make_shared<NativeReplay>();
            auto& u = replay->uniform;
            u.styleParameters[0] = .5F;
            u.styleParameters[2] = 1;
            u.styleToggles[0] = u.styleToggles[3] = 1;
            u.styleAudio[0] = u.styleAudio[1] = u.styleAudio[3] = 1; u.styleAudio[2] = 56;
            u.stylePresentation[0] = u.stylePresentation[2] = 1;
            u.sceneControls[0] = u.sceneControls[1] = u.sceneControls[3] = 1; u.sceneControls[2] = 84;
            u.sceneLighting[0] = u.sceneLighting[2] = 1; u.sceneLighting[1] = .6F;
            u.materialParameters[0] = 1; u.materialParameters[1] = .45F;
            u.waveParameters[0] = u.waveParameters[1] = u.waveParameters[2] = 1;
            const auto encode = [](float linear) {
                return linear <= .0031308F ? linear * 12.92F
                    : 1.055F * std::pow(linear, 1.0F / 2.4F) - .055F;
            };
            const float roles[5][3] = {{.04F,.10F,.20F}, {.10F,.80F,1.0F},
                {1.0F,.20F,.10F}, {.20F,.40F,.80F}, {1.0F,.60F,0.0F}};
            for (int role=0; role<5; ++role) for (int channel=0; channel<3; ++channel)
                u.colors[role][channel] = encode(roles[role][channel]);
            u.rippleColor[0] = encode(.2F); u.rippleColor[1] = encode(.9F); u.rippleColor[2] = 1; u.rippleColor[3] = 1;
            u.bodyColor[0]=encode(.24F); u.bodyColor[1]=encode(.30F); u.bodyColor[2]=encode(.40F); u.bodyColor[3]=1;
            for (const auto value : sample.value("events").toArray()) {
                const auto event = value.toObject(); const int slot = event.value("slot").toInt();
                if (slot < 0 || slot >= 10) { validSlots = false; return {}; }
                const auto pos = event.value("pos").toArray();
                u.waveSources[slot][0] = float(pos[0].toDouble());
                u.waveSources[slot][1] = float(pos[1].toDouble());
                u.waveSources[slot][2] = -float(event.value("time").toDouble());
                const float strength = float(event.value("strength").toDouble());
                u.waveSources[slot][3] = event.value("type").toInt() == 0 ? strength : -strength;
            }
            replay->instances.push_back({{0,0,0},{4,1,4},{0,0,.5F,0}});
            replay->projection.ortho(-5,5,-5,5,.1F,100);
            if (top) {
                replay->view.lookAt({0,20,0},{0,0,0},{0,0,-1}); u.cameraPosition[1]=20;
            } else {
                replay->view.lookAt({0,3,20},{0,3,0},{0,1,0}); u.cameraPosition[1]=3; u.cameraPosition[2]=20;
            }
            auto counters = std::make_shared<StudyCounters>();
            QQuickWindow window; window.resize(640,640);
            auto* item = new ColumnItem(window.contentItem(), counters); item->replay=replay;
            window.show();
            if (!waitForStudyWindow(window)) return {};
            const auto ready = [&] { QMutexLocker lock(&counters->replayMutex); return !counters->replayFrames.isEmpty(); };
            QElapsedTimer elapsed; elapsed.start();
            while (!ready() && !counters->failed && elapsed.elapsed() < 10000) QTest::qWait(10);
            if (counters->failed || !ready()) return {};
            QMutexLocker lock(&counters->replayMutex);
            const auto bytes = counters->replayFrames.first();
            if (bytes.size() != 640*640*4) return {};
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888).copy();
        };
        const QImage side = render(false);
        QVERIFY2(validSlots, "Ripple oracle slot must fit the ten-slot reference UBO");
        QVERIFY(!side.isNull());
        QRect bounds;
        for (int y=0;y<side.height();++y) for (int x=0;x<side.width();++x)
            if (side.pixelColor(x,y).alpha()>8) bounds |= QRect(x,y,1,1);
        const double expectedPixels = 64.0 * (1.0 + sample.value("actualElevation").toDouble());
        qInfo() << "Reference ripple height pixels" << sample.value("name").toString()
                << expectedPixels << bounds.height() << "oracle elevation" << sample.value("actualElevation");
        QVERIFY2(std::abs(bounds.height() - expectedPixels) <= 1.0,
                 "Reference ripple height must agree with the original GPU within one raster pixel");
        QCOMPARE(bounds.bottom(), 511);
        const QImage overhead = render(true);
        QVERIFY(!overhead.isNull());
        const auto expected = sample.value("actualRgba").toArray();
        const QColor actual = overhead.pixelColor(320,320);
        qInfo() << "Reference ripple top RGBA" << sample.value("name").toString()
                << "original" << expected << "native" << actual;
        QVERIFY(std::abs(actual.red() - expected[0].toInt()) <= 1);
        QVERIFY(std::abs(actual.green() - expected[1].toInt()) <= 1);
        QVERIFY(std::abs(actual.blue() - expected[2].toInt()) <= 1);
        QCOMPARE(actual.alpha(), expected[3].toInt());
    }
    void geometryRestSlabAndDisk_data()
    {
        QTest::addColumn<bool>("outside");
        QTest::addColumn<float>("radius");
        QTest::addColumn<bool>("idleEnabled");
        QTest::addColumn<bool>("audioEnabled");
        QTest::addColumn<int>("waveCount");
        QTest::newRow("rest-height-foot-and-width") << false << 0.0F << false << false << 0;
        QTest::newRow("inside-fade-start-55") << false << 55.0F << false << false << 0;
        QTest::newRow("at-fade-end-78") << true << 78.0F << false << false << 0;
        QTest::newRow("outside-normalized-disk") << true << 80.0F << false << false << 0;
        QTest::newRow("silent-relief-end-60") << false << 60.0F << true << false << 0;
        QTest::newRow("silent-outer-ring-75") << false << 75.0F << true << false << 0;
        QTest::newRow("music-relief-end-60") << false << 60.0F << true << true << 0;
        QTest::newRow("music-outer-ring-75") << false << 75.0F << true << true << 0;
        QTest::newRow("outer-eight-wave-overlap-bounded") << false << 60.0F << false << false << 8;
    }
    void geometryRestSlabAndDisk()
    {
        QFETCH(bool, outside);
        QFETCH(float, radius);
        QFETCH(bool, idleEnabled);
        QFETCH(bool, audioEnabled);
        QFETCH(int, waveCount);
        const auto capture = [&](float restHeight) {
            auto replay = std::make_shared<NativeReplay>();
            auto& u = replay->uniform;
            u.sceneControls[0] = u.sceneControls[1] = u.sceneControls[3] = 1;
            u.sceneControls[2] = 84; u.styleAudio[2] = 56;
            // Enlarging audio response must not enlarge the silent relief disk.
            if (idleEnabled) { u.styleAudio[2] = 100; u.styleToggles[3] = 1; }
            if (audioEnabled) {
                for (float& band : u.bandsLow) band = 1;
                for (float& band : u.bandsHigh) band = 1;
                for (float& gain : u.equalizerLow) gain = 1;
                for (float& gain : u.equalizerHigh) gain = 1;
                u.styleParameters[0] = .5F;
                u.parameters[0] = u.styleAudio[3] = 1;
                u.audioEnvelope[0] = u.audioEnvelope[1] = u.audioEnvelope[2] = 1;
                u.impact[0] = 1; // Core event light is not an outer travelling ripple.
            }
            u.materialParameters[0] = 1; u.materialParameters[1] = .45F;
            for (auto& color : u.colors) for (float& value : color) value = .7F;
            const float x = radius;
            replay->instances.push_back({{x,0,0},{4,restHeight,4},{0,0,.5F,0}});
            replay->projection.ortho(-5,5,-5,5,.1F,100);
            replay->view.lookAt({x,3,20},{x,3,0},{0,1,0});
            u.cameraPosition[0]=x; u.cameraPosition[1]=3; u.cameraPosition[2]=20;
            if (waveCount > 0) {
                u.styleToggles[0] = 1; u.effects[3] = float(waveCount);
                u.styleParameters[0] = 1;
                u.waveParameters[0] = 2; u.waveParameters[1] = 2; u.waveParameters[2] = 1;
                for (int i=0;i<waveCount;++i) {
                    u.waveSources[i][0]=x; u.waveSources[i][3]=1;
                }
                // Wide orthographic view catches an unbounded wave spike without
                // hiding it behind near/far clipping. Four pixels per unit.
                replay->projection.setToIdentity();
                replay->projection.ortho(-80,80,-80,80,.1F,100);
                replay->view.setToIdentity();
                replay->view.lookAt({x,30,20},{x,30,0},{0,1,0});
                u.cameraPosition[1]=30;
            }
            auto counters = std::make_shared<StudyCounters>();
            QQuickWindow window; window.resize(640,640);
            auto* item = new ColumnItem(window.contentItem(), counters);
            item->replay = replay;
            window.show();
            if (!waitForStudyWindow(window)) return QImage{};
            QElapsedTimer timer; timer.start();
            QByteArray bytes;
            while (timer.elapsed() < 10000) {
                { QMutexLocker lock(&counters->replayMutex);
                  if (!counters->replayFrames.isEmpty()) { bytes=counters->replayFrames.first(); break; } }
                QTest::qWait(10);
            }
            if (bytes.size()!=640*640*4) return QImage{};
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()),640,640,QImage::Format_RGBA8888).copy();
        };
        const auto bounds = [](const QImage& image) {
            QRect result;
            for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
                if (image.pixelColor(x,y).alpha()>8) result |= QRect(x,y,1,1);
            return result;
        };
        const auto low = capture(1);
        QVERIFY(!low.isNull());
        const auto lowBounds = bounds(low);
        if (waveCount > 0) {
            qInfo()<<"Overlapping wave height in pixels"<<lowBounds.height();
            QVERIFY2(lowBounds.height()>100,"Outer waves must remain active outside the musical disk");
            QVERIFY2(lowBounds.height()<=173,"Native wave overlap must stay within the42-unit extension budget plus rest slab");
            return;
        }
        if (outside) { QVERIFY2(lowBounds.isEmpty(), "r80 must be outside fade endpoint78 at extent84"); return; }
        if (idleEnabled) {
            qInfo() << "Outer ring audio/radius/bounds" << audioEnabled << radius << lowBounds;
            QVERIFY(!lowBounds.isEmpty());
            // Orthographic ten-unit vertical span / 640 pixels: rest1 =64px.
            QVERIFY2(std::abs(lowBounds.height()-64)<=1,
                     "Without a travelling ripple, r>=60 must remain the one-unit rest slab even with music");
            return;
        }
        if(radius>0){ QVERIFY2(!lowBounds.isEmpty(),"Fade start55 must still show physical base slab"); return; }
        const auto high = capture(3);
        QVERIFY(!high.isNull());
        const auto highBounds = bounds(high);
        qInfo() << "Rest slab low/high bounds" << lowBounds << highBounds;
        QVERIFY2(lowBounds.height() >= 63, "One-unit rest slab must remain physically visible at zero audio");
        QVERIFY2(highBounds.height()-lowBounds.height() >= 127, "Increasing rest height by2 must add128 projected pixels");
        QCOMPARE(lowBounds.bottom(), highBounds.bottom());
        QVERIFY2(lowBounds.width() >= 255, "Four-unit physical width must occupy256 pixels without hidden shrink");
    }
    void nativeZeroInputReplay()
    {
        QVERIFY2(!(qEnvironmentVariable("AGPLAYER_PARITY_HASH_RETAIN_RESOLVE") == "1"
                   && qEnvironmentVariable("AGPLAYER_PARITY_HASH_REPEAT_RESOLVE") == "1"),
                 "HASH_RETAIN_RESOLVE and HASH_REPEAT_RESOLVE are mutually exclusive");
        const auto path = qEnvironmentVariable("AGPLAYER_PARITY_REFERENCE");
        if (path.isEmpty()) QSKIP("Opt-in reference-engine-silence diagnostic");
        std::shared_ptr<const NativeReplay> replay;
        try { replay = loadNativeReplay(path); }
        catch (const std::exception& error) { QFAIL(error.what()); }
        const QString geometryDiagnostic = qEnvironmentVariable("AGPLAYER_PARITY_HASH_GEOMETRY");
        QVERIFY(geometryDiagnostic.isEmpty() || geometryDiagnostic == "single"
                || geometryDiagnostic == "sparse-nine");
        if (!geometryDiagnostic.isEmpty()) {
            QVERIFY(qEnvironmentVariable("AGPLAYER_PARITY_HASH_DIAGNOSTIC") == "1");
            auto diagnostic = std::make_shared<NativeReplay>(*replay);
            diagnostic->instances.clear();
            const int extent = geometryDiagnostic == "single" ? 0 : 1;
            for (int z = -extent; z <= extent; ++z) for (int x = -extent; x <= extent; ++x) {
                const float tx = float(x * 8), tz = float(z * 8);
                const auto nearest = std::min_element(replay->instances.begin(), replay->instances.end(),
                    [=](const auto& a, const auto& b) {
                        const auto distance = [=](const auto& p) {
                            return std::pow(p.position[0]-tx,2)+std::pow(p.position[2]-tz,2);
                        };
                        return distance(a) < distance(b);
                    });
                diagnostic->instances.push_back(*nearest);
            }
            diagnostic->manifest["hashGeometryDiagnostic"] = geometryDiagnostic;
            diagnostic->manifest["hashGeometryInstanceCount"] = int(diagnostic->instances.size());
            replay = diagnostic;
        }
        const QString runtimeMaterial = qEnvironmentVariable("AGPLAYER_PARITY_RUNTIME_MATERIAL");
        QVERIFY(runtimeMaterial.isEmpty() || runtimeMaterial == "defaults"
                || runtimeMaterial == "clarity-high");
        if (!runtimeMaterial.isEmpty()) {
            auto diagnostic = std::make_shared<NativeReplay>(*replay);
            diagnostic->uniform.timbre[3] = 1;
            diagnostic->uniform.sceneLighting[0] = 1.55F;
            diagnostic->uniform.stylePresentation[2] = runtimeMaterial == "defaults" ? 1.14F : 1.4F;
            diagnostic->manifest.insert("runtimeMaterialDiagnostic", runtimeMaterial);
            diagnostic->manifest.insert("nativeEventEnvelopes", "zero: not a live event replay");
            replay = diagnostic;
        }
        // The fixed reference uses centerY=.5 and box height=1. Its base is zero.
        // Assert actual loader output before creating any rendering resources.
        QCOMPARE(replay->manifest.value("referenceGeometryParameters").toObject().value("height").toDouble(), 1.0);
        for (const auto& instance : replay->instances)
            QCOMPARE(instance.position[1], 0.0F);
        auto counters = std::make_shared<StudyCounters>();
        QQuickWindow window;
        window.resize(960, 540);
        const QString frameOption = qEnvironmentVariable("AGPLAYER_PARITY_FRAMES", "2");
        QVERIFY(frameOption == "2" || frameOption == "4");
        const int frameCount = frameOption.toInt();
        counters->replayFrameLimit = frameCount;
        auto* item = new ColumnItem(window.contentItem(), counters);
        item->replay = replay;
        item->setSize(QSizeF(960, 540));
        item->setFixedColorBufferWidth(1920); item->setFixedColorBufferHeight(1080);
        const QString sampleOption = qEnvironmentVariable("AGPLAYER_PARITY_SAMPLES", "1");
        QVERIFY2(sampleOption == "1" || sampleOption == "4", "Replay samples must be explicitly 1 or 4");
        const int samples = sampleOption.toInt();
        item->setSampleCount(samples);
        const bool rasterMirror = qEnvironmentVariable("AGPLAYER_PARITY_RASTER_MIRROR") == "1";
        item->setMirrorVertically(rasterMirror);
        window.show();
        QVERIFY(waitForStudyWindow(window));
        const auto captured = [&] { QMutexLocker lock(&counters->replayMutex); return counters->replayFrames.size(); };
        QTRY_VERIFY_WITH_TIMEOUT(captured() >= 1 || counters->failed.load(), 30000);
        QVERIFY(!counters->failed);
        for (int frame = 2; frame <= frameCount; ++frame) {
            item->update();
            QTRY_VERIFY_WITH_TIMEOUT(captured() >= frame, 30000);
        }
        QMutexLocker lock(&counters->replayMutex);
        QCOMPARE(counters->replayManifest.value("width").toInt(), 1920);
        QCOMPARE(counters->replayManifest.value("sampleCount").toInt(), samples);
        QCOMPARE(counters->replayManifest.value("height").toInt(), 1080);
        QCOMPARE(counters->replayManifest.value("format").toInt(), int(QRhiTexture::RGBA8));
        QCOMPARE(counters->replayFrames[0].size(), 1920 * 1080 * 4);
        const auto output = qEnvironmentVariable("AGPLAYER_PARITY_OUTPUT");
        QVERIFY2(!output.isEmpty(), "Set AGPLAYER_PARITY_OUTPUT");
        QVERIFY(QDir().mkpath(output));
        auto manifest = replay->manifest;
        for (auto it = counters->replayManifest.begin(); it != counters->replayManifest.end(); ++it) manifest[it.key()] = it.value();
        manifest["nativeRepeatabilityOnly"] = true;
        const bool blendOffDiagnostic = manifest.value("hashDiagnostic").toObject()
            .value("blendEnabled").isBool()
            && !manifest.value("hashDiagnostic").toObject().value("blendEnabled").toBool();
        manifest["readbackAlphaMode"] = blendOffDiagnostic
            ? "nonappearance hash RGBA8; RGB is not associated with output alpha"
            : "premultiplied RGBA8";
        manifest["pngAlphaMode"] = blendOffDiagnostic
            ? "raw diagnostic channels; no unpremultiplication; not valid for appearance comparison"
            : "straight RGBA8, unpremultiplied once by QImage PNG writer";
        manifest["rasterMirrorDiagnostic"] = rasterMirror;
        bool repeatable = true;
        for (int frame=1; frame<frameCount; ++frame)
            repeatable &= counters->replayFrames[0] == counters->replayFrames[frame];
        manifest["repeatabilityPassed"] = repeatable;
        QJsonArray rawDifferences;
        for (int frame = 1; frame < frameCount; ++frame) {
            const auto& first = counters->replayFrames[0];
            const auto& next = counters->replayFrames[frame];
            QCOMPARE(first.size(), next.size());
            int changed = 0, opaque = 0, alphaChanged = 0, aboveOne = 0, maximum = 0;
            QJsonArray examples;
            for (qsizetype offset = 0; offset < first.size(); offset += 4) {
                int delta = 0;
                QJsonArray before, after;
                for (int channel = 0; channel < 4; ++channel) {
                    const int a = uchar(first[offset + channel]), b = uchar(next[offset + channel]);
                    delta = std::max(delta, std::abs(a - b));
                }
                if (!delta) continue;
                ++changed;
                opaque += uchar(first[offset + 3]) == 255 && uchar(next[offset + 3]) == 255;
                alphaChanged += first[offset + 3] != next[offset + 3];
                aboveOne += delta > 1;
                if (examples.size() < 8 || delta > maximum) {
                    for (int channel = 0; channel < 4; ++channel) {
                        before.append(int(uchar(first[offset + channel])));
                        after.append(int(uchar(next[offset + channel])));
                    }
                    examples.append(QJsonObject{{"x", int(offset / 4 % 1920)},
                        {"y", int(offset / 4 / 1920)}, {"before", before}, {"after", after}});
                }
                maximum = std::max(maximum, delta);
            }
            rawDifferences.append(QJsonObject{{"frame", frame}, {"changedPixels", changed},
                {"opaqueChangedPixels", opaque}, {"alphaChangedPixels", alphaChanged},
                {"aboveOneLsbPixels", aboveOne}, {"maxLsbDelta", maximum}, {"examples", examples}});
        }
        manifest["rawRgbaDifferencesFromFirst"] = rawDifferences;
        manifest["uniformHashes"] = QJsonArray::fromStringList(counters->replayUniformHashes);
        manifest["rawSha256"] = QString(QCryptographicHash::hash(counters->replayFrames[0], QCryptographicHash::Sha256).toHex());
        QImage image(reinterpret_cast<const uchar*>(counters->replayFrames[0].constData()), 1920, 1080, QImage::Format_RGBA8888);
        if (manifest.value("framebufferYUp").toBool()) image = image.mirrored();
        if (rasterMirror) image = image.mirrored();
        if (qEnvironmentVariable("AGPLAYER_PARITY_HASH_DIAGNOSTIC") == "1") {
            // Break caught: the diagnostic selector falls back to the
            // appearance shader, drops flat hash transport, or writes an
            // unrelated blue channel.  These are encoded framebuffer values,
            // not a material-parity assertion.
            const auto diagnostic = manifest.value("hashDiagnostic").toObject();
            QVERIFY(diagnostic.value("nonAppearanceOnly").toBool());
            QCOMPARE(diagnostic.value("contract").toString(),
                     QStringLiteral("R=flat columnRandom; G=clamp(flat reliefHeight/16); B=0; A=opacity"));
            QSet<int> randomBytes;
            QSet<int> opaqueRandomBytes;
            bool hasGeometry = false;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    // QImage owns RGBA8888 channel decoding. Reinterpreting
                    // its little-endian bytes as QRgb would instead read an
                    // ARGB integer and swap the diagnostic channels.
                    const QColor pixel = image.pixelColor(x, y);
                    if (pixel.alpha() == 0) continue;
                    hasGeometry = true;
                    // Direct RGBA8 D3D11 resolves may round a zero source to
                    // one code point at coverage edges.  Any material path
                    // has a materially larger blue signal, so one LSB keeps
                    // this a transport diagnostic rather than a backend
                    // quantization test.
                    QVERIFY(pixel.blue() <= 1);
                    randomBytes.insert(pixel.red());
                    if (pixel.alpha() == 255) opaqueRandomBytes.insert(pixel.red());
                }
            }
            QVERIFY(hasGeometry);
            QVERIFY2(randomBytes.size() > 1,
                     "Hash diagnostic must preserve distinct flat per-column random values");
            if (geometryDiagnostic == "single")
                QCOMPARE(opaqueRandomBytes.size(), 1); // One instance has exactly one flat ID.
            else
                QVERIFY2(opaqueRandomBytes.size() > 1,
                         "Hash diagnostic must preserve distinct random values before alpha coverage blending");
        }
        QVERIFY(saveNativeReadbackPng(image, output + "/native-u.png", !blendOffDiagnostic));
        for (int frame=1; frame<frameCount; ++frame) {
            QImage next(reinterpret_cast<const uchar*>(counters->replayFrames[frame].constData()), 1920, 1080, QImage::Format_RGBA8888);
            if (manifest.value("framebufferYUp").toBool()) next = next.mirrored();
            if (rasterMirror) next = next.mirrored();
            QVERIFY(saveNativeReadbackPng(next, output + QString("/native-u-frame%1.png").arg(frame+1), !blendOffDiagnostic));
        }
        QFile report(output + "/native-u-manifest.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        QVERIFY(report.write(QJsonDocument(manifest).toJson()) > 0);
        // Preserve both raw frames even when strict determinism fails.
        for (int frame=1; frame<frameCount; ++frame)
            QCOMPARE(counters->replayFrames[0], counters->replayFrames[frame]);
    }
    void referenceUniformFixtureMapsOnlyMappedDescriptors()
    {
        // Break caught: a synthetic descriptor fixture is rejected like a
        // silence replay, a descriptor reaches the wrong UBO lane, or an
        // unknown source field is silently accepted. This fixture is loader
        // input only; it does not claim to be captured audio.
        const QString sourcePath = qEnvironmentVariable("AGPLAYER_PARITY_REFERENCE");
        if (sourcePath.isEmpty()) QSKIP("Set AGPLAYER_PARITY_REFERENCE to an original silence export");
        QFile source(sourcePath); QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(sourcePath));
        const auto sourceDocument = QJsonDocument::fromJson(source.readAll());
        QVERIFY(sourceDocument.isObject());
        const auto loadCandidate = [](const QJsonObject& root) {
            QTemporaryFile file;
            if (!file.open() || file.write(QJsonDocument(root).toJson()) < 1) throw std::runtime_error("Temporary fixture write failed");
            file.close();
            return loadNativeReplay(file.fileName());
        };
        auto mapped = sourceDocument.object();
        mapped["input"] = "reference-uniform-fixture";
        auto uniforms = mapped.value("uniforms").toObject();
        // The fixed MapShaderMaterial export has no uStream uniform. Its
        // high-band fragment passes are unconditional, so the adapter must
        // express that source behavior through the native stream gate rather
        // than silently leaving the unrelated native default at zero.
        QVERIFY(!uniforms.contains("uStream"));
        const QJsonObject descriptors{{"uSubBass", .11}, {"uBass", .22}, {"uLowMid", .33}, {"uMid", .44},
            {"uHighMid", .55}, {"uPresence", .66}, {"uBrilliance", .77}, {"uAir", .88},
            {"uWarmth", .19}, {"uBrightness", .29}, {"uSharpness", .39}, {"uSmoothness", .49},
            {"uDensity", .59}, {"uEnergy", .69}, {"uSpectralCentroid", .79}};
        for (auto it = descriptors.begin(); it != descriptors.end(); ++it) uniforms[it.key()] = it.value();
        mapped["uniforms"] = uniforms;
        std::shared_ptr<const NativeReplay> replay;
        try { replay = loadCandidate(mapped); }
        catch (const std::exception& error) { QFAIL(error.what()); }
        QCOMPARE(replay->uniform.bandsLow[0], .11F); QCOMPARE(replay->uniform.bandsLow[1], .22F);
        QCOMPARE(replay->uniform.bandsLow[2], .33F); QCOMPARE(replay->uniform.bandsLow[3], .44F);
        QCOMPARE(replay->uniform.bandsHigh[0], .55F); QCOMPARE(replay->uniform.bandsHigh[1], .66F);
        QCOMPARE(replay->uniform.bandsHigh[2], .77F); QCOMPARE(replay->uniform.bandsHigh[3], .88F);
        QCOMPARE(replay->uniform.timbre[0], .19F); QCOMPARE(replay->uniform.timbre[1], .29F);
        QCOMPARE(replay->uniform.timbre[2], .39F); QCOMPARE(replay->uniform.waveParameters[3], .49F);
        QCOMPARE(replay->uniform.sceneLighting[3], .59F); QCOMPARE(replay->uniform.parameters[0], .69F);
        QCOMPARE(replay->uniform.parameters[1], 0.0F); // Spectral centroid remains intentionally unused.
        QCOMPARE(replay->uniform.waveParameters[0], 1.0F); QCOMPARE(replay->uniform.waveParameters[1], 1.0F);
        QCOMPARE(replay->uniform.waveParameters[2], 1.0F);
        QCOMPARE(replay->uniform.styleExtra[2], 1.0F);
        QCOMPARE(replay->manifest.value("input").toString(), "reference-uniform-fixture");
        QCOMPARE(replay->manifest.value("referenceStreamContract").toString(),
                 "MapShaderMaterial declares no uStream uniform; its high-band top effects are unconditional, so native styleExtra.z is explicitly 1");
        auto silenceWithAudio = mapped; silenceWithAudio["input"] = "reference-engine-silence";
        bool silenceRejected = false;
        try { loadCandidate(silenceWithAudio); } catch (const std::exception&) { silenceRejected = true; }
        QVERIFY(silenceRejected);
        auto unknown = mapped; auto unknownUniforms = unknown.value("uniforms").toObject(); unknownUniforms["uUnknown"] = .5; unknown["uniforms"] = unknownUniforms;
        bool unknownRejected = false;
        try { loadCandidate(unknown); } catch (const std::exception&) { unknownRejected = true; }
        QVERIFY(unknownRejected);
    }
    void referenceUniformFixtureImportsActiveRipples()
    {
        // Catch event-type loss, absolute time uploaded as age, ignored slot 9,
        // and malformed exports silently becoming a different visual input.
        const QString sourcePath = qEnvironmentVariable("AGPLAYER_PARITY_REFERENCE");
        if (sourcePath.isEmpty()) QSKIP("Set AGPLAYER_PARITY_REFERENCE to an original silence export");
        QFile source(sourcePath); QVERIFY(source.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(source.readAll()).object();
        root["input"] = "reference-uniform-fixture";
        auto uniforms = root.value("uniforms").toObject();
        uniforms["uTime"] = 7.0;
        auto events = uniforms.value("uRipples").toArray();
        QCOMPARE(events.size(), 10);
        events[0] = QJsonObject{{"isActive",1},{"pos",QJsonArray{2,-3}},
            {"time",6.0},{"strength",.25},{"rippleType",0}};
        events[9] = QJsonObject{{"isActive",1},{"pos",QJsonArray{-4,5}},
            {"time",6.5},{"strength",.5},{"rippleType",1}};
        uniforms["uRipples"] = events; root["uniforms"] = uniforms;
        const auto load = [](const QJsonObject& candidate) {
            QTemporaryFile file;
            if (!file.open() || file.write(QJsonDocument(candidate).toJson()) < 1)
                throw std::runtime_error("Cannot write temporary replay input");
            file.close(); return loadNativeReplay(file.fileName());
        };
        std::shared_ptr<const NativeReplay> replay;
        try { replay = load(root); } catch (const std::exception& e) { QFAIL(e.what()); }
        const auto& u = replay->uniform;
        QCOMPARE(u.waveSources[0][0],2.0F); QCOMPARE(u.waveSources[0][1],-3.0F);
        QCOMPARE(u.waveSources[0][2],1.0F); QCOMPARE(u.waveSources[0][3],.25F);
        QCOMPARE(u.waveSources[9][0],-4.0F); QCOMPARE(u.waveSources[9][1],5.0F);
        QCOMPARE(u.waveSources[9][2],.5F); QCOMPARE(u.waveSources[9][3],-.5F);
        for (int slot=1;slot<9;++slot) QCOMPARE(u.waveSources[slot][3],0.0F);
        QCOMPARE(u.waveParameters[0],1.0F);
        QCOMPARE(u.waveParameters[1],1.0F); QCOMPARE(u.waveParameters[2],1.0F);
        const QString output = qEnvironmentVariable("AGPLAYER_ACTIVE_REPLAY_OUTPUT");
        if (!output.isEmpty()) {
            QFile fixture(output);
            QVERIFY2(fixture.open(QIODevice::WriteOnly | QIODevice::NewOnly), qPrintable(output));
            const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
            QCOMPARE(fixture.write(bytes), qint64(bytes.size()));
        }
        const auto rejects = [&](QJsonObject candidate) {
            try { load(candidate); return false; } catch (const std::exception&) { return true; }
        };
        auto silence = root; silence["input"] = "reference-engine-silence";
        QVERIFY(rejects(silence));
        for (const auto key : {"uTime", "uAmplitude", "uGlowIntensity"}) {
            auto candidate = root; auto fields = uniforms;
            auto idle = events;
            for (int i=0;i<idle.size();++i)
                idle[i]=QJsonObject{{"isActive",0},{"pos",QJsonArray{0,0}},{"time",-100},{"strength",0}};
            fields["uRipples"]=idle; fields[key]=1e100; candidate["uniforms"]=fields;
            QVERIFY2(rejects(candidate), key);
        }
        for (const auto key : {"rippleType","time","strength","pos"}) {
            auto broken = events; auto event = broken[0].toObject(); event.remove(key); broken[0]=event;
            auto fields=uniforms; fields["uRipples"]=broken; auto candidate=root; candidate["uniforms"]=fields;
            QVERIFY2(rejects(candidate), key);
        }
        for (const auto pair : {qMakePair(QString("rippleType"),2.0), qMakePair(QString("time"),8.0),
                               qMakePair(QString("strength"),-.1), qMakePair(QString("isActive"),2.0)}) {
            auto broken=events; auto event=broken[0].toObject(); event[pair.first]=pair.second; broken[0]=event;
            auto fields=uniforms; fields["uRipples"]=broken; auto candidate=root; candidate["uniforms"]=fields;
            QVERIFY(rejects(candidate));
        }
        events.append(events[0]); uniforms["uRipples"]=events; root["uniforms"]=uniforms;
        QVERIFY(rejects(root));
    }
    void referenceModeDoesNotUseLegacyInteriorLight()
    {
        // Break caught: low-frequency reference input still feeds the native
        // volume, received-column, or thin-shell sources. MapShaderMaterial
        // has no such secondary light path: its sub-bass response is only its
        // own height/current-glow material. sceneLighting is a native legacy
        // control and must not change this reference-mode replay.
        const QString path = qEnvironmentVariable("AGPLAYER_SUBBASS_REFERENCE");
        if (path.isEmpty()) QSKIP("Set AGPLAYER_SUBBASS_REFERENCE to the synthetic subbass reference fixture");
        std::shared_ptr<const NativeReplay> source;
        try { source = loadNativeReplay(path); } catch (const std::exception& error) { QFAIL(error.what()); }
        auto noLegacyLight = std::make_shared<NativeReplay>(*source);
        noLegacyLight->uniform.sceneLighting[0] = 0;
        const auto capture = [](std::shared_ptr<const NativeReplay> replay) {
            auto counters = std::make_shared<StudyCounters>();
            QQuickWindow window; window.resize(640, 640);
            auto* item = new ColumnItem(window.contentItem(), counters); item->replay = std::move(replay);
            window.show(); if (!waitForStudyWindow(window)) return QImage{};
            const auto ready = [&] { QMutexLocker lock(&counters->replayMutex); return !counters->replayFrames.isEmpty(); };
            QElapsedTimer elapsed; elapsed.start();
            while (!ready() && !counters->failed && elapsed.elapsed() < 15000) QTest::qWait(10);
            if (counters->failed || !ready()) return QImage{};
            QMutexLocker lock(&counters->replayMutex);
            const auto bytes = counters->replayFrames.first();
            if (bytes.size() != 640 * 640 * 4) return QImage{};
            return QImage(reinterpret_cast<const uchar*>(bytes.constData()), 640, 640,
                          QImage::Format_RGBA8888).copy();
        };
        const QImage lit = capture(source);
        const QImage unlit = capture(noLegacyLight);
        QVERIFY(!lit.isNull() && !unlit.isNull());
        const auto hash = [](const QImage& image) {
            return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),
                                                        image.sizeInBytes()), QCryptographicHash::Sha256);
        };
        QCOMPARE(hash(lit), hash(unlit));
    }
    void sceneTimeChangesColumnsWithoutMovingBase_data() {
        QTest::addColumn<float>("audioLevel");
        QTest::addColumn<float>("midAudioLevel");
        QTest::newRow("silence-with-idle-enabled") << 0.0F << 0.0F;
        QTest::newRow("steady-audio-with-motion-enabled") << 0.65F << 0.35F;
    }
    void sceneTimeChangesColumnsWithoutMovingBase();
    void runtimeSteadyAudioDoesNotFreeRun();
    void runtimeMotionControlDoesNotDoubleScaleShaderRelief();
    void consecutiveWavesUseDifferentPaletteAnchors();
    void explicitThemeTravellingWaveTintRequiresActiveWave();
    void referenceRippleSeparatesNormalAndWhiteContracts();
    void discreteBeatDoesNotMoveCanonicalColumns_data() {
        QTest::addColumn<int>("materialMode");
        QTest::addColumn<float>("randomValue");
        QTest::newRow("crystal-unselected") << 0 << 0.90F;
        QTest::newRow("jelly-unselected") << 1 << 0.90F;
        QTest::newRow("crystal-selected") << 0 << 0.10F;
        QTest::newRow("jelly-selected") << 1 << 0.10F;
    }
    void discreteBeatDoesNotMoveCanonicalColumns();
    void unsupportedDepthMaterialFallsBack_data() {
        QTest::addColumn<int>("stage");
        QTest::newRow("bindings") << 1;
        QTest::newRow("pipeline") << 2;
    }
    void unsupportedDepthMaterialFallsBack();
    void sharedShadowChangesLightingWithoutMovingArraySilhouette();
    void globalOpacityDoesNotSwitchShadowAtFiftyFivePercent();
    void audioDrivesInnerLightWithoutWashingOutShell();
    void beatLightTravelsUpInsideFixedColumn();
    void decayingBeatRetainsVisibleUpwardLightTravel();
    void innerLightHasOpticalDepthAcrossSmoothFace();
    void jellyHeightFollowsContinuousBands();
    void neutralGrayWithoutInnerLightRemainsNeutral();
    void softnessChangesLuminousCapRolloff();
    void everyColumnHasLocalCapFlash_data() {
        QTest::addColumn<float>("randomValue");
        QTest::newRow("low-random") << 0.10F;
        QTest::newRow("last-selected") << 0.24F;
        QTest::newRow("first-unselected") << 0.26F;
        QTest::newRow("unselected-forty-percent") << 0.90F;
    }
    void everyColumnHasLocalCapFlash();
    void referenceThemeTopFlashesUseHashAndStreamGate();
    void lightControlsReachNativeMaterial_data() {
        QTest::addColumn<int>("lane");
        QTest::newRow("inner-source") << 0;
        QTest::newRow("light-spill") << 1;
        QTest::newRow("light-radius") << 2;
        QTest::newRow("high-only-inner-source") << 3;
    }
    void lightControlsReachNativeMaterial();
    void columnSpillDoesNotPaintTopCaps();
    void brightThemeCapsRetainHeadroom();
    void smoothInteriorRemainsStable_data() {
        QTest::addColumn<int>("materialMode");
        QTest::addColumn<bool>("rainbow");
        QTest::newRow("crystal") << 0 << false;
        QTest::newRow("jelly") << 1 << false;
        QTest::newRow("jelly-rainbow") << 1 << true;
    }
    void smoothInteriorRemainsStable();
};

void TerrainColumnMaterialTest::sceneTimeChangesColumnsWithoutMovingBase()
{
    QFETCH(float, audioLevel);
    QFETCH(float, midAudioLevel);
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    // Distinguish even a dark silent column from the background. Assertions
    // measure occupied pixels, not material brightness or shader height math.
    window.resize(640, 640);
    window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.array = true;
    item.parameters.material = 0;
    item.parameters.motionControl = 0.5F; // Real supported motion, not the frozen fixture default.
    item.parameters.idleRelief = true;
    item.parameters.audioLevel = audioLevel;
    item.parameters.midAudioLevel = midAudioLevel;
    item.parameters.stream = false;
    item.parameters.beat = 0;
    item.parameters.waveSlot = -1;
    item.parameters.lighting = {0, 0, 1};
    item.parameters.tint = QColor::fromRgbF(0.42F, 0.42F, 0.42F);
    item.parameters.camera = {34, 30, 48};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage baseline = studyFrame(window);
    QVERIFY(!baseline.isNull());
    bool movedColumns = false;
    for (const float time : {2.0F, 5.0F, 11.0F}) {
        const int before = counters->frames;
        item.parameters.time = time;
        item.update();
        QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before || counters->failed, 3000);
        QVERIFY(!counters->failed);
        const QImage current = studyFrame(window);
        QCOMPARE(current.size(), baseline.size());
        const auto frames = compareFrames(baseline, current);
        qInfo() << "Reference-time relief / coverage / mismatch / bounds:"
                << time << frames.commonVisible << frames.silhouetteMismatch
                << frames.firstBounds << frames.secondBounds;
        QVERIFY2(frames.commonVisible > 5000, "The stationary column array must remain visible");
        QVERIFY2(std::abs(frames.firstBounds.bottom()-frames.secondBounds.bottom()) <= 1,
                 "Changing column relief must not move the ground's bottom anchor");
        movedColumns |= frames.silhouetteMismatch > frames.commonVisible / 1000;
    }
    // The reference field has explicit time input. Its columns may change;
    // unlike the superseded frozen sine field, only the base must stay fixed.
    QVERIFY2(movedColumns, "Reference scene time must advance the non-rigid column field");
    const int before = counters->frames;
    item.parameters.time = 0;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before || counters->failed, 3000);
    const auto repeated = compareFrames(baseline, studyFrame(window));
    QVERIFY(nearlySameBounds(repeated.firstBounds, repeated.secondBounds));
    QVERIFY2(repeated.silhouetteMismatch <= repeated.commonVisible / 1000,
             "Replaying identical time/audio input must reproduce the same geometry");
}

void TerrainColumnMaterialTest::runtimeSteadyAudioDoesNotFreeRun()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.array = true;
    item.parameters.runtimeMode = 1.0F;
    item.parameters.stageHalfExtent = 84.0F;
    item.parameters.idleRelief = true;
    item.parameters.audioLevel = 0.65F;
    item.parameters.midAudioLevel = 0.35F;
    item.parameters.beat = 0.0F;
    item.parameters.waveSlot = -1;
    item.parameters.camera = {34, 30, 48};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage baseline = studyFrame(window);
    QVERIFY(!baseline.isNull());

    const int before = counters->frames;
    item.parameters.time = 5.0F;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before || counters->failed, 3000);
    QVERIFY(!counters->failed);
    const auto change = compareFrames(baseline, studyFrame(window));
    QVERIFY2(nearlySameBounds(change.firstBounds, change.secondBounds),
             "Runtime terrain must keep its fixed ground and steady-audio silhouette");
    QVERIFY2(change.silhouetteMismatch <= change.commonVisible / 1000,
             "Steady audio must not animate columns from a free-running clock");
}

void TerrainColumnMaterialTest::runtimeMotionControlDoesNotDoubleScaleShaderRelief()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.array = true;
    item.parameters.runtimeMode = 1.0F;
    item.parameters.stageHalfExtent = 84.0F;
    item.parameters.idleRelief = false;
    item.parameters.audioLevel = 0.72F;
    item.parameters.midAudioLevel = 0.48F;
    item.parameters.motionControl = 0.0F;
    item.parameters.beat = 0.0F;
    item.parameters.waveSlot = -1;
    item.parameters.camera = {34, 30, 48};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage restrained = studyFrame(window);
    QVERIFY(!restrained.isNull());

    const int before = counters->frames;
    item.parameters.motionControl = 1.0F;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before || counters->failed, 3000);
    QVERIFY(!counters->failed);
    const auto change = compareFrames(restrained, studyFrame(window));
    QVERIFY2(nearlySameBounds(change.firstBounds, change.secondBounds),
             "The material shader must not apply motion response a second time");
    QVERIFY2(change.silhouetteMismatch <= change.commonVisible / 1000,
             "Motion response belongs to the terrain response layer, not shader geometry");
}

void TerrainColumnMaterialTest::consecutiveWavesUseDifferentPaletteAnchors()
{
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(Qt::black);
    auto counters = std::make_shared<StudyCounters>();
    ColumnItem item(window.contentItem(), counters);
    item.parameters.camera = {0, 6, 50};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QImage previous;
    for (int tint = 0; tint < 4; ++tint) {
        // Reusing one slot models the smallest quality pool: event color must
        // change independently of which storage slot is available.
        item.parameters.waveSlot = 0;
        item.parameters.waveTint = tint;
        const int frames = counters->frames;
        item.update();
        QTRY_VERIFY_WITH_TIMEOUT(counters->frames > frames, 5000);
        const QImage frame = studyFrame(window);
        if (!previous.isNull()) {
            const auto difference = compareFrames(previous, frame);
            QVERIFY(difference.commonVisible > 1000);
            QVERIFY2(difference.changedCommon > difference.commonVisible / 3,
                     "Consecutive wave events reuse the same color");
        }
        previous = frame;
    }
}

void TerrainColumnMaterialTest::explicitThemeTravellingWaveTintRequiresActiveWave()
{
    // Break caught: changing an active travelling-wave palette anchor has no
    // visible effect once explicit bodyColor bypasses the vertex color, or a
    // disabled event still stains the theme through a stale varying.
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    // Overhead view samples the wave's cap material directly, rather than a
    // side wall whose explicit base2 intentionally dominates the albedo.
    item.parameters.camera = {0, 38, 10};
    item.parameters.cameraTarget = {0, 0, 0};
    item.parameters.bodyTint = QColor::fromRgbF(0.24F, 0.30F, 0.40F);
    item.parameters.rainbow = true; // material.w < 0: bypass vertex-color mediumTint.
    item.parameters.audioLevel = 0;
    item.parameters.heightControl = 1;
    item.parameters.lighting = {0, 0.6F, 1}; // no old medium/emission contribution.
    item.parameters.waveSlot = 0;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const auto capture = [&]() {
        const int before = counters->frames;
        item.update();
        QElapsedTimer elapsed; elapsed.start();
        while (counters->frames <= before && !counters->failed && elapsed.elapsed() < 3000)
            QTest::qWait(10);
        return counters->failed ? QImage{} : studyFrame(window);
    };
    const auto positiveDelta = [](const QImage& off, const QImage& on) {
        QVector3D sum;
        int count = 0;
        for (int y = 0; y < off.height(); ++y)
            for (int x = 0; x < off.width(); ++x) {
                const QColor a = off.pixelColor(x, y);
                const QColor b = on.pixelColor(x, y);
                sum += QVector3D(std::max(0, b.red() - a.red()),
                                 std::max(0, b.green() - a.green()),
                                 std::max(0, b.blue() - a.blue()));
                ++count;
            }
        return sum / float(std::max(1, count));
    };
    const auto captureTintDelta = [&](int tint, QImage* activeFrame) {
        item.parameters.waveTint = tint;
        item.parameters.waveSourceActive = true;
        item.parameters.waveEnabled = false;
        const QImage off = capture();
        item.parameters.waveEnabled = true;
        const QImage on = capture();
        if (off.isNull() || on.isNull()) return QVector3D(-1000, -1000, -1000);
        *activeFrame = on;
        return positiveDelta(off, on);
    };
    QImage cyanFrame, redFrame;
    const QVector3D cyanDelta = captureTintDelta(0, &cyanFrame);
    const QVector3D redDelta = captureTintDelta(1, &redFrame);
    QVERIFY(cyanDelta.x() > -999 && redDelta.x() > -999);
    qInfo() << "Explicit-theme travelling-wave RGB delta cyan/red:"
            << cyanDelta << redDelta;
    const QString waveEvidenceDirectory = qEnvironmentVariable("AGPLAYER_WAVE_EVIDENCE_DIR");
    if (!waveEvidenceDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(waveEvidenceDirectory));
        QVERIFY(cyanFrame.save(QDir(waveEvidenceDirectory).filePath("wave-color-cyan.png")));
        QVERIFY(redFrame.save(QDir(waveEvidenceDirectory).filePath("wave-color-red.png")));
    }
    QVERIFY2(cyanDelta.z() > cyanDelta.x() + 0.25F,
             "Active cyan travelling wave must tint explicit-theme material after medium bypass");
    QVERIFY2(redDelta.x() > redDelta.z() + 0.25F,
             "Changing only the wave palette slot must change the explicit-theme wave hue");

    const FrameComparison hueGeometry = compareFrames(cyanFrame, redFrame);
    QVERIFY2(nearlySameBounds(hueGeometry.firstBounds, hueGeometry.secondBounds),
             "Changing only the wave palette slot must not move the travelling-wave geometry");

    // A populated source with the toggle off is behaviorally identical to the
    // disabled capture above. Re-render it to make stale/ungated tint varyings
    // observable without relying on the old medium emission path.
    item.parameters.waveTint = 1;
    item.parameters.waveEnabled = false;
    item.parameters.waveSourceActive = true;
    const QImage toggleOff = capture();
    item.parameters.waveEnabled = true;
    item.parameters.waveSourceActive = false;
    const QImage inactive = capture();
    QVERIFY(!toggleOff.isNull() && !inactive.isNull());
    const FrameComparison disabled = compareFrames(toggleOff, inactive);
    const auto imageHash = [](const QImage& image) {
        return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),
                                                    image.sizeInBytes()), QCryptographicHash::Sha256);
    };
    QCOMPARE(imageHash(toggleOff), imageHash(inactive));
    QVERIFY2(nearlySameBounds(disabled.firstBounds, disabled.secondBounds)
             && disabled.totalRgbDifference == 0,
             "Inactive or toggle-off travelling waves must not stain explicit-theme material");
    window.close();
}

void TerrainColumnMaterialTest::referenceRippleSeparatesNormalAndWhiteContracts()
{
    // Break caught: reference ripple input falls into the native 42-unit
    // travelling-wave path, ignores signed white events, or colors a zero-U
    // replay. The literals are the fixed source contract: normal/white
    // elevation 4/1, with white receiving its own brightness channel.
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.camera = {0, 38, 10};
    item.parameters.cameraTarget = {0, 0, 0};
    item.parameters.bodyTint = QColor::fromRgbF(0.24F, 0.30F, 0.40F);
    item.parameters.audioLevel = 0;
    item.parameters.heightControl = 0;
    item.parameters.rippleTint = QColor::fromRgbF(0.2F, 0.9F, 1.0F);
    item.parameters.rippleAge = 0.0F;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const auto capture = [&](float strength) {
        item.parameters.rippleStrength = strength;
        const int before = counters->frames;
        item.update();
        QElapsedTimer elapsed; elapsed.start();
        while (counters->frames <= before && !counters->failed && elapsed.elapsed() < 3000)
            QTest::qWait(10);
        return counters->failed ? QImage{} : studyFrame(window);
    };
    const QImage zero = capture(0.0F);
    const QImage normal = capture(1.0F);
    const QImage white = capture(-1.0F);
    const QImage zeroAgain = capture(0.0F);
    item.parameters.rippleEnabled = false;
    const QImage toggleOff = capture(1.0F);
    item.parameters.rippleEnabled = true;
    QVERIFY(!zero.isNull() && !normal.isNull() && !white.isNull() && !zeroAgain.isNull()
             && !toggleOff.isNull());
    const auto hash = [](const QImage& image) {
        return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),
                                                    image.sizeInBytes()), QCryptographicHash::Sha256);
    };
    QCOMPARE(hash(zero), hash(zeroAgain));
    QCOMPARE(hash(zero), hash(toggleOff));
    const FrameComparison normalChange = compareFrames(zero, normal);
    const FrameComparison whiteChange = compareFrames(zero, white);
    qInfo() << "Reference ripple normal/white bounds:"
            << normalChange.secondBounds << whiteChange.secondBounds;
    QVERIFY2(normalChange.secondBounds.height() > whiteChange.secondBounds.height() + 8,
             "Reference normal ripple must use elevation 4, above white elevation 1");
    QVERIFY2(whiteChange.secondBounds.height() > whiteChange.firstBounds.height() + 2,
             "Reference white ripple must retain its own nonzero elevation 1");
    const auto positiveDelta = [](const QImage& off, const QImage& on) {
        QVector3D sum;
        for (int y = 0; y < off.height(); ++y)
            for (int x = 0; x < off.width(); ++x) {
                const QColor a = off.pixelColor(x, y);
                const QColor b = on.pixelColor(x, y);
                sum += QVector3D(std::max(0, b.red() - a.red()),
                                 std::max(0, b.green() - a.green()),
                                 std::max(0, b.blue() - a.blue()));
            }
        return sum / float(off.width() * off.height());
    };
    const QVector3D normalColor = positiveDelta(zero, normal);
    const QVector3D whiteColor = positiveDelta(zero, white);
    qInfo() << "Reference ripple normal/white RGB delta:" << normalColor << whiteColor;
    QVERIFY2(normalColor.z() > normalColor.x() + 0.25F,
             "Normal ripple must use the configured cyan ripple tint");
    QVERIFY2(std::abs(whiteColor.x() - whiteColor.z()) < 1.0F,
             "White ripple must use the independent white channel, not cyan tint");

    // The three existing wave controls remain live in reference mode.  Their
    // neutral values are 1, so this exercises production UBO transport rather
    // than a test-only branch: strength zero is exactly inactive; a wider ring
    // reaches the off-centre point more strongly; faster decay reduces an
    // age-one crest without changing its elapsed age.
    item.parameters.rippleStrength = 0.5F;
    item.parameters.rippleParameters = {0, 1, 1};
    const QImage strengthZero = capture(0.5F);
    QCOMPARE(hash(zero), hash(strengthZero));
    item.parameters.rippleCenter = {0.5F, 0}; item.parameters.rippleAge = 0;
    item.parameters.rippleParameters = {1, 0.2F, 1};
    const QImage narrow = capture(0.5F);
    item.parameters.rippleParameters = {1, 2, 1};
    const QImage wide = capture(0.5F);
    const FrameComparison widthChange = compareFrames(narrow, wide);
    QVERIFY2(widthChange.secondBounds.height() > widthChange.firstBounds.height() + 2,
             "Reference width control must widen the physical Gaussian denominator");
    item.parameters.rippleCenter = {-15, 0}; item.parameters.rippleAge = 1;
    item.parameters.rippleParameters = {1, 1, 0.2F};
    const QImage slowDecay = capture(0.5F);
    item.parameters.rippleParameters = {1, 1, 2};
    const QImage fastDecay = capture(0.5F);
    const FrameComparison decayChange = compareFrames(fastDecay, slowDecay);
    QVERIFY2(decayChange.secondBounds.height() > decayChange.firstBounds.height() + 2,
             "Reference decay-speed control must reduce the age-one physical crest");
    qInfo() << "Reference ripple control bounds width/decay"
            << widthChange.firstBounds << widthChange.secondBounds
            << decayChange.firstBounds << decayChange.secondBounds;
    window.close();
}

void TerrainColumnMaterialTest::discreteBeatDoesNotMoveCanonicalColumns()
{
    QFETCH(int, materialMode);
    QFETCH(float, randomValue);
    QQuickWindow window;
    window.resize(640, 640);
    // Beat illumination is allowed to change. A chromatic backdrop keeps the
    // coverage measurement independent of how brightly the shell is lit.
    window.setColor(QColor(160, 0, 160));
    auto counters = std::make_shared<StudyCounters>();
    ColumnItem item(window.contentItem(), counters);
    item.parameters.material = materialMode;
    item.parameters.randomValue = randomValue;
    item.parameters.runtimeMode = 1.0F;
    item.parameters.stageHalfExtent = 84.0F;
    item.parameters.camera = {0, 6, 50};
    item.parameters.midAudioLevel = 1;
    item.parameters.stream = false;
    item.parameters.beat = 0;
    item.parameters.waveSlot = -1;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage steady = studyFrame(window);
    QVERIFY(!steady.isNull());
    const int previous = counters->frames;
    item.parameters.beat = 1;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > previous || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage beat = studyFrame(window);
    QCOMPARE(beat.size(), steady.size());
    const FrameComparison change = compareFrames(steady, beat);
    qInfo() << "Fixed-band beat0/1 center bounds / coverage mismatch:"
            << change.firstBounds << change.secondBounds << change.silhouetteMismatch;
    QVERIFY2(change.commonVisible > 1000,
             "The center column must be visibly driven by the nonzero bands");
    QVERIFY2(change.firstBounds.height() < 230,
             "Sustained audio makes a tall tower instead of a low floating terrain");
    QVERIFY2(std::abs(change.secondBounds.left() - change.firstBounds.left()) <= 1
             && std::abs(change.secondBounds.right() - change.firstBounds.right()) <= 1,
             "A center transient may lift columns, not expand their width");
    QVERIFY2(std::abs(change.secondBounds.bottom() - change.firstBounds.bottom()) <= 1,
             "The column foot must remain anchored to the fixed ground");
    QVERIFY2(nearlySameBounds(change.firstBounds, change.secondBounds),
             "A discrete beat may change lighting, not canonical column geometry");
    QVERIFY2(change.silhouetteMismatch <= change.commonVisible / 1000,
             "Continuous frequency response is the only runtime column-height source");
}

void TerrainColumnMaterialTest::jellyHeightFollowsContinuousBands()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.material = 1;
    item.parameters.beat = 0;
    item.parameters.audioLevel = 0.35F;
    item.parameters.midAudioLevel = 0.35F;
    item.parameters.camera = {0, 6, 50};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const auto quietBands = studyFrame(window);
    QVERIFY(!quietBands.isNull());
    const int before = counters->frames;
    item.parameters.audioLevel = 1;
    item.parameters.midAudioLevel = 1;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before || counters->failed, 3000);
    QVERIFY(!counters->failed);
    const auto strongBands = studyFrame(window);
    QCOMPARE(strongBands.size(), quietBands.size());
    const auto frames = compareFrames(quietBands, strongBands);
    qInfo() << "Jelly continuous-band center bounds:" << frames.firstBounds << frames.secondBounds;
    QVERIFY(frames.commonVisible > 1000);
    QVERIFY2(frames.secondBounds.height() > frames.firstBounds.height() + 4,
             "Stronger continuous audio must still raise jelly columns without a beat event");
}

void TerrainColumnMaterialTest::innerLightHasOpticalDepthAcrossSmoothFace()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    frameColumnOptics(item.parameters);
    // Isolate optical depth on a fixed cube. The reference height-field tests
    // separately exercise musical geometry; changing it must not move these
    // optical sampling locations or silently change the medium aspect ratio.
    item.parameters.restHeight = 4;
    item.parameters.heightControl = 0;
    item.parameters.lighting = {1, 0, 1};
    item.parameters.stream = false;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const auto frame = studyFrame(window);
    const FrontFace face = locateFrontFace(frame);
    qInfo() << "Optical depth sampled face cap/foot:" << face.cap << face.foot;
    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(frame.save(QDir(directory).filePath("column-optical-depth-samples.png")));
    }
    QVERIFY(face.capContrast >= 3 && face.foot > face.cap + 100);
    const int y = (face.cap + face.foot) / 2;
    const QColor background = frame.pixelColor(20, 20);
    const auto covered = [&](int x, int row) {
        const QColor pixel = frame.pixelColor(x, row);
        return std::abs(pixel.red() - background.red())
             + std::abs(pixel.green() - background.green())
             + std::abs(pixel.blue() - background.blue()) > 10;
    };
    // The lowest silhouette point is the near vertical corner separating the
    // inspected left wall from the right. Use coverage, not a highlight edge.
    int cornerX = -1, wallLeft = -1;
    for (int row = frame.height() - 2; row > y && cornerX < 0; --row) {
        int first = -1, last = -1;
        for (int x = 1; x < frame.width() - 1; ++x) {
            if (!covered(x, row)) continue;
            if (first < 0) first = x;
            last = x;
        }
        if (first >= 0) cornerX = (first + last) / 2;
    }
    for (int x = 1; x < cornerX; ++x) {
        if (covered(x, y)) { wallLeft = x; break; }
    }
    QVERIFY(wallLeft > 0 && cornerX - wallLeft > 80);
    const int wallWidth = cornerX - wallLeft;
    const int leftX = wallLeft + qRound(wallWidth * 0.125);
    const int centerX = wallLeft + qRound(wallWidth * 0.5);
    const int rightX = wallLeft + qRound(wallWidth * 0.875);
    // Preserve the measured patch mean instead of rounding twice through qRgb/qGray.
    const auto preciseLight=[](QVector3D rgb){return (11.0*rgb.x()+16.0*rgb.y()+5.0*rgb.z())/32.0;};
    const double left = preciseLight(meanPatch(frame, leftX - 2, leftX + 3, y - 3, y + 3));
    const double center = preciseLight(meanPatch(frame, centerX - 2, centerX + 3, y - 3, y + 3));
    const double right = preciseLight(meanPatch(frame, rightX - 2, rightX + 3, y - 3, y + 3));
    qInfo() << "Optical depth wall span / sample centers / row:"
            << wallLeft << cornerX << leftX << centerX << rightX << y;
    const double opticalRelief = std::abs(center - (left + right) * 0.5);
    qInfo() << "Optical face relief / samples:" << opticalRelief << left << center << right;
    // Remove the linear projected height ramp: uniform face paint cannot
    // create the nonlinear depth transition at a refracted box exit.
    QVERIFY2(opticalRelief > 1.0,
             "Smooth emitting shell must reveal optical thickness beyond a height ramp");
    const int before = counters->frames;
    item.parameters.lighting.setX(0);
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage unpowered = studyFrame(window);
    const double darkCenter = patchLight(meanPatch(unpowered, centerX-2, centerX+3,y-3,y+3));
    qInfo()<<"Optical center powered/unpowered/background"<<center<<darkCenter<<qGray(background.rgb());
    QVERIFY2(center > darkCenter * 2 && center > qGray(background.rgb()) * 3,
             "Powered inner center must visibly exceed its same-pixel unpowered shell and background");
}

void TerrainColumnMaterialTest::unsupportedDepthMaterialFallsBack()
{
    QFETCH(int, stage);
    auto counters = std::make_shared<StudyCounters>();
    counters->rejectDepthStage = stage;
    QQuickWindow window;
    window.setPersistentSceneGraph(false);
    window.setPersistentGraphics(false);
    window.resize(640, 640);
    window.setColor(Qt::black);
    auto* item = new ColumnItem(window.contentItem(), counters);
    item->parameters.shadows = true;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->failed || counters->frames > 0, 5000);
    QVERIFY2(!counters->failed, "Optional depth sampling failure must retain native material rendering");
    QVERIFY(counters->frames > 0);
    QVERIFY(!counters->shadowAvailable);
    QCOMPARE(counters->materialAttempts.load(), 2);
    const QImage image = window.grabWindow();
    QVERIFY(!image.isNull());
    int litPixels = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) > 12) ++litPixels;
    QVERIFY(litPixels > 1000);
    window.hide();
    window.releaseResources();
    QTRY_COMPARE(counters->live.load(), 0);
}

void TerrainColumnMaterialTest::sharedShadowChangesLightingWithoutMovingArraySilhouette()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    // A neutral receiver can legitimately become as dark as a black backdrop.
    // Use a chromatically distinct backdrop so the silhouette mask measures
    // geometry coverage instead of classifying real shadows as missing pixels.
    window.resize(640, 640); window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.array = true;
    // Exercise actual tall-column occlusion via the supported height control;
    // the default reference centre is deliberately a much shallower relief.
    item.parameters.heightControl = 1.0F;
    item.parameters.shadows = false;
    item.parameters.time = 0.15F;
    item.parameters.stream = false;
    item.parameters.beat = 0;
    item.parameters.lighting = {0, 0, 1};
    item.parameters.tint = QColor::fromRgbF(0.42F, 0.42F, 0.42F);
    item.parameters.camera = {34, 30, 48};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage shadowOff = studyFrame(window);
    QVERIFY2(counters->shadowAvailable, "Native D3D11 fixture must create an actual depth map, not silently exercise fallback");
    const int before = counters->frames;
    item.parameters.shadows = true;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage shadowOn = studyFrame(window);
    qInfo() << "Shadow depth samples / min / max:" << counters->shadowSamples.load()
            << counters->depthMinimum.load() << counters->depthMaximum.load();
    if (qEnvironmentVariableIsSet("AGPLAYER_SHADOW_READBACK")) {
        QCOMPARE(counters->shadowSamples.load(), TerrainShadowMap::resolution * TerrainShadowMap::resolution);
        QVERIFY(counters->depthMinimum.load() > 0 && counters->depthMinimum.load() < 0.99F);
        QCOMPARE(counters->depthMaximum.load(), 1.0F);
    }
    QVERIFY(!shadowOff.isNull() && shadowOff.size() == shadowOn.size());

    const FrameComparison comparison = compareFrames(shadowOff, shadowOn);
    const QString shadowStudyDirectory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!shadowStudyDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(shadowStudyDirectory));
        QVERIFY(shadowOn.save(QDir(shadowStudyDirectory).filePath("receiver-shadow-inspection.png")));
    }
    qInfo() << "Array shadow off/on visible, mismatch, changed, RGB delta, light delta:"
            << comparison.firstVisible << comparison.secondVisible
            << comparison.silhouetteMismatch << comparison.changedCommon
            << comparison.totalRgbDifference << comparison.firstMinusSecondLight;
    QVERIFY2(comparison.commonVisible > 5000, "The 5x5 receiver array must be visible");
    QVERIFY2(nearlySameBounds(comparison.firstBounds, comparison.secondBounds),
             "A shared shadow must not move or resize the array silhouette");
    QVERIFY2(comparison.silhouetteMismatch < comparison.commonVisible / 100,
             "Shadowing may change lighting, not the rendered geometry outline");
    QVERIFY2(comparison.changedCommon > comparison.commonVisible / 100,
             "A constant external visibility of one must fail this shadow test");
    QVERIFY2(comparison.totalRgbDifference > quint64(comparison.commonVisible),
             "The shared map must cause a measurable interior lighting difference");
    QVERIFY2(comparison.firstMinusSecondLight > 0,
             "Enabling the shadow must reduce incident light on real receiver pixels");

    // Located on the front-left column wall in receiver-shadow-inspection.png
    // at the reference height: exclude caps, background and neighbour edges.
    // Archived acne frame: horizontal second-difference RMS 5.69; fixed: 0.
    double planeSecondDifference = 0;
    int planeSamples = 0;
    for (int y = 505; y < 518; ++y) {
        for (int x = 255; x < 270; ++x) {
            const double second = qGray(shadowOn.pixel(x - 1, y))
                                - 2 * qGray(shadowOn.pixel(x, y))
                                + qGray(shadowOn.pixel(x + 1, y));
            planeSecondDifference += second * second;
            ++planeSamples;
        }
    }
    const double planeRms = std::sqrt(planeSecondDifference / planeSamples);
    qInfo() << "Planar wall shadow-acne second-difference RMS:" << planeRms;
    QVERIFY2(planeRms < 1.5,
             "A uniform planar receiver must not acquire repeated shadow-map diagonal stripes");

    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(shadowOff.save(QDir(directory).filePath("column-array-shadow-off.png")));
        QVERIFY(shadowOn.save(QDir(directory).filePath("column-array-shadow-on.png")));
    }
}

void TerrainColumnMaterialTest::globalOpacityDoesNotSwitchShadowAtFiftyFivePercent()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.array = true;
    item.parameters.shadows = true;
    item.parameters.time = 0.15F;
    item.parameters.lighting = {0, 0, 1};
    item.parameters.tint = QColor::fromRgbF(0.42F, 0.42F, 0.42F);
    item.parameters.camera = {34, 30, 48};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    QVERIFY(counters->shadowAvailable);

    // Adjacent equal-sized steps distinguish ordinary alpha blending from a
    // discontinuous caster cutoff specifically between 54% and 56%.
    const std::array<int, 4> percentages{{52, 54, 56, 58}};
    std::array<QVector3D, 4> wallColors;
    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) QVERIFY(QDir().mkpath(directory));
    for (std::size_t i = 0; i < percentages.size(); ++i) {
        const int before = counters->frames;
        item.parameters.opacity = float(percentages[i]) / 100;
        item.update();
        QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
        const QImage frame = studyFrame(window);
        QVERIFY(!frame.isNull());
        wallColors[i] = meanPatch(frame, 337, 365, 440, 535);
        if (!directory.isEmpty())
            QVERIFY(frame.save(QDir(directory).filePath(
                QStringLiteral("column-array-opacity-%1.png").arg(percentages[i]))));
    }
    const auto colorDistance = [](QVector3D a, QVector3D b) {
        const QVector3D d = a - b;
        return (std::abs(d.x()) + std::abs(d.y()) + std::abs(d.z())) / 3.0;
    };
    const double lowerStep = colorDistance(wallColors[0], wallColors[1]);
    const double crossingStep = colorDistance(wallColors[1], wallColors[2]);
    const double upperStep = colorDistance(wallColors[2], wallColors[3]);
    qInfo() << "Shadow opacity equal-step RGB differences:" << lowerStep << crossingStep << upperStep;
    QVERIFY2(crossingStep > 0.1, "The actual opacity input must change the rendered wall");
    QVERIFY2(crossingStep <= 2 * std::max(lowerStep, upperStep) + 0.75,
             "Crossing 55% opacity must not abruptly enable the whole solid shadow map");
}

void TerrainColumnMaterialTest::beatLightTravelsUpInsideFixedColumn()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    frameColumnOptics(item.parameters);
    item.parameters.beat = 0;
    item.parameters.beatAge = 1;
    item.parameters.stream = false;
    item.parameters.lighting = {1, 0, 1};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    // Locate the cap with the beat light off: even at age 1 the nonzero beat
    // still lights the wall. Geometry is independent of this light envelope.
    // The two compared frames below remain strength 1, age 0 and 0.55.
    const QImage baseline = studyFrame(window);
    const FrontFace face = locateFrontFace(baseline);
    QVERIFY(face.cap > 0 && face.foot > face.cap + 100);
    item.parameters.beat = 1;
    item.parameters.beatAge = 0;
    const int baselineFrames = counters->frames;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > baselineFrames, 3000);
    const QImage early = studyFrame(window);
    item.parameters.beatAge = 0.55F;
    const int before = counters->frames;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage late = studyFrame(window);
    QCOMPARE(early.size(), late.size());
    qInfo() << "Fixed-strength rise sampled face cap/foot:" << face.cap << face.foot;
    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(early.save(QDir(directory).filePath("column-fixed-rise-early.png")));
        QVERIFY(late.save(QDir(directory).filePath("column-fixed-rise-late.png")));
    }
    const auto band = [&](const QImage& frame, float fraction) {
        double sum = 0;
        const int centerY = face.cap + qRound((face.foot - face.cap) * fraction);
        for (int y = centerY - 4; y <= centerY + 4; ++y)
            for (int x = 270; x < 300; ++x) sum += qGray(frame.pixel(x, y));
        return sum / 270.0;
    };
    const double upperGain = band(late, 0.25F) - band(early, 0.25F);
    const double lowerRelease = band(early, 0.75F) - band(late, 0.75F);
    for (int i = 2; i <= 18; ++i) {
        const float fraction = float(i) / 20.0F;
        qInfo() << "Fixed-strength rise fraction / late-minus-early:"
                << fraction << band(late, fraction) - band(early, fraction);
    }
    qInfo() << "Upward inner light upper gain/lower release:" << upperGain << lowerRelease;
    QVERIFY2(upperGain > 3, "The inner pulse must reach the upper wall later in the beat");
    QVERIFY2(lowerRelease > 3, "The lower wall must release after the pulse rises");
}

void TerrainColumnMaterialTest::decayingBeatRetainsVisibleUpwardLightTravel()
{
    agplayer::terrain::RendererResourceState lifecycle;
    agplayer::terrain::BeatEventConsumer consumer(lifecycle);
    QVERIFY(consumer.consume({1.0F, 1}, 0.0F));
    const std::array<agplayer::terrain::BeatPulseSnapshot, 2> pulses{
        consumer.snapshot(0.0F), consumer.snapshot(0.18F)};
    QVERIFY(pulses[0].active && pulses[1].active);
    QVERIFY(pulses[1].strength < pulses[0].strength * 0.3F);
    QVERIFY(pulses[1].age > pulses[0].age);

    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    frameColumnOptics(item.parameters);
    item.parameters.stream = false;
    item.parameters.lighting = {1, 0, 1};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);

    std::array<double, 2> centers{}, peakGains{};
    for (std::size_t phase = 0; phase < pulses.size(); ++phase) {
        item.parameters.beat = pulses[phase].strength;
        std::array<QImage, 2> frames;
        for (int sample = 0; sample < 2; ++sample) {
            // Hold the real, decayed strength and resulting geometry fixed
            // within this pair. Age=1 is the finished upward-light state;
            // subtraction isolates the traveling light from steady emission.
            item.parameters.beatAge = sample == 0 ? pulses[phase].age : 1.0F;
            const int before = counters->frames;
            item.update();
            QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
            frames[std::size_t(sample)] = studyFrame(window);
            QVERIFY(!frames[std::size_t(sample)].isNull());
        }
        QCOMPARE(frames[0].size(), frames[1].size());
        const FrontFace face = locateFrontFace(frames[1]);
        QVERIFY(face.cap > 0 && face.foot > face.cap + 100);
        double weight = 0, weightedPosition = 0;
        for (int band = 2; band <= 18; ++band) {
            const double fraction = band / 20.0;
            const int yCenter = face.cap + qRound((face.foot - face.cap) * fraction);
            double gain = 0;
            for (int y = yCenter - 3; y <= yCenter + 3; ++y)
                for (int x = 270; x < 300; ++x)
                    gain += qGray(frames[0].pixel(x, y)) - qGray(frames[1].pixel(x, y));
            gain /= 210.0;
            peakGains[phase] = std::max(peakGains[phase], gain);
            // Ignore one-code-value quantization when locating the visible
            // light band, without assuming any shader shape or trajectory.
            const double visibleGain = std::max(0.0, gain - 1.0);
            weight += visibleGain;
            weightedPosition += visibleGain * fraction;
        }
        qInfo() << "Real beat phase/strength/peak light gain:"
                << pulses[phase].age << pulses[phase].strength << peakGains[phase];
        QVERIFY2(peakGains[phase] > 3.0,
                 "Both real beat snapshots need a visibly distinct inner light band");
        QVERIFY(weight > 0);
        centers[phase] = weightedPosition / weight;
    }
    qInfo() << "Real decaying beat light centroid, footward fraction:" << centers[0] << centers[1];
    QVERIFY2(centers[0] > 0.55, "The detected beat must first illuminate the lower wall");
    QVERIFY2(centers[1] < 0.50, "The decaying beat must carry light into the upper wall");
    QVERIFY2(centers[0] - centers[1] > 0.25,
             "Visible light must travel upward, not merely dim on the same part of the wall");
}

void TerrainColumnMaterialTest::audioDrivesInnerLightWithoutWashingOutShell()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    const QColor background(160, 0, 160);
    window.resize(640, 640); window.setColor(background);
    ColumnItem item(window.contentItem(), counters);
    item.parameters.material = 1;
    // Dark-theme base2 is independent of the bright cyan emitter. Encoded
    // values below represent reference Nocturnal linear(.03,.05,.09).
    item.parameters.bodyTint = QColor::fromRgbF(.18974828F,.24780053F,.33183002F);
    item.parameters.time = 0.15F;
    item.parameters.stream = false;
    item.parameters.audioLevel = 0;
    item.parameters.beat = 0;
    item.parameters.lighting = {1, 0, 1};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage silence = studyFrame(window);
    const int before = counters->frames;
    item.parameters.audioLevel = 1;
    item.parameters.beat = 1;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage music = studyFrame(window);
    QVERIFY(!silence.isNull() && silence.size() == music.size());
    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(silence.save(QDir(directory).filePath("column-inner-silence.png")));
        QVERIFY(music.save(QDir(directory).filePath("column-inner-music.png")));
    }

    // Audio legitimately changes column height. Measure only rasterized
    // interiors, not an obsolete fixed-height ROI or total screen brightness.
    const auto inside = [background](const QImage& frame, int x, int y) {
        const auto covered = [background](QColor c) {
            return std::abs(c.red() - background.red())
                 + std::abs(c.green() - background.green())
                 + std::abs(c.blue() - background.blue()) > 80;
        };
        return covered(frame.pixelColor(x, y)) && covered(frame.pixelColor(x - 1, y))
            && covered(frame.pixelColor(x + 1, y)) && covered(frame.pixelColor(x, y - 1))
            && covered(frame.pixelColor(x, y + 1));
    };
    double silenceLight = 0, musicLight = 0;
    int silenceCount = 0, musicCount = 0, commonCount = 0, nearWhite = 0, colored = 0;
    for (int y = 1; y < music.height() - 1; ++y) {
        for (int x = 1; x < music.width() - 1; ++x) {
            const bool inSilence = inside(silence, x, y);
            const bool inMusic = inside(music, x, y);
            if (inSilence) { silenceLight += qGray(silence.pixel(x, y)); ++silenceCount; }
            if (inSilence && inMusic) ++commonCount;
            if (inMusic) {
                const QColor c = music.pixelColor(x, y);
                musicLight += qGray(c.rgb()); ++musicCount;
                if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) ++nearWhite;
                if (c.blue() > c.red() + 8 && c.green() > c.red() + 8) ++colored;
            }
        }
    }
    qInfo() << "Audio-driven inner source visible/common pixels:" << silenceCount << musicCount << commonCount;
    QVERIFY(silenceCount > 20 && musicCount > 1000 && commonCount > 10);
    silenceLight /= silenceCount;
    musicLight /= musicCount;
    qInfo() << "Silence/music mean light and near-white fraction:"
            << silenceLight << musicLight << double(nearWhite) / musicCount;
    QVERIFY2(musicLight > silenceLight * 1.5 && musicLight > silenceLight + 8,
             "The internal source must brighten materially with audio, not remain an always-on studio light");
    QVERIFY2(silenceLight < musicLight * 0.35,
             "Ambient light must remain subordinate to the musical internal source");
    QVERIFY2(double(nearWhite) / musicCount < 0.03, "A full beat must retain shell detail, not clip to white");
    QVERIFY2(colored > musicCount / 3, "The musical inner source must preserve its cyan material color");
}

void TerrainColumnMaterialTest::neutralGrayWithoutInnerLightRemainsNeutral()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.array = true;
    item.parameters.shadows = true;
    item.parameters.time = 0.15F;
    item.parameters.material = 0;
    item.parameters.softness = 0.55F;
    item.parameters.lighting = {0, 0, 1};
    item.parameters.tint = QColor::fromRgbF(0.46F, 0.46F, 0.46F);
    item.parameters.camera = {34, 30, 48};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage frame = studyFrame(window);
    QVERIFY(!frame.isNull());
    const QColor background = frame.pixelColor(20, 20);
    quint64 red = 0, green = 0, blue = 0;
    int visible = 0, nearWhite = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QColor c = frame.pixelColor(x, y);
            if (std::abs(c.red() - background.red())
                    + std::abs(c.green() - background.green())
                    + std::abs(c.blue() - background.blue()) <= 10) continue;
            red += quint64(c.red()); green += quint64(c.green()); blue += quint64(c.blue());
            ++visible;
            if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) ++nearWhite;
        }
    }
    QVERIFY2(visible > 5000, "The neutral 5x5 array must remain visible without inner light");
    const double meanRed = double(red) / visible;
    const double meanGreen = double(green) / visible;
    const double meanBlue = double(blue) / visible;
    const double channelSpread = std::max({meanRed, meanGreen, meanBlue})
                               - std::min({meanRed, meanGreen, meanBlue});
    qInfo() << "Neutral no-inner-light RGB/spread/white fraction:"
            << meanRed << meanGreen << meanBlue << channelSpread
            << double(nearWhite) / visible;
    QVERIFY2(channelSpread < 18.0, "Neutral gray must not acquire a colored emissive core");
    QVERIFY2(double(nearWhite) / visible < 0.03,
             "Neutral material without inner light must retain highlight headroom");
    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(frame.save(QDir(directory).filePath("column-neutral-gray-no-inner-light.png")));
    }
}

void TerrainColumnMaterialTest::softnessChangesLuminousCapRolloff()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    // With the newly required dim room, an unlit face can match black.
    // Keep geometry classification independent of reflection brightness.
    window.resize(640, 640); window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.material = 0;
    item.parameters.time = 0.15F;
    item.parameters.lighting = {0, 0, 1};
    item.parameters.tint = QColor::fromRgbF(0.08F, 0.55F, 0.72F);
    item.parameters.softness = 0.05F;
    item.parameters.camera = {0,14,6};
    item.parameters.cameraTarget = {0,0,0};
    item.parameters.audioLevel = 0;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage polished = studyFrame(window);

    int before = counters->frames;
    item.parameters.softness = 0.95F;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage rough = studyFrame(window);
    const FrameComparison roughness = compareFrames(polished, rough);
    qInfo() << "Roughness reflected pixels / visible / RGB delta:"
            << roughness.changedCommon << roughness.commonVisible << roughness.totalRgbDifference;
    QVERIFY2(nearlySameBounds(roughness.firstBounds, roughness.secondBounds),
             "Softness must change cap rolloff, not column geometry");
    QVERIFY2(roughness.changedCommon > roughness.commonVisible / 100,
             "Softness must measurably broaden the luminous cap edge");
    QVERIFY2(roughness.totalRgbDifference > quint64(roughness.commonVisible / 4),
             "Cap edge softness produced no measurable pixel response");

    before = counters->frames;
    item.parameters.softness = 0.05F;
    item.parameters.camera.setX(-item.parameters.camera.x());
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage opposite = studyFrame(window).mirrored(true, false);
    const FrameComparison view = compareFrames(polished, opposite);
    qInfo() << "Roughness/view changed pixels and RGB delta:"
            << roughness.changedCommon << roughness.totalRgbDifference
            << view.changedCommon << view.totalRgbDifference;
    QVERIFY2(nearlySameBounds(view.firstBounds, view.secondBounds, 2),
             "Mirrored opposite views of the symmetric column must retain its silhouette");
    // Flat boxes retain only the approved weak environment reflection; there
    // is no longer a broad moving radial/studio highlight to require here.
    // A symmetric silent solid must not invent a camera-dependent studio spot.
    QVERIFY2(view.totalRgbDifference < quint64(view.commonVisible * 2),
             "Silent cap must remain stable without a studio reflection");

    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) {
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(polished.save(QDir(directory).filePath("column-cyan-polished.png")));
        QVERIFY(rough.save(QDir(directory).filePath("column-cyan-rough.png")));
        QVERIFY(opposite.save(QDir(directory).filePath("column-cyan-opposite-view-mirrored.png")));
    }
}

void TerrainColumnMaterialTest::lightControlsReachNativeMaterial()
{
    QFETCH(int, lane);
    const bool highOnly = lane == 3;
    if (highOnly) lane = 0;
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(lane == 1 ? QColor(160, 0, 160) : QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    if (lane == 2) frameColumnOptics(item.parameters);
    if (lane == 1) {
        item.parameters.array = true;
        item.parameters.heightControl = 1.0F;
        item.parameters.camera = {34, 30, 48};
    }
    item.parameters.lighting[lane] = lane == 2 ? 0.2F : 0;
    // Spill is powered by music, not an always-on point light. Compare both
    // control endpoints at the same fixed beat, with identical geometry.
    item.parameters.beat = highOnly ? 0.0F : 0.5F;
    item.parameters.lowAudioLevel = highOnly ? 0.0F : 1.0F;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage low = studyFrame(window);
    const int generation = counters->generations;
    const int before = counters->frames;
    item.parameters.lighting[lane] = 2;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage high = studyFrame(window);
    QVERIFY(!low.isNull() && low.size() == high.size());
    if (lane == 1) {
        const FrameComparison receivers = compareFrames(low, high);
        qInfo() << "Spill receiver visible/common/mismatch/changed/RGB/light delta:"
                << receivers.firstVisible << receivers.commonVisible
                << receivers.silhouetteMismatch << receivers.changedCommon
                << receivers.totalRgbDifference << receivers.firstMinusSecondLight;
        QVERIFY2(receivers.commonVisible > 80000,
                 "Spill must be tested on a real neighbouring receiver array, not one source column");
        QVERIFY2(nearlySameBounds(receivers.firstBounds, receivers.secondBounds),
                 "Spill may light receiver surfaces without moving their silhouette");
        QVERIFY2(receivers.silhouetteMismatch < receivers.commonVisible / 100,
                 "Spill must not appear by changing receiver geometry coverage");
        QVERIFY2(receivers.changedCommon > receivers.commonVisible / 100,
                 "Neighbouring receiver pixels must visibly respond to column spill");
        QVERIFY2(receivers.changedCommon < receivers.commonVisible * 3 / 4,
                 "Column spill must remain local instead of washing the whole receiver array");
        QVERIFY2(receivers.totalRgbDifference > quint64(receivers.commonVisible),
                 "The local spill response must exceed native readback quantization noise");
        QVERIFY2(receivers.firstMinusSecondLight < 0,
                 "Increasing spill must brighten, never darken, neighbouring receivers overall");
        const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(low.save(QDir(directory).filePath("column-array-light-spill-off.png")));
            QVERIFY(high.save(QDir(directory).filePath("column-array-light-spill-on.png")));
        }
        // Spill is deliberately wall-only: the array-wide assertions above
        // measure the real receiver surfaces. The generic rectangular ROI
        // below contains mostly cap pixels and is appropriate only for the
        // inner-source/radius controls that are allowed to affect caps.
        QCOMPARE(counters->generations.load(), generation);
        return;
    }
    QRect sample(230, 200, 165, 300);
    if (lane == 2) {
        // Measure the actual wall, not a fixed rectangle diluted by background
        // after the center-height contract changes. Keep the >1 code-value
        // mean response requirement identical for every lighting control.
        const FrontFace face = locateFrontFace(high);
        QVERIFY(face.cap > 0 && face.foot > face.cap + 100);
        const int height = face.foot - face.cap;
        sample = QRect(265, face.cap + qRound(height * 0.20),
                       40, qRound(height * 0.65));
    }
    quint64 difference = 0;
    for (int y = sample.top(); y <= sample.bottom(); ++y)
        for (int x = sample.left(); x <= sample.right(); ++x)
            difference += std::abs(qGray(low.pixel(x, y)) - qGray(high.pixel(x, y)));
    const int samples = sample.width() * sample.height();
    qInfo() << "Native light control lane / ROI / mean difference:"
            << lane << sample << double(difference) / samples;
    QVERIFY2(difference > quint64(samples),
             "A lighting slider must visibly affect the rendered surface at fixed geometry/time");
    QCOMPARE(counters->generations.load(), generation);
}

void TerrainColumnMaterialTest::columnSpillDoesNotPaintTopCaps()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.camera = {0, 20, 8};
    item.parameters.cameraTarget = {0, 0, 0};
    item.parameters.audioLevel = 1.0F;
    item.parameters.lowAudioLevel = 1.0F;
    item.parameters.lighting = {1.0F, 0.0F, 1.0F};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage spillOff = studyFrame(window);
    const int before = counters->frames;
    item.parameters.lighting.setY(2.0F);
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before || counters->failed, 3000);
    QVERIFY(!counters->failed);
    const QImage spillOn = studyFrame(window);
    const FrameComparison cap = compareFrames(spillOff, spillOn);
    qInfo() << "Top-cap spill changed/common/RGB delta:"
            << cap.changedCommon << cap.commonVisible << cap.totalRgbDifference;
    QVERIFY2(cap.commonVisible > 1000,
             "The top-cap spill regression must inspect a visible cap");
    QVERIFY2(nearlySameBounds(cap.firstBounds, cap.secondBounds),
             "Changing received-column spill must not move the top cap");
    QVERIFY2(cap.changedCommon < std::max(1, cap.commonVisible / 100),
             "Received-column spill must not paint a projector-shaped patch across top caps");
}

void TerrainColumnMaterialTest::brightThemeCapsRetainHeadroom()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(QColor(QStringLiteral("#D8E6EA")));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.camera = {0, 20, 8};
    item.parameters.cameraTarget = {0, 0, 0};
    item.parameters.material = 0;
    item.parameters.audioLevel = 0.0F;
    item.parameters.lowAudioLevel = 0.0F;
    item.parameters.heightControl = 0.0F;
    item.parameters.restHeight = 4.0F;
    item.parameters.tint = QColor(QStringLiteral("#2D8EA3"));
    item.parameters.bodyTint = QColor(QStringLiteral("#E5EEF0"));
    item.parameters.lighting = {0.0F, 0.0F, 1.0F};
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage frame = studyFrame(window);
    const QVector3D center = meanPatch(frame, 300, 340, 300, 340);
    qInfo() << "Bright-theme quiet cap mean RGB:" << center;
    QVERIFY2(std::max({center.x(), center.y(), center.z()}) < 240.0F,
             "A bright theme cap must retain highlight headroom instead of clipping to white");
    QVERIFY2(center.z() - center.x() > 3.0F,
             "A bright theme cap must retain its cool material tint after exposure control");
}

void TerrainColumnMaterialTest::smoothInteriorRemainsStable()
{
    QFETCH(int, materialMode);
    QFETCH(bool, rainbow);
    const QString imagePrefix = QStringLiteral("column-%1%2").arg(materialMode)
        .arg(rainbow ? QStringLiteral("-rainbow") : QString());
    auto counters = std::make_shared<StudyCounters>();
    double structureChange[2]{};
    double steadyInteriorLight = 0;
    {
        QQuickWindow window;
        window.resize(640, 640); window.setColor(QColor(3, 5, 9));
        ColumnItem item(window.contentItem(), counters);
        item.parameters.material = materialMode;
        item.parameters.rainbow = rainbow;
        window.show();
        QVERIFY(waitForStudyWindow(window));
        QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
        QVERIFY(!counters->failed);
        QCOMPARE(counters->live.load(), 1);
        const int generations = counters->generations;
        const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
        if (!directory.isEmpty()) QVERIFY(QDir().mkpath(directory));
        for (int stream = 0; stream < 2; ++stream) {
            QImage first;
            QRect interior;
            for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
                item.parameters.stream = stream != 0;
                // A short continuous observation, not two separate flashes.
                item.parameters.time = float(frameIndex) * 0.15F;
                const int before = counters->frames;
                item.update();
                QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
                const QImage frame = studyFrame(window);
                QVERIFY(!frame.isNull());
                if (first.isNull()) {
                    first = frame;
                    const FrontFace face = locateFrontFace(first);
                    QVERIFY(face.capContrast >= 3 && face.foot > face.cap + 100);
                    const int height = face.foot - face.cap;
                    interior = QRect(245, face.cap + height / 5, 75, height / 2);
                }
                double sum = 0, squares = 0;
                for (int y = interior.top(); y <= interior.bottom(); ++y) {
                    for (int x = interior.left(); x <= interior.right(); ++x) {
                        const double delta = qGray(frame.pixel(x, y)) - qGray(first.pixel(x, y));
                        sum += delta; squares += delta * delta;
                    }
                }
                const double count = interior.width() * interior.height();
                if (stream == 1 && frameIndex == 1) {
                    const FrontFace face = locateFrontFace(frame);
                    QVERIFY(face.cap > 0 && face.foot > face.cap + 100);
                    steadyInteriorLight = face.coreLight;
                    qInfo() << "Single front face cap/foot/upper/middle/lower/edge/rolloff:"
                            << face.cap << face.foot << face.upper << face.middle << face.lower
                            << face.capContrast << face.horizontalContrast;
                    if (!rainbow) {
                        QVERIFY2(patchLight(face.upper) > patchLight(face.middle) * 1.15,
                                 "The same monochrome column must fade from bright upper body to middle");
                        QVERIFY2(patchLight(face.middle) > patchLight(face.lower),
                                 "The dim internal source must retain its vertical light gradient");
                        QVERIFY2(face.capContrast >= 3, "The cap must remain distinct from the side");
                    }
                    QVERIFY2(patchLight(face.upper) > patchLight(face.lower) + 4,
                             "The light core must remain brighter above its absorbing foot");
                    if (materialMode == 1) {
                        const int y = (face.cap + face.foot) / 2;
                        double previous = -1;
                        for (int x = 235; x <= 315; x += 10) {
                            const double current = patchLight(meanPatch(frame, x, x + 5, y - 3, y + 3));
                            QVERIFY2(current + 2 >= previous,
                                     "Flat box optical depth must not create a central radial peak");
                            previous = current;
                        }
                    }
                }
                // Remove the temporal DC component: uniform brightening is
                // not evidence that a texture moved inside the column.
                const double residual = std::sqrt(std::max(0.0, squares / count - std::pow(sum / count, 2)));
                structureChange[stream] = std::max(structureChange[stream], residual);
                int visible = 0, nearWhite = 0;
                for (int y = 0; y < frame.height(); ++y) {
                    for (int x = 0; x < frame.width(); ++x) {
                        const QColor c = frame.pixelColor(x, y);
                        if (qGray(c.rgb()) > 15) {
                            ++visible;
                            if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) ++nearWhite;
                        }
                    }
                }
                QVERIFY2(visible > 10000 && visible < 200000, "Single column must retain a bounded visible silhouette");
                QVERIFY2(double(nearWhite) / visible < 0.03, "Column highlights must not wash out its colored shell");
                QVERIFY(qGray(frame.pixel(20, 20)) < 10);
                if (rainbow && stream == 1 && frameIndex == 1) {
                    // Three patches of the same inspected face, away from its
                    // top/rim/foot. Compare chromaticity, not mere brightness.
                    const FrontFace face = locateFrontFace(frame);
                    const QVector3D upper = face.upper, middle = face.middle, lower = face.lower;
                    qInfo() << "Rainbow upper/middle/lower RGB:" << upper << middle << lower;
                    const auto chroma = [](QVector3D c) { return c / std::max(1.0F, c.x() + c.y() + c.z()); };
                    const auto separation = [&chroma](QVector3D a, QVector3D b) {
                        const QVector3D d = chroma(a) - chroma(b);
                        return std::abs(d.x()) + std::abs(d.y()) + std::abs(d.z());
                    };
                    QVERIFY2(separation(upper, middle) > 0.12F
                             && separation(middle, lower) > 0.12F
                             && separation(upper, lower) > 0.12F,
                             "Rainbow needs distinct upper/middle/lower hues, not a brightness-only gradient");
                    for (const QVector3D c : {upper, middle, lower})
                        QVERIFY2(std::max({c.x(), c.y(), c.z()}) - std::min({c.x(), c.y(), c.z()}) > 12,
                                 "Rainbow interior must retain color rather than becoming neutral white");
                }
                if (!directory.isEmpty())
                    QVERIFY(frame.save(QDir(directory).filePath(QStringLiteral("%1-%2-%3.png")
                        .arg(imagePrefix).arg(stream).arg(frameIndex))));
            }
        }
        qInfo() << "Smooth interior temporal RMS off/on:" << structureChange[0] << structureChange[1];
        QVERIFY2(structureChange[1] < 0.25,
                 "Smooth inner light must not contain moving internal texture");
        if (!directory.isEmpty()) {
            // Keep time fixed for the alternate view: these images are visual
            // evidence only, not a false parallax assertion on changed lighting.
            item.parameters.time = 0.15F;
            int before = counters->frames;
            item.update();
            QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
            QVERIFY(studyFrame(window).save(QDir(directory).filePath(imagePrefix + "-view-reference.png")));
            item.parameters.camera.setX(-15);
            before = counters->frames;
            item.update();
            QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
            QVERIFY(studyFrame(window).save(QDir(directory).filePath(
                imagePrefix + "-opposite-view.png")));
        }
        item.parameters.camera.setX(15);
        item.parameters.time = 0.15F;
        item.parameters.beat = 1;
        const int beforeBeat = counters->frames;
        item.update();
        QTRY_VERIFY_WITH_TIMEOUT(counters->frames > beforeBeat, 3000);
        const QImage beatFrame = studyFrame(window);
        QVERIFY(!beatFrame.isNull());
        int beatVisible = 0, beatWhite = 0;
        for (int y = 0; y < beatFrame.height(); ++y)
            for (int x = 0; x < beatFrame.width(); ++x) {
                const QColor c = beatFrame.pixelColor(x, y);
                if (qGray(c.rgb()) > 15) {
                    ++beatVisible;
                    if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) ++beatWhite;
                }
            }
        QVERIFY(beatVisible > 10000);
        QVERIFY2(double(beatWhite) / beatVisible < 0.03, "Maximum beat must not wash out the colored column");
        const FrontFace beatFace = locateFrontFace(beatFrame);
        QVERIFY(beatFace.cap > 0 && beatFace.foot > beatFace.cap + 100);
        const double beatInteriorLight = beatFace.coreLight;
        qInfo() << "Steady/maximum beat interior light:" << steadyInteriorLight << beatInteriorLight;
        QVERIFY2(beatInteriorLight > steadyInteriorLight * 1.05,
                 "A beat must brighten the smooth inner light core without adding texture");
        if (!directory.isEmpty())
            QVERIFY(beatFrame.save(QDir(directory).filePath(imagePrefix + "-maxbeat.png")));
        QCOMPARE(counters->generations.load(), generations);
        window.close();
    }
    QTRY_COMPARE_WITH_TIMEOUT(counters->live.load(), 0, 3000);
    qInfo() << "Interior temporal structure RMS, stream off/on:" << structureChange[0] << structureChange[1];
    QVERIFY2(structureChange[0] < 0.25, "Without stream, the fixed interior must remain stable");
    QVERIFY2(structureChange[1] < 0.25,
             "High-frequency top flashes must not introduce moving texture inside the smooth column");
}

void TerrainColumnMaterialTest::everyColumnHasLocalCapFlash()
{
    QFETCH(float, randomValue);
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    frameColumnOptics(item.parameters);
    item.parameters.randomValue = randomValue;
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage reference = studyFrame(window);
    QVERIFY(!reference.isNull());
    const FrontFace face = locateFrontFace(reference);
    QVERIFY(face.cap > 0 && face.foot > face.cap + 100);
    QVERIFY2(face.capContrast >= 3, "Every column must retain a clear top/side boundary");
    const QColor background = reference.pixelColor(20, 20);
    QList<QPoint> capPixels;
    // Each random value changes the physical height. Build an independent cap
    // mask from that column's static image, excluding background and both rims.
    for (int x = 245; x < 325; ++x) {
        int first = -1;
        for (int y = 10; y < face.foot; ++y) {
            const QColor c = reference.pixelColor(x, y);
            if (std::abs(c.red() - background.red()) + std::abs(c.green() - background.green())
                    + std::abs(c.blue() - background.blue()) > 10) { first = y; break; }
        }
        if (first < 0) continue;
        // With a clear shell, brightness gradients locate the internal source,
        // not a geometric top/side boundary. This inspected 4-unit box at the
        // fixed study camera has >20px cap depth throughout this x interval.
        // Sample a strip across its width, inset from the silhouette edges.
        for (int y = first + 8; y < first + 18; ++y)
            capPixels.append(QPoint(x, y));
    }
    QVERIFY2(capPixels.size() > 400, "The cap mask must contain a real interior patch, not background");
    const int sideHeight = face.foot - face.cap;
    const QRect side(265, face.cap + sideHeight / 4, 40, sideHeight * 2 / 5);
    double topRms[2]{}, sideRms[2]{};
      double bestLocalCoverage = 0;
      QVector<bool> flashed(capPixels.size(), false);
    double topMeanChange = 0;
    const QString directory = qEnvironmentVariable("AGPLAYER_COLUMN_STUDY_DIR");
    if (!directory.isEmpty()) QVERIFY(QDir().mkpath(directory));
    for (int stream = 0; stream < 2; ++stream) {
        QImage first;
        for (int sample = 0; sample < 12; ++sample) {
            item.parameters.stream = stream != 0;
            item.parameters.time = float(sample) * 0.35F;
            const int before = counters->frames;
            item.update();
            QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
            const QImage frame = studyFrame(window);
            QVERIFY(!frame.isNull());
            if (first.isNull()) first = frame;
            double sum = 0, squares = 0;
            int changed = 0, nearWhite = 0, colored = 0;
              int pixelIndex = 0;
              for (const QPoint p : capPixels) {
                const QColor c = frame.pixelColor(p);
                const double delta = qGray(c.rgb()) - qGray(first.pixel(p));
                sum += delta; squares += delta * delta;
                  if (std::abs(delta) > 3) ++changed;
                  if (stream == 1 && std::abs(delta) > 3) flashed[pixelIndex] = true;
                  ++pixelIndex;
                if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) ++nearWhite;
                if (std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()}) > 12) ++colored;
            }
            const double count = capPixels.size();
            const double residual = std::sqrt(std::max(0.0, squares / count - std::pow(sum / count, 2)));
            topRms[stream] = std::max(topRms[stream], residual);
            if (stream == 1) {
                bestLocalCoverage = std::max(bestLocalCoverage, changed / count);
                topMeanChange = std::max(topMeanChange, std::abs(sum / count));
            }
            QVERIFY2(nearWhite / count < 0.03, "A top flash must not whiten the cap");
            QVERIFY2(colored / count > 0.75, "Most of the cap must preserve its material color");
            sum = 0; squares = 0;
            for (int y = side.top(); y <= side.bottom(); ++y)
                for (int x = side.left(); x <= side.right(); ++x) {
                    const double delta = qGray(frame.pixel(x, y)) - qGray(first.pixel(x, y));
                    sum += delta; squares += delta * delta;
                }
            const double sideCount = side.width() * side.height();
            sideRms[stream] = std::max(sideRms[stream],
                std::sqrt(std::max(0.0, squares / sideCount - std::pow(sum / sideCount, 2))));
            if (!directory.isEmpty() && stream == 1)
                QVERIFY(frame.save(QDir(directory).filePath(QStringLiteral("cap-random-%1-%2.png")
                    .arg(qRound(randomValue * 100)).arg(sample))));
        }
    }
    qInfo() << "Random/cap pixels/top RMS off-on/side RMS off-on/local coverage:"
            << randomValue << capPixels.size() << topRms[0] << topRms[1]
            << sideRms[0] << sideRms[1] << bestLocalCoverage;
    if (randomValue >= 0.25F) {
        QVERIFY2(topRms[1] < 0.25, "The unselected three quarters must retain a steady cap");
        QVERIFY(sideRms[0] < 0.25 && sideRms[1] < 0.25);
        return;
    }
      QVERIFY2(topRms[0] < 0.25, "The cap must be stable with stream disabled");
    QVERIFY2(topMeanChange > 3, "Every column needs a visible face-wide top flash");
    QVERIFY2(topRms[1] > 4, "Fine glints must have visible contrast on the flashing plane");
      const double temporalCoverage = double(std::count(flashed.begin(), flashed.end(), true))
                                    / flashed.size();
      QVERIFY2(temporalCoverage > 0.80,
               "Twinkles must visit the whole cap over time, not a narrow glowing line");
      QVERIFY2(bestLocalCoverage > 0.90,
               "A selected column must flash across its whole cap in the same frame");
    QVERIFY2(sideRms[0] < 0.25 && sideRms[1] < 0.25,
             "Top flashes must not create texture inside the smooth column");
}

void TerrainColumnMaterialTest::referenceThemeTopFlashesUseHashAndStreamGate()
{
    // These locations are captured from the fixed reference shader's actual
    // vertex hash probe.  Keep instanceData.z deliberately unselected: the
    // reference theme must select from its grid-position hash, not the legacy
    // per-instance stream field.
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.bodyTint = QColor::fromRgbF(0.24F, 0.30F, 0.40F);
    item.parameters.tint = QColor::fromRgbF(0.04F, 0.10F, 0.20F);
    item.parameters.randomValue = 0.90F;
    item.parameters.explicitHighBands = true;
    item.parameters.restHeight = 1.0F;
    item.parameters.heightControl = 0.0F;
    item.parameters.time = 0.10F;
    const auto frameAt = [&](QVector3D position, QVector4D highBands, bool stream) {
        item.parameters.position = position;
        item.parameters.cameraTarget = position + QVector3D(0, 3, 0);
        item.parameters.camera = position + QVector3D(9.75F, 9.5F, 15.6F);
        item.parameters.highBands = highBands;
        item.parameters.stream = stream;
        const int before = counters->frames;
        item.update();
        QElapsedTimer elapsed;
        elapsed.start();
        while (counters->frames <= before && !counters->failed && elapsed.elapsed() < 3000)
            QTest::qWait(10);
        if (counters->frames <= before || counters->failed) return QImage{};
        const QImage frame = studyFrame(window);
        return frame;
    };
    const auto topDifference = [](const QImage& first, const QImage& second) {
        const FrontFace face = locateFrontFace(first);
        if (face.cap <= 12) return -1.0;
        double total = 0;
        int count = 0;
        for (int y = std::max(0, face.cap - 35); y <= face.cap + 5; ++y)
            for (int x = 245; x < 325; ++x) {
                const QColor a = first.pixelColor(x, y);
                const QColor b = second.pixelColor(x, y);
                total += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green())
                       + std::abs(a.blue() - b.blue());
                ++count;
            }
        return total / std::max(1, count);
    };
    window.show();
    QVERIFY(waitForStudyWindow(window));
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);

    // Reference hash: (-32, 9) is presence-selected (fract(hash * 53) =
    // .990235...), while (-32, -31) is not (.374999...).
    const QVector4D presenceBands(0, 1, 0, 0);
    const QImage presenceOff = frameAt({-32, 0, 9}, presenceBands, false);
    const QImage presenceOn = frameAt({-32, 0, 9}, presenceBands, true);
    const QImage absentOff = frameAt({-32, 0, -31}, presenceBands, false);
    const QImage absentOn = frameAt({-32, 0, -31}, presenceBands, true);
    QVERIFY(!presenceOff.isNull() && !presenceOn.isNull()
             && !absentOff.isNull() && !absentOn.isNull());
    const double presenceDelta = topDifference(presenceOff, presenceOn);
    const double absentDelta = topDifference(absentOff, absentOn);
    qInfo() << "Reference presence top delta selected/unselected:" << presenceDelta << absentDelta;
    QVERIFY2(presenceDelta > 2.0,
             "The reference presence-selected cell must flash across the top plane");
    QVERIFY2(absentDelta < presenceDelta * 0.30,
             "A reference presence-unselected cell must not inherit the flash");

    // Reference hash: (-32, -14) is air-selected (fract(hash * 31) =
    // .989746...).  With no height lift it must still activate the low cap;
    // the physical bounds may not change.
    const QVector4D airBands(0, 0, 0, 1);
    const QImage airOff = frameAt({-32, 0, -14}, airBands, false);
    const QImage airOn = frameAt({-32, 0, -14}, airBands, true);
    QVERIFY(!airOff.isNull() && !airOn.isNull());
    const FrameComparison airComparison = compareFrames(airOff, airOn);
    const double airDelta = topDifference(airOff, airOn);
    qInfo() << "Reference air low-cap delta/bounds:" << airDelta
            << airComparison.firstBounds << airComparison.secondBounds;
    QVERIFY2(airDelta > 2.0, "Air-selected low column must light its top without elevation");
    QVERIFY2(nearlySameBounds(airComparison.firstBounds, airComparison.secondBounds),
             "Air flash is material-only and must not alter the low-column silhouette");

    // Make capGlow exactly bodyTint, so this small air input has a known rim
    // direction: source rim .8 * (.2 + .3) replaces quiet rim .24.  Replacing
    // the whole bodyLight first would discard the quiet rim and then subtract
    // it again, visibly darkening this selected edge instead of brightening it.
    item.parameters.glow = 1.0F;
    const QVector4D subtleAirBands(0, 0, 0, 0.10F);
    const QImage subtleAirOff = frameAt({-32, 0, -14}, subtleAirBands, false);
    const QImage subtleAirOn = frameAt({-32, 0, -14}, subtleAirBands, true);
    QVERIFY(!subtleAirOff.isNull() && !subtleAirOn.isNull());
    const auto signedCapEdgeDifference = [](const QImage& first, const QImage& second) {
        const FrontFace face = locateFrontFace(first);
        if (face.cap <= 12) return -1.0;
        const auto edgeMean = [&](const QImage& frame, int left, int right) {
            double total = 0;
            int count = 0;
            for (int y = face.cap - 26; y <= face.cap + 4; ++y)
                for (int x = left; x < right; ++x) {
                    total += qGray(frame.pixel(x, y));
                    ++count;
                }
            return total / std::max(1, count);
        };
        return std::max(edgeMean(second, 242, 260) - edgeMean(first, 242, 260),
                        edgeMean(second, 310, 328) - edgeMean(first, 310, 328));
    };
    const double subtleAirEdgeDelta = signedCapEdgeDifference(subtleAirOff, subtleAirOn);
    qInfo() << "Reference subtle-air selected cap-edge signed delta:" << subtleAirEdgeDelta;
    // Fixed 640px D3D11 capture, RGB8: the repaired path measures .25448
    // grayscale units here, while the erroneous double-subtraction rounds to
    // exactly 0.  This is an ownership/rim-conservation guard, not a claim of
    // cross-backend pixel parity with the reference renderer.
    QVERIFY2(subtleAirEdgeDelta >= 0.20,
             "Air must replace the quiet rim once, never subtract it after replacing the cap");
    item.parameters.glow = 0.5F;

    // At t=.1 the same presence coordinate is also edge-brilliance selected:
    // fract(hash * 89 + time * 2) = .9949....  The reference puts that energy
    // at the cap edge rather than emitting legacy 24x24 interior particles.
    const QVector4D brillianceBands(0, 0, 1, 0);
    const QImage brillianceOff = frameAt({-32, 0, 9}, brillianceBands, false);
    const QImage brillianceOn = frameAt({-32, 0, 9}, brillianceBands, true);
    const QImage brillianceAbsentOff = frameAt({-32, 0, -31}, brillianceBands, false);
    const QImage brillianceAbsentOn = frameAt({-32, 0, -31}, brillianceBands, true);
    QVERIFY(!brillianceOff.isNull() && !brillianceOn.isNull());
    QVERIFY(!brillianceAbsentOff.isNull() && !brillianceAbsentOn.isNull());
    const auto edgeDifference = [](const QImage& first, const QImage& second) {
        const FrontFace face = locateFrontFace(first);
        if (face.cap <= 12) return -1.0;
        const auto regionDifference = [&](int left, int right) {
        double total = 0;
        int count = 0;
        for (int y = face.cap - 26; y <= face.cap + 4; ++y)
            for (int x = left; x < right; ++x) {
                const QColor a = first.pixelColor(x, y);
                const QColor b = second.pixelColor(x, y);
                total += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green())
                       + std::abs(a.blue() - b.blue());
                ++count;
            }
        return total / std::max(1, count);
        };
        return std::max(regionDifference(242, 260), regionDifference(310, 328));
    };
    const double edgeDelta = edgeDifference(brillianceOff, brillianceOn);
    const double absentEdgeDelta = edgeDifference(brillianceAbsentOff, brillianceAbsentOn);
    qInfo() << "Reference brilliance cap-edge selected/unselected delta:"
            << edgeDelta << absentEdgeDelta;
    // The camera sees the opposite cap rim through the transparent volume, so
    // a screen-center comparison is not a face-local edge measurement.  This
    // side strip directly proves the selected brilliance edge is energized;
    // the shader branch is deliberately free of the legacy interior facets.
    QVERIFY2(edgeDelta > 1.0 && absentEdgeDelta < edgeDelta * 0.30,
             "Brilliance must energize only the reference-selected cap edge");
    window.close();
}

QTEST_MAIN(TerrainColumnMaterialTest)
#include "terrain_column_material_test.moc"
