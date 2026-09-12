#pragma once

#include "../decoder.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace agplayer::lossless {

inline constexpr std::string_view kAnalysisSchemaVersion = "1";
inline constexpr std::string_view kAlgorithmVersion = "lossless-1.13";
inline constexpr std::string_view kParameterVersion = "lossless-params-16";
inline constexpr std::string_view kInferenceDisclaimer =
    u8"\u7ed3\u679c\u4e3a\u4fe1\u53f7\u7279\u5f81\u63a8\u65ad\uff0c"
    u8"\u4e0d\u4ee3\u8868\u53ef\u6062\u590d\u539f\u59cb\u6587\u4ef6\u3002";

enum class Verdict {
    CredibleLossless,
    SuspectedLossyTranscode,
    SuspectedUpsample,
    SuspectedBitDepthExpansion,
    SuspectedLossyUpsample,
    CredibleNativeDsd,
    SuspectedPcmToDsd,
    Inconclusive,
    AnalysisFailed,
    Cancelled,
};

[[nodiscard]] constexpr std::string_view verdictCode(const Verdict verdict) noexcept
{
    switch (verdict) {
    case Verdict::CredibleLossless: return "credible_lossless";
    case Verdict::SuspectedLossyTranscode: return "suspected_lossy_transcode";
    case Verdict::SuspectedUpsample: return "suspected_upsample";
    case Verdict::SuspectedBitDepthExpansion: return "suspected_bit_depth_expansion";
    case Verdict::SuspectedLossyUpsample: return "suspected_lossy_upsample";
    case Verdict::CredibleNativeDsd: return "credible_native_dsd";
    case Verdict::SuspectedPcmToDsd: return "suspected_pcm_to_dsd";
    case Verdict::Inconclusive: return "inconclusive";
    case Verdict::AnalysisFailed: return "analysis_failed";
    case Verdict::Cancelled: return "cancelled";
    }
    return "analysis_failed";
}

enum class SourceKind {
    Unknown,
    PcmInteger,
    PcmFloatingPoint,
    Dsd,
    Dst,
};

enum class EvidenceFamily {
    Bandwidth,
    CodecStructure,
    Quantization,
    Resampling,
    Dsd,
    ContentQuality,
};

enum class EvidenceDirection {
    Neutral,
    SupportsAuthenticity,
    SupportsLossySource,
    SupportsUpsampling,
    SupportsBitDepthExpansion,
    SupportsPcmToDsd,
    ContradictsConclusion,
};

struct FileIdentity final {
    std::string path;
    std::uint64_t fileSize = 0;
    std::int64_t modifiedUnixMs = 0;
};

struct SourceFormat final {
    std::string container;
    std::string codec;
    std::string channelLayout;
    SourceKind kind = SourceKind::Unknown;
    int sampleRate = 0;
    int rawDsdSampleRate = 0;
    int decodedSampleRate = 0;
    int bitsPerSample = 0;
    int channels = 0;
    std::int64_t durationMs = 0;
    bool codecIsLossless = false;
    bool dstDecoded = false;
};

struct CoverageSummary final {
    std::uint64_t decodedFrames = 0;
    std::uint64_t analyzedWindows = 0;
    std::uint64_t activeWindows = 0;
    double decodedRatio = 0.0;
    double activeWindowRatio = 0.0;
};

struct AnalysisMeasurements final {
    double peak = 0.0;
    double rms = 0.0;
    double dynamicRangeDb = 0.0;
    double cutoffHz = 0.0;
    double cutoffStability = 0.0;
    double highFrequencyEnergyRatio = 0.0;
    double spectralFlatness = 0.0;
    double spectralEntropy = 0.0;
    double spectralEdgeFrequencyHz = 0.0;
    double spectralEdgeDepthDb = 0.0;
    double spectralEdgeStability = 0.0;
    bool lowLevelSpectralEdgeMeasured = false;
    double lowLevelSpectralEdgeHz = 0.0;
    double lowLevelSpectralEdgeDepthDb = 0.0;
    double resamplingPhaseSourceRate = 0.0;
    double resamplingPhaseStrength = 0.0;
    double resamplingPhaseCoherence = 0.0;
    std::uint64_t resamplingPhaseSegments = 0;
    double resamplingResidualEntropy = 0.0;
    bool resamplingResidualEntropyMeasured = false;
    double resamplingBandSuppressionDb = 0.0;
    bool resamplingBandSuppressionMeasured = false;
    double mdctFrameCoherentPeakDb = 0.0;
    double mdctFramePeakZ = 0.0;
    std::uint64_t mdctFrameBlocks = 0;
    std::uint64_t mdctFrameAlignedBlocks = 0;
    std::string mdctFrameWindow;
    int mdctAnalysisSampleRate = 0;
    std::uint64_t celtFrameBlocks = 0;
    std::uint64_t celtFrameSamples = 0;
    std::uint64_t celtFramesPerAnchor = 0;
    double celtFrameMinimumBandZ = 0.0;
    double celtFrameMinimumAnchorCoherence = 0.0;
    std::uint64_t celtFrameMaximumPeakWidth = 0;
    std::uint64_t celtFrameBandPhaseDifference = 0;
    double codecHoleScore = 0.0;
    // Mean excess precursor/attack power ratio across eligible PCM attacks;
    // structural observation only, not proof of codec-induced pre-echo.
    double transientPreEchoScore = 0.0;
    bool transientPreEchoMeasured = false;
    // Zero means unavailable (silence, steady/quiet content or DSD), not a
    // measured absence. Reports must gate score on transientPreEchoMeasured.
    std::uint64_t transientCount = 0;
    double resamplingMirrorScore = 0.0;
    double lowBitUsageRatio = 0.0;
    double effectiveBits = 0.0;
    double channelCorrelation = 0.0;
    double dominantFrequencyHz = 0.0;
    double dsdUltrasonicSlopeDbPerOctave = 0.0;
    double dsdNoiseShapingStability = 0.0;
    double dsdRepeatedPatternRatio = 0.0;
    double dsdRawBitOneRatio = 0.0;
    double dsdRawByteEntropy = 0.0;
    std::uint64_t dsdRawPacketBytes = 0;
    std::vector<std::size_t> stftWindowSizes;
};

struct Evidence final {
    std::string code;
    EvidenceFamily family = EvidenceFamily::ContentQuality;
    EvidenceDirection direction = EvidenceDirection::Neutral;
    double value = 0.0;
    std::string unit;
    std::string reference;
    int severity = 0;
    double coverageStartSeconds = 0.0;
    double coverageEndSeconds = 0.0;
    std::string explanation;
};

struct SourceCandidate final {
    std::string format;
    int confidence = 0;
    std::string limitation;
};

struct SpectrumSummary final {
    double nyquistHz = 0.0;
    double minDb = -120.0;
    double maxDb = 0.0;
    std::vector<double> db;
};

// Row-major peak-hold dB evidence. Each cell is the strongest real STFT power
// observed inside its bounded time/frequency bucket; values are never
// interpolated and this remains a UI evidence summary, not a full STFT matrix.
struct SpectrogramSummary final {
    std::size_t timeBins = 0;
    std::size_t frequencyBins = 0;
    double nyquistHz = 0.0;
    std::vector<float> db;
};

struct AnalysisOptions final {
    int isoTrackIndex = -1;
    std::size_t spectrumBins = 256;
    bool includeSpectrogram = false;
    std::size_t maxSpectrogramTimeBins = 256;
    std::size_t maxSpectrogramFrequencyBins = 256;
    // Optional virtual input. The callback context must outlive analyzeFile().
    DecoderOpenOptions decoderOpenOptions;
};

using ProgressCallback = std::function<void(float)>;

struct AnalysisResult final {
    std::int64_t analysisStartedUnixMs = 0;
    std::string schemaVersion{std::string(kAnalysisSchemaVersion)};
    std::string algorithmVersion{std::string(kAlgorithmVersion)};
    std::string parameterVersion{std::string(kParameterVersion)};
    FileIdentity file;
    SourceFormat source;
    Verdict verdict = Verdict::Inconclusive;
    int confidence = 0;
    CoverageSummary coverage;
    AnalysisMeasurements measurements;
    std::vector<Evidence> evidence;
    std::vector<SourceCandidate> candidates;
    std::vector<std::string> chain;
    SpectrumSummary spectrum;
    SpectrogramSummary spectrogram;
    std::vector<std::string> warnings;
    std::string error;
    bool cancelled = false;
};

} // namespace agplayer::lossless
