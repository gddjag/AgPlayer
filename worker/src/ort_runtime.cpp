#include "ort_runtime.hpp"

#include <QFileInfo>

namespace agplayer::separation {

bool DynamicOrtRuntime::load(const QString& libraryPath)
{
    unload();
    if (!QFileInfo(libraryPath).isFile()) {
        errorCode_ = QStringLiteral("runtime_missing");
        errorMessage_ = QStringLiteral("onnxruntime.dll does not exist");
        return false;
    }
    library_.setFileName(libraryPath);
    library_.setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!library_.load()) {
        errorCode_ = QStringLiteral("runtime_load_failed");
        errorMessage_ = library_.errorString();
        return false;
    }
    getApiBase_ = library_.resolve("OrtGetApiBase");
    if (getApiBase_ == nullptr) {
        errorCode_ = QStringLiteral("runtime_symbol_missing");
        errorMessage_ = QStringLiteral("onnxruntime.dll does not export OrtGetApiBase");
        library_.unload();
        return false;
    }
    errorCode_.clear();
    errorMessage_.clear();
    return true;
}

void DynamicOrtRuntime::unload()
{
    getApiBase_ = nullptr;
    if (library_.isLoaded()) library_.unload();
    library_.setFileName({});
    errorCode_.clear();
    errorMessage_.clear();
}

bool DynamicOrtRuntime::isLoaded() const
{
    return library_.isLoaded() && getApiBase_ != nullptr;
}

QString DynamicOrtRuntime::errorCode() const { return errorCode_; }
QString DynamicOrtRuntime::errorMessage() const { return errorMessage_; }
QFunctionPointer DynamicOrtRuntime::ortGetApiBase() const { return getApiBase_; }
QFunctionPointer DynamicOrtRuntime::resolve(const char* symbol)
{
    return library_.isLoaded() ? library_.resolve(symbol) : nullptr;
}

ProviderDecision chooseProvider(DeviceMode mode, InferenceProbeBackend& backend)
{
    QString error;
    if (mode == DeviceMode::Gpu) {
        if (backend.probeDirectMl(&error)) {
            return {true, ExecutionProvider::DirectMl, {}, {}, {}};
        }
        return {false, ExecutionProvider::DirectMl,
                QStringLiteral("gpu_probe_failed"), error, {}};
    }
    if (mode == DeviceMode::Cpu) {
        if (backend.probeCpu(&error)) {
            return {true, ExecutionProvider::Cpu, {}, {}, {}};
        }
        return {false, ExecutionProvider::Cpu,
                QStringLiteral("cpu_probe_failed"), error, {}};
    }

    QString gpuError;
    if (backend.probeDirectMl(&gpuError)) {
        return {true, ExecutionProvider::DirectMl, {}, {}, {}};
    }
    if (backend.probeCpu(&error)) {
        return {true, ExecutionProvider::Cpu, {}, {}, gpuError};
    }
    return {false, ExecutionProvider::Cpu,
            QStringLiteral("no_usable_provider"), error, gpuError};
}

} // namespace agplayer::separation
