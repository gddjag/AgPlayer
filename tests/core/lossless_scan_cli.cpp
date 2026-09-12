#include "lossless/lossless_analyzer.hpp"
#include "lossless/sacd_reader.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QFileInfo>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
namespace {
struct QaMemorySnapshot {
    bool available = false;
    DWORD error = 0;
    quint64 privateBytes = 0;
    quint64 lifetimePeakPrivateBytes = 0;
};
QaMemorySnapshot qaMemorySnapshot() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    QaMemorySnapshot value;
    value.available = K32GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0;
    if (!value.available) { value.error = GetLastError(); return value; }
    value.privateBytes = counters.PrivateUsage;
    value.lifetimePeakPrivateBytes = counters.PeakPagefileUsage;
    return value;
}
}
#endif

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QJsonArray rows;std::atomic_bool cancel{false};
    const bool qaMemory = app.arguments().size() > 1 && app.arguments()[1] == "--qa-memory";
    if (qaMemory && (app.arguments().size() != 3
        || QFileInfo(app.arguments()[2]).suffix().compare("iso", Qt::CaseInsensitive) == 0)) {
        QTextStream(stderr) << "Usage: --qa-memory <one non-ISO audio file>\n"; return 2;
    }
#ifndef _WIN32
    if (qaMemory) { QTextStream(stderr) << "--qa-memory requires Windows\n"; return 2; }
#endif
    QJsonObject memoryReport;
    int qaExit = 0;
    for(int a=qaMemory ? 2 : 1;a<app.arguments().size();++a){
        const auto path=app.arguments()[a];
        int tracks=1;const bool iso=QFileInfo(path).suffix().compare("iso",Qt::CaseInsensitive)==0;
        if(iso){agplayer::lossless::SacdReader reader;std::string error;
            if(reader.open(path.toUtf8().toStdString(),error))tracks=static_cast<int>(reader.tracks().size());}
        for(int t=0;t<tracks;++t){
            agplayer::lossless::AnalysisOptions options;options.isoTrackIndex=iso?t:-1;
            QElapsedTimer timer;timer.start();
            const auto sourcePath = path.toUtf8().toStdString();
#ifdef _WIN32
            const auto memoryBefore = qaMemory ? qaMemorySnapshot() : QaMemorySnapshot{};
#endif
            const auto r=agplayer::lossless::analyzeFile(sourcePath,options,cancel);
#ifdef _WIN32
            // Snapshot before constructing JSON, so serialization is outside the interval.
            if (qaMemory) {
                const auto after = qaMemorySnapshot();
                const bool available = memoryBefore.available && after.available;
                memoryReport = QJsonObject{
                    {"available", available},
                    {"beforeWin32Error", static_cast<qint64>(memoryBefore.error)},
                    {"afterWin32Error", static_cast<qint64>(after.error)},
                    {"beforePrivateUsageBytes", static_cast<qint64>(memoryBefore.privateBytes)},
                    {"beforePeakPagefileUsageBytes", static_cast<qint64>(memoryBefore.lifetimePeakPrivateBytes)},
                    {"afterPrivateUsageBytes", static_cast<qint64>(after.privateBytes)},
                    {"afterPeakPagefileUsageBytes", static_cast<qint64>(after.lifetimePeakPrivateBytes)},
                    {"peakMinusBeforePrivateBytes", available ? QJsonValue(static_cast<qint64>(
                        after.lifetimePeakPrivateBytes > memoryBefore.privateBytes
                            ? after.lifetimePeakPrivateBytes - memoryBefore.privateBytes : 0)) : QJsonValue(QJsonValue::Null)},
                    {"lifetimePeakIncreaseBytes", available ? QJsonValue(static_cast<qint64>(
                        after.lifetimePeakPrivateBytes > memoryBefore.lifetimePeakPrivateBytes
                            ? after.lifetimePeakPrivateBytes - memoryBefore.lifetimePeakPrivateBytes : 0)) : QJsonValue(QJsonValue::Null)},
                    {"scope", "fresh-process private-commit telemetry around analyzeFile; includes decoder, lazy runtime allocations and returned result; not a DSP allocation profiler"},
                    {"baseline", "after QCoreApplication/options/path initialization; before analyzeFile; no warmup, allocator purge or working-set trimming"},
                    {"limitations", "PeakPagefileUsage is a process-lifetime high-water mark; neither subtraction resets it or isolates live DSP buffers. Baseline allocation release can also affect the difference."}
                };
                qaExit = !available ? 4 : (!r.error.empty() ? 3 : 0);
            }
#endif
            const auto text=[](const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));};
            QJsonArray evidence,warnings;
            for(const auto& e:r.evidence)evidence.append(QJsonObject{{"code",text(e.code)},{"value",e.value},{"unit",text(e.unit)},{"reference",text(e.reference)},{"text",text(e.explanation)}});
            for(const auto& w:r.warnings)warnings.append(text(w));
            const auto code=agplayer::lossless::verdictCode(r.verdict);
            rows.append(QJsonObject{{"path",path},{"algorithmVersion",text(r.algorithmVersion)},{"parameterVersion",text(r.parameterVersion)},
                {"trackIndex",iso?t:-1},{"verdict",QString::fromUtf8(code.data(),static_cast<qsizetype>(code.size()))},
                {"confidence",r.confidence},{"confidenceKind","ordinal_evidence_score"},
                {"calibrationStatus","uncalibrated"},{"calibratedProbability",QJsonValue::Null},
                {"codec",text(r.source.codec)},{"sampleRate",r.source.sampleRate},{"rawDsdSampleRate",r.source.rawDsdSampleRate},
                {"decodedSampleRate",r.source.decodedSampleRate},{"bits",r.source.bitsPerSample},{"channels",r.source.channels},{"durationMs",r.source.durationMs},
                {"coverage",r.coverage.decodedRatio},{"windows",static_cast<qint64>(r.coverage.analyzedWindows)},{"cutoffHz",r.measurements.cutoffHz},
                {"cutoffStability",r.measurements.cutoffStability},{"highFrequencyEnergyRatio",r.measurements.highFrequencyEnergyRatio},
                {"spectralEntropy",r.measurements.spectralEntropy},{"spectralFlatness",r.measurements.spectralFlatness},
                {"spectralEdgeFrequencyHz",r.measurements.spectralEdgeFrequencyHz},{"spectralEdgeDepthDb",r.measurements.spectralEdgeDepthDb},
                {"spectralEdgeStability",r.measurements.spectralEdgeStability},
                {"lowLevelSpectralEdgeMeasured",r.measurements.lowLevelSpectralEdgeMeasured},
                {"lowLevelSpectralEdgeHz",r.measurements.lowLevelSpectralEdgeHz},
                {"lowLevelSpectralEdgeDepthDb",r.measurements.lowLevelSpectralEdgeDepthDb},
                {"resamplingPhaseSourceRate",r.measurements.resamplingPhaseSourceRate},
                {"resamplingPhaseStrength",r.measurements.resamplingPhaseStrength},
                {"resamplingPhaseCoherence",r.measurements.resamplingPhaseCoherence},
                {"resamplingPhaseSegments",static_cast<qint64>(r.measurements.resamplingPhaseSegments)},
                {"resamplingResidualEntropy",r.measurements.resamplingResidualEntropy},
                {"resamplingResidualEntropyMeasured",r.measurements.resamplingResidualEntropyMeasured},
                {"resamplingBandSuppressionDb",r.measurements.resamplingBandSuppressionDb},
                {"resamplingBandSuppressionMeasured",r.measurements.resamplingBandSuppressionMeasured},
                {"mdctFrameCoherentPeakDb",r.measurements.mdctFrameCoherentPeakDb},
                {"mdctFramePeakZ",r.measurements.mdctFramePeakZ},
                {"mdctFrameBlocks",static_cast<qint64>(r.measurements.mdctFrameBlocks)},
                {"mdctFrameAlignedBlocks",static_cast<qint64>(r.measurements.mdctFrameAlignedBlocks)},
                {"mdctFrameWindow",text(r.measurements.mdctFrameWindow)},
                {"mdctAnalysisSampleRate",r.measurements.mdctAnalysisSampleRate},
                {"celtFrameBlocks",static_cast<qint64>(r.measurements.celtFrameBlocks)},
                {"celtFrameSamples",static_cast<qint64>(r.measurements.celtFrameSamples)},
                {"celtFramesPerAnchor",static_cast<qint64>(r.measurements.celtFramesPerAnchor)},
                {"celtFrameMinimumBandZ",r.measurements.celtFrameMinimumBandZ},
                {"celtFrameMinimumAnchorCoherence",r.measurements.celtFrameMinimumAnchorCoherence},
                {"celtFrameMaximumPeakWidth",static_cast<qint64>(r.measurements.celtFrameMaximumPeakWidth)},
                {"celtFrameBandPhaseDifference",static_cast<qint64>(r.measurements.celtFrameBandPhaseDifference)},
                {"transientPreEchoMeasured",r.measurements.transientPreEchoMeasured},
                {"transientPreEchoScore",r.measurements.transientPreEchoMeasured ? QJsonValue(r.measurements.transientPreEchoScore) : QJsonValue(QJsonValue::Null)},
                {"transientCount",static_cast<qint64>(r.measurements.transientCount)},
                {"effectiveBits",r.measurements.effectiveBits},{"holeScore",r.measurements.codecHoleScore},{"mirrorScore",r.measurements.resamplingMirrorScore},
                {"elapsedMs",timer.elapsed()},{"error",text(r.error)},{"evidence",evidence},{"warnings",warnings}});
        }
    }
    if (qaMemory) {
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"qaMemory", memoryReport},
            {"analysisExitCode", qaExit}, {"results", rows}}).toJson(QJsonDocument::Indented);
        return qaExit;
    }
    QTextStream(stdout)<<QJsonDocument(rows).toJson(QJsonDocument::Indented);return 0;
}
