#pragma once

#include "ort_runtime.hpp"
#include "trusted_profiles.hpp"
#include "worker_engine.hpp"

#include <QFile>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace agplayer::separation {

struct NativeStartRequest {
    QString runtimePath;
    QString inputPath;
    QStringList modelFiles;
    QString outputDirectory;
    QString baseName;
    QString extension;
    QStringList stems;
    DeviceMode device = DeviceMode::Auto;
};

struct StartRequestParseResult {
    bool ok = false;
    QString code;
    QString message;
    NativeStartRequest request;
};

[[nodiscard]] StartRequestParseResult parseStartRequest(const QJsonObject& payload);
[[nodiscard]] QString hashFileSha256(const QString& path);

class FloatWaveWriter final {
public:
    FloatWaveWriter() = default;
    ~FloatWaveWriter();

    FloatWaveWriter(const FloatWaveWriter&) = delete;
    FloatWaveWriter& operator=(const FloatWaveWriter&) = delete;

    [[nodiscard]] bool open(const QString& path, int sampleRate, int channels);
    [[nodiscard]] bool write(const QVector<float>& interleavedSamples);
    [[nodiscard]] bool finish();
    [[nodiscard]] QString errorString() const;

private:
    QFile file_;
    quint64 dataBytes_ = 0;
    int sampleRate_ = 0;
    int channels_ = 0;
    QString error_;
};

class NativeWorkerBackend final : public WorkerBackend {
public:
    BackendResult probe(const QJsonObject& payload) override;
    BackendResult separate(const QJsonObject& payload,
                           const std::atomic_bool& cancelled,
                           const ProgressCallback& progress) override;
};

} // namespace agplayer::separation
