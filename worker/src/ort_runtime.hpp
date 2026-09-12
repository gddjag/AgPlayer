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
    Cuda,
    CoreMl,
};

} // namespace agplayer::separation
