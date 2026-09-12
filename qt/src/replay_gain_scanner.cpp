#include "replay_gain_scanner.hpp"

#include "decoder.hpp"
#include "library_model.hpp"

#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr double kTargetLufs = -18.0;

struct Biquad final {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;

    double process(double input, const std::array<double, 3>& b,
                   const std::array<double, 3>& a) noexcept
    {
        const double output = b[0] * input + b[1] * x1 + b[2] * x2
                              - a[1] * y1 - a[2] * y2;
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
        return output;
    }
};

constexpr std::array<double, 3> kShelfB{
    1.53512485958697, -2.69169618940638, 1.19839281085285};
constexpr std::array<double, 3> kShelfA{
    1.0, -1.69065929318241, 0.73248077421585};
constexpr std::array<double, 3> kHighPassB{1.0, -2.0, 1.0};
constexpr std::array<double, 3> kHighPassA{
    1.0, -1.99004745483398, 0.99007225036621};

double loudnessForEnergy(double energy) noexcept
{
    return -0.691 + 10.0 * std::log10(std::max(energy, 1.0e-20));
}

double average(const std::vector<double>& values) noexcept
{
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double value : values) sum += value;
    return sum / static_cast<double>(values.size());
}

struct ScanRecord final {
    QString trackId;
    QString albumKey;
    ReplayGainAnalysis analysis;
};
}

ReplayGainScanner::ReplayGainScanner(LibraryModel* library, QObject* parent)
    : QObject(parent), library_(library)
{
}

bool ReplayGainScanner::running() const noexcept { return running_; }
double ReplayGainScanner::progress() const noexcept { return progress_; }
QString ReplayGainScanner::errorMessage() const { return errorMessage_; }
void ReplayGainScanner::setLibraryModel(LibraryModel* library) noexcept { library_ = library; }

ReplayGainAnalysis ReplayGainScanner::analyzeFile(const QString& path)
{
    ReplayGainAnalysis result;
    agplayer::Decoder decoder;
    const ag_result opened = decoder.open(path.toUtf8().constData(), kSampleRate,
                                          kChannels);
    if (opened != AG_OK) {
        result.error = QStringLiteral("无法解码音频文件");
        return result;
    }

    std::array<Biquad, kChannels> shelf;
    std::array<Biquad, kChannels> highPass;
    constexpr std::size_t framesPerChunk = kSampleRate / 10;
    std::vector<double> chunkEnergies;
    double chunkSum = 0.0;
    std::size_t chunkFrames = 0;
    double peak = 0.0;

    agplayer::DecodedAudioBlock block;
    do {
        if (decoder.read(block) != AG_OK) {
            result.error = QStringLiteral("读取音频样本失败");
            return result;
        }
        for (std::size_t frame = 0; frame < block.frames; ++frame) {
            double frameEnergy = 0.0;
            for (int channel = 0; channel < kChannels; ++channel) {
                const std::size_t sampleIndex = frame * kChannels
                                                + static_cast<std::size_t>(channel);
                if (sampleIndex >= block.samples.size()) break;
                const double sample = block.samples[sampleIndex];
                peak = std::max(peak, std::abs(sample));
                const double weighted = highPass[channel].process(
                    shelf[channel].process(sample, kShelfB, kShelfA),
                    kHighPassB, kHighPassA);
                frameEnergy += weighted * weighted;
            }
            chunkSum += frameEnergy / kChannels;
            ++chunkFrames;
            if (chunkFrames == framesPerChunk) {
                chunkEnergies.push_back(chunkSum / chunkFrames);
                chunkSum = 0.0;
                chunkFrames = 0;
            }
        }
    } while (!block.end_of_stream);

    if (chunkFrames > 0) chunkEnergies.push_back(chunkSum / chunkFrames);
    if (chunkEnergies.empty()) {
        result.error = QStringLiteral("音频文件没有可分析的样本");
        return result;
    }

    std::vector<double> blocks;
    if (chunkEnergies.size() < 4) {
        blocks.push_back(average(chunkEnergies));
    } else {
        for (std::size_t index = 0; index + 4 <= chunkEnergies.size(); ++index) {
            blocks.push_back((chunkEnergies[index] + chunkEnergies[index + 1]
                              + chunkEnergies[index + 2] + chunkEnergies[index + 3])
                             / 4.0);
        }
    }

    std::vector<double> absoluteGated;
    for (double energy : blocks) {
        if (loudnessForEnergy(energy) >= -70.0) absoluteGated.push_back(energy);
    }
    if (absoluteGated.empty()) {
        result.error = QStringLiteral("音频响度低于可分析阈值");
        return result;
    }
    const double relativeGate = loudnessForEnergy(average(absoluteGated)) - 10.0;
    std::vector<double> gated;
    for (double energy : absoluteGated) {
        if (loudnessForEnergy(energy) >= relativeGate) gated.push_back(energy);
    }

    result.loudnessLufs = loudnessForEnergy(average(gated));
    result.gainDb = kTargetLufs - result.loudnessLufs;
    result.peak = peak;
    result.clipping = peak * std::pow(10.0, result.gainDb / 20.0) > 1.0;
    result.success = std::isfinite(result.loudnessLufs)
                     && std::isfinite(result.gainDb);
    return result;
}

bool ReplayGainScanner::applyResult(const QString& trackId,
                                    const ReplayGainAnalysis& result)
{
    return result.success && library_ != nullptr
           && library_->applyReplayGainResult(trackId, result.gainDb,
                                              result.gainDb, result.peak);
}

bool ReplayGainScanner::scanTrack(const QString& trackId)
{
    if (running_ || library_ == nullptr) return false;
    const int row = library_->indexForTrackId(trackId);
    if (row < 0) return false;
    const QString path = library_->tracks().at(row).path;
    running_ = true;
    progress_ = 0.0;
    errorMessage_.clear();
    emit runningChanged();
    emit progressChanged();
    emit errorMessageChanged();

    QPointer<ReplayGainScanner> self(this);
    QThreadPool::globalInstance()->start([self, trackId, path] {
        const ReplayGainAnalysis result = analyzeFile(path);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, trackId, result] {
            if (!self) return;
            self->running_ = false;
            self->progress_ = 1.0;
            self->errorMessage_ = result.error;
            if (self->applyResult(trackId, result)) {
                emit self->trackScanned(trackId, result.gainDb, result.peak,
                                        result.clipping);
            }
            emit self->runningChanged();
            emit self->progressChanged();
            emit self->errorMessageChanged();
        });
    });
    return true;
}

bool ReplayGainScanner::scanAll()
{
    if (running_ || library_ == nullptr) return false;
    const QList<TrackRecord> tracks = library_->tracks();
    if (tracks.isEmpty()) return false;
    running_ = true;
    progress_ = 0.0;
    errorMessage_.clear();
    emit runningChanged();
    emit progressChanged();
    emit errorMessageChanged();

    QPointer<ReplayGainScanner> self(this);
    QThreadPool::globalInstance()->start([self, tracks] {
        QList<ScanRecord> records;
        records.reserve(tracks.size());
        for (int index = 0; index < tracks.size(); ++index) {
            const TrackRecord& track = tracks.at(index);
            if (track.available) {
                records.append({track.trackId, track.album.trimmed().toCaseFolded(),
                                analyzeFile(track.path)});
            }
            if (self) {
                const double value = static_cast<double>(index + 1)
                                     / static_cast<double>(tracks.size());
                QMetaObject::invokeMethod(self, [self, value] {
                    if (!self) return;
                    self->progress_ = value;
                    emit self->progressChanged();
                });
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, records] {
            if (!self || self->library_ == nullptr) return;
            QHash<QString, std::vector<double>> albumEnergies;
            for (const ScanRecord& record : records) {
                if (!record.analysis.success || record.albumKey.isEmpty()) continue;
                const double energy = std::pow(
                    10.0, (record.analysis.loudnessLufs + 0.691) / 10.0);
                albumEnergies[record.albumKey].push_back(energy);
            }
            for (const ScanRecord& record : records) {
                if (!record.analysis.success) continue;
                double albumGain = record.analysis.gainDb;
                const auto found = albumEnergies.constFind(record.albumKey);
                if (found != albumEnergies.cend() && !found->empty()) {
                    albumGain = kTargetLufs - loudnessForEnergy(average(*found));
                }
                self->library_->applyReplayGainResult(
                    record.trackId, record.analysis.gainDb, albumGain,
                    record.analysis.peak);
                emit self->trackScanned(record.trackId, record.analysis.gainDb,
                                        record.analysis.peak,
                                        record.analysis.clipping);
            }
            self->running_ = false;
            self->progress_ = 1.0;
            emit self->runningChanged();
            emit self->progressChanged();
        });
    });
    return true;
}
