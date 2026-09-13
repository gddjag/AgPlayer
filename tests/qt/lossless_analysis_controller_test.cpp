#include "lossless_analysis_controller.hpp"
#include "lossless_evidence_item.hpp"
#include "lossless_report.hpp"
#include "lossless_task_model.hpp"

#include "lossless/lossless_types.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {

class TestableLosslessEvidenceItem final : public LosslessEvidenceItem {
public:
    using LosslessEvidenceItem::updatePaintNode;
};

QSGGeometry* spectrogramGeometry(QSGNode* root)
{
    if (root == nullptr || root->childCount() < 1) return nullptr;
    QSGNode* heatmap = root->firstChild();
    return static_cast<QSGGeometryNode*>(heatmap)->geometry();
}

QVariantList spectrogramMatrix(const int frames, const int bins,
                               const int peakFrame = -1,
                               const int peakBin = -1)
{
    QVariantList matrix;
    matrix.reserve(frames);
    for (int frame = 0; frame < frames; ++frame) {
        QVariantList row;
        row.reserve(bins);
        for (int bin = 0; bin < bins; ++bin) {
            row.append(frame == peakFrame && bin == peakBin ? 0.0 : -120.0);
        }
        matrix.append(QVariant::fromValue(row));
    }
    return matrix;
}

std::array<uchar, 4> firstSpectrogramColor(QSGGeometry* geometry)
{
    const auto* vertices = geometry->vertexDataAsColoredPoint2D();
    return {vertices[0].r, vertices[0].g, vertices[0].b, vertices[0].a};
}

QString writeFile(const QTemporaryDir& directory, const QString& name,
                  const QByteArray& bytes = QByteArray("audio"))
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

QString writePcm16Wav(const QTemporaryDir& directory, const QString& name)
{
    constexpr quint32 sampleRate = 48000;
    constexpr quint16 channels = 1;
    constexpr quint16 bitsPerSample = 16;
    constexpr quint32 frames = sampleRate * 2;
    constexpr quint32 dataBytes = frames * channels * (bitsPerSample / 8);
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataBytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << channels << sampleRate
           << quint32(sampleRate * channels * (bitsPerSample / 8))
           << quint16(channels * (bitsPerSample / 8)) << bitsPerSample;
    stream.writeRawData("data", 4);
    stream << dataBytes;
    constexpr double pi = 3.14159265358979323846;
    for (quint32 frame = 0; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sampleRate;
        const double sample = 0.55 * std::sin(2.0 * pi * 440.0 * time)
            + 0.20 * std::sin(2.0 * pi * 7000.0 * time);
        stream << static_cast<qint16>(sample * 32767.0);
    }
    return writeFile(directory, name, bytes);
}

QByteArray sha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result();
}

class CandidateFormatTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* sourceText,
                      const char*, int) const override
    {
        if (QString::fromLatin1(context) == QStringLiteral("LosslessEvidence")
            && QString::fromUtf8(sourceText) == QString::fromUtf8("CELT帧结构")) {
            return QStringLiteral("CELT frame structure");
        }
        if (QString::fromLatin1(context) == QStringLiteral("LosslessEvidence")
            && QString::fromUtf8(sourceText) == QString::fromUtf8("%1 Hz PCM（推测）")) {
            return QStringLiteral("%1 Hz PCM (inferred)");
        }
        if (QString::fromLatin1(context) == QStringLiteral("LosslessEvidence")) {
            if (QString::fromUtf8(sourceText) == QString::fromUtf8("重采样或周期调制"))
                return QStringLiteral("Resampling or periodic modulation");
            if (QString::fromUtf8(sourceText) == QString::fromUtf8("检测到稳定周期结构；重采样与周期调制均可形成，不能单独确定升频历史。"))
                return QStringLiteral("Periodic structure does not establish upsampling history.");
        }
        return {};
    }
};

agplayer::lossless::AnalysisResult completedResult(
    const QString& path, const agplayer::lossless::Verdict verdict,
    const bool includeSpectrogram = false)
{
    using namespace agplayer::lossless;
    const QFileInfo info(path);
    AnalysisResult result;
    result.analysisStartedUnixMs = 1'780'000'000'000LL;
    result.file.path = path.toUtf8().toStdString();
    result.file.fileSize = static_cast<std::uint64_t>(info.size());
    result.file.modifiedUnixMs = info.lastModified().toMSecsSinceEpoch();
    result.source.container = info.suffix().toUpper().toStdString();
    result.source.codec = "pcm_s24le";
    result.source.channelLayout = "stereo";
    result.source.kind = SourceKind::PcmInteger;
    result.source.sampleRate = 96000;
    result.source.decodedSampleRate = 96000;
    result.source.bitsPerSample = 24;
    result.source.channels = 2;
    result.source.durationMs = 1234;
    result.source.codecIsLossless = true;
    result.verdict = verdict;
    result.confidence = 82;
    result.coverage.decodedFrames = 118464;
    result.coverage.analyzedWindows = 8;
    result.coverage.activeWindows = 7;
    result.coverage.decodedRatio = 1.0;
    result.coverage.activeWindowRatio = 0.875;
    result.measurements.cutoffHz = 22000.0;
    result.measurements.effectiveBits = 20.5;
    result.measurements.spectralEdgeFrequencyHz = 21850.5;
    result.measurements.spectralEdgeDepthDb = 34.25;
    result.measurements.spectralEdgeStability = 0.875;
    result.measurements.codecHoleScore = 0.12;
    result.measurements.resamplingMirrorScore = 0.25;
    result.evidence.push_back({
        "bandwidth", EvidenceFamily::Bandwidth,
        EvidenceDirection::SupportsAuthenticity, 22000.0, "Hz", "> 20 kHz",
        1, 0.0, 1.234, "高频覆盖稳定"});
    result.candidates.push_back({"PCM 96 kHz / 24-bit", 72,
                                 "仅为信号特征推断"});
    result.chain.push_back("PCM 96 kHz / 24-bit（推测）");
    result.spectrum.nyquistHz = 48000.0;
    result.spectrum.db = {-80.0, -20.0, -40.0, -90.0};
    if (includeSpectrogram) {
        result.spectrogram.timeBins = 2;
        result.spectrogram.frequencyBins = 2;
        result.spectrogram.nyquistHz = 48000.0;
        result.spectrogram.db = {-80.0F, -30.0F, -50.0F, -90.0F};
    }
    return result;
}

QVariantList urls(std::initializer_list<QString> paths)
{
    QVariantList result;
    for (const QString& path : paths) result.append(QUrl::fromLocalFile(path));
    return result;
}

QString taskIdAt(QAbstractItemModel* model, const int row)
{
    return model->index(row, 0).data(LosslessTaskModel::TaskIdRole).toString();
}

void be32(std::vector<unsigned char>& bytes, const std::size_t position,
          std::uint32_t value)
{
    for (int index = 3; index >= 0; --index) {
        bytes[position + static_cast<std::size_t>(index)] =
            static_cast<unsigned char>(value);
        value >>= 8;
    }
}

void tag(std::vector<unsigned char>& bytes, const std::size_t position,
         const char* value)
{
    std::copy(value, value + 8,
              bytes.begin() + static_cast<std::ptrdiff_t>(position));
}

QString writeTwoTrackSacd(const QTemporaryDir& directory)
{
    std::vector<unsigned char> bytes(535U * 2048U, 0);
    constexpr std::size_t master = 510U * 2048U;
    constexpr std::size_t area = 520U * 2048U;
    constexpr std::size_t table = 521U * 2048U;
    tag(bytes, master, "SACDMTOC");
    bytes[master + 8] = 1;
    bytes[master + 9] = 20;
    be32(bytes, master + 64, 520);
    tag(bytes, area, "TWOCHTOC");
    bytes[area + 8] = 1;
    bytes[area + 9] = 20;
    bytes[area + 11] = 2;
    bytes[area + 20] = 4;
    bytes[area + 21] = 2;
    bytes[area + 32] = 2;
    bytes[area + 69] = 2;
    be32(bytes, area + 72, 525);
    be32(bytes, area + 76, 534);
    tag(bytes, table, "SACDTRL1");
    be32(bytes, table + 8, 525);
    be32(bytes, table + 12, 530);
    be32(bytes, table + 1028, 5);
    be32(bytes, table + 1032, 5);
    return writeFile(
        directory, QString::fromUtf8("双声道镜像.iso"),
        QByteArray(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<qsizetype>(bytes.size())));
}

} // namespace

class LosslessAnalysisControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void importsNativeDialogUrlStringsAndLocalPaths()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeFile(directory, QString::fromUtf8("中文 空格 #100%.wav"));
        LosslessAnalysisController controller;
        controller.loadFiles({QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), path});
        QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
        QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error()));
    }
    void taskModelExposesTheUiContractRoles();
    void controllerExposesCompleteEmptyResultContract();
    void discoversRecursivelyDeduplicatesAndHonorsPendingStart();
    void expandsSacdIsoIntoDistinctTrackJobs();
    void filtersSearchesSortsAndSelectsVisibleRows();
    void pcmToDsdCountMatchesInconclusiveFilter();
    void removingRowsPreservesActiveSort();
    void neverExceedsConfiguredConcurrency();
    void cancellationAndDestructionStopWorkersSafely();
    void cacheHitsAndFileIdentityChangesInvalidateEntries();
    void realWavPublishesSpectrumAndCacheKeepsIt();
    void requestsSpectrogramOnDemandAndFormatsListColumns();
    void spectrogramRequestsDoNotCacheStrippedMatrices();
    void spectrogramRendererUsesFixedOpaqueDbScale();
    void spectrogramRendererPeakReducesIntoFullBudget();
    void sourceChangesDuringAnalysisFailWithoutCaching();
    void terminalStatusSummarizesAllOutcomes();
    void resamplingGridRequiresQualifiedEvidence_data();
    void resamplingGridRequiresQualifiedEvidence();
    void dsdSelectedResultUsesRawRateAndHidesPcmDiagnostics();
    void candidateFormatsTranslateInitiallyAndOnLanguageRefresh();
    void rejectsUnboundedDiscoveryAndConcurrentReports();
    void exportsVersionedJsonAndCsvWithoutTouchingSources_data();
    void exportsVersionedJsonAndCsvWithoutTouchingSources();
    void reportCancellationPreservesExistingDestination();
    void unknownWorkerExceptionsBecomeExplicitFailures();
};

void LosslessAnalysisControllerTest::taskModelExposesTheUiContractRoles()
{
    LosslessTaskModel model;
    const QHash<int, QByteArray> roles = model.roleNames();
    const QList<QByteArray> expected{
        "taskId", "fileName", "filePath", "formatName", "audioFormat",
        "verdictCode", "verdictText", "confidence", "state", "stateText",
        "checked", "progress"};
    QCOMPARE(roles.size(), expected.size());
    for (const QByteArray& name : expected) {
        QVERIFY2(roles.values().contains(name), name.constData());
    }
}

void LosslessAnalysisControllerTest::controllerExposesCompleteEmptyResultContract()
{
    LosslessAnalysisController controller;
    const QMetaObject* meta = controller.metaObject();
    const QList<QByteArray> properties{
        "tasks", "selectedResult", "running", "stopping", "progress",
        "completedCount", "totalCount", "selectedCount", "concurrency",
        "filter", "searchText", "statusText", "error", "counts"};
    for (const QByteArray& property : properties) {
        QVERIFY2(meta->indexOfProperty(property.constData()) >= 0,
                 property.constData());
    }
    const QList<QByteArray> methods{
        "loadFiles(QVariantList)", "addFolder(QUrl)", "start()", "cancel()",
        "retrySelected()", "removeSelected()", "clear()",
        "selectTask(QString)", "setChecked(QString,bool)", "selectAll(bool)",
        "exportReport(QUrl,QString)", "requestSpectrogram()",
        "notifyError(QString)", "refreshTranslations()"};
    for (const QByteArray& method : methods) {
        QVERIFY2(meta->indexOfMethod(method.constData()) >= 0,
                 method.constData());
    }

    const QSet<QString> expectedKeys{
        "taskId", "fileName", "path", "formatName", "codec", "sampleRate",
        "rawDsdSampleRate", "bitsPerSample", "channels", "durationMs",
        "fileSize", "modified",
        "verdictCode", "verdictText", "confidence", "coverage", "cutoffHz",
        "effectiveBits", "resamplingText", "holesText", "evidence",
        "candidates", "chain", "spectrum", "spectrogram", "error",
        "warnings"};
    const QVariantMap empty = controller.selectedResult();
    QCOMPARE(QSet<QString>(empty.keyBegin(), empty.keyEnd()), expectedKeys);
    QVERIFY(empty.value(QStringLiteral("spectrum")).toList().isEmpty());
    QVERIFY(empty.value(QStringLiteral("evidence")).toList().isEmpty());
}

void LosslessAnalysisControllerTest::discoversRecursivelyDeduplicatesAndHonorsPendingStart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("nested")));
    const QString first = writeFile(directory, QStringLiteral("first.wav"));
    const QString second = writeFile(
        directory, QStringLiteral("nested/second.flac"), QByteArray("music"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(!writeFile(directory, QStringLiteral("ignore.txt")).isEmpty());
    const QByteArray beforeFirst = sha256(first);
    const QByteArray beforeSecond = sha256(second);
    std::atomic_int calls{0};

    LosslessAnalysisController controller(
        [&](const std::string& path,
            const agplayer::lossless::AnalysisOptions&,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback progress) {
            ++calls;
            progress(0.2F);
            progress(1.0F);
            const QString filePath = QString::fromUtf8(path);
            return completedResult(
                filePath,
                filePath.endsWith(QStringLiteral(".flac"))
                    ? agplayer::lossless::Verdict::CredibleLossless
                    : agplayer::lossless::Verdict::SuspectedUpsample);
        }, nullptr);

    controller.loadFiles({QUrl::fromLocalFile(directory.path()),
                          QUrl::fromLocalFile(first),
                          QUrl::fromLocalFile(first)});
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(controller.completedCount(), 2);
    QCOMPARE(calls.load(), 2);
    QCOMPARE(sha256(first), beforeFirst);
    QCOMPARE(sha256(second), beforeSecond);
    QCOMPARE(controller.counts(), QVariantList({2, 1, 0, 1, 0}));
}

void LosslessAnalysisControllerTest::expandsSacdIsoIntoDistinctTrackJobs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString image = writeTwoTrackSacd(directory);
    QVERIFY(!image.isEmpty());
    QMutex lock;
    QSet<int> trackIndexes;
    std::atomic_bool pathMatched{true};

    LosslessAnalysisController controller(
        [&](const std::string& path,
            const agplayer::lossless::AnalysisOptions& options,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            if (QString::fromUtf8(path) != image) pathMatched = false;
            QMutexLocker locker(&lock);
            trackIndexes.insert(options.isoTrackIndex);
            return completedResult(image,
                agplayer::lossless::Verdict::CredibleNativeDsd);
        }, nullptr);
    controller.loadFiles(urls({image, image}));
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QMutexLocker locker(&lock);
    QCOMPARE(trackIndexes, QSet<int>({0, 1}));
    QVERIFY(pathMatched.load());
    QCOMPARE(controller.completedCount(), 2);
}

void LosslessAnalysisControllerTest::filtersSearchesSortsAndSelectsVisibleRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString alpha = writeFile(directory, QStringLiteral("zeta.wav"));
    const QString beta = writeFile(directory, QStringLiteral("alpha.flac"));
    LosslessAnalysisController controller(
        [](const std::string& path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            const QString filePath = QString::fromUtf8(path);
            return completedResult(
                filePath,
                filePath.endsWith(QStringLiteral(".flac"))
                    ? agplayer::lossless::Verdict::CredibleLossless
                    : agplayer::lossless::Verdict::SuspectedLossyTranscode);
        }, nullptr);
    controller.loadFiles(urls({alpha, beta}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 2, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);

    controller.setFilter(QStringLiteral("credible"));
    QCOMPARE(controller.tasks()->rowCount(), 1);
    QCOMPARE(controller.tasks()->index(0, 0)
                 .data(LosslessTaskModel::FileNameRole).toString(),
             QStringLiteral("alpha.flac"));
    controller.selectAll(false);
    QCOMPARE(controller.selectedCount(), 1);
    controller.setFilter(QStringLiteral("all"));
    controller.setSearchText(QStringLiteral("zeta"));
    QCOMPARE(controller.tasks()->rowCount(), 1);
    controller.setChecked(taskIdAt(controller.tasks(), 0), false);
    QCOMPARE(controller.selectedCount(), 0);
    controller.setSearchText({});
    auto* model = qobject_cast<LosslessTaskModel*>(controller.tasks());
    QVERIFY(model != nullptr);
    model->sort(1, Qt::AscendingOrder);
    QCOMPARE(model->index(0, 0).data(LosslessTaskModel::FileNameRole).toString(),
             QStringLiteral("alpha.flac"));
}

void LosslessAnalysisControllerTest::pcmToDsdCountMatchesInconclusiveFilter()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("converted.dsf"));
    LosslessAnalysisController controller(
        [](const std::string& utf8Path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            return completedResult(QString::fromUtf8(utf8Path),
                agplayer::lossless::Verdict::SuspectedPcmToDsd);
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);

    QCOMPARE(controller.counts(), QVariantList({1, 0, 0, 0, 1}));
    controller.setFilter(QStringLiteral("inconclusive"));
    QCOMPARE(controller.tasks()->rowCount(), 1);
}

void LosslessAnalysisControllerTest::removingRowsPreservesActiveSort()
{
    LosslessTaskModel model;
    const auto task = [](const QString& id, const QString& name) {
        return QVariantMap{
            {QStringLiteral("taskId"), id},
            {QStringLiteral("_identity"), id},
            {QStringLiteral("fileName"), name},
            {QStringLiteral("checked"), true},
        };
    };
    QVERIFY(model.appendTask(task(QStringLiteral("zeta"),
                                  QStringLiteral("zeta.wav"))));
    QVERIFY(model.appendTask(task(QStringLiteral("alpha"),
                                  QStringLiteral("alpha.wav"))));
    QVERIFY(model.appendTask(task(QStringLiteral("beta"),
                                  QStringLiteral("beta.wav"))));
    model.sort(1, Qt::AscendingOrder);
    QCOMPARE(model.index(0, 0).data(LosslessTaskModel::FileNameRole).toString(),
             QStringLiteral("alpha.wav"));

    model.removeTasks({QStringLiteral("beta")});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0).data(LosslessTaskModel::FileNameRole).toString(),
             QStringLiteral("alpha.wav"));
    QCOMPARE(model.index(1, 0).data(LosslessTaskModel::FileNameRole).toString(),
             QStringLiteral("zeta.wav"));
}

void LosslessAnalysisControllerTest::neverExceedsConfiguredConcurrency()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVariantList inputs;
    for (int index = 0; index < 6; ++index) {
        const QString path = writeFile(
            directory, QStringLiteral("track-%1.wav").arg(index));
        QVERIFY(!path.isEmpty());
        inputs.append(QUrl::fromLocalFile(path));
    }
    QSemaphore gate;
    const auto releaseWorkers = qScopeGuard([&gate] { gate.release(12); });
    std::atomic_int active{0};
    std::atomic_int maximum{0};
    LosslessAnalysisController controller(
        [&](const std::string& path,
            const agplayer::lossless::AnalysisOptions&,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            const int current = ++active;
            int observed = maximum.load();
            while (current > observed
                   && !maximum.compare_exchange_weak(observed, current)) {
            }
            gate.acquire();
            --active;
            return completedResult(QString::fromUtf8(path),
                agplayer::lossless::Verdict::CredibleLossless);
        }, nullptr);
    QCOMPARE(controller.concurrency(), 2);
    controller.setConcurrency(99);
    QCOMPARE(controller.concurrency(), 4);
    controller.setConcurrency(2);
    controller.loadFiles(inputs);
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 6, 3000);
    controller.start();
    QTRY_COMPARE_WITH_TIMEOUT(active.load(), 2, 3000);
    QCOMPARE(maximum.load(), 2);
    gate.release(6);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QVERIFY(maximum.load() <= 2);
}

void LosslessAnalysisControllerTest::cancellationAndDestructionStopWorkersSafely()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("long.wav"));
    std::atomic_bool started{false};
    std::atomic_bool observedCancellation{false};
    const auto analyzer = [&](const std::string& utf8Path,
                              const agplayer::lossless::AnalysisOptions&,
                              const std::atomic_bool& cancelled,
                              agplayer::lossless::ProgressCallback) {
        started.store(true);
        while (!cancelled.load()) QThread::msleep(2);
        observedCancellation.store(true);
        auto result = completedResult(QString::fromUtf8(utf8Path),
            agplayer::lossless::Verdict::Cancelled);
        result.cancelled = true;
        return result;
    };

    {
        LosslessAnalysisController controller(analyzer, nullptr);
        controller.loadFiles(urls({path}));
        QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
        controller.start();
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 3000);
        QElapsedTimer timer;
        timer.start();
        controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 500);
        QVERIFY(timer.elapsed() < 500);
        QVERIFY(observedCancellation.load());
    }

    started.store(false);
    observedCancellation.store(false);
    auto controller = std::make_unique<LosslessAnalysisController>(
        analyzer, nullptr);
    controller->loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller->totalCount(), 1, 3000);
    controller->start();
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 3000);
    QElapsedTimer timer;
    timer.start();
    controller.reset();
    QVERIFY(timer.elapsed() < 500);
    QVERIFY(observedCancellation.load());
}

void LosslessAnalysisControllerTest::cacheHitsAndFileIdentityChangesInvalidateEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("cached.wav"));
    std::atomic_int calls{0};
    LosslessAnalysisController controller(
        [&](const std::string& utf8Path,
            const agplayer::lossless::AnalysisOptions&,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            ++calls;
            auto result = completedResult(QString::fromUtf8(utf8Path),
                agplayer::lossless::Verdict::CredibleLossless);
            result.spectrum.db.assign(256, -48.0);
            return result;
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(calls.load(), 1);
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrum")).toList().size(), 256);
    QVERIFY(controller.cacheBytesForTesting() > 0);
    QVERIFY(controller.cacheBytesForTesting()
            <= controller.cacheLimitBytesForTesting());
    QVERIFY(controller.totalRetainedBytesForTesting()
            <= controller.cacheLimitBytesForTesting());

    QSignalSpy finished(&controller,
                        &LosslessAnalysisController::taskFinished);
    controller.retrySelected();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QCOMPARE(calls.load(), 1);
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrum")).toList().size(), 256);

    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("changed"), qint64(7));
    file.close();
    controller.retrySelected();
    QTRY_COMPARE_WITH_TIMEOUT(calls.load(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
}

void LosslessAnalysisControllerTest::realWavPublishesSpectrumAndCacheKeepsIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writePcm16Wav(
        directory, QStringLiteral("real-spectrum.wav"));
    QVERIFY(!path.isEmpty());
    LosslessAnalysisController controller;
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    // Full framing refinement is deliberately exercised twice (spectrum then spectrogram).
    // Measured dense-probe work exceeded the former 10s/30s waits; this is a
    // functional completion budget, not the separate cancellation latency target.
#ifdef NDEBUG
    constexpr int analysisTimeoutMs = 20000;
#else
    constexpr int analysisTimeoutMs = 60000;
#endif
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), analysisTimeoutMs);
    QCOMPARE(controller.tasks()->index(0, 0)
                 .data(LosslessTaskModel::StateRole).toString(),
             QStringLiteral("completed"));
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrum")).toList().size(), 256);

    controller.requestSpectrogram();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), analysisTimeoutMs);
    const QVariantList frames = controller.selectedResult()
                                    .value(QStringLiteral("spectrogram")).toList();
    QVERIFY(!frames.isEmpty());
    QVERIFY(frames.size() <= 256);
    const qsizetype bins = frames.constFirst().toList().size();
    QCOMPARE(bins, 256);
    for (const QVariant& frame : frames) {
        const QVariantList row = frame.toList();
        QCOMPARE(row.size(), bins);
        for (const QVariant& bin : row) {
            bool numeric = false;
            const double value = bin.toDouble(&numeric);
            QVERIFY(numeric && qIsFinite(value));
        }
    }
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrum")).toList().size(), 256);

    QSignalSpy finished(&controller,
                        &LosslessAnalysisController::taskFinished);
    controller.retrySelected();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QCOMPARE(controller.tasks()->index(0, 0)
                 .data(LosslessTaskModel::StateTextRole).toString(),
             QString::fromUtf8("已完成（缓存）"));
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrum")).toList().size(), 256);
}

void LosslessAnalysisControllerTest::requestsSpectrogramOnDemandAndFormatsListColumns()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("format.wav"));
    std::atomic_int calls{0};
    LosslessAnalysisController controller(
        [&](const std::string& utf8Path,
            const agplayer::lossless::AnalysisOptions& options,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            ++calls;
            return completedResult(QString::fromUtf8(utf8Path),
                agplayer::lossless::Verdict::CredibleLossless,
                options.includeSpectrogram);
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(calls.load(), 1);
    const QModelIndex row = controller.tasks()->index(0, 0);
    QCOMPARE(row.data(LosslessTaskModel::FormatNameRole).toString(),
             QStringLiteral("WAV"));
    QCOMPARE(row.data(LosslessTaskModel::AudioFormatRole).toString(),
             QString::fromUtf8("96 kHz · 24-bit"));
    QVERIFY(controller.selectedResult()
                .value(QStringLiteral("spectrogram")).toList().isEmpty());

    controller.requestSpectrogram();
    QTRY_COMPARE_WITH_TIMEOUT(calls.load(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrogram")).toList().size(), 2);
}

void LosslessAnalysisControllerTest::spectrogramRequestsDoNotCacheStrippedMatrices()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = writeFile(directory, QStringLiteral("first.wav"));
    const QString secondPath = writeFile(directory, QStringLiteral("second.wav"));
    std::atomic_int calls{0};
    LosslessAnalysisController controller(
        [&](const std::string& utf8Path,
            const agplayer::lossless::AnalysisOptions& options,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            ++calls;
            return completedResult(QString::fromUtf8(utf8Path),
                agplayer::lossless::Verdict::CredibleLossless,
                options.includeSpectrogram);
        }, nullptr);
    controller.loadFiles(urls({firstPath, secondPath}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 2, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(calls.load(), 2);

    const QString firstId = taskIdAt(controller.tasks(), 0);
    const QString secondId = taskIdAt(controller.tasks(), 1);
    controller.selectTask(firstId);
    controller.requestSpectrogram();
    QTRY_COMPARE_WITH_TIMEOUT(calls.load(), 3, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrogram")).toList().size(), 2);

    controller.selectTask(secondId);
    controller.selectTask(firstId);
    QVERIFY(controller.selectedResult()
                .value(QStringLiteral("spectrogram")).toList().isEmpty());
    controller.requestSpectrogram();
    QTRY_COMPARE_WITH_TIMEOUT(calls.load(), 4, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("spectrogram")).toList().size(), 2);
}

void LosslessAnalysisControllerTest::spectrogramRendererUsesFixedOpaqueDbScale()
{
    // Catches a heatmap whose evidence colors change with Theme.trace or with
    // the background showing through a dB-dependent alpha channel.
    TestableLosslessEvidenceItem item;
    item.setWidth(100.0);
    item.setHeight(100.0);
    item.setMode(LosslessEvidenceItem::Mode::Spectrogram);
    item.setSpectrogram({QVariant(QVariantList{-30.0})});
    item.setTraceColor(QColor(QStringLiteral("#ff0000")));
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    // Opaque evidence cells must be behind the grid, otherwise the scale grid
    // is fully covered even though its geometry still exists.
    const auto* firstLayer = static_cast<const QSGGeometryNode*>(
        node->firstChild());
    const auto* secondLayer = static_cast<const QSGGeometryNode*>(
        node->firstChild()->nextSibling());
    QCOMPARE(firstLayer->geometry()->drawingMode(), QSGGeometry::DrawTriangles);
    QCOMPARE(secondLayer->geometry()->drawingMode(), QSGGeometry::DrawLines);
    QSGGeometry* geometry = spectrogramGeometry(node);
    QVERIFY(geometry != nullptr);
    const auto redThemeColor = firstSpectrogramColor(geometry);

    item.setTraceColor(QColor(QStringLiteral("#00ffff")));
    node = item.updatePaintNode(node, nullptr);
    geometry = spectrogramGeometry(node);
    QVERIFY(geometry != nullptr);
    const auto cyanThemeColor = firstSpectrogramColor(geometry);
    QCOMPARE(cyanThemeColor, redThemeColor);
    QCOMPARE(static_cast<int>(cyanThemeColor[3]), 255);

    item.setSpectrogram({QVariant(QVariantList{
        -120.0, -90.0, -60.0, -30.0, 0.0})});
    node = item.updatePaintNode(node, nullptr);
    geometry = spectrogramGeometry(node);
    QVERIFY(geometry != nullptr);
    const auto* vertices = geometry->vertexDataAsColoredPoint2D();
    int previousLuminance = -1;
    for (int bin = 0; bin < 5; ++bin) {
        const auto& vertex = vertices[bin * 6];
        const int luminance = 2'126 * vertex.r + 7'152 * vertex.g
            + 722 * vertex.b;
        QVERIFY(luminance > previousLuminance);
        QCOMPARE(static_cast<int>(vertex.a), 255);
        previousLuminance = luminance;
    }
    delete node;
}

void LosslessAnalysisControllerTest::spectrogramRendererPeakReducesIntoFullBudget()
{
    // Catches both the former 128-bin renderer ceiling and nearest-neighbor
    // decimation, which discarded a narrow cell between selected samples.
    TestableLosslessEvidenceItem item;
    item.setWidth(256.0);
    item.setHeight(256.0);
    item.setMode(LosslessEvidenceItem::Mode::Spectrogram);
    item.setSpectrogram(spectrogramMatrix(512, 512));
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QSGGeometry* geometry = spectrogramGeometry(node);
    QVERIFY(geometry != nullptr);
    QCOMPARE(geometry->vertexCount(), 256 * 256 * 6);
    const auto floorColor = firstSpectrogramColor(geometry);
    delete node;

    item.setSpectrogram(spectrogramMatrix(512, 512, 1, 1));
    node = item.updatePaintNode(nullptr, nullptr);
    geometry = spectrogramGeometry(node);
    QVERIFY(geometry != nullptr);
    const auto preservedPeakColor = firstSpectrogramColor(geometry);
    QVERIFY(preservedPeakColor != floorColor);
    QCOMPARE(static_cast<int>(preservedPeakColor[3]), 255);
    delete node;
}

void LosslessAnalysisControllerTest::sourceChangesDuringAnalysisFailWithoutCaching()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original("audio");
    const QByteArray appended("changed");
    const QString path = writeFile(directory, QStringLiteral("changing.wav"),
                                   original);
    LosslessAnalysisController controller(
        [&](const std::string& utf8Path,
            const agplayer::lossless::AnalysisOptions&,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            const QString sourcePath = QString::fromUtf8(utf8Path);
            QFile source(sourcePath);
            if (source.open(QIODevice::Append)) source.write(appended);
            source.close();
            return completedResult(sourcePath,
                agplayer::lossless::Verdict::CredibleLossless);
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);

    const QModelIndex row = controller.tasks()->index(0, 0);
    QCOMPARE(row.data(LosslessTaskModel::StateRole).toString(),
             QStringLiteral("failed"));
    QVERIFY(controller.selectedResult().value(QStringLiteral("error"))
                .toString().contains(QString::fromUtf8("发生变化")));
    QCOMPARE(controller.cacheBytesForTesting(), qint64(0));
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), original + appended);
}

void LosslessAnalysisControllerTest::terminalStatusSummarizesAllOutcomes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString good = writeFile(directory, QStringLiteral("good.wav"));
    const QString failed = writeFile(directory, QStringLiteral("failed.wav"));
    const QString cancelled = writeFile(directory, QStringLiteral("cancelled.wav"));
    LosslessAnalysisController controller(
        [](const std::string& utf8Path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            const QString path = QString::fromUtf8(utf8Path);
            auto result = completedResult(path,
                agplayer::lossless::Verdict::CredibleLossless);
            if (path.endsWith(QStringLiteral("failed.wav"))) {
                result.verdict = agplayer::lossless::Verdict::AnalysisFailed;
                result.error = "decode failed";
            } else if (path.endsWith(QStringLiteral("cancelled.wav"))) {
                result.verdict = agplayer::lossless::Verdict::Cancelled;
                result.cancelled = true;
            }
            return result;
        }, nullptr);
    controller.loadFiles(urls({good, failed, cancelled}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 3, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);

    QCOMPARE(controller.completedCount(), 1);
    QCOMPARE(controller.statusText(),
             QString::fromUtf8("已处理 3/3（成功 1，失败 1，取消 1）"));
    controller.refreshTranslations();
    QCOMPARE(controller.statusText(),
             QString::fromUtf8("已处理 3/3（成功 1，失败 1，取消 1）"));
}

void LosslessAnalysisControllerTest::resamplingGridRequiresQualifiedEvidence_data()
{
    QTest::addColumn<double>("sourceRate");
    QTest::addColumn<int>("targetRate");
    QTest::addColumn<bool>("qualified");
    QTest::addColumn<int>("sourceKind");
    QTest::addColumn<QString>("expected");
    using agplayer::lossless::SourceKind;
    const int pcm = static_cast<int>(SourceKind::PcmInteger);
    QTest::newRow("qualified-grid") << 44100.0 << 96000 << true << pcm
        << QString::fromUtf8("44.1 kHz → 96 kHz");
    QTest::newRow("candidate-only") << 44100.0 << 96000 << false << pcm << QString{};
    QTest::newRow("zero-source") << 0.0 << 96000 << true << pcm << QString{};
    QTest::newRow("nan-source") << std::numeric_limits<double>::quiet_NaN()
        << 96000 << true << pcm << QString{};
    QTest::newRow("overflow-source") << std::numeric_limits<double>::max()
        << 96000 << true << pcm << QString{};
    QTest::newRow("invalid-target") << 44100.0 << 0 << true << pcm << QString{};
    QTest::newRow("not-upsampled") << 96000.0 << 44100 << true << pcm << QString{};
    QTest::newRow("dsd-hidden") << 44100.0 << 96000 << true
        << static_cast<int>(SourceKind::Dsd) << QString{};
    QTest::newRow("dst-hidden") << 44100.0 << 96000 << true
        << static_cast<int>(SourceKind::Dst) << QString{};
}

void LosslessAnalysisControllerTest::resamplingGridRequiresQualifiedEvidence()
{
    QFETCH(double, sourceRate);
    QFETCH(int, targetRate);
    QFETCH(bool, qualified);
    QFETCH(int, sourceKind);
    QFETCH(QString, expected);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("grid.wav"));
    LosslessAnalysisController controller(
        [=](const std::string& utf8Path,
            const agplayer::lossless::AnalysisOptions&,
            const std::atomic_bool&,
            agplayer::lossless::ProgressCallback) {
            using namespace agplayer::lossless;
            auto result = completedResult(QString::fromUtf8(utf8Path),
                                           Verdict::SuspectedUpsample);
            result.source.kind = static_cast<SourceKind>(sourceKind);
            result.source.sampleRate = targetRate;
            result.measurements.resamplingMirrorScore = 0.0;
            result.measurements.resamplingPhaseSourceRate = sourceRate;
            if (qualified) {
                result.evidence.push_back({"resampling_polyphase_grid",
                    EvidenceFamily::Resampling, EvidenceDirection::SupportsUpsampling,
                    sourceRate, "Hz", "qualified grid", 2, 0.0, 1.234,
                    "qualified resampling evidence"});
            }
            return result;
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    QCOMPARE(controller.selectedResult().value(QStringLiteral("resamplingText")).toString(),
             expected);
    controller.refreshTranslations();
    QCOMPARE(controller.selectedResult().value(QStringLiteral("resamplingText")).toString(),
             expected);
}

void LosslessAnalysisControllerTest::dsdSelectedResultUsesRawRateAndHidesPcmDiagnostics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("native.dsf"));
    LosslessAnalysisController controller(
        [](const std::string& utf8Path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            auto result = completedResult(
                QString::fromUtf8(utf8Path),
                agplayer::lossless::Verdict::CredibleNativeDsd);
            result.source.kind = agplayer::lossless::SourceKind::Dsd;
            result.source.codec = "dsd_lsbf_planar";
            result.source.sampleRate = 352800;
            result.source.rawDsdSampleRate = 2822400;
            result.source.bitsPerSample = 1;
            result.measurements.resamplingMirrorScore = 0.933;
            result.measurements.codecHoleScore = 0.875;
            return result;
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);

    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("sampleRate")).toInt(), 352800);
    QCOMPARE(controller.selectedResult()
                 .value(QStringLiteral("rawDsdSampleRate")).toInt(), 2822400);
    QVERIFY(controller.selectedResult()
                .value(QStringLiteral("resamplingText")).toString().isEmpty());
    QVERIFY(controller.selectedResult()
                .value(QStringLiteral("holesText")).toString().isEmpty());

    controller.refreshTranslations();
    QVERIFY(controller.selectedResult()
                .value(QStringLiteral("resamplingText")).toString().isEmpty());
    QVERIFY(controller.selectedResult()
                .value(QStringLiteral("holesText")).toString().isEmpty());
}

void LosslessAnalysisControllerTest::candidateFormatsTranslateInitiallyAndOnLanguageRefresh()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("opus-history.wav"));
    LosslessAnalysisController controller(
        [](const std::string& utf8Path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            auto result = completedResult(
                QString::fromUtf8(utf8Path),
                agplayer::lossless::Verdict::SuspectedLossyTranscode);
            result.candidates.clear();
            result.candidates.push_back(
                {"CELT帧结构", 87, "仅为信号特征推断"});
            result.candidates.push_back({"重采样或周期调制", 33, ""});
            result.measurements.resamplingMirrorScore = 0.0;
            result.measurements.resamplingPhaseSourceRate = 44100;
            result.evidence.push_back({"resampling_polyphase_grid",
                agplayer::lossless::EvidenceFamily::Resampling,
                agplayer::lossless::EvidenceDirection::Neutral,
                44100, "Hz", "", 1, 0.0, 1.234,
                "检测到稳定周期结构；重采样与周期调制均可形成，不能单独确定升频历史。"});
            result.chain.push_back("44100 Hz PCM（推测）");
            return result;
        }, nullptr);

    CandidateFormatTranslator translator;
    {
        const auto removeTranslator = qScopeGuard([&translator] {
            QCoreApplication::removeTranslator(&translator);
        });
        QVERIFY(QCoreApplication::installTranslator(&translator));
        controller.loadFiles(urls({path}));
        QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
        controller.start();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
        const QVariantMap candidate = controller.selectedResult()
            .value(QStringLiteral("candidates")).toList().first().toMap();
        QCOMPARE(candidate.value(QStringLiteral("format")).toString(),
                 QStringLiteral("CELT frame structure"));
        QVERIFY(controller.selectedResult().value(QStringLiteral("resamplingText")).toString().isEmpty());
        QCOMPARE(controller.selectedResult().value(QStringLiteral("candidates")).toList().last()
                     .toMap().value(QStringLiteral("format")).toString(),
                 QStringLiteral("Resampling or periodic modulation"));
        QCOMPARE(controller.selectedResult().value(QStringLiteral("evidence")).toList().last()
                     .toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Periodic structure does not establish upsampling history."));
        controller.refreshTranslations();
        QVERIFY(controller.selectedResult().value(QStringLiteral("resamplingText")).toString().isEmpty());
        QCOMPARE(controller.selectedResult().value(QStringLiteral("evidence")).toList().last()
                     .toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Periodic structure does not establish upsampling history."));
        QCOMPARE(controller.selectedResult().value(QStringLiteral("chain")).toList().last().toString(),
                 QStringLiteral("44100 Hz PCM (inferred)"));

        QSignalSpy exported(&controller,
                            &LosslessAnalysisController::reportExported);
        const QString reportPath = directory.filePath(
            QStringLiteral("candidate-report.json"));
        controller.exportReport(QUrl::fromLocalFile(reportPath),
                                QStringLiteral("json"));
        QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 3000);
        QFile reportFile(reportPath);
        QVERIFY(reportFile.open(QIODevice::ReadOnly));
        const QJsonDocument report = QJsonDocument::fromJson(reportFile.readAll());
        const QString rawFormat = report.object()
            .value(QStringLiteral("results")).toArray().first().toObject()
            .value(QStringLiteral("candidates")).toArray().first().toObject()
            .value(QStringLiteral("format")).toString();
        QCOMPARE(rawFormat, QString::fromUtf8("CELT帧结构"));
    }

    controller.refreshTranslations();
    const QVariantMap refreshedCandidate = controller.selectedResult()
        .value(QStringLiteral("candidates")).toList().first().toMap();
    QCOMPARE(refreshedCandidate.value(QStringLiteral("format")).toString(),
             QString::fromUtf8("CELT帧结构"));
    QVERIFY(controller.selectedResult().value(QStringLiteral("resamplingText")).toString().isEmpty());
    QCOMPARE(controller.selectedResult().value(QStringLiteral("candidates")).toList().last()
                 .toMap().value(QStringLiteral("format")).toString(),
             QString::fromUtf8("重采样或周期调制"));
}

void LosslessAnalysisControllerTest::rejectsUnboundedDiscoveryAndConcurrentReports()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = writeFile(directory, QStringLiteral("source.wav"));
    LosslessAnalysisController controller(
        [](const std::string& path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            return completedResult(QString::fromUtf8(path),
                agplayer::lossless::Verdict::CredibleLossless);
        }, nullptr);
    QVariantList excessive;
    excessive.reserve(10001);
    for (int index = 0; index < 10001; ++index) {
        excessive.append(QUrl::fromLocalFile(source));
    }
    controller.loadFiles(excessive);
    QVERIFY(controller.error().contains(QString::fromUtf8("上限")));
    QCOMPARE(controller.totalCount(), 0);

    controller.loadFiles(urls({source}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    controller.exportReport(
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("one.json"))),
        QStringLiteral("json"));
    controller.exportReport(
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("two.json"))),
        QStringLiteral("json"));
    QVERIFY(controller.error().contains(QString::fromUtf8("正在导出")));
}

void LosslessAnalysisControllerTest::exportsVersionedJsonAndCsvWithoutTouchingSources_data()
{
    QTest::addColumn<bool>("preEchoMeasured");
    QTest::addColumn<bool>("extendedMeasured");
    QTest::addColumn<bool>("extendedFinite");
    QTest::newRow("measurements-unavailable") << false << false << true;
    QTest::newRow("measurements-available") << true << true << true;
    QTest::newRow("non-finite-measurements") << true << true << false;
}

void LosslessAnalysisControllerTest::exportsVersionedJsonAndCsvWithoutTouchingSources()
{
    QFETCH(bool, preEchoMeasured);
    QFETCH(bool, extendedMeasured);
    QFETCH(bool, extendedFinite);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = writeFile(directory, QStringLiteral("source.wav"));
    const QByteArray sourceHash = sha256(source);
    LosslessAnalysisController controller(
        [preEchoMeasured, extendedMeasured, extendedFinite](const std::string& path,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback) {
            auto result = completedResult(QString::fromUtf8(path),
                agplayer::lossless::Verdict::CredibleLossless);
            result.measurements.transientPreEchoMeasured = preEchoMeasured;
            result.measurements.transientPreEchoScore = preEchoMeasured ? 0.125 : 0.0;
            result.measurements.transientCount = preEchoMeasured ? 7 : 0;
            const double measuredValue = extendedFinite
                ? 1.0 : std::numeric_limits<double>::quiet_NaN();
            result.measurements.lowLevelSpectralEdgeMeasured = extendedMeasured;
            result.measurements.lowLevelSpectralEdgeHz = extendedFinite
                ? 21500.25 : measuredValue;
            result.measurements.lowLevelSpectralEdgeDepthDb = extendedFinite
                ? 42.5 : measuredValue;
            result.measurements.resamplingPhaseSourceRate = extendedFinite
                ? 44100.0 : measuredValue;
            result.measurements.resamplingPhaseStrength = extendedFinite
                ? 0.03125 : measuredValue;
            result.measurements.resamplingPhaseCoherence = extendedFinite
                ? 0.9985 : measuredValue;
            result.measurements.resamplingPhaseSegments = extendedMeasured ? 5 : 0;
            result.measurements.resamplingResidualEntropyMeasured = extendedMeasured;
            result.measurements.resamplingResidualEntropy = extendedFinite
                ? 0.625 : measuredValue;
            result.measurements.resamplingBandSuppressionMeasured = extendedMeasured;
            result.measurements.resamplingBandSuppressionDb = extendedFinite
                ? 38.75 : measuredValue;
            result.measurements.mdctFrameCoherentPeakDb = extendedFinite
                ? 7.25 : measuredValue;
            result.measurements.mdctFramePeakZ = extendedFinite
                ? 13.5 : measuredValue;
            result.measurements.mdctFrameBlocks = extendedMeasured ? 6 : 0;
            result.measurements.mdctFrameAlignedBlocks = extendedMeasured ? 3 : 0;
            result.measurements.mdctFrameWindow = extendedMeasured
                ? "AAC-LC 1024" : "must-not-be-serialized";
            result.measurements.celtFrameBlocks = extendedMeasured ? 48 : 0;
            result.measurements.celtFrameSamples = 240;
            result.measurements.celtFramesPerAnchor = 48;
            result.measurements.mdctAnalysisSampleRate = 44100;
            result.measurements.celtFrameMinimumBandZ = extendedFinite
                ? 11.75 : measuredValue;
            result.measurements.celtFrameMinimumAnchorCoherence = extendedFinite
                ? 0.9375 : measuredValue;
            result.measurements.celtFrameMaximumPeakWidth = extendedMeasured ? 2 : 91;
            result.measurements.celtFrameBandPhaseDifference = extendedMeasured ? 1 : 37;
            return result;
        }, nullptr);
    controller.loadFiles(urls({source}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);

    for (int refresh = 0; refresh < 2; ++refresh) {
        const auto evidence = controller.selectedResult().value(QStringLiteral("evidence"))
            .toList().first().toMap();
        QCOMPARE(evidence.value(QStringLiteral("reference")).toString(), QStringLiteral("> 20 kHz"));
        QCOMPARE(evidence.value(QStringLiteral("coverageStartSeconds")).toDouble(), 0.0);
        QCOMPARE(evidence.value(QStringLiteral("coverageEndSeconds")).toDouble(), 1.234);
        controller.refreshTranslations();
    }

    QSignalSpy exported(&controller,
                        &LosslessAnalysisController::reportExported);
    const QString jsonPath = directory.filePath(QStringLiteral("report.json"));
    controller.exportReport(QUrl::fromLocalFile(jsonPath),
                            QStringLiteral("json"));
    QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 3000);
    QFile jsonFile(jsonPath);
    QVERIFY(jsonFile.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        jsonFile.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toString(),
             QStringLiteral("1"));
    QCOMPARE(document.object().value(QStringLiteral("results")).toArray().size(),
             1);
    const auto firstResult = document.object().value(QStringLiteral("results"))
        .toArray().first().toObject();
    QCOMPARE(QDateTime::fromString(firstResult.value(QStringLiteral("analysisStartedAt"))
                 .toString(), Qt::ISODateWithMs).toMSecsSinceEpoch(),
             qint64(1'780'000'000'000LL));
    QVERIFY(!document.object().value(QStringLiteral("appVersion")).toString().isEmpty());
    const QJsonObject scoredResult = document.object()
        .value(QStringLiteral("results")).toArray().first().toObject();
    QCOMPARE(scoredResult.value(QStringLiteral("confidenceKind")).toString(),
             QStringLiteral("ordinal_evidence_score"));
    QCOMPARE(scoredResult.value(QStringLiteral("calibrationStatus")).toString(),
             QStringLiteral("uncalibrated"));
    QVERIFY(scoredResult.contains(QStringLiteral("calibratedProbability")));
    QVERIFY(scoredResult.value(QStringLiteral("calibratedProbability")).isNull());
    const QJsonObject measurements = document.object()
        .value(QStringLiteral("results")).toArray().first().toObject()
        .value(QStringLiteral("measurements")).toObject();
    QCOMPARE(measurements.value(
                 QStringLiteral("spectralEdgeFrequencyHz")).toDouble(),
             21850.5);
    QCOMPARE(measurements.value(
                 QStringLiteral("spectralEdgeDepthDb")).toDouble(),
             34.25);
    QCOMPARE(measurements.value(
                 QStringLiteral("spectralEdgeStability")).toDouble(),
             0.875);
    QCOMPARE(measurements.value(QStringLiteral("transientPreEchoMeasured")).toBool(), preEchoMeasured);
    QVERIFY(measurements.contains(QStringLiteral("transientCount")));
    QCOMPARE(measurements.value(QStringLiteral("transientCount")).toInt(), preEchoMeasured ? 7 : 0);
    if (preEchoMeasured)
        QCOMPARE(measurements.value(QStringLiteral("transientPreEchoScore")).toDouble(), 0.125);
    else
        QVERIFY(measurements.value(QStringLiteral("transientPreEchoScore")).isNull());
    QCOMPARE(measurements.value(QStringLiteral("lowLevelSpectralEdgeMeasured")).toBool(),
             extendedMeasured);
    QCOMPARE(measurements.value(QStringLiteral("resamplingResidualEntropyMeasured")).toBool(),
             extendedMeasured);
    QCOMPARE(measurements.value(QStringLiteral("resamplingBandSuppressionMeasured")).toBool(),
             extendedMeasured);
    QCOMPARE(measurements.value(QStringLiteral("resamplingPhaseSegments")).toInt(),
             extendedMeasured ? 5 : 0);
    QCOMPARE(measurements.value(QStringLiteral("mdctFrameBlocks")).toInt(),
             extendedMeasured ? 6 : 0);
    QCOMPARE(measurements.value(QStringLiteral("mdctFrameAlignedBlocks")).toInt(),
             extendedMeasured ? 3 : 0);
    QCOMPARE(measurements.value(QStringLiteral("celtFrameBlocks")).toInt(),
             extendedMeasured ? 48 : 0);
    QVERIFY(measurements.contains(QStringLiteral("celtFrameSamples")));
    QVERIFY(measurements.contains(QStringLiteral("celtFramesPerAnchor")));
    QVERIFY(measurements.contains(QStringLiteral("mdctAnalysisSampleRate")));
    if (extendedMeasured)
        QCOMPARE(measurements.value(QStringLiteral("mdctAnalysisSampleRate")).toInt(), 44100);
    else
        QVERIFY(measurements.value(QStringLiteral("mdctAnalysisSampleRate")).isNull());
    if (extendedMeasured) {
        QCOMPARE(measurements.value(QStringLiteral("celtFrameSamples")).toInt(), 240);
        QCOMPARE(measurements.value(QStringLiteral("celtFramesPerAnchor")).toInt(), 48);
    } else {
        QVERIFY(measurements.value(QStringLiteral("celtFrameSamples")).isNull());
        QVERIFY(measurements.value(QStringLiteral("celtFramesPerAnchor")).isNull());
    }
    const QVariantMap nullableMeasurements{
        {QStringLiteral("lowLevelSpectralEdgeHz"), 21500.25},
        {QStringLiteral("lowLevelSpectralEdgeDepthDb"), 42.5},
        {QStringLiteral("resamplingPhaseSourceRate"), 44100.0},
        {QStringLiteral("resamplingPhaseStrength"), 0.03125},
        {QStringLiteral("resamplingPhaseCoherence"), 0.9985},
        {QStringLiteral("resamplingResidualEntropy"), 0.625},
        {QStringLiteral("resamplingBandSuppressionDb"), 38.75},
        {QStringLiteral("mdctFrameCoherentPeakDb"), 7.25},
        {QStringLiteral("mdctFramePeakZ"), 13.5},
        {QStringLiteral("celtFrameMinimumBandZ"), 11.75},
        {QStringLiteral("celtFrameMinimumAnchorCoherence"), 0.9375},
    };
    for (auto it = nullableMeasurements.cbegin();
         it != nullableMeasurements.cend(); ++it) {
        const QString& key = it.key();
        const QJsonValue value = measurements.value(key);
        if (extendedMeasured && extendedFinite) {
            QVERIFY2(value.isDouble(), qPrintable(key));
            QVERIFY2(std::isfinite(value.toDouble()), qPrintable(key));
            QCOMPARE(value.toDouble(), it.value().toDouble());
        } else {
            QVERIFY2(value.isNull(), qPrintable(key));
        }
    }
    QCOMPARE(measurements.value(QStringLiteral("mdctFrameWindow")).toString(),
             extendedMeasured ? QStringLiteral("AAC-LC 1024") : QString{});
    const QJsonValue celtPeakWidth = measurements.value(
        QStringLiteral("celtFrameMaximumPeakWidth"));
    const QJsonValue celtPhaseDifference = measurements.value(
        QStringLiteral("celtFrameBandPhaseDifference"));
    if (extendedMeasured) {
        QVERIFY(celtPeakWidth.isDouble());
        QCOMPARE(celtPeakWidth.toInt(), 2);
        QVERIFY(celtPhaseDifference.isDouble());
        QCOMPARE(celtPhaseDifference.toInt(), 1);
    } else {
        QVERIFY(celtPeakWidth.isNull());
        QVERIFY(celtPhaseDifference.isNull());
    }
    QVERIFY(!document.object().value(QStringLiteral("disclaimer")).toString()
                 .isEmpty());

    const QString csvPath = directory.filePath(QStringLiteral("report.csv"));
    {
        QFile stale(csvPath);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        QCOMPARE(stale.write("stale"), qint64(5));
    }
    controller.exportReport(QUrl::fromLocalFile(csvPath),
                            QStringLiteral("csv"));
    QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 2, 3000);
    QFile csvFile(csvPath);
    QVERIFY(csvFile.open(QIODevice::ReadOnly));
    const QByteArray csv = csvFile.readAll();
    QVERIFY(csv.startsWith("schemaVersion,algorithmVersion"));
    QVERIFY(csv.contains("confidence,confidenceKind,calibrationStatus,calibratedProbability"));
    QVERIFY(csv.contains("\"ordinal_evidence_score\",\"uncalibrated\",\"\""));
    QVERIFY(csv.contains("appVersion,generatedAt,parameterVersion,analysisStartedAt"));
    QVERIFY(csv.contains("sourceDetails,measurements"));
    QVERIFY(csv.contains("\"\"rawDsdSampleRate\"\":0"));
    QVERIFY(csv.contains("\"\"stftWindowSizes\"\":"));
    QVERIFY(csv.contains("spectralEdgeFrequencyHz,spectralEdgeDepthDb,"
                         "spectralEdgeStability"));
    QVERIFY(csv.contains("transientPreEchoMeasured,transientPreEchoScore,transientCount"));
    QVERIFY(csv.contains("\"21850.5\",\"34.25\",\"0.875\""));
    QVERIFY(csv.contains(preEchoMeasured ? "\"true\",\"0.125\",\"7\"" : "\"false\",\"\",\"0\""));
    QVERIFY(csv.contains("\"\"lowLevelSpectralEdgeMeasured\"\":"));
    QVERIFY(csv.contains("\"\"resamplingPhaseSegments\"\":"));
    QVERIFY(csv.contains("\"\"resamplingResidualEntropyMeasured\"\":"));
    QVERIFY(csv.contains("\"\"resamplingBandSuppressionMeasured\"\":"));
    QVERIFY(csv.contains("\"\"mdctFrameBlocks\"\":"));
    QVERIFY(csv.contains(extendedMeasured
        ? "\"\"mdctFrameAlignedBlocks\"\":3"
        : "\"\"mdctFrameAlignedBlocks\"\":0"));
    QVERIFY(csv.contains("\"\"mdctFrameWindow\"\":"));
    QVERIFY(csv.contains("\"\"celtFrameBlocks\"\":"));
    QVERIFY(csv.contains("\"\"celtFrameMinimumBandZ\"\":"));
    QVERIFY(csv.contains("\"\"celtFrameMinimumAnchorCoherence\"\":"));
    QVERIFY(csv.contains("\"\"celtFrameMaximumPeakWidth\"\":"));
    QVERIFY(csv.contains("\"\"celtFrameBandPhaseDifference\"\":"));
    for (auto it = nullableMeasurements.cbegin();
         it != nullableMeasurements.cend(); ++it) {
        QVERIFY2(csv.contains((QStringLiteral("\"\"") + it.key()
                               + QStringLiteral("\"\":")).toUtf8()),
                 qPrintable(it.key()));
    }
    if (extendedMeasured && extendedFinite) {
        QVERIFY(csv.contains("\"\"lowLevelSpectralEdgeHz\"\":21500.25"));
        QVERIFY(csv.contains("\"\"resamplingResidualEntropy\"\":0.625"));
        QVERIFY(csv.contains("\"\"resamplingBandSuppressionDb\"\":38.75"));
        QVERIFY(csv.contains("\"\"mdctFrameCoherentPeakDb\"\":7.25"));
        QVERIFY(csv.contains("\"\"celtFrameMinimumBandZ\"\":11.75"));
        QVERIFY(csv.contains("\"\"celtFrameMinimumAnchorCoherence\"\":0.9375"));
    } else {
        QVERIFY(csv.contains("\"\"lowLevelSpectralEdgeHz\"\":null"));
        QVERIFY(csv.contains("\"\"resamplingResidualEntropy\"\":null"));
        QVERIFY(csv.contains("\"\"resamplingBandSuppressionDb\"\":null"));
        QVERIFY(csv.contains("\"\"mdctFrameCoherentPeakDb\"\":null"));
        QVERIFY(csv.contains("\"\"celtFrameMinimumBandZ\"\":null"));
        QVERIFY(csv.contains("\"\"celtFrameMinimumAnchorCoherence\"\":null"));
    }
    QVERIFY(csv.contains(extendedMeasured
        ? "\"\"celtFrameMaximumPeakWidth\"\":2"
        : "\"\"celtFrameMaximumPeakWidth\"\":null"));
    QVERIFY(csv.contains(extendedMeasured
        ? "\"\"celtFrameBandPhaseDifference\"\":1"
        : "\"\"celtFrameBandPhaseDifference\"\":null"));
    QVERIFY(csv.contains("credible_lossless"));
    QVERIFY(csv.contains("disclaimer"));
    QVERIFY(csv.contains(QString::fromUtf8(
        agplayer::lossless::kInferenceDisclaimer.data(),
        static_cast<qsizetype>(
            agplayer::lossless::kInferenceDisclaimer.size())).toUtf8()));
    QCOMPARE(sha256(source), sourceHash);

    QSignalSpy failed(&controller,
                      &LosslessAnalysisController::reportExportFailed);
    controller.exportReport(QUrl::fromLocalFile(source),
                            QStringLiteral("json"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(sha256(source), sourceHash);

    const QString impossible = directory.filePath(
        QStringLiteral("missing/sub/report.json"));
    controller.exportReport(QUrl::fromLocalFile(impossible),
                            QStringLiteral("json"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 3000);
    QVERIFY(!QFileInfo::exists(impossible));
}

void LosslessAnalysisControllerTest::reportCancellationPreservesExistingDestination()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("report.json"));
    const QByteArray sentinel("existing report");
    const auto restoreSentinel = [&] {
        QFile file(destination);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(sentinel), sentinel.size());
    };
    const auto readDestination = [&] {
        QFile file(destination);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
        return file.readAll();
    };
    const QVariantList results{QVariantMap{
        {QStringLiteral("payload"), QString(200 * 1024, QLatin1Char('x'))},
    }};

    restoreSentinel();
    std::atomic_int checks{0};
    const LosslessReportWriteResult midWrite = LosslessReport::write(
        destination, QStringLiteral("json"), results, {}, [&] {
            return ++checks >= 4;
        });
    QVERIFY(!midWrite.success);
    QVERIFY(midWrite.error.contains(QString::fromUtf8("取消")));
    QVERIFY(checks.load() >= 4);
    QCOMPARE(readDestination(), sentinel);

    restoreSentinel();
    const LosslessReportWriteResult preCancelled = LosslessReport::write(
        destination, QStringLiteral("json"), results, {}, [] { return true; });
    QVERIFY(!preCancelled.success);
    QVERIFY(preCancelled.error.contains(QString::fromUtf8("取消")));
    QCOMPARE(readDestination(), sentinel);
}

void LosslessAnalysisControllerTest::unknownWorkerExceptionsBecomeExplicitFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("broken.wav"));
    LosslessAnalysisController controller(
        [](const std::string&,
           const agplayer::lossless::AnalysisOptions&,
           const std::atomic_bool&,
           agplayer::lossless::ProgressCallback)
            -> agplayer::lossless::AnalysisResult {
            throw 42;
        }, nullptr);
    controller.loadFiles(urls({path}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.totalCount(), 1, 3000);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 3000);
    controller.selectTask(taskIdAt(controller.tasks(), 0));
    QVERIFY(controller.selectedResult().value(QStringLiteral("error"))
                .toString().contains(QString::fromUtf8("未知")));
    QCOMPARE(controller.tasks()->index(0, 0)
                 .data(LosslessTaskModel::StateRole).toString(),
             QStringLiteral("failed"));
}

QTEST_MAIN(LosslessAnalysisControllerTest)

#include "lossless_analysis_controller_test.moc"
