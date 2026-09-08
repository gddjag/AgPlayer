#include "terrain_column_mesh.hpp"
#include "terrain_reactor_state.hpp"
#include "terrain_shadow_map.hpp"

#include <QDir>
#include <QFile>
#include <QMatrix4x4>
#include <QQuickRhiItem>
#include <QQuickWindow>
#include <QTest>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

using namespace agplayer::terrain::gpu;

namespace {
struct StudyParameters {
    float time = 0;
    int material = 1;
    bool rainbow = false;
    bool stream = false;
    float beat = 0;
    float beatAge = 0;
    float audioLevel = 1;
    float lowAudioLevel = 1;
    float midAudioLevel = 0;
    int waveSlot = -1;
    int waveTint = 0;
    float exposure = 1;
    float opacity = 1;
    float softness = 0.45F;
    float randomValue = 0.9F;
    bool array = false;
    bool shadows = false;
    QVector3D lighting{1, 0.6F, 1};
    QColor tint = QColor::fromRgbF(0.08F, 0.55F, 0.72F);
    QVector3D camera{15, 16, 24};
};
struct StudyCounters {
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
    explicit ColumnRenderer(std::shared_ptr<StudyCounters> counters) : counters_(std::move(counters))
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
    std::shared_ptr<StudyCounters> counters;
protected:
    QQuickRhiItemRenderer* createRenderer() override { return new ColumnRenderer(counters); }
};

void ColumnRenderer::initialize(QRhiCommandBuffer*)
{
    if (pipeline_ && lastTarget_ == renderTarget()) return;
    release();
    const auto shader = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
    };
    const QShader vertexShader = shader(QStringLiteral(":/terrain/shaders/terrain_reactor.vert.qsb"));
    const QShader fragmentShader = shader(QStringLiteral(":/terrain/shaders/terrain_reactor.frag.qsb"));
    const QShader shadowShader = shader(QStringLiteral(":/terrain/shaders/terrain_shadow.frag.qsb"));
    const QShader shadowVertexShader = shader(QStringLiteral(":/terrain-shadow/shaders/terrain_shadow.vert.qsb"));
    if (!vertexShader.isValid() || !fragmentShader.isValid()
        || !shadowShader.isValid() || !shadowVertexShader.isValid()) {
        counters_->failed = true; return;
    }
    vertices_.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                     sizeof(columnVertices)));
    indices_.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                    sizeof(columnIndices)));
    instances_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                      25 * sizeof(GpuInstance)));
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
        pipeline_->setDepthTest(true); pipeline_->setDepthWrite(true);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true; blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
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
    view.lookAt(parameters_.camera, QVector3D(0, 6, 0), QVector3D(0, 1, 0));
    const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * projection * view;
    std::memcpy(u.mvp, mvp.constData(), sizeof(u.mvp));
    for (int i = 0; i < 4; ++i) {
        u.equalizerLow[i] = u.equalizerHigh[i] = 1;
        u.bandsHigh[i] = 0.9F * parameters_.audioLevel;
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
    u.styleParameters[0] = 0.60F;
    // Test-only fixture freezes the existing affine motion input. This does
    // not change material time, geometry algorithms, or the production UBO.
    u.styleParameters[1] = -0.125F;
    u.styleParameters[2] = 0.5F;
    u.styleDynamics[1] = 0.5F;
    u.styleDynamics[2] = parameters_.rainbow ? 3.0F : 1.0F;
    u.styleDynamics[3] = 0.5F;
    u.styleExtra[2] = parameters_.stream ? 1.0F : 0.0F;
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
    u.sceneControls[2] = 112;
    u.sceneControls[3] = 1.0F;
    u.waveParameters[0] = u.waveParameters[1] = u.waveParameters[2] = 1;
    if (parameters_.waveSlot >= 0) {
        const QVector3D anchors[] = {{0.1F,0.8F,1}, {1,0.2F,0.4F}, {1,0.7F,0.1F}};
        for (int i = 0; i < 3; ++i)
            for (int channel = 0; channel < 3; ++channel)
                u.colors[i + 1][channel] = anchors[i][channel];
        u.styleToggles[0] = 1;
        u.effects[3] = 8;
        u.waveSources[parameters_.waveSlot][3] = 0.8F;
        u.waveSources[parameters_.waveSlot][2] = 32.0F * parameters_.waveTint;
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
                    {3.7F, 12.0F, 3.7F}, {0, 0, random, 0}};
            }
        }
    } else {
        columns[0] = {{0, 0, 0}, {4, 12, 4}, {0, 0, parameters_.randomValue, 0}};
    }
    auto* updates = rhi()->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(uniform_.get(), 0, sizeof(u), &u);
    updates->updateDynamicBuffer(instances_.get(), 0,
                                 instanceCount * sizeof(GpuInstance), columns.data());
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
    cb->beginPass(renderTarget(), QColor(0, 0, 0, 0), {1, 0});
    cb->setGraphicsPipeline(pipeline_.get()); cb->setShaderResources(bindings_.get());
    const auto size = renderTarget()->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
    const QRhiCommandBuffer::VertexInput inputs[] = {{vertices_.get(), 0}, {instances_.get(), 0}};
    cb->setVertexInput(0, 2, inputs, indices_.get(), 0, QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(quint32(columnIndices.size()), instanceCount);
    cb->endPass();
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
    void consecutiveWavesUseDifferentPaletteAnchors();
    void sustainedReliefLeavesRoomForBeatLift();
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
    void jellyReboundStartsAtBeatOnset();
    void neutralGrayWithoutInnerLightRemainsNeutral();
    void roughnessAndViewChangeReflectionResponse();
    void everyColumnHasLocalCapFlash_data() {
        QTest::addColumn<float>("randomValue");
        QTest::newRow("low-random") << 0.10F;
        QTest::newRow("last-selected") << 0.49F;
        QTest::newRow("first-unselected") << 0.51F;
        QTest::newRow("unselected-forty-percent") << 0.90F;
    }
    void everyColumnHasLocalCapFlash();
    void lightControlsReachNativeMaterial_data() {
        QTest::addColumn<int>("lane");
        QTest::newRow("inner-source") << 0;
        QTest::newRow("light-spill") << 1;
        QTest::newRow("light-radius") << 2;
        QTest::newRow("high-only-inner-source") << 3;
    }
    void lightControlsReachNativeMaterial();
    void smoothInteriorRemainsStable_data() {
        QTest::addColumn<int>("materialMode");
        QTest::addColumn<bool>("rainbow");
        QTest::newRow("crystal") << 0 << false;
        QTest::newRow("jelly") << 1 << false;
        QTest::newRow("jelly-rainbow") << 1 << true;
    }
    void smoothInteriorRemainsStable();
};

void TerrainColumnMaterialTest::consecutiveWavesUseDifferentPaletteAnchors()
{
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(Qt::black);
    auto counters = std::make_shared<StudyCounters>();
    ColumnItem item(window.contentItem(), counters);
    item.parameters.camera = {0, 6, 50};
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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

void TerrainColumnMaterialTest::sustainedReliefLeavesRoomForBeatLift()
{
    QQuickWindow window;
    window.resize(640, 640);
    window.setColor(Qt::black);
    auto counters = std::make_shared<StudyCounters>();
    ColumnItem item(window.contentItem(), counters);
    item.parameters.material = 0;
    item.parameters.camera = {0, 6, 50};
    item.parameters.midAudioLevel = 1;
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0, 5000);
    const QImage steady = studyFrame(window);
    const int previous = counters->frames;
    item.parameters.beat = 0.7F;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > previous, 5000);
    const FrameComparison change = compareFrames(steady, studyFrame(window));
    qInfo() << "Sustained/beat column image heights:" << change.firstBounds.height()
            << change.secondBounds.height();
    QVERIFY2(change.firstBounds.height() < 230,
             "Sustained audio makes a tall tower instead of a low floating terrain");
    QVERIFY2(change.secondBounds.height() > change.firstBounds.height() * 1.15,
             "Sustained relief masks the short beat lift");
}

void TerrainColumnMaterialTest::jellyReboundStartsAtBeatOnset()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(160, 0, 160));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.material = 0;
    item.parameters.beat = 1;
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const auto rigid = studyFrame(window);
    const int before = counters->frames;
    item.parameters.material = 1;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const auto elastic = studyFrame(window);
    const auto frames = compareFrames(rigid, elastic);
    QVERIFY2(frames.secondBounds.top() < frames.firstBounds.top() - 4,
             "Elastic columns must rise on beat onset, not wait for a sine phase");
}

void TerrainColumnMaterialTest::innerLightHasOpticalDepthAcrossSmoothFace()
{
    auto counters = std::make_shared<StudyCounters>();
    QQuickWindow window;
    window.resize(640, 640); window.setColor(QColor(3, 5, 9));
    ColumnItem item(window.contentItem(), counters);
    item.parameters.lighting = {1, 0, 1};
    item.parameters.stream = false;
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const auto frame = studyFrame(window);
    const FrontFace face = locateFrontFace(frame);
    QVERIFY(face.capContrast >= 3 && face.foot > face.cap + 100);
    const int y = (face.cap + face.foot) / 2;
    const double left = patchLight(meanPatch(frame, 235, 240, y - 3, y + 3));
    const double center = patchLight(meanPatch(frame, 277, 282, y - 3, y + 3));
    const double right = patchLight(meanPatch(frame, 319, 324, y - 3, y + 3));
    const double opticalRelief = std::abs(center - (left + right) * 0.5);
    qInfo() << "Optical face relief / samples:" << opticalRelief << left << center << right;
    // Remove the linear projected height ramp: uniform face paint cannot
    // create the nonlinear depth transition at a refracted box exit.
    QVERIFY2(opticalRelief > 1.0,
             "Smooth emitting shell must reveal optical thickness beyond a height ramp");
    QVERIFY2(face.coreLight > 55.0,
             "The powered interior must be legibly luminous, not only a dark tinted surface");
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
    QVERIFY(QTest::qWaitForWindowExposed(&window));
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
    item.parameters.shadows = false;
    item.parameters.time = 0.15F;
    item.parameters.stream = false;
    item.parameters.beat = 0;
    item.parameters.lighting = {0, 0, 1};
    item.parameters.tint = QColor::fromRgbF(0.42F, 0.42F, 0.42F);
    item.parameters.camera = {34, 30, 48};
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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

    // Manually located on the frontmost column's broad planar wall in the
    // fixed 640px reference: no bevel, silhouette, cap or neighbour boundary.
    // Archived acne frame: horizontal second-difference RMS 5.69; fixed: 0.
    double planeSecondDifference = 0;
    int planeSamples = 0;
    for (int y = 440; y < 535; ++y) {
        for (int x = 337; x < 365; ++x) {
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
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
    item.parameters.beat = 1;
    item.parameters.stream = false;
    item.parameters.lighting = {1, 0, 1};
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > 0 || counters->failed, 5000);
    QVERIFY(!counters->failed);
    const QImage early = studyFrame(window);
    const FrontFace face = locateFrontFace(early);
    QVERIFY(face.cap > 0 && face.foot > face.cap + 100);
    item.parameters.beatAge = 0.55F;
    const int before = counters->frames;
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(counters->frames > before, 3000);
    const QImage late = studyFrame(window);
    QCOMPARE(early.size(), late.size());
    const auto band = [&](const QImage& frame, float fraction) {
        double sum = 0;
        const int centerY = face.cap + qRound((face.foot - face.cap) * fraction);
        for (int y = centerY - 4; y <= centerY + 4; ++y)
            for (int x = 270; x < 300; ++x) sum += qGray(frame.pixel(x, y));
        return sum / 270.0;
    };
    const double upperGain = band(late, 0.25F) - band(early, 0.25F);
    const double lowerRelease = band(early, 0.75F) - band(late, 0.75F);
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
    item.parameters.stream = false;
    item.parameters.lighting = {1, 0, 1};
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
    item.parameters.time = 0.15F;
    item.parameters.stream = false;
    item.parameters.audioLevel = 0;
    item.parameters.beat = 0;
    item.parameters.lighting = {1, 0, 1};
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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

void TerrainColumnMaterialTest::roughnessAndViewChangeReflectionResponse()
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
    // Look toward the weak side probe reflected by the now shorter column.
    // The former high camera misses that incoming light; emission stays off.
    item.parameters.camera.setY(0.0F);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
             "Roughness must change reflected light, not column geometry");
    QVERIFY2(roughness.changedCommon > roughness.commonVisible / 20,
             "Material softness must visibly broaden or attenuate reflection response");
    QVERIFY2(roughness.totalRgbDifference > quint64(roughness.commonVisible),
             "Roughness change produced no measurable PBR response");

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
    QVERIFY2(view.changedCommon > view.commonVisible / 100,
             "Changing the camera ray must still affect the weak shell reflection");

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
    if (lane == 1) {
        item.parameters.array = true;
        item.parameters.camera = {34, 30, 48};
    }
    item.parameters.lighting[lane] = lane == 2 ? 0.2F : 0;
    // Spill is powered by music, not an always-on point light. Compare both
    // control endpoints at the same fixed beat, with identical geometry.
    item.parameters.beat = highOnly ? 0.0F : 0.5F;
    item.parameters.lowAudioLevel = highOnly ? 0.0F : 1.0F;
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
    }
    quint64 difference = 0;
    for (int y = 200; y < 500; ++y)
        for (int x = 230; x < 395; ++x)
            difference += std::abs(qGray(low.pixel(x, y)) - qGray(high.pixel(x, y)));
    qInfo() << "Native light control lane / mean difference:" << lane << double(difference) / (300 * 165);
    QVERIFY2(difference > 300 * 165,
             "A lighting slider must visibly affect the rendered surface at fixed geometry/time");
    QCOMPARE(counters->generations.load(), generation);
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
        QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
    item.parameters.randomValue = randomValue;
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
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
    if (randomValue >= 0.50F) {
        QVERIFY2(topRms[1] < 0.25, "The unselected half must retain a steady cap without twinkles");
        QVERIFY(sideRms[0] < 0.25 && sideRms[1] < 0.25);
        return;
    }
      QVERIFY2(topRms[0] < 0.25, "The cap must be stable with stream disabled");
    QVERIFY2(topMeanChange > 3, "Every column needs a visible face-wide top flash");
      const double temporalCoverage = double(std::count(flashed.begin(), flashed.end(), true))
                                    / flashed.size();
      QVERIFY2(temporalCoverage > 0.80,
               "Twinkles must visit the whole cap over time, not a narrow glowing line");
      QVERIFY2(bestLocalCoverage > 0.90,
               "A selected column must flash across its whole cap in the same frame");
    QVERIFY2(sideRms[0] < 0.25 && sideRms[1] < 0.25,
             "Top flashes must not create texture inside the smooth column");
}

QTEST_MAIN(TerrainColumnMaterialTest)
#include "terrain_column_material_test.moc"
