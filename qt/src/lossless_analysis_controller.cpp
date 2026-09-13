#include "lossless_analysis_controller.hpp"

#include "lossless/lossless_analyzer.hpp"
#include "lossless/sacd_reader.hpp"
#include "lossless_report.hpp"
#include "lossless_task_model.hpp"
#include "resource_path.hpp"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QTimeZone>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace {

constexpr int kDefaultConcurrency = 2;
constexpr int kMaximumConcurrency = 4;
constexpr int kMaximumPendingDiscoveryJobs = 4;
constexpr int kMaximumRequestedInputs = 10000;
constexpr int kMaximumTasks = 1500;
constexpr qint64 kDefaultCacheLimit = 64LL * 1024LL * 1024LL;

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QVariant finiteMeasurement(const double value, const bool measured)
{
    return measured && std::isfinite(value) ? QVariant(value) : QVariant{};
}

QString verdictText(const agplayer::lossless::Verdict verdict)
{
    using agplayer::lossless::Verdict;
    switch (verdict) {
    case Verdict::CredibleLossless:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "可信无损");
    case Verdict::SuspectedLossyTranscode:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "疑似有损转码");
    case Verdict::SuspectedUpsample:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "疑似升频");
    case Verdict::SuspectedBitDepthExpansion:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "疑似扩位");
    case Verdict::SuspectedLossyUpsample:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "疑似有损转码后升频");
    case Verdict::CredibleNativeDsd:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "DSD 来源可信");
    case Verdict::SuspectedPcmToDsd:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "疑似 PCM 转 DSD");
    case Verdict::Inconclusive:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "无法可靠判定");
    case Verdict::AnalysisFailed:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "分析失败");
    case Verdict::Cancelled:
        return QCoreApplication::translate(
            "LosslessAnalysisController", "已取消");
    }
    return QCoreApplication::translate(
        "LosslessAnalysisController", "分析失败");
}

QString reportVerdictText(const agplayer::lossless::Verdict verdict)
{
    using agplayer::lossless::Verdict;
    switch (verdict) {
    case Verdict::CredibleLossless: return QString::fromUtf8("可信无损");
    case Verdict::SuspectedLossyTranscode:
        return QString::fromUtf8("疑似有损转码");
    case Verdict::SuspectedUpsample: return QString::fromUtf8("疑似升频");
    case Verdict::SuspectedBitDepthExpansion:
        return QString::fromUtf8("疑似扩位");
    case Verdict::SuspectedLossyUpsample:
        return QString::fromUtf8("疑似有损转码后升频");
    case Verdict::CredibleNativeDsd: return QString::fromUtf8("DSD 来源可信");
    case Verdict::SuspectedPcmToDsd: return QString::fromUtf8("疑似 PCM 转 DSD");
    case Verdict::Inconclusive: return QString::fromUtf8("无法可靠判定");
    case Verdict::AnalysisFailed: return QString::fromUtf8("分析失败");
    case Verdict::Cancelled: return QString::fromUtf8("已取消");
    }
    return QString::fromUtf8("分析失败");
}

QString translatedVerdictCode(const QString& code)
{
    using agplayer::lossless::Verdict;
    if (code == QStringLiteral("credible_lossless"))
        return verdictText(Verdict::CredibleLossless);
    if (code == QStringLiteral("suspected_lossy_transcode"))
        return verdictText(Verdict::SuspectedLossyTranscode);
    if (code == QStringLiteral("suspected_upsample"))
        return verdictText(Verdict::SuspectedUpsample);
    if (code == QStringLiteral("suspected_bit_depth_expansion"))
        return verdictText(Verdict::SuspectedBitDepthExpansion);
    if (code == QStringLiteral("suspected_lossy_upsample"))
        return verdictText(Verdict::SuspectedLossyUpsample);
    if (code == QStringLiteral("credible_native_dsd"))
        return verdictText(Verdict::CredibleNativeDsd);
    if (code == QStringLiteral("suspected_pcm_to_dsd"))
        return verdictText(Verdict::SuspectedPcmToDsd);
    if (code == QStringLiteral("inconclusive"))
        return verdictText(Verdict::Inconclusive);
    if (code == QStringLiteral("analysis_failed"))
        return verdictText(Verdict::AnalysisFailed);
    if (code == QStringLiteral("cancelled"))
        return verdictText(Verdict::Cancelled);
    return {};
}

QString verdictCodeString(const agplayer::lossless::Verdict verdict)
{
    const std::string_view code = agplayer::lossless::verdictCode(verdict);
    return QString::fromLatin1(code.data(), static_cast<qsizetype>(code.size()));
}

QString stateText(const QString& state)
{
    if (state == QStringLiteral("waiting"))
        return QCoreApplication::translate("LosslessAnalysisController", "等待");
    if (state == QStringLiteral("probing"))
        return QCoreApplication::translate("LosslessAnalysisController", "探测中");
    if (state == QStringLiteral("decoding"))
        return QCoreApplication::translate("LosslessAnalysisController", "解码中");
    if (state == QStringLiteral("analyzing"))
        return QCoreApplication::translate("LosslessAnalysisController", "分析中");
    if (state == QStringLiteral("summarizing"))
        return QCoreApplication::translate("LosslessAnalysisController", "汇总中");
    if (state == QStringLiteral("completed"))
        return QCoreApplication::translate("LosslessAnalysisController", "已完成");
    if (state == QStringLiteral("failed"))
        return QCoreApplication::translate("LosslessAnalysisController", "失败");
    if (state == QStringLiteral("cancelling"))
        return QCoreApplication::translate("LosslessAnalysisController", "正在取消");
    if (state == QStringLiteral("cancelled"))
        return QCoreApplication::translate("LosslessAnalysisController", "已取消");
    return {};
}

QString sampleRateText(const int sampleRate)
{
    if (sampleRate <= 0) return {};
    if (sampleRate % 1000 == 0) {
        return QStringLiteral("%1 kHz").arg(sampleRate / 1000);
    }
    return QStringLiteral("%1 kHz")
        .arg(static_cast<double>(sampleRate) / 1000.0, 0, 'f', 1);
}

QString resamplingGridText(const double sourceRate, const int targetRate)
{
    if (!std::isfinite(sourceRate) || sourceRate <= 0.0
        || targetRate <= 0 || sourceRate >= targetRate
        || std::floor(sourceRate) != sourceRate) {
        return {};
    }
    return QStringLiteral("%1 → %2")
        .arg(sampleRateText(static_cast<int>(sourceRate)), sampleRateText(targetRate));
}

QString audioFormatText(const agplayer::lossless::SourceFormat& source)
{
    using agplayer::lossless::SourceKind;
    if (source.kind == SourceKind::Dsd || source.kind == SourceKind::Dst) {
        const int rate = source.rawDsdSampleRate > 0
            ? source.rawDsdSampleRate : source.sampleRate;
        const int multiple = rate > 0
            ? std::max(1, qRound(static_cast<double>(rate) / 44100.0)) : 0;
        QString text = source.kind == SourceKind::Dst
            ? QStringLiteral("DST") : QStringLiteral("DSD");
        if (multiple > 0) text += QString::number(multiple);
        if (source.channels > 0) {
            text += QCoreApplication::translate(
                        "LosslessAnalysisController", " · %1 声道")
                        .arg(source.channels);
        }
        return text;
    }
    QString text = sampleRateText(source.sampleRate);
    if (source.bitsPerSample > 0) {
        if (!text.isEmpty()) text += QStringLiteral(" · ");
        text += QStringLiteral("%1-bit").arg(source.bitsPerSample);
        if (source.kind == SourceKind::PcmFloatingPoint) {
            text += QStringLiteral(" float");
        }
    }
    return text;
}

QString displayContainerName(const agplayer::lossless::SourceFormat& source)
{
    const QString container = fromUtf8(source.container);
    if (container.compare(QStringLiteral("sacd_iso"),
                          Qt::CaseInsensitive) == 0) {
        return QStringLiteral("SACD ISO");
    }
    if ((source.kind == agplayer::lossless::SourceKind::Dsd
         || source.kind == agplayer::lossless::SourceKind::Dst)
        && container.compare(QStringLiteral("iff"),
                             Qt::CaseInsensitive) == 0) {
        return QStringLiteral("DFF");
    }
    return container.toUpper();
}

QString sourceKindCode(const agplayer::lossless::SourceKind kind)
{
    using agplayer::lossless::SourceKind;
    switch (kind) {
    case SourceKind::Unknown: return QStringLiteral("unknown");
    case SourceKind::PcmInteger: return QStringLiteral("pcm_integer");
    case SourceKind::PcmFloatingPoint: return QStringLiteral("pcm_float");
    case SourceKind::Dsd: return QStringLiteral("dsd");
    case SourceKind::Dst: return QStringLiteral("dst");
    }
    return QStringLiteral("unknown");
}

QString evidenceFamilyCode(const agplayer::lossless::EvidenceFamily family)
{
    using agplayer::lossless::EvidenceFamily;
    switch (family) {
    case EvidenceFamily::Bandwidth: return QStringLiteral("bandwidth");
    case EvidenceFamily::CodecStructure: return QStringLiteral("codec_structure");
    case EvidenceFamily::Quantization: return QStringLiteral("quantization");
    case EvidenceFamily::Resampling: return QStringLiteral("resampling");
    case EvidenceFamily::Dsd: return QStringLiteral("dsd");
    case EvidenceFamily::ContentQuality: return QStringLiteral("content_quality");
    }
    return QStringLiteral("content_quality");
}

QString evidenceDirectionCode(
    const agplayer::lossless::EvidenceDirection direction)
{
    using agplayer::lossless::EvidenceDirection;
    switch (direction) {
    case EvidenceDirection::Neutral: return QStringLiteral("neutral");
    case EvidenceDirection::SupportsAuthenticity:
        return QStringLiteral("supports_authenticity");
    case EvidenceDirection::SupportsLossySource:
        return QStringLiteral("supports_lossy_source");
    case EvidenceDirection::SupportsUpsampling:
        return QStringLiteral("supports_upsampling");
    case EvidenceDirection::SupportsBitDepthExpansion:
        return QStringLiteral("supports_bit_depth_expansion");
    case EvidenceDirection::SupportsPcmToDsd:
        return QStringLiteral("supports_pcm_to_dsd");
    case EvidenceDirection::ContradictsConclusion:
        return QStringLiteral("contradicts_conclusion");
    }
    return QStringLiteral("neutral");
}

QString canonicalPathKey(const QString& path)
{
    QString identity = agplayer::qt::resourcePathIdentity(path);
    if (agplayer::qt::resourcePathCaseSensitivity() == Qt::CaseInsensitive) {
        identity = identity.toCaseFolded();
    }
    return identity;
}

QString inputIdentity(const QString& path, const int isoTrackIndex)
{
    return canonicalPathKey(path) + QStringLiteral("#iso=")
        + QString::number(isoTrackIndex);
}

QString taskIdForIdentity(const QString& identity)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}

const QSet<QString>& supportedExtensions()
{
    static const QSet<QString> extensions{
        QStringLiteral("wav"), QStringLiteral("wave"),
        QStringLiteral("rf64"), QStringLiteral("bwf"),
        QStringLiteral("aif"), QStringLiteral("aiff"),
        QStringLiteral("flac"), QStringLiteral("alac"),
        QStringLiteral("m4a"), QStringLiteral("ape"),
        QStringLiteral("wv"), QStringLiteral("wavpack"),
        QStringLiteral("mp3"), QStringLiteral("aac"),
        QStringLiteral("ogg"), QStringLiteral("opus"),
        QStringLiteral("vorbis"), QStringLiteral("wma"),
        QStringLiteral("dsf"), QStringLiteral("dff"),
        QStringLiteral("dsdiff"), QStringLiteral("dst"),
        QStringLiteral("iso"), QStringLiteral("mp4"),
        QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("mov"), QStringLiteral("avi"),
        QStringLiteral("m4v")};
    return extensions;
}

bool terminalState(const QString& state)
{
    return state == QStringLiteral("completed")
        || state == QStringLiteral("failed")
        || state == QStringLiteral("cancelled");
}

QVariantList stringList(const std::vector<std::string>& values)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const std::string& value : values) result.append(fromUtf8(value));
    return result;
}

QString translatedEvidence(const QString& value)
{
    const QString suffix = QStringLiteral(" Hz PCM（推测）");
    if (value.endsWith(suffix)) {
        const QString rate = value.left(value.size() - suffix.size());
        bool valid = false;
        const int numericRate = rate.toInt(&valid);
        if (valid && numericRate > 0)
            return QCoreApplication::translate("LosslessEvidence", "%1 Hz PCM（推测）").arg(rate);
    }
    const QByteArray utf8 = value.toUtf8();
    return value.isEmpty() ? QString{} : QCoreApplication::translate(
        "LosslessEvidence", utf8.constData());
}

QString translatedEvidence(const std::string& value)
{
    return translatedEvidence(QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())));
}

QVariantList translatedEvidenceList(const std::vector<std::string>& values)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const std::string& value : values) {
        result.append(translatedEvidence(value));
    }
    return result;
}

QVariantList doubleList(const std::vector<double>& values)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const double value : values) result.append(value);
    return result;
}

QVariantMap emptySelectedResult()
{
    return {
        {QStringLiteral("taskId"), {}},
        {QStringLiteral("fileName"), {}},
        {QStringLiteral("path"), {}},
        {QStringLiteral("formatName"), {}},
        {QStringLiteral("codec"), {}},
        {QStringLiteral("sampleRate"), {}},
        {QStringLiteral("rawDsdSampleRate"), {}},
        {QStringLiteral("bitsPerSample"), {}},
        {QStringLiteral("channels"), {}},
        {QStringLiteral("durationMs"), {}},
        {QStringLiteral("fileSize"), {}},
        {QStringLiteral("modified"), {}},
        {QStringLiteral("verdictCode"), {}},
        {QStringLiteral("verdictText"), {}},
        {QStringLiteral("confidence"), {}},
        {QStringLiteral("coverage"), {}},
        {QStringLiteral("cutoffHz"), {}},
        {QStringLiteral("effectiveBits"), {}},
        {QStringLiteral("resamplingText"), {}},
        {QStringLiteral("holesText"), {}},
        {QStringLiteral("evidence"), QVariantList{}},
        {QStringLiteral("candidates"), QVariantList{}},
        {QStringLiteral("chain"), QVariantList{}},
        {QStringLiteral("spectrum"), QVariantList{}},
        {QStringLiteral("spectrogram"), QVariantList{}},
        {QStringLiteral("error"), {}},
        {QStringLiteral("warnings"), QVariantList{}},
    };
}

qint64 estimatedResultBytes(const agplayer::lossless::AnalysisResult& result)
{
    qint64 bytes = static_cast<qint64>(sizeof(result));
    const auto add = [&bytes](const std::string& text) {
        bytes += static_cast<qint64>(text.size());
    };
    add(result.schemaVersion);
    add(result.algorithmVersion);
    add(result.parameterVersion);
    add(result.file.path);
    add(result.source.container);
    add(result.source.codec);
    add(result.source.channelLayout);
    for (const auto& item : result.evidence) {
        bytes += static_cast<qint64>(sizeof(item));
        add(item.code);
        add(item.unit);
        add(item.reference);
        add(item.explanation);
    }
    for (const auto& item : result.candidates) {
        bytes += static_cast<qint64>(sizeof(item));
        add(item.format);
        add(item.limitation);
    }
    for (const auto& item : result.chain) add(item);
    for (const auto& item : result.warnings) add(item);
    add(result.error);
    bytes += static_cast<qint64>(result.spectrum.db.size() * sizeof(double));
    bytes += static_cast<qint64>(result.spectrogram.db.size() * sizeof(float));
    bytes += static_cast<qint64>(
        result.measurements.stftWindowSizes.size() * sizeof(std::size_t));
    return std::max<qint64>(bytes, 1);
}

qint64 estimatedVariantBytes(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return 0;
    if (value.metaType().id() == QMetaType::QString) {
        return static_cast<qint64>(value.toString().size()) * 2;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        qint64 bytes = static_cast<qint64>(sizeof(QVariantList));
        for (const QVariant& item : value.toList()) {
            bytes += static_cast<qint64>(sizeof(QVariant));
            bytes += estimatedVariantBytes(item);
        }
        return bytes;
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        qint64 bytes = static_cast<qint64>(sizeof(QVariantMap));
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            bytes += static_cast<qint64>(it.key().size()) * 2;
            bytes += static_cast<qint64>(sizeof(QVariant));
            bytes += estimatedVariantBytes(it.value());
        }
        return bytes;
    }
    return static_cast<qint64>(sizeof(QVariant));
}

agplayer::lossless::AnalysisResult compactResult(
    const agplayer::lossless::AnalysisResult& source,
    const bool includeSpectrogram)
{
    agplayer::lossless::AnalysisResult result = source;
    if (!includeSpectrogram) result.spectrogram = {};
    if (result.spectrum.db.size() > 256) result.spectrum.db.resize(256);
    if (result.evidence.size() > 32) result.evidence.resize(32);
    if (result.candidates.size() > 8) result.candidates.resize(8);
    if (result.chain.size() > 8) result.chain.resize(8);
    if (result.warnings.size() > 16) result.warnings.resize(16);
    if (result.measurements.stftWindowSizes.size() > 8) {
        result.measurements.stftWindowSizes.resize(8);
    }
    return result;
}

} // namespace

class LosslessAnalysisController::Private final {
public:
    struct DiscoveredInput final {
        QString path;
        QString fileName;
        QString formatName;
        QString audioFormat;
        int isoTrackIndex = -1;
        int channels = 0;
        qint64 durationMs = 0;
        QString error;
    };

    struct DiscoveryOutcome final {
        QVector<DiscoveredInput> inputs;
        QStringList errors;
        bool cancelled = false;
    };

    struct WorkerOutcome final {
        QString taskId;
        quint64 generation = 0;
        QString cacheKey;
        agplayer::lossless::AnalysisResult result;
    };

    struct ActiveJob final {
        quint64 generation = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
        QFutureWatcherBase* watcher = nullptr;
    };

    struct DiscoveryJob final {
        std::shared_ptr<std::atomic_bool> cancelled;
        QFutureWatcherBase* watcher = nullptr;
    };

    struct CacheEntry final {
        agplayer::lossless::AnalysisResult result;
        qint64 bytes = 0;
        quint64 access = 0;
    };

    Private(LosslessAnalysisController* owner, AnalyzerFunction function)
        : q(owner), model(owner), analyzer(std::move(function))
    {
        analysisPool.setMaxThreadCount(kMaximumConcurrency);
        analysisPool.setExpiryTimeout(30000);
        discoveryPool.setMaxThreadCount(1);
        discoveryPool.setExpiryTimeout(30000);
        reportPool.setMaxThreadCount(1);
        reportPool.setExpiryTimeout(30000);
    }

    void shutdown()
    {
        shuttingDown = true;
        if (reportCancelled) {
            reportCancelled->store(true, std::memory_order_release);
        }
        for (auto it = activeJobs.begin(); it != activeJobs.end(); ++it) {
            it->cancelled->store(true, std::memory_order_release);
            QObject::disconnect(it->watcher, nullptr, q, nullptr);
        }
        for (const DiscoveryJob& job : discoveryJobs) {
            job.cancelled->store(true, std::memory_order_release);
            QObject::disconnect(job.watcher, nullptr, q, nullptr);
        }
        for (QFutureWatcherBase* watcher : reportWatchers) {
            QObject::disconnect(watcher, nullptr, q, nullptr);
        }
        analysisPool.waitForDone();
        discoveryPool.waitForDone();
        reportPool.waitForDone();
    }

    static DiscoveryOutcome discover(
        const QVariantList& requested,
        const std::shared_ptr<std::atomic_bool>& cancelled)
    {
        DiscoveryOutcome outcome;
        QSet<QString> seen;
        bool limitReached = false;
        const auto addFile = [&](const QFileInfo& info,
                                 const bool requireSupported) {
            if (cancelled->load(std::memory_order_acquire) || limitReached) {
                return;
            }
            if (!info.isFile()
                || (requireSupported
                    && !supportedExtensions().contains(
                        info.suffix().toCaseFolded()))) {
                return;
            }
            const QString path = agplayer::qt::resourcePathIdentity(
                info.absoluteFilePath());
            const QString pathKey = canonicalPathKey(path);
            if (path.isEmpty() || seen.contains(pathKey)) return;
            seen.insert(pathKey);
            if (info.suffix().compare(QStringLiteral("iso"),
                                      Qt::CaseInsensitive) == 0) {
                agplayer::lossless::SacdReader reader;
                std::string error;
                if (!reader.open(path.toUtf8().toStdString(), error,
                                 cancelled.get())) {
                    if (cancelled->load(std::memory_order_acquire)) return;
                    outcome.inputs.append({
                        path, info.fileName(), QStringLiteral("SACD ISO"), {},
                        -1, 0, 0, fromUtf8(error)});
                    return;
                }
                const auto& tracks = reader.tracks();
                for (std::size_t index = 0; index < tracks.size(); ++index) {
                    if (outcome.inputs.size() >= kMaximumTasks) {
                        limitReached = true;
                        break;
                    }
                    const auto& track = tracks[index];
                    outcome.inputs.append({
                        path,
                        QStringLiteral("%1 · SACD %2/%3")
                            .arg(info.fileName())
                            .arg(track.areaIndex + 1)
                            .arg(track.trackNumber),
                        QStringLiteral("SACD ISO"),
                        track.dstEncoded ? QStringLiteral("DST")
                                         : QStringLiteral("DSD"),
                        static_cast<int>(index), track.channels,
                        track.durationMs, {}});
                }
                return;
            }
            if (outcome.inputs.size() >= kMaximumTasks) {
                limitReached = true;
                return;
            }
            outcome.inputs.append({
                path, info.fileName(), info.suffix().toUpper(), {},
                -1, 0, 0, {}});
        };

        for (const QVariant& value : requested) {
            if (cancelled->load(std::memory_order_acquire) || limitReached) {
                break;
            }
            QUrl url = value.toUrl();
            if (!url.isValid() || url.isEmpty() || url.scheme().isEmpty()
                || QDir::isAbsolutePath(value.toString())) {
                const QString path = value.toString();
                if (!path.trimmed().isEmpty()) url = QUrl::fromLocalFile(path);
            }
            if (!url.isLocalFile()) {
                outcome.errors.append(QCoreApplication::translate(
                    "LosslessAnalysisController", "仅支持本地文件：%1")
                                          .arg(url.toString()));
                continue;
            }
            const QFileInfo info(url.toLocalFile());
            if (info.isDir()) {
                QDirIterator iterator(
                    info.absoluteFilePath(), QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
                while (iterator.hasNext()
                       && !cancelled->load(std::memory_order_acquire)
                       && !limitReached) {
                    addFile(QFileInfo(iterator.next()), true);
                }
            } else if (info.isFile()) {
                addFile(info, false);
            } else {
                outcome.errors.append(QCoreApplication::translate(
                    "LosslessAnalysisController", "文件或文件夹不存在：%1")
                                          .arg(info.absoluteFilePath()));
            }
        }
        if (limitReached) {
            outcome.errors.append(QCoreApplication::translate(
                "LosslessAnalysisController",
                "任务数量已达到 %1 项上限，已停止继续发现")
                    .arg(kMaximumTasks));
        }
        outcome.cancelled = cancelled->load(std::memory_order_acquire);
        return outcome;
    }

    void launchDiscovery(const QVariantList& requested)
    {
        if (requested.isEmpty()) return;
        if (requested.size() > kMaximumRequestedInputs) {
            setError(QCoreApplication::translate(
                "LosslessAnalysisController", "单次导入数量超过 %1 项上限")
                         .arg(kMaximumRequestedInputs));
            return;
        }
        if (discoveryJobs.size() >= kMaximumPendingDiscoveryJobs) {
            setError(QCoreApplication::translate(
                "LosslessAnalysisController",
                "文件发现请求过多，请等待当前发现完成"));
            return;
        }
        if (model.taskCount() >= kMaximumTasks) {
            setError(QCoreApplication::translate(
                "LosslessAnalysisController", "任务数量已达到 %1 项上限")
                         .arg(kMaximumTasks));
            return;
        }
        setError({});
        setStatus(QCoreApplication::translate(
            "LosslessAnalysisController", "正在发现文件…"));
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        auto* watcher = new QFutureWatcher<DiscoveryOutcome>(q);
        discoveryJobs.append({cancelled, watcher});
        QObject::connect(
            watcher, &QFutureWatcher<DiscoveryOutcome>::finished, q,
            [this, watcher, cancelled] {
                for (auto it = discoveryJobs.begin();
                     it != discoveryJobs.end(); ++it) {
                    if (it->watcher == watcher) {
                        discoveryJobs.erase(it);
                        break;
                    }
                }
                const DiscoveryOutcome outcome = watcher->result();
                watcher->deleteLater();
                if (!shuttingDown && !outcome.cancelled) {
                    for (const DiscoveredInput& input : outcome.inputs) {
                        if (model.taskCount() >= kMaximumTasks) {
                            setError(QCoreApplication::translate(
                                "LosslessAnalysisController",
                                "任务数量已达到 %1 项上限")
                                         .arg(kMaximumTasks));
                            break;
                        }
                        appendDiscovered(input);
                    }
                    if (!outcome.errors.isEmpty()) {
                        setError(outcome.errors.join(QLatin1Char('\n')));
                    }
                }
                updateAggregates();
                if (running && !stopping) dispatchAvailable();
                finishIfIdle();
            });
        watcher->setFuture(QtConcurrent::run(
            &discoveryPool, [requested, cancelled] {
                return discover(requested, cancelled);
            }));
    }

    void appendDiscovered(const DiscoveredInput& input)
    {
        const QString identity = inputIdentity(input.path, input.isoTrackIndex);
        if (model.containsIdentity(identity)) return;
        const QFileInfo info(input.path);
        const QString taskId = taskIdForIdentity(identity);
        QVariantMap selected = emptySelectedResult();
        selected.insert(QStringLiteral("taskId"), taskId);
        selected.insert(QStringLiteral("fileName"), input.fileName);
        selected.insert(QStringLiteral("path"), input.path);
        selected.insert(QStringLiteral("formatName"), input.formatName);
        selected.insert(QStringLiteral("channels"),
                        input.channels > 0 ? QVariant(input.channels)
                                           : QVariant{});
        selected.insert(QStringLiteral("durationMs"),
                        input.durationMs > 0 ? QVariant(input.durationMs)
                                             : QVariant{});
        selected.insert(QStringLiteral("fileSize"), info.size());
        selected.insert(
            QStringLiteral("modified"),
            info.lastModified().toString(Qt::ISODateWithMs));

        QVariantMap task{
            {QStringLiteral("taskId"), taskId},
            {QStringLiteral("fileName"), input.fileName},
            {QStringLiteral("filePath"), input.path},
            {QStringLiteral("formatName"), input.formatName},
            {QStringLiteral("audioFormat"), input.audioFormat},
            {QStringLiteral("verdictCode"), QString{}},
            {QStringLiteral("verdictText"), QString{}},
            {QStringLiteral("confidence"), 0},
            {QStringLiteral("state"), QStringLiteral("waiting")},
            {QStringLiteral("stateText"), stateText(QStringLiteral("waiting"))},
            {QStringLiteral("checked"), true},
            {QStringLiteral("progress"), 0.0},
            {QStringLiteral("_identity"), identity},
            {QStringLiteral("_isoTrackIndex"), input.isoTrackIndex},
            {QStringLiteral("_generation"), 0ULL},
            {QStringLiteral("_includeSpectrogram"), false},
            {QStringLiteral("_selectedResult"), selected},
            {QStringLiteral("_reportResult"), QVariantMap{}},
            {QStringLiteral("_resultBytes"), 0LL},
            {QStringLiteral("_fromCache"), false},
        };
        if (!input.error.isEmpty()) {
            selected.insert(QStringLiteral("verdictCode"),
                            QStringLiteral("analysis_failed"));
            selected.insert(QStringLiteral("verdictText"),
                            verdictText(
                                agplayer::lossless::Verdict::AnalysisFailed));
            selected.insert(QStringLiteral("error"), input.error);
            task.insert(QStringLiteral("verdictCode"),
                        QStringLiteral("analysis_failed"));
            task.insert(QStringLiteral("verdictText"),
                        verdictText(
                            agplayer::lossless::Verdict::AnalysisFailed));
            task.insert(QStringLiteral("state"), QStringLiteral("failed"));
            task.insert(QStringLiteral("stateText"),
                        stateText(QStringLiteral("failed")));
            task.insert(QStringLiteral("progress"), 1.0);
            task.insert(QStringLiteral("_selectedResult"), selected);
            task.insert(QStringLiteral("_reportResult"),
                        failureReport(task, selected, input.error));
            task.insert(QStringLiteral("_resultBytes"),
                        estimatedVariantBytes(
                            task.value(QStringLiteral("_selectedResult")))
                            + estimatedVariantBytes(
                                task.value(QStringLiteral("_reportResult"))));
        }
        if (model.appendTask(task) && selectedTaskId.isEmpty()) {
            selectedTaskId = taskId;
            selectedResult = selected;
            emit q->selectedResultChanged();
        }
    }

    static QVariantMap failureReport(
        const QVariantMap& task,
        const QVariantMap& selected,
        const QString& error)
    {
        return {
            {QStringLiteral("schemaVersion"), QStringLiteral("1")},
            {QStringLiteral("algorithmVersion"),
             QString::fromLatin1(
                 agplayer::lossless::kAlgorithmVersion.data(),
                 static_cast<qsizetype>(
                     agplayer::lossless::kAlgorithmVersion.size()))},
            {QStringLiteral("parameterVersion"),
             QString::fromLatin1(
                 agplayer::lossless::kParameterVersion.data(),
                 static_cast<qsizetype>(
                     agplayer::lossless::kParameterVersion.size()))},
            {QStringLiteral("taskId"),
             task.value(QStringLiteral("taskId"))},
            {QStringLiteral("fileName"),
             task.value(QStringLiteral("fileName"))},
            {QStringLiteral("file"),
             QVariantMap{
                 {QStringLiteral("path"),
                  task.value(QStringLiteral("filePath"))},
                 {QStringLiteral("fileSize"),
                  selected.value(QStringLiteral("fileSize"))},
                 {QStringLiteral("modifiedUnixMs"), QVariant{}}}},
            {QStringLiteral("source"), QVariantMap{}},
            {QStringLiteral("verdictCode"),
             QStringLiteral("analysis_failed")},
            {QStringLiteral("verdictText"),
             reportVerdictText(
                 agplayer::lossless::Verdict::AnalysisFailed)},
            {QStringLiteral("confidence"), 0},
            {QStringLiteral("coverage"), QVariantMap{}},
            {QStringLiteral("measurements"), QVariantMap{}},
            {QStringLiteral("evidence"), QVariantList{}},
            {QStringLiteral("candidates"), QVariantList{}},
            {QStringLiteral("chain"), QVariantList{}},
            {QStringLiteral("spectrum"), QVariantMap{}},
            {QStringLiteral("spectrogram"), QVariantMap{}},
            {QStringLiteral("warnings"), QVariantList{}},
            {QStringLiteral("error"), error},
        };
    }

    QString cacheKeyForTask(const QVariantMap& task,
                            const bool includeSpectrogram) const
    {
        const QFileInfo info(task.value(QStringLiteral("filePath")).toString());
        return inputIdentity(info.absoluteFilePath(),
                             task.value(QStringLiteral("_isoTrackIndex")).toInt())
            + QStringLiteral("#size=%1#mtime=%2#algorithm=%3#parameters=%4#spectrogram=%5")
                  .arg(info.exists() ? info.size() : -1)
                  .arg(info.exists()
                           ? info.lastModified().toMSecsSinceEpoch() : -1)
                  .arg(QString::fromLatin1(
                      agplayer::lossless::kAlgorithmVersion.data(),
                      static_cast<qsizetype>(
                          agplayer::lossless::kAlgorithmVersion.size())))
                  .arg(QString::fromLatin1(
                      agplayer::lossless::kParameterVersion.data(),
                      static_cast<qsizetype>(
                          agplayer::lossless::kParameterVersion.size())))
                  .arg(includeSpectrogram ? 1 : 0);
    }

    std::optional<agplayer::lossless::AnalysisResult> cached(
        const QString& key)
    {
        auto found = cache.find(key);
        if (found == cache.end()) return std::nullopt;
        found->access = ++cacheClock;
        return found->result;
    }

    void storeCache(const QString& key,
                    const agplayer::lossless::AnalysisResult& result)
    {
        const qint64 bytes = estimatedResultBytes(result);
        if (bytes > cacheLimit) return;
        const auto existing = cache.find(key);
        if (existing != cache.end()) {
            cacheBytes -= existing->bytes;
            cache.erase(existing);
        }
        cache.insert(key, {result, bytes, ++cacheClock});
        cacheBytes += bytes;
        trimCache();
    }

    void trimCache()
    {
        while (cacheBytes + retainedResultBytes() > cacheLimit
               && !cache.isEmpty()) {
            auto oldest = cache.begin();
            for (auto it = std::next(cache.begin()); it != cache.end(); ++it) {
                if (it->access < oldest->access) oldest = it;
            }
            cacheBytes -= oldest->bytes;
            cache.erase(oldest);
        }
    }

    qint64 retainedResultBytes() const
    {
        qint64 bytes = 0;
        for (const QVariantMap& task : model.allTasks()) {
            bytes += task.value(QStringLiteral("_resultBytes")).toLongLong();
        }
        return bytes;
    }

    void dispatchAvailable()
    {
        if (!running || stopping) return;
        while (activeJobs.size() < concurrency) {
            QString nextId;
            for (const QString& id : model.taskIds()) {
                const QVariantMap task = model.task(id);
                if (task.value(QStringLiteral("checked")).toBool()
                    && task.value(QStringLiteral("state")).toString()
                           == QStringLiteral("waiting")
                    && !activeJobs.contains(id)) {
                    nextId = id;
                    break;
                }
            }
            if (nextId.isEmpty()) break;
            const QVariantMap task = model.task(nextId);
            const bool includeSpectrogram =
                task.value(QStringLiteral("_includeSpectrogram")).toBool();
            const QString cacheKey =
                cacheKeyForTask(task, includeSpectrogram);
            const auto cachedResult = cached(cacheKey);
            if (cachedResult.has_value()) {
                applyResult(nextId,
                            task.value(QStringLiteral("_generation")).toULongLong(),
                            cacheKey, *cachedResult, true);
                continue;
            }
            launchAnalysis(nextId, cacheKey, includeSpectrogram);
        }
        updateStatusForWork();
    }

    void launchAnalysis(const QString& taskId,
                        const QString& cacheKey,
                        const bool includeSpectrogram)
    {
        QVariantMap task = model.task(taskId);
        const quint64 generation = ++generationClock;
        model.updateTask(taskId, {
            {QStringLiteral("_generation"), generation},
            {QStringLiteral("state"), QStringLiteral("probing")},
            {QStringLiteral("stateText"), stateText(QStringLiteral("probing"))},
            {QStringLiteral("progress"), 0.0},
        });
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        auto* watcher = new QFutureWatcher<WorkerOutcome>(q);
        activeJobs.insert(taskId, {generation, cancelled, watcher});
        const QString path = task.value(QStringLiteral("filePath")).toString();
        agplayer::lossless::AnalysisOptions options;
        options.isoTrackIndex =
            task.value(QStringLiteral("_isoTrackIndex")).toInt();
        options.includeSpectrogram = includeSpectrogram;
        const AnalyzerFunction function = analyzer;
        QPointer<LosslessAnalysisController> receiver(q);
        QObject::connect(
            watcher, &QFutureWatcher<WorkerOutcome>::finished, q,
            [this, watcher, taskId, generation] {
                if (shuttingDown) return;
                const WorkerOutcome outcome = watcher->result();
                watcher->deleteLater();
                const auto active = activeJobs.constFind(taskId);
                if (active == activeJobs.cend()
                    || active->generation != generation) {
                    return;
                }
                const bool wasCancelled =
                    active->cancelled->load(std::memory_order_acquire);
                activeJobs.remove(taskId);
                agplayer::lossless::AnalysisResult result = outcome.result;
                if (wasCancelled) {
                    result.cancelled = true;
                    result.verdict = agplayer::lossless::Verdict::Cancelled;
                    result.error.clear();
                }
                applyResult(taskId, generation, outcome.cacheKey, result, false);
                dispatchAvailable();
                finishIfIdle();
            });
        watcher->setFuture(QtConcurrent::run(
            &analysisPool,
            [function, path, options, cancelled, receiver,
             taskId, generation, cacheKey] {
                WorkerOutcome outcome;
                outcome.taskId = taskId;
                outcome.generation = generation;
                outcome.cacheKey = cacheKey;
                try {
                    const auto progressCallback =
                        [receiver, taskId, generation](const float value) {
                        if (!receiver) return;
                        QMetaObject::invokeMethod(
                            receiver.data(),
                            [receiver, taskId, generation, value] {
                                if (receiver && receiver->d_) {
                                    receiver->d_->applyProgress(
                                        taskId, generation, value);
                                }
                            },
                            Qt::QueuedConnection);
                    };
                    outcome.result = function(
                        path.toUtf8().toStdString(), options,
                        *cancelled, progressCallback);
                } catch (const std::exception& exception) {
                    outcome.result.verdict =
                        agplayer::lossless::Verdict::AnalysisFailed;
                    outcome.result.error =
                        QCoreApplication::translate(
                            "LosslessAnalysisController", "分析失败：%1")
                            .arg(QString::fromUtf8(exception.what()))
                            .toUtf8().toStdString();
                } catch (...) {
                    outcome.result.verdict =
                        agplayer::lossless::Verdict::AnalysisFailed;
                    outcome.result.error = QCoreApplication::translate(
                        "LosslessAnalysisController", "分析发生未知错误")
                                                .toUtf8().toStdString();
                }
                return outcome;
            }));
    }

    void applyProgress(const QString& taskId,
                       const quint64 generation,
                       const float rawProgress)
    {
        if (shuttingDown) return;
        const QVariantMap task = model.task(taskId);
        if (task.value(QStringLiteral("_generation")).toULongLong()
                != generation
            || !activeJobs.contains(taskId)) {
            return;
        }
        const double taskProgress = std::clamp(
            static_cast<double>(rawProgress), 0.0, 1.0);
        QString state = QStringLiteral("analyzing");
        if (stopping) state = QStringLiteral("cancelling");
        else if (taskProgress < 0.10) state = QStringLiteral("probing");
        else if (taskProgress < 0.25) state = QStringLiteral("decoding");
        else if (taskProgress >= 0.95) state = QStringLiteral("summarizing");
        model.updateTask(taskId, {
            {QStringLiteral("state"), state},
            {QStringLiteral("stateText"), stateText(state)},
            {QStringLiteral("progress"), taskProgress},
        });
        updateAggregates();
    }

    void applyResult(
        const QString& taskId,
        const quint64 generation,
        const QString& cacheKey,
        const agplayer::lossless::AnalysisResult& result,
        const bool fromCache)
    {
        QVariantMap task = model.task(taskId);
        if (task.isEmpty()) return;
        const quint64 taskGeneration =
            task.value(QStringLiteral("_generation")).toULongLong();
        if (generation != taskGeneration && taskGeneration != 0) return;

        const bool requestedSpectrogram =
            task.value(QStringLiteral("_includeSpectrogram")).toBool();
        agplayer::lossless::AnalysisResult checkedResult = result;
        if (!fromCache && !result.cancelled
            && result.verdict != agplayer::lossless::Verdict::Cancelled
            && cacheKey != cacheKeyForTask(task, requestedSpectrogram)) {
            checkedResult = {};
            checkedResult.analysisStartedUnixMs = result.analysisStartedUnixMs;
            const QString path =
                task.value(QStringLiteral("filePath")).toString();
            const QFileInfo currentFile(path);
            checkedResult.file.path = path.toUtf8().toStdString();
            checkedResult.file.fileSize = currentFile.exists()
                ? static_cast<std::uint64_t>(currentFile.size()) : 0;
            checkedResult.file.modifiedUnixMs = currentFile.exists()
                ? currentFile.lastModified().toMSecsSinceEpoch() : 0;
            checkedResult.verdict =
                agplayer::lossless::Verdict::AnalysisFailed;
            checkedResult.error =
                u8"分析期间源文件已发生变化，请重试";
        }

        QString state = QStringLiteral("completed");
        if (checkedResult.cancelled
            || checkedResult.verdict
                == agplayer::lossless::Verdict::Cancelled) {
            state = QStringLiteral("cancelled");
        } else if (!checkedResult.error.empty()
                   || checkedResult.verdict
                       == agplayer::lossless::Verdict::AnalysisFailed) {
            state = QStringLiteral("failed");
        }
        agplayer::lossless::AnalysisResult displayResult = compactResult(
            checkedResult, requestedSpectrogram && selectedTaskId == taskId);
        agplayer::lossless::AnalysisResult reportResult = displayResult;
        QVariantMap selected = resultToSelected(task, displayResult);
        QVariantMap report = resultToReport(task, reportResult);
        QVariantMap reportSpectrum =
            report.value(QStringLiteral("spectrum")).toMap();
        reportSpectrum.insert(QStringLiteral("db"),
            selected.value(QStringLiteral("spectrum")));
        report.insert(QStringLiteral("spectrum"), reportSpectrum);
        qint64 resultBytes = estimatedVariantBytes(selected)
            + estimatedVariantBytes(report);
        const QString container = displayContainerName(displayResult.source);
        const QString audioDescription = audioFormatText(displayResult.source);
        model.updateTask(taskId, {
            {QStringLiteral("formatName"),
             container.isEmpty()
                 ? task.value(QStringLiteral("formatName"))
                 : QVariant(container)},
            {QStringLiteral("audioFormat"),
             audioDescription.isEmpty()
                 ? task.value(QStringLiteral("audioFormat"))
                 : QVariant(audioDescription)},
            {QStringLiteral("verdictCode"), verdictCodeString(displayResult.verdict)},
            {QStringLiteral("verdictText"), verdictText(displayResult.verdict)},
            {QStringLiteral("confidence"),
             std::clamp(displayResult.confidence, 0, 100)},
            {QStringLiteral("state"), state},
            {QStringLiteral("stateText"),
             fromCache && state == QStringLiteral("completed")
                 ? QCoreApplication::translate(
                       "LosslessAnalysisController", "已完成（缓存）")
                 : stateText(state)},
            {QStringLiteral("progress"), 1.0},
            {QStringLiteral("_selectedResult"), selected},
            {QStringLiteral("_reportResult"), report},
            {QStringLiteral("_resultBytes"), resultBytes},
            {QStringLiteral("_fromCache"), fromCache},
            {QStringLiteral("_includeSpectrogram"), false},
        });
        if (state == QStringLiteral("completed") && !fromCache
            && !requestedSpectrogram) {
            agplayer::lossless::AnalysisResult cacheResult =
                compactResult(checkedResult, false);
            storeCache(cacheKey, cacheResult);
        }
        trimCache();
        if (selectedTaskId == taskId) {
            selectedResult = selected;
            emit q->selectedResultChanged();
        }
        emit q->taskFinished(taskId);
        updateAggregates();
    }

    static QVariantMap resultToSelected(
        const QVariantMap& task,
        const agplayer::lossless::AnalysisResult& result)
    {
        QVariantList evidence;
        QString gridText;
        evidence.reserve(static_cast<qsizetype>(result.evidence.size()));
        for (const auto& item : result.evidence) {
            if (gridText.isEmpty()
                && result.source.kind != agplayer::lossless::SourceKind::Dsd
                && result.source.kind != agplayer::lossless::SourceKind::Dst
                && item.code == "resampling_polyphase_grid"
                && item.family == agplayer::lossless::EvidenceFamily::Resampling
                && item.direction == agplayer::lossless::EvidenceDirection::SupportsUpsampling) {
                gridText = resamplingGridText(item.value, result.source.sampleRate);
            }
            const QString explanation = translatedEvidence(item.explanation);
            evidence.append(QVariantMap{
                {QStringLiteral("text"),
                 explanation.isEmpty() ? fromUtf8(item.code) : explanation},
                {QStringLiteral("value"), item.value},
                {QStringLiteral("unit"), fromUtf8(item.unit)},
                {QStringLiteral("reference"), translatedEvidence(item.reference)},
                {QStringLiteral("coverageStartSeconds"), item.coverageStartSeconds},
                {QStringLiteral("coverageEndSeconds"), item.coverageEndSeconds},
            });
        }
        QVariantList candidates;
        candidates.reserve(static_cast<qsizetype>(result.candidates.size()));
        for (const auto& item : result.candidates) {
            candidates.append(QVariantMap{
                {QStringLiteral("format"), translatedEvidence(item.format)},
                {QStringLiteral("confidence"),
                 std::clamp(item.confidence, 0, 100)},
                {QStringLiteral("limitation"),
                 translatedEvidence(item.limitation)},
            });
        }
        QVariantList spectrogram;
        const std::size_t timeBins = result.spectrogram.timeBins;
        const std::size_t frequencyBins = result.spectrogram.frequencyBins;
        if (timeBins > 0 && frequencyBins > 0
            && timeBins <= result.spectrogram.db.size() / frequencyBins) {
            spectrogram.reserve(static_cast<qsizetype>(timeBins));
            for (std::size_t time = 0; time < timeBins; ++time) {
                QVariantList row;
                row.reserve(static_cast<qsizetype>(frequencyBins));
                const std::size_t offset = time * frequencyBins;
                for (std::size_t frequency = 0;
                     frequency < frequencyBins; ++frequency) {
                    row.append(
                        static_cast<double>(
                            result.spectrogram.db[offset + frequency]));
                }
                spectrogram.append(QVariant::fromValue(row));
            }
        }
        const qint64 fileSize = result.file.fileSize > 0
            ? static_cast<qint64>(std::min<std::uint64_t>(
                  result.file.fileSize,
                  static_cast<std::uint64_t>(
                      std::numeric_limits<qint64>::max())))
            : QFileInfo(task.value(QStringLiteral("filePath")).toString()).size();
        const qint64 modified = result.file.modifiedUnixMs != 0
            ? result.file.modifiedUnixMs
            : QFileInfo(task.value(QStringLiteral("filePath")).toString())
                  .lastModified().toMSecsSinceEpoch();
        return {
            {QStringLiteral("taskId"),
             task.value(QStringLiteral("taskId"))},
            {QStringLiteral("fileName"),
             task.value(QStringLiteral("fileName"))},
            {QStringLiteral("path"),
             task.value(QStringLiteral("filePath"))},
            {QStringLiteral("formatName"),
             displayContainerName(result.source)},
            {QStringLiteral("codec"), fromUtf8(result.source.codec)},
            {QStringLiteral("sampleRate"), result.source.sampleRate},
            {QStringLiteral("rawDsdSampleRate"),
             result.source.rawDsdSampleRate},
            {QStringLiteral("bitsPerSample"), result.source.bitsPerSample},
            {QStringLiteral("channels"), result.source.channels},
            {QStringLiteral("durationMs"), result.source.durationMs},
            {QStringLiteral("fileSize"), fileSize},
            {QStringLiteral("modified"),
             QDateTime::fromMSecsSinceEpoch(modified)
                 .toString(Qt::ISODateWithMs)},
            {QStringLiteral("verdictCode"), verdictCodeString(result.verdict)},
            {QStringLiteral("verdictText"),
             verdictText(result.verdict)},
            {QStringLiteral("confidence"),
             std::clamp(result.confidence, 0, 100)},
            {QStringLiteral("coverage"), result.coverage.decodedRatio},
            {QStringLiteral("cutoffHz"), result.measurements.cutoffHz},
            {QStringLiteral("effectiveBits"),
             result.measurements.effectiveBits},
            {QStringLiteral("resamplingText"),
             !gridText.isEmpty() ? gridText
             : result.source.kind != agplayer::lossless::SourceKind::Dsd
                     && result.source.kind != agplayer::lossless::SourceKind::Dst
                     && result.measurements.resamplingMirrorScore > 0.0
                 ? QCoreApplication::translate(
                       "LosslessAnalysisController", "镜像评分 %1")
                       .arg(result.measurements.resamplingMirrorScore,
                            0, 'f', 3)
                 : QString{}},
            {QStringLiteral("holesText"),
             result.source.kind != agplayer::lossless::SourceKind::Dsd
                     && result.source.kind != agplayer::lossless::SourceKind::Dst
                     && result.measurements.codecHoleScore > 0.0
                 ? QCoreApplication::translate(
                       "LosslessAnalysisController", "结构空洞评分 %1")
                       .arg(result.measurements.codecHoleScore, 0, 'f', 3)
                 : QString{}},
            {QStringLiteral("evidence"), evidence},
            {QStringLiteral("candidates"), candidates},
            {QStringLiteral("chain"), translatedEvidenceList(result.chain)},
            {QStringLiteral("spectrum"), doubleList(result.spectrum.db)},
            {QStringLiteral("spectrogram"), spectrogram},
            {QStringLiteral("error"), translatedEvidence(result.error)},
            {QStringLiteral("warnings"),
             translatedEvidenceList(result.warnings)},
        };
    }

    static QVariantMap resultToReport(
        const QVariantMap& task,
        const agplayer::lossless::AnalysisResult& result)
    {
        QVariantList evidence;
        for (const auto& item : result.evidence) {
            evidence.append(QVariantMap{
                {QStringLiteral("code"), fromUtf8(item.code)},
                {QStringLiteral("family"),
                 evidenceFamilyCode(item.family)},
                {QStringLiteral("direction"),
                 evidenceDirectionCode(item.direction)},
                {QStringLiteral("value"), item.value},
                {QStringLiteral("unit"), fromUtf8(item.unit)},
                {QStringLiteral("reference"), fromUtf8(item.reference)},
                {QStringLiteral("severity"), item.severity},
                {QStringLiteral("coverageStartSeconds"),
                 item.coverageStartSeconds},
                {QStringLiteral("coverageEndSeconds"),
                 item.coverageEndSeconds},
                {QStringLiteral("explanation"),
                 fromUtf8(item.explanation)},
            });
        }
        QVariantList candidates;
        for (const auto& item : result.candidates) {
            candidates.append(QVariantMap{
                {QStringLiteral("format"), fromUtf8(item.format)},
                {QStringLiteral("confidence"), item.confidence},
                {QStringLiteral("limitation"), fromUtf8(item.limitation)},
            });
        }
        QVariantList stftWindows;
        for (const std::size_t value : result.measurements.stftWindowSizes) {
            stftWindows.append(static_cast<qulonglong>(value));
        }
        QVariantList spectrogram;
        spectrogram.reserve(
            static_cast<qsizetype>(result.spectrogram.db.size()));
        for (const float value : result.spectrogram.db) {
            spectrogram.append(static_cast<double>(value));
        }
        return {
            {QStringLiteral("schemaVersion"),
             fromUtf8(result.schemaVersion)},
            {QStringLiteral("algorithmVersion"),
             fromUtf8(result.algorithmVersion)},
            {QStringLiteral("parameterVersion"),
             fromUtf8(result.parameterVersion)},
            {QStringLiteral("analysisStartedAt"), result.analysisStartedUnixMs > 0
                 ? QVariant(QDateTime::fromMSecsSinceEpoch(
                       result.analysisStartedUnixMs, QTimeZone::utc()).toString(Qt::ISODateWithMs))
                 : QVariant{}},
            {QStringLiteral("taskId"),
             task.value(QStringLiteral("taskId"))},
            {QStringLiteral("fileName"),
             task.value(QStringLiteral("fileName"))},
            {QStringLiteral("file"),
             QVariantMap{
                 {QStringLiteral("path"),
                  task.value(QStringLiteral("filePath"))},
                 {QStringLiteral("fileSize"),
                  static_cast<qulonglong>(result.file.fileSize)},
                 {QStringLiteral("modifiedUnixMs"),
                  result.file.modifiedUnixMs}}},
            {QStringLiteral("source"),
             QVariantMap{
                 {QStringLiteral("container"),
                  fromUtf8(result.source.container)},
                 {QStringLiteral("codec"), fromUtf8(result.source.codec)},
                 {QStringLiteral("channelLayout"),
                  fromUtf8(result.source.channelLayout)},
                 {QStringLiteral("kind"),
                  sourceKindCode(result.source.kind)},
                 {QStringLiteral("sampleRate"), result.source.sampleRate},
                 {QStringLiteral("rawDsdSampleRate"),
                  result.source.rawDsdSampleRate},
                 {QStringLiteral("decodedSampleRate"),
                  result.source.decodedSampleRate},
                 {QStringLiteral("bitsPerSample"),
                  result.source.bitsPerSample},
                 {QStringLiteral("channels"), result.source.channels},
                 {QStringLiteral("durationMs"), result.source.durationMs},
                 {QStringLiteral("codecIsLossless"),
                  result.source.codecIsLossless},
                 {QStringLiteral("dstDecoded"), result.source.dstDecoded}}},
            {QStringLiteral("verdictCode"), verdictCodeString(result.verdict)},
            {QStringLiteral("verdictText"),
             reportVerdictText(result.verdict)},
            // Legacy confidence is an ordinal score, never a calibrated probability.
            {QStringLiteral("confidence"), result.confidence},
            {QStringLiteral("confidenceKind"), QStringLiteral("ordinal_evidence_score")},
            {QStringLiteral("calibrationStatus"), QStringLiteral("uncalibrated")},
            {QStringLiteral("calibratedProbability"), QVariant{}},
            {QStringLiteral("coverage"),
             QVariantMap{
                 {QStringLiteral("decodedFrames"),
                  static_cast<qulonglong>(result.coverage.decodedFrames)},
                 {QStringLiteral("analyzedWindows"),
                  static_cast<qulonglong>(result.coverage.analyzedWindows)},
                 {QStringLiteral("activeWindows"),
                  static_cast<qulonglong>(result.coverage.activeWindows)},
                 {QStringLiteral("decodedRatio"),
                  result.coverage.decodedRatio},
                 {QStringLiteral("activeWindowRatio"),
                  result.coverage.activeWindowRatio}}},
            {QStringLiteral("measurements"),
             QVariantMap{
                 {QStringLiteral("peak"), result.measurements.peak},
                 {QStringLiteral("rms"), result.measurements.rms},
                 {QStringLiteral("dynamicRangeDb"),
                  result.measurements.dynamicRangeDb},
                 {QStringLiteral("cutoffHz"),
                  result.measurements.cutoffHz},
                 {QStringLiteral("cutoffStability"),
                  result.measurements.cutoffStability},
                 {QStringLiteral("highFrequencyEnergyRatio"),
                  result.measurements.highFrequencyEnergyRatio},
                 {QStringLiteral("spectralFlatness"),
                  result.measurements.spectralFlatness},
                 {QStringLiteral("spectralEntropy"),
                  result.measurements.spectralEntropy},
                 {QStringLiteral("spectralEdgeFrequencyHz"),
                  result.measurements.spectralEdgeFrequencyHz},
                 {QStringLiteral("spectralEdgeDepthDb"),
                  result.measurements.spectralEdgeDepthDb},
                 {QStringLiteral("spectralEdgeStability"),
                  result.measurements.spectralEdgeStability},
                 {QStringLiteral("lowLevelSpectralEdgeMeasured"),
                  result.measurements.lowLevelSpectralEdgeMeasured},
                 {QStringLiteral("lowLevelSpectralEdgeHz"),
                  finiteMeasurement(
                      result.measurements.lowLevelSpectralEdgeHz,
                      result.measurements.lowLevelSpectralEdgeMeasured)},
                 {QStringLiteral("lowLevelSpectralEdgeDepthDb"),
                  finiteMeasurement(
                      result.measurements.lowLevelSpectralEdgeDepthDb,
                      result.measurements.lowLevelSpectralEdgeMeasured)},
                 {QStringLiteral("resamplingPhaseSourceRate"),
                  finiteMeasurement(
                      result.measurements.resamplingPhaseSourceRate,
                      result.measurements.resamplingPhaseSegments > 0)},
                 {QStringLiteral("resamplingPhaseStrength"),
                  finiteMeasurement(
                      result.measurements.resamplingPhaseStrength,
                      result.measurements.resamplingPhaseSegments > 0)},
                 {QStringLiteral("resamplingPhaseCoherence"),
                  finiteMeasurement(
                      result.measurements.resamplingPhaseCoherence,
                      result.measurements.resamplingPhaseSegments > 0)},
                 {QStringLiteral("resamplingPhaseSegments"),
                  static_cast<qulonglong>(
                      result.measurements.resamplingPhaseSegments)},
                 {QStringLiteral("resamplingResidualEntropyMeasured"),
                  result.measurements.resamplingResidualEntropyMeasured},
                 {QStringLiteral("resamplingResidualEntropy"),
                  finiteMeasurement(
                      result.measurements.resamplingResidualEntropy,
                      result.measurements.resamplingResidualEntropyMeasured)},
                 {QStringLiteral("resamplingBandSuppressionMeasured"),
                  result.measurements.resamplingBandSuppressionMeasured},
                 {QStringLiteral("resamplingBandSuppressionDb"),
                  finiteMeasurement(
                      result.measurements.resamplingBandSuppressionDb,
                      result.measurements.resamplingBandSuppressionMeasured)},
                 {QStringLiteral("mdctFrameCoherentPeakDb"),
                  finiteMeasurement(
                      result.measurements.mdctFrameCoherentPeakDb,
                      result.measurements.mdctFrameBlocks > 0)},
                 {QStringLiteral("mdctFramePeakZ"),
                  finiteMeasurement(
                      result.measurements.mdctFramePeakZ,
                      result.measurements.mdctFrameBlocks > 0)},
                 {QStringLiteral("mdctFrameBlocks"),
                  static_cast<qulonglong>(
                      result.measurements.mdctFrameBlocks)},
                 {QStringLiteral("mdctFrameAlignedBlocks"),
                  static_cast<qulonglong>(result.measurements.mdctFrameAlignedBlocks)},
                 {QStringLiteral("mdctFrameWindow"),
                  result.measurements.mdctFrameBlocks > 0
                      ? QVariant(fromUtf8(result.measurements.mdctFrameWindow))
                      : QVariant{}},
                 {QStringLiteral("celtFrameBlocks"),
                  static_cast<qulonglong>(result.measurements.celtFrameBlocks)},
                 {QStringLiteral("mdctAnalysisSampleRate"),
                  result.measurements.mdctFrameBlocks > 0
                      ? QVariant(result.measurements.mdctAnalysisSampleRate) : QVariant{}},
                 {QStringLiteral("celtFrameSamples"),
                  result.measurements.celtFrameBlocks > 0
                      ? QVariant::fromValue(static_cast<qulonglong>(result.measurements.celtFrameSamples))
                      : QVariant{}},
                 {QStringLiteral("celtFramesPerAnchor"),
                  result.measurements.celtFrameBlocks > 0
                      ? QVariant::fromValue(static_cast<qulonglong>(result.measurements.celtFramesPerAnchor))
                      : QVariant{}},
                 {QStringLiteral("celtFrameMinimumBandZ"),
                  finiteMeasurement(
                      result.measurements.celtFrameMinimumBandZ,
                      result.measurements.celtFrameBlocks > 0)},
                 {QStringLiteral("celtFrameMinimumAnchorCoherence"),
                  finiteMeasurement(
                      result.measurements.celtFrameMinimumAnchorCoherence,
                      result.measurements.celtFrameBlocks > 0)},
                 {QStringLiteral("celtFrameMaximumPeakWidth"),
                  result.measurements.celtFrameBlocks > 0
                      ? QVariant::fromValue(static_cast<qulonglong>(
                            result.measurements.celtFrameMaximumPeakWidth))
                      : QVariant{}},
                 {QStringLiteral("celtFrameBandPhaseDifference"),
                  result.measurements.celtFrameBlocks > 0
                      ? QVariant::fromValue(static_cast<qulonglong>(
                            result.measurements.celtFrameBandPhaseDifference))
                      : QVariant{}},
                 {QStringLiteral("codecHoleScore"),
                  result.measurements.codecHoleScore},
                 {QStringLiteral("transientPreEchoMeasured"),
                  result.measurements.transientPreEchoMeasured},
                 {QStringLiteral("transientCount"),
                  static_cast<qulonglong>(result.measurements.transientCount)},
                 {QStringLiteral("transientPreEchoScore"),
                  result.measurements.transientPreEchoMeasured
                      ? QVariant(result.measurements.transientPreEchoScore)
                      : QVariant{}},
                 {QStringLiteral("resamplingMirrorScore"),
                  result.measurements.resamplingMirrorScore},
                 {QStringLiteral("lowBitUsageRatio"),
                  result.measurements.lowBitUsageRatio},
                 {QStringLiteral("effectiveBits"),
                  result.measurements.effectiveBits},
                 {QStringLiteral("channelCorrelation"),
                  result.measurements.channelCorrelation},
                 {QStringLiteral("dominantFrequencyHz"),
                  result.measurements.dominantFrequencyHz},
                 {QStringLiteral("dsdUltrasonicSlopeDbPerOctave"),
                  result.measurements.dsdUltrasonicSlopeDbPerOctave},
                 {QStringLiteral("dsdNoiseShapingStability"),
                  result.measurements.dsdNoiseShapingStability},
                  {QStringLiteral("dsdRepeatedPatternRatio"),
                   result.measurements.dsdRepeatedPatternRatio},
                  {QStringLiteral("dsdRawBitOneRatio"),
                   result.measurements.dsdRawBitOneRatio},
                  {QStringLiteral("dsdRawByteEntropy"),
                   result.measurements.dsdRawByteEntropy},
                  {QStringLiteral("dsdRawPacketBytes"),
                   static_cast<qulonglong>(
                       result.measurements.dsdRawPacketBytes)},
                  {QStringLiteral("stftWindowSizes"), stftWindows}}},
            {QStringLiteral("evidence"), evidence},
            {QStringLiteral("candidates"), candidates},
            {QStringLiteral("chain"), stringList(result.chain)},
            {QStringLiteral("spectrum"),
             QVariantMap{
                 {QStringLiteral("nyquistHz"), result.spectrum.nyquistHz},
                 {QStringLiteral("minDb"), result.spectrum.minDb},
                 {QStringLiteral("maxDb"), result.spectrum.maxDb},
                 {QStringLiteral("db"), doubleList(result.spectrum.db)}}},
            {QStringLiteral("spectrogram"),
             QVariantMap{
                 {QStringLiteral("timeBins"),
                  static_cast<qulonglong>(
                      result.spectrogram.timeBins)},
                 {QStringLiteral("frequencyBins"),
                  static_cast<qulonglong>(
                      result.spectrogram.frequencyBins)},
                 {QStringLiteral("nyquistHz"),
                  result.spectrogram.nyquistHz},
                 {QStringLiteral("db"), spectrogram}}},
            {QStringLiteral("warnings"), stringList(result.warnings)},
            {QStringLiteral("error"), fromUtf8(result.error)},
        };
    }

    void updateAggregates()
    {
        const QVector<QVariantMap> all = model.allTasks();
        int nextCompleted = 0;
        double sumProgress = 0.0;
        int credible = 0;
        int transcode = 0;
        int upsample = 0;
        int inconclusive = 0;
        for (const QVariantMap& task : all) {
            const QString state =
                task.value(QStringLiteral("state")).toString();
            if (state == QStringLiteral("completed")) ++nextCompleted;
            sumProgress += terminalState(state)
                ? 1.0
                : std::clamp(
                      task.value(QStringLiteral("progress")).toDouble(),
                      0.0, 1.0);
            const QString verdict =
                task.value(QStringLiteral("verdictCode")).toString();
            if (verdict == QStringLiteral("credible_lossless")
                || verdict == QStringLiteral("credible_native_dsd")) {
                ++credible;
            } else if (verdict
                           == QStringLiteral("suspected_lossy_transcode")
                       || verdict
                           == QStringLiteral("suspected_lossy_upsample")) {
                ++transcode;
            } else if (verdict == QStringLiteral("suspected_upsample")
                       || verdict
                           == QStringLiteral(
                               "suspected_bit_depth_expansion")) {
                ++upsample;
            } else if (!verdict.isEmpty()) {
                ++inconclusive;
            }
        }
        const int nextTotal = all.size();
        const int nextSelected = model.checkedCount();
        const double nextProgress = nextTotal > 0
            ? sumProgress / static_cast<double>(nextTotal) : 0.0;
        const QVariantList nextCounts{
            nextTotal, credible, transcode, upsample, inconclusive};
        if (completedCount != nextCompleted) {
            completedCount = nextCompleted;
            emit q->completedCountChanged();
        }
        if (totalCount != nextTotal) {
            totalCount = nextTotal;
            emit q->totalCountChanged();
        }
        if (selectedCount != nextSelected) {
            selectedCount = nextSelected;
            emit q->selectedCountChanged();
        }
        if (!qFuzzyCompare(progress + 1.0, nextProgress + 1.0)) {
            progress = nextProgress;
            emit q->progressChanged();
        }
        if (counts != nextCounts) {
            counts = nextCounts;
            emit q->countsChanged();
        }
    }

    void setRunning(const bool value)
    {
        if (running == value) return;
        running = value;
        emit q->runningChanged();
    }

    void setStopping(const bool value)
    {
        if (stopping == value) return;
        stopping = value;
        emit q->stoppingChanged();
    }

    void setStatus(const QString& value)
    {
        if (statusText == value) return;
        statusText = value;
        emit q->statusTextChanged();
    }

    void setError(const QString& value)
    {
        if (error == value) return;
        error = value;
        emit q->errorChanged();
    }

    void updateStatusForWork()
    {
        if (stopping) {
            setStatus(QCoreApplication::translate(
                "LosslessAnalysisController", "正在停止…"));
        } else if (!discoveryJobs.isEmpty() && activeJobs.isEmpty()) {
            setStatus(QCoreApplication::translate(
                "LosslessAnalysisController", "正在发现文件…"));
        } else if (running) {
            setStatus(QCoreApplication::translate(
                "LosslessAnalysisController", "正在分析 %1/%2")
                          .arg(completedCount)
                          .arg(totalCount));
        }
    }

    bool hasWaitingCheckedTask() const
    {
        for (const QVariantMap& task : model.allTasks()) {
            if (task.value(QStringLiteral("checked")).toBool()
                && task.value(QStringLiteral("state")).toString()
                    == QStringLiteral("waiting")) {
                return true;
            }
        }
        return false;
    }

    void setTerminalStatus()
    {
        if (totalCount == 0) {
            setStatus(QCoreApplication::translate(
                "LosslessAnalysisController", "尚未添加文件"));
            return;
        }
        int failedCount = 0;
        int cancelledCount = 0;
        for (const QVariantMap& task : model.allTasks()) {
            const QString state =
                task.value(QStringLiteral("state")).toString();
            if (state == QStringLiteral("failed")) ++failedCount;
            else if (state == QStringLiteral("cancelled")) {
                ++cancelledCount;
            }
        }
        if (failedCount > 0 || cancelledCount > 0) {
            const int processedCount =
                completedCount + failedCount + cancelledCount;
            setStatus(QCoreApplication::translate(
                "LosslessAnalysisController",
                "已处理 %1/%2（成功 %3，失败 %4，取消 %5）")
                          .arg(processedCount)
                          .arg(totalCount)
                          .arg(completedCount)
                          .arg(failedCount)
                          .arg(cancelledCount));
            return;
        }
        setStatus(QCoreApplication::translate(
            "LosslessAnalysisController", "已完成 %1/%2")
                      .arg(completedCount)
                      .arg(totalCount));
    }

    void finishIfIdle()
    {
        if (!activeJobs.isEmpty()) return;
        if (running && !stopping && hasWaitingCheckedTask()) {
            dispatchAvailable();
            if (!activeJobs.isEmpty()) return;
        }
        if (running && pendingStart && !discoveryJobs.isEmpty()) {
            updateStatusForWork();
            return;
        }
        if (clearWhenIdle && discoveryJobs.isEmpty()) {
            model.clearTasks();
            selectedTaskId.clear();
            selectedResult = emptySelectedResult();
            clearWhenIdle = false;
            emit q->selectedResultChanged();
            updateAggregates();
        }
        pendingStart = false;
        setRunning(false);
        setStopping(false);
        if (!discoveryJobs.isEmpty()) {
            setStatus(QCoreApplication::translate(
                "LosslessAnalysisController", "正在发现文件…"));
        } else {
            setTerminalStatus();
        }
    }

    LosslessAnalysisController* q;
    LosslessTaskModel model;
    AnalyzerFunction analyzer;
    QThreadPool analysisPool;
    QThreadPool discoveryPool;
    QThreadPool reportPool;
    QHash<QString, ActiveJob> activeJobs;
    QList<DiscoveryJob> discoveryJobs;
    QList<QFutureWatcherBase*> reportWatchers;
    std::shared_ptr<std::atomic_bool> reportCancelled;
    QHash<QString, CacheEntry> cache;
    qint64 cacheBytes = 0;
    qint64 cacheLimit = kDefaultCacheLimit;
    quint64 cacheClock = 0;
    quint64 generationClock = 0;
    QVariantMap selectedResult{emptySelectedResult()};
    QString selectedTaskId;
    QString filter{QStringLiteral("all")};
    QString searchText;
    QString statusText{QCoreApplication::translate(
        "LosslessAnalysisController", "尚未添加文件")};
    QString error;
    QVariantList counts{0, 0, 0, 0, 0};
    bool running = false;
    bool stopping = false;
    bool pendingStart = false;
    bool clearWhenIdle = false;
    bool shuttingDown = false;
    double progress = 0.0;
    int completedCount = 0;
    int totalCount = 0;
    int selectedCount = 0;
    int concurrency = kDefaultConcurrency;
};

LosslessAnalysisController::LosslessAnalysisController(QObject* parent)
    : LosslessAnalysisController(
          [](const std::string& path,
             const agplayer::lossless::AnalysisOptions& options,
             const std::atomic_bool& cancelled,
             agplayer::lossless::ProgressCallback progress) {
              return agplayer::lossless::analyzeFile(
                  path, options, cancelled, std::move(progress));
          },
          parent)
{
}

LosslessAnalysisController::LosslessAnalysisController(
    AnalyzerFunction analyzer, QObject* parent)
    : QObject(parent),
      d_(std::make_unique<Private>(this, std::move(analyzer)))
{
}

LosslessAnalysisController::~LosslessAnalysisController()
{
    d_->shutdown();
    d_.reset();
}

QAbstractItemModel* LosslessAnalysisController::tasks() const noexcept
{
    return &d_->model;
}

QVariantMap LosslessAnalysisController::selectedResult() const
{
    return d_->selectedResult;
}

bool LosslessAnalysisController::running() const noexcept { return d_->running; }
bool LosslessAnalysisController::stopping() const noexcept { return d_->stopping; }
double LosslessAnalysisController::progress() const noexcept { return d_->progress; }
int LosslessAnalysisController::completedCount() const noexcept
{
    return d_->completedCount;
}
int LosslessAnalysisController::totalCount() const noexcept { return d_->totalCount; }
int LosslessAnalysisController::selectedCount() const noexcept
{
    return d_->selectedCount;
}
int LosslessAnalysisController::concurrency() const noexcept
{
    return d_->concurrency;
}
QString LosslessAnalysisController::filter() const { return d_->filter; }
QString LosslessAnalysisController::searchText() const { return d_->searchText; }
QString LosslessAnalysisController::statusText() const { return d_->statusText; }
QString LosslessAnalysisController::error() const { return d_->error; }
QVariantList LosslessAnalysisController::counts() const { return d_->counts; }

void LosslessAnalysisController::setConcurrency(const int requested)
{
    const int value = std::clamp(requested, 1, kMaximumConcurrency);
    if (d_->concurrency == value) return;
    d_->concurrency = value;
    emit concurrencyChanged();
    d_->dispatchAvailable();
}

void LosslessAnalysisController::setFilter(const QString& requested)
{
    const QString normalized = requested.trimmed().toCaseFolded();
    const QString value = normalized.isEmpty() ? QStringLiteral("all")
                                                : normalized;
    if (d_->filter == value) return;
    d_->filter = value;
    d_->model.setFilter(value);
    emit filterChanged();
}

void LosslessAnalysisController::setSearchText(const QString& value)
{
    const QString normalized = value.trimmed();
    if (d_->searchText == normalized) return;
    d_->searchText = normalized;
    d_->model.setSearchText(normalized);
    emit searchTextChanged();
}

void LosslessAnalysisController::loadFiles(const QVariantList& urls)
{
    d_->launchDiscovery(urls);
}

void LosslessAnalysisController::addFolder(const QUrl& folder)
{
    d_->launchDiscovery({folder});
}

void LosslessAnalysisController::start()
{
    if (d_->running && !d_->stopping) {
        if (!d_->discoveryJobs.isEmpty()) d_->pendingStart = true;
        d_->dispatchAvailable();
        return;
    }
    d_->setError({});
    d_->setStopping(false);
    d_->pendingStart = !d_->discoveryJobs.isEmpty();
    if (!d_->hasWaitingCheckedTask() && !d_->pendingStart) {
        d_->setStatus(d_->totalCount == 0
            ? QCoreApplication::translate(
                  "LosslessAnalysisController", "尚未添加文件")
            : QCoreApplication::translate(
                  "LosslessAnalysisController", "没有可开始的已选任务"));
        return;
    }
    d_->setRunning(true);
    d_->dispatchAvailable();
    d_->finishIfIdle();
}

void LosslessAnalysisController::cancel()
{
    if (d_->reportCancelled) {
        d_->reportCancelled->store(true, std::memory_order_release);
    }
    if (!d_->running && d_->discoveryJobs.isEmpty()) return;
    d_->pendingStart = false;
    d_->setStopping(true);
    d_->setStatus(QCoreApplication::translate(
        "LosslessAnalysisController", "正在停止…"));
    for (auto it = d_->activeJobs.begin(); it != d_->activeJobs.end(); ++it) {
        it->cancelled->store(true, std::memory_order_release);
        d_->model.updateTask(it.key(), {
            {QStringLiteral("state"), QStringLiteral("cancelling")},
            {QStringLiteral("stateText"),
             stateText(QStringLiteral("cancelling"))},
        });
    }
    for (const auto& job : d_->discoveryJobs) {
        job.cancelled->store(true, std::memory_order_release);
    }
    for (const QString& id : d_->model.taskIds()) {
        const QVariantMap task = d_->model.task(id);
        if (task.value(QStringLiteral("state")).toString()
                == QStringLiteral("waiting")) {
            QVariantMap selected =
                task.value(QStringLiteral("_selectedResult")).toMap();
            selected.insert(QStringLiteral("verdictCode"),
                            QStringLiteral("cancelled"));
            selected.insert(QStringLiteral("verdictText"),
                            verdictText(
                                agplayer::lossless::Verdict::Cancelled));
            d_->model.updateTask(id, {
                {QStringLiteral("state"), QStringLiteral("cancelled")},
                {QStringLiteral("stateText"),
                 stateText(QStringLiteral("cancelled"))},
                {QStringLiteral("verdictCode"), QStringLiteral("cancelled")},
                {QStringLiteral("verdictText"),
                 verdictText(agplayer::lossless::Verdict::Cancelled)},
                {QStringLiteral("progress"), 1.0},
                {QStringLiteral("_selectedResult"), selected},
            });
            if (d_->selectedTaskId == id) {
                d_->selectedResult = selected;
                emit selectedResultChanged();
            }
        }
    }
    d_->updateAggregates();
    d_->finishIfIdle();
}

void LosslessAnalysisController::retrySelected()
{
    if (d_->stopping) return;
    bool any = false;
    for (const QString& id : d_->model.taskIds()) {
        const QVariantMap task = d_->model.task(id);
        if (!task.value(QStringLiteral("checked")).toBool()
            || d_->activeJobs.contains(id)) {
            continue;
        }
        any = true;
        QVariantMap selected = task.value(QStringLiteral("_selectedResult")).toMap();
        selected.insert(QStringLiteral("verdictCode"), QVariant{});
        selected.insert(QStringLiteral("verdictText"), QVariant{});
        selected.insert(QStringLiteral("confidence"), QVariant{});
        selected.insert(QStringLiteral("error"), QVariant{});
        selected.insert(QStringLiteral("evidence"), QVariantList{});
        selected.insert(QStringLiteral("candidates"), QVariantList{});
        selected.insert(QStringLiteral("chain"), QVariantList{});
        selected.insert(QStringLiteral("spectrum"), QVariantList{});
        selected.insert(QStringLiteral("spectrogram"), QVariantList{});
        d_->model.updateTask(id, {
            {QStringLiteral("state"), QStringLiteral("waiting")},
            {QStringLiteral("stateText"), stateText(QStringLiteral("waiting"))},
            {QStringLiteral("verdictCode"), QString{}},
            {QStringLiteral("verdictText"), QString{}},
            {QStringLiteral("confidence"), 0},
            {QStringLiteral("progress"), 0.0},
            {QStringLiteral("_generation"), ++d_->generationClock},
            {QStringLiteral("_includeSpectrogram"), false},
            {QStringLiteral("_selectedResult"), selected},
            {QStringLiteral("_reportResult"), QVariantMap{}},
            {QStringLiteral("_resultBytes"), 0LL},
            {QStringLiteral("_fromCache"), false},
        });
        if (d_->selectedTaskId == id) {
            d_->selectedResult = selected;
            emit selectedResultChanged();
        }
    }
    if (any) start();
}

void LosslessAnalysisController::removeSelected()
{
    QStringList removed;
    for (const QString& id : d_->model.taskIds()) {
        if (d_->model.task(id).value(QStringLiteral("checked")).toBool()) {
            if (d_->activeJobs.contains(id)) {
                d_->setError(QCoreApplication::translate(
                    "LosslessAnalysisController",
                    "分析进行中，无法移除活动任务"));
                return;
            }
            removed.append(id);
        }
    }
    if (removed.isEmpty()) return;
    d_->model.removeTasks(removed);
    if (removed.contains(d_->selectedTaskId)) {
        const QStringList remaining = d_->model.taskIds();
        d_->selectedTaskId = remaining.isEmpty() ? QString{} : remaining.first();
        d_->selectedResult = remaining.isEmpty()
            ? emptySelectedResult()
            : d_->model.task(d_->selectedTaskId)
                  .value(QStringLiteral("_selectedResult")).toMap();
        emit selectedResultChanged();
    }
    d_->updateAggregates();
    d_->finishIfIdle();
}

void LosslessAnalysisController::clear()
{
    if (d_->running || !d_->discoveryJobs.isEmpty()) {
        d_->clearWhenIdle = true;
        cancel();
        return;
    }
    d_->model.clearTasks();
    d_->selectedTaskId.clear();
    d_->selectedResult = emptySelectedResult();
    d_->setError({});
    d_->updateAggregates();
    d_->setStatus(QCoreApplication::translate(
        "LosslessAnalysisController", "尚未添加文件"));
    emit selectedResultChanged();
}

void LosslessAnalysisController::selectTask(const QString& id)
{
    const QVariantMap task = d_->model.task(id);
    if (task.isEmpty() || d_->selectedTaskId == id) return;
    if (!d_->selectedTaskId.isEmpty()) {
        QVariantMap previous = d_->model.task(d_->selectedTaskId);
        QVariantMap previousSelected =
            previous.value(QStringLiteral("_selectedResult")).toMap();
        if (!previousSelected.value(QStringLiteral("spectrogram"))
                 .toList().isEmpty()) {
            previousSelected.insert(QStringLiteral("spectrogram"),
                                    QVariantList{});
            const QVariantMap previousReport =
                previous.value(QStringLiteral("_reportResult")).toMap();
            QVariantMap compactPreviousReport = previousReport;
            if (!compactPreviousReport.isEmpty()) {
                compactPreviousReport.insert(
                    QStringLiteral("spectrogram"), QVariantMap{});
            }
            d_->model.updateTask(d_->selectedTaskId, {
                {QStringLiteral("_selectedResult"), previousSelected},
                {QStringLiteral("_reportResult"), compactPreviousReport},
                {QStringLiteral("_resultBytes"),
                 estimatedVariantBytes(previousSelected)
                     + estimatedVariantBytes(compactPreviousReport)},
            });
        }
    }
    d_->selectedTaskId = id;
    d_->selectedResult =
        task.value(QStringLiteral("_selectedResult")).toMap();
    if (d_->selectedResult.isEmpty()) d_->selectedResult = emptySelectedResult();
    emit selectedResultChanged();
}

void LosslessAnalysisController::setChecked(const QString& id,
                                            const bool checked)
{
    d_->model.setChecked(id, checked);
    d_->updateAggregates();
}

void LosslessAnalysisController::selectAll(const bool checked)
{
    d_->model.selectAllVisible(checked);
    d_->updateAggregates();
}

void LosslessAnalysisController::exportReport(
    const QUrl& destination, const QString& format)
{
    if (!d_->reportWatchers.isEmpty()) {
        const QString message = QCoreApplication::translate(
            "LosslessAnalysisController", "报告正在导出，请稍候");
        d_->setError(message);
        emit reportExportFailed(message);
        return;
    }
    if (!destination.isLocalFile()) {
        const QString message = QCoreApplication::translate(
            "LosslessAnalysisController", "报告必须保存到本地文件");
        d_->setError(message);
        emit reportExportFailed(message);
        return;
    }
    QVariantList results;
    QStringList sourcePaths;
    for (const QVariantMap& task : d_->model.allTasks()) {
        sourcePaths.append(
            task.value(QStringLiteral("filePath")).toString());
        const QVariantMap report =
            task.value(QStringLiteral("_reportResult")).toMap();
        if (!report.isEmpty()) results.append(report);
    }
    if (results.isEmpty()) {
        const QString message = QCoreApplication::translate(
            "LosslessAnalysisController", "没有可导出的分析结果");
        d_->setError(message);
        emit reportExportFailed(message);
        return;
    }
    const QString path = destination.toLocalFile();
    auto reportCancelled = std::make_shared<std::atomic_bool>(false);
    d_->reportCancelled = reportCancelled;
    d_->setError({});
    d_->setStatus(QCoreApplication::translate(
        "LosslessAnalysisController", "正在导出报告…"));
    auto* watcher = new QFutureWatcher<LosslessReportWriteResult>(this);
    d_->reportWatchers.append(watcher);
    QPointer<LosslessAnalysisController> receiver(this);
    connect(
        watcher, &QFutureWatcher<LosslessReportWriteResult>::finished, this,
        [this, watcher, receiver, destination] {
            d_->reportWatchers.removeOne(watcher);
            d_->reportCancelled.reset();
            const LosslessReportWriteResult result = watcher->result();
            watcher->deleteLater();
            if (!receiver || !receiver->d_) return;
            if (result.success) {
                d_->setStatus(QCoreApplication::translate(
                    "LosslessAnalysisController", "报告已导出"));
                emit reportExported(destination);
            } else {
                d_->setError(result.error);
                d_->setStatus(QCoreApplication::translate(
                    "LosslessAnalysisController", "报告导出失败"));
                emit reportExportFailed(result.error);
            }
        });
    watcher->setFuture(QtConcurrent::run(
        &d_->reportPool,
        [path, format, results, sourcePaths, reportCancelled] {
            return LosslessReport::write(
                path, format, results, sourcePaths,
                [reportCancelled] {
                    return reportCancelled->load(std::memory_order_acquire);
                });
        }));
}

void LosslessAnalysisController::requestSpectrogram()
{
    if (d_->selectedTaskId.isEmpty()
        || d_->activeJobs.contains(d_->selectedTaskId)) {
        return;
    }
    const QVariantMap task = d_->model.task(d_->selectedTaskId);
    const QVariantMap selected =
        task.value(QStringLiteral("_selectedResult")).toMap();
    if (task.value(QStringLiteral("state")).toString()
            != QStringLiteral("completed")) {
        d_->setError(QCoreApplication::translate(
            "LosslessAnalysisController", "请先完成该文件的基础分析"));
        return;
    }
    if (!selected.value(QStringLiteral("spectrogram")).toList().isEmpty()) {
        return;
    }
    d_->model.updateTask(d_->selectedTaskId, {
        {QStringLiteral("checked"), true},
        {QStringLiteral("state"), QStringLiteral("waiting")},
        {QStringLiteral("stateText"), stateText(QStringLiteral("waiting"))},
        {QStringLiteral("progress"), 0.0},
        {QStringLiteral("_generation"), ++d_->generationClock},
        {QStringLiteral("_includeSpectrogram"), true},
    });
    d_->updateAggregates();
    start();
}

void LosslessAnalysisController::refreshTranslations()
{
    bool selectedChanged = false;
    for (const QString& id : d_->model.taskIds()) {
        const QVariantMap task = d_->model.task(id);
        const QString state = task.value(QStringLiteral("state")).toString();
        const QString code =
            task.value(QStringLiteral("verdictCode")).toString();
        QString translatedState = stateText(state);
        if (task.value(QStringLiteral("_fromCache")).toBool()
            && state == QStringLiteral("completed")) {
            translatedState = QCoreApplication::translate(
                "LosslessAnalysisController", "已完成（缓存）");
        }
        const QString translatedVerdict = translatedVerdictCode(code);
        QVariantMap selected =
            task.value(QStringLiteral("_selectedResult")).toMap();
        const QVariantMap report =
            task.value(QStringLiteral("_reportResult")).toMap();
        if (!selected.isEmpty()) {
            selected.insert(QStringLiteral("verdictText"), translatedVerdict);
            if (!report.isEmpty()) {
                QVariantList evidence;
                for (const QVariant& value :
                     report.value(QStringLiteral("evidence")).toList()) {
                    const QVariantMap raw = value.toMap();
                    const QString explanation =
                        raw.value(QStringLiteral("explanation")).toString();
                    evidence.append(QVariantMap{
                        {QStringLiteral("text"),
                         translatedEvidence(explanation.isEmpty()
                             ? raw.value(QStringLiteral("code")).toString()
                             : explanation)},
                        {QStringLiteral("value"),
                         raw.value(QStringLiteral("value"))},
                        {QStringLiteral("unit"),
                         raw.value(QStringLiteral("unit"))},
                        {QStringLiteral("reference"),
                         translatedEvidence(raw.value(QStringLiteral("reference")).toString())},
                        {QStringLiteral("coverageStartSeconds"),
                         raw.value(QStringLiteral("coverageStartSeconds"))},
                        {QStringLiteral("coverageEndSeconds"),
                         raw.value(QStringLiteral("coverageEndSeconds"))},
                    });
                }
                QVariantList candidates;
                for (const QVariant& value :
                     report.value(QStringLiteral("candidates")).toList()) {
                    const QVariantMap raw = value.toMap();
                    candidates.append(QVariantMap{
                        {QStringLiteral("format"),
                         translatedEvidence(raw.value(
                             QStringLiteral("format")).toString())},
                        {QStringLiteral("confidence"),
                         raw.value(QStringLiteral("confidence"))},
                        {QStringLiteral("limitation"),
                         translatedEvidence(raw.value(
                             QStringLiteral("limitation")).toString())},
                    });
                }
                const auto translateList = [](const QVariantList& raw) {
                    QVariantList translated;
                    translated.reserve(raw.size());
                    for (const QVariant& value : raw) {
                        translated.append(
                            translatedEvidence(value.toString()));
                    }
                    return translated;
                };
                selected.insert(QStringLiteral("evidence"), evidence);
                selected.insert(QStringLiteral("candidates"), candidates);
                selected.insert(QStringLiteral("chain"),
                    translateList(report.value(
                        QStringLiteral("chain")).toList()));
                selected.insert(QStringLiteral("warnings"),
                    translateList(report.value(
                        QStringLiteral("warnings")).toList()));
                selected.insert(QStringLiteral("error"),
                    translatedEvidence(report.value(
                        QStringLiteral("error")).toString()));
                const QVariantMap measurements =
                    report.value(QStringLiteral("measurements")).toMap();
                const QString sourceKind = report.value(
                    QStringLiteral("source")).toMap().value(
                        QStringLiteral("kind")).toString();
                const bool dsdLike = sourceKind == QStringLiteral("dsd")
                    || sourceKind == QStringLiteral("dst");
                const double mirror = measurements.value(
                    QStringLiteral("resamplingMirrorScore")).toDouble();
                const double holes = measurements.value(
                    QStringLiteral("codecHoleScore")).toDouble();
                QString gridText;
                if (!dsdLike) {
                    const int targetRate = report.value(QStringLiteral("source"))
                        .toMap().value(QStringLiteral("sampleRate")).toInt();
                    for (const QVariant& value : report.value(QStringLiteral("evidence")).toList()) {
                        const QVariantMap item = value.toMap();
                        if (item.value(QStringLiteral("code")).toString() == QStringLiteral("resampling_polyphase_grid")
                            && item.value(QStringLiteral("family")).toString() == QStringLiteral("resampling")
                            && item.value(QStringLiteral("direction")).toString() == QStringLiteral("supports_upsampling")) {
                            gridText = resamplingGridText(
                                item.value(QStringLiteral("value")).toDouble(), targetRate);
                            if (!gridText.isEmpty()) break;
                        }
                    }
                }
                selected.insert(QStringLiteral("resamplingText"),
                    !gridText.isEmpty() ? gridText
                    : !dsdLike && mirror > 0.0
                        ? QCoreApplication::translate(
                              "LosslessAnalysisController", "镜像评分 %1")
                              .arg(mirror, 0, 'f', 3)
                        : QString{});
                selected.insert(QStringLiteral("holesText"),
                    !dsdLike && holes > 0.0
                        ? QCoreApplication::translate(
                              "LosslessAnalysisController", "结构空洞评分 %1")
                              .arg(holes, 0, 'f', 3)
                        : QString{});
            }
        }
        d_->model.updateTask(id, {
            {QStringLiteral("stateText"), translatedState},
            {QStringLiteral("verdictText"), translatedVerdict},
            {QStringLiteral("_selectedResult"), selected},
            {QStringLiteral("_resultBytes"),
             estimatedVariantBytes(selected)
                 + estimatedVariantBytes(report)},
        });
        if (d_->selectedTaskId == id) {
            d_->selectedResult = selected;
            selectedChanged = true;
        }
    }
    if (d_->running || !d_->discoveryJobs.isEmpty()) {
        d_->updateStatusForWork();
    } else {
        d_->setTerminalStatus();
    }
    d_->trimCache();
    if (selectedChanged) emit selectedResultChanged();
}

void LosslessAnalysisController::notifyError(const QString& message)
{
    d_->setError(message);
}

qint64 LosslessAnalysisController::cacheBytesForTesting() const noexcept
{
    return d_->cacheBytes;
}

qint64 LosslessAnalysisController::totalRetainedBytesForTesting() const noexcept
{
    return d_->cacheBytes + d_->retainedResultBytes();
}

qint64 LosslessAnalysisController::cacheLimitBytesForTesting() const noexcept
{
    return d_->cacheLimit;
}

void LosslessAnalysisController::setCacheLimitBytesForTesting(
    const qint64 bytes)
{
    d_->cacheLimit = std::max<qint64>(0, bytes);
    d_->trimCache();
}
