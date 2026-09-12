#pragma once

#include "cancellation_token.hpp"
#include "ort_runtime.hpp"
#include "trusted_profiles.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

struct OrtApi;
struct OrtRunOptions;

namespace agplayer::separation {

struct DxgiAdapterInfo {
    int deviceId = -1;
    QString name;
    quint64 dedicatedVideoMemory = 0;
};

struct OrtOperationResult {
    bool ok = false;
    QString code;
    QString message;
    QVector<float> output;
};

[[nodiscard]] OrtOperationResult requestOrtRunTermination(
    const OrtApi* api, OrtRunOptions* runOptions);

[[nodiscard]] QVector<DxgiAdapterInfo> enumerateDxgiHardwareAdapters();

class OrtModelSession final {
public:
    OrtModelSession();
    ~OrtModelSession();

    OrtModelSession(const OrtModelSession&) = delete;
    OrtModelSession& operator=(const OrtModelSession&) = delete;

    [[nodiscard]] OrtOperationResult open(const QString& runtimePath,
                                          const QByteArray& approvedModelBytes,
                                          ExecutionProvider provider,
                                          int directMlDeviceId,
                                          const CancellationToken& cancelled);
    [[nodiscard]] const ModelMetadata& metadata() const;
    [[nodiscard]] OrtOperationResult run(const QVector<float>& input,
                                         const QVector<qint64>& inputShape,
                                         const QVector<qint64>& outputShape,
                                         const CancellationToken& cancelled);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer::separation
