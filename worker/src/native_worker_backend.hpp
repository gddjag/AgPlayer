#pragma once

#include "ort_runtime.hpp"
#include "ort_session.hpp"
#include "trusted_profiles.hpp"
#include "worker_engine.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

struct AVIOContext;

namespace agplayer::separation {

struct NativeStartRequest {
    QString runtimePath;
    QString inputPath;
    QStringList modelFiles;
    QString modelProfile;
    QStringList modelSha256;
    QVector<qint64> modelBytes;
    QStringList modelRoles;
    QString outputDirectory;
    QString baseName;
    QString directoryName;
    QString modelName;
    QString extension;
    QStringList stems;
    QStringList stemLabels;
    DeviceMode device = DeviceMode::Auto;
};

struct StartRequestParseResult {
    bool ok = false;
    QString code;
    QString message;
    NativeStartRequest request;
};

struct ModelArtifactReadResult {
    bool ok = false;
    QString code;
    QString message;
    QString sha256;
    QByteArray bytes;
};

using ModelReadProgress = std::function<void(qint64, qint64)>;

[[nodiscard]] ModelArtifactReadResult readModelArtifact(
    const QString& path, bool captureBytes,
    const QVector<TrustedModelFile>& trustedFiles,
    const CancellationToken& cancelled,
    const ModelReadProgress& progress = {});

struct NativeProviderSelection {
    bool ok = false;
    ExecutionProvider provider = ExecutionProvider::Cpu;
    int adapterId = 0;
    QString fallbackReason;
    QString code;
    QString message;
};

class NativeProviderProbe {
public:
    virtual ~NativeProviderProbe() = default;
    [[nodiscard]] virtual QVector<DxgiAdapterInfo> hardwareAdapters() = 0;
    [[nodiscard]] virtual BackendResult prove(
        const NativeStartRequest& request,
        const TrustedModelProfile& profile,
        ExecutionProvider provider,
        int adapterId,
        const CancellationToken& cancelled) = 0;
};

[[nodiscard]] NativeProviderSelection selectNativeProvider(
    const NativeStartRequest& request,
    const TrustedModelProfile& profile,
    const CancellationToken& cancelled,
    NativeProviderProbe& probe);

[[nodiscard]] StartRequestParseResult parseStartRequest(const QJsonObject& payload);
[[nodiscard]] double nativeInferenceProgress(qint64 completed, qint64 total);

class FfmpegWaveWriter final {
public:
    using CloseFunction = std::function<int(AVIOContext**)>;

    explicit FfmpegWaveWriter(CloseFunction close = {});
    ~FfmpegWaveWriter();

    FfmpegWaveWriter(const FfmpegWaveWriter&) = delete;
    FfmpegWaveWriter& operator=(const FfmpegWaveWriter&) = delete;

    [[nodiscard]] bool open(const QString& path, int sampleRate, int channels);
    [[nodiscard]] bool write(const QVector<float>& interleavedSamples);
    [[nodiscard]] bool finish();
    [[nodiscard]] QString errorString() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class NativeWorkerBackend final : public WorkerBackend {
public:
    BackendResult probe(const QJsonObject& payload) override;
    BackendResult probeCancellable(const QJsonObject& payload, const CancellationToken& cancelled) override;
    BackendResult separate(const QJsonObject& payload,
                           const CancellationToken& cancelled,
                           const ProgressCallback& progress) override;
};

} // namespace agplayer::separation
