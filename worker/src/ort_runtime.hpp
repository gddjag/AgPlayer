#pragma once

#include <QLibrary>
#include <QString>

namespace agplayer::separation {

class DynamicOrtRuntime final {
public:
    DynamicOrtRuntime() = default;
    ~DynamicOrtRuntime() = default;

    DynamicOrtRuntime(const DynamicOrtRuntime&) = delete;
    DynamicOrtRuntime& operator=(const DynamicOrtRuntime&) = delete;

    [[nodiscard]] bool load(const QString& libraryPath);
    void unload();
    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] QString errorCode() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QFunctionPointer ortGetApiBase() const;
    [[nodiscard]] QFunctionPointer resolve(const char* symbol);

private:
    QLibrary library_;
    QFunctionPointer getApiBase_ = nullptr;
    QString errorCode_;
    QString errorMessage_;
};

enum class DeviceMode {
    Auto,
    Cpu,
    Gpu,
};

enum class ExecutionProvider {
    Cpu,
    DirectMl,
};

class InferenceProbeBackend {
public:
    virtual ~InferenceProbeBackend() = default;
    virtual bool probeCpu(QString* error) = 0;
    // Implementations must enumerate a hardware DXGI adapter and complete a
    // real minimal inference. Provider presence/session creation is not enough.
    virtual bool probeDirectMl(QString* error) = 0;
};

struct ProviderDecision {
    bool ok = false;
    ExecutionProvider provider = ExecutionProvider::Cpu;
    QString code;
    QString message;
    QString fallbackReason;
};

[[nodiscard]] ProviderDecision chooseProvider(DeviceMode mode,
                                              InferenceProbeBackend& backend);

} // namespace agplayer::separation
