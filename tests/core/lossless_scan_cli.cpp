#include "lossless/lossless_analyzer.hpp"
#include "lossless/sacd_reader.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QFileInfo>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QJsonArray rows;std::atomic_bool cancel{false};
    for(int a=1;a<app.arguments().size();++a){
        const auto path=app.arguments()[a];
        int tracks=1;const bool iso=QFileInfo(path).suffix().compare("iso",Qt::CaseInsensitive)==0;
        if(iso){agplayer::lossless::SacdReader reader;std::string error;
            if(reader.open(path.toUtf8().toStdString(),error))tracks=static_cast<int>(reader.tracks().size());}
        for(int t=0;t<tracks;++t){
            agplayer::lossless::AnalysisOptions options;options.isoTrackIndex=iso?t:-1;
            QElapsedTimer timer;timer.start();
            const auto r=agplayer::lossless::analyzeFile(path.toUtf8().toStdString(),options,cancel);
            const auto text=[](const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));};
            QJsonArray evidence,warnings;
            for(const auto& e:r.evidence)evidence.append(QJsonObject{{"code",text(e.code)},{"value",e.value},{"unit",text(e.unit)},{"reference",text(e.reference)},{"text",text(e.explanation)}});
            for(const auto& w:r.warnings)warnings.append(text(w));
            const auto code=agplayer::lossless::verdictCode(r.verdict);
            rows.append(QJsonObject{{"path",path},{"algorithmVersion",text(r.algorithmVersion)},{"parameterVersion",text(r.parameterVersion)},
                {"trackIndex",iso?t:-1},{"verdict",QString::fromUtf8(code.data(),static_cast<qsizetype>(code.size()))},
                {"confidence",r.confidence},{"codec",text(r.source.codec)},{"sampleRate",r.source.sampleRate},{"rawDsdSampleRate",r.source.rawDsdSampleRate},
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
    QTextStream(stdout)<<QJsonDocument(rows).toJson(QJsonDocument::Indented);return 0;
}
