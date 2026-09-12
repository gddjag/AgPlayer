#include "ort_runtime.hpp"

#include <QFileInfo>
#include <QDir>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace agplayer::separation {

bool DynamicOrtRuntime::load(const QString& libraryPath)
{
    unload();
    if (!QFileInfo(libraryPath).isFile()) {
        errorCode_ = QStringLiteral("runtime_missing");
        errorMessage_ = QStringLiteral("ONNX Runtime library does not exist");
        return false;
    }
    library_.setFileName(libraryPath);
#ifdef Q_OS_WIN
    // CUDA dependencies are confined to the selected optional runtime directory.
    // This affects the isolated worker only, never the system PATH or drivers.
    if (QFileInfo(QDir(QFileInfo(libraryPath).absolutePath())
                     .filePath("onnxruntime_providers_cuda.dll")).isFile()) {
        const QString directory = QDir::toNativeSeparators(QFileInfo(libraryPath).absolutePath());
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        SetDllDirectoryW(reinterpret_cast<LPCWSTR>(directory.utf16()));
    }
#endif
    library_.setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!library_.load()) {
        errorCode_ = QStringLiteral("runtime_load_failed");
        errorMessage_ = library_.errorString();
        return false;
    }
    getApiBase_ = library_.resolve("OrtGetApiBase");
    if (getApiBase_ == nullptr) {
        errorCode_ = QStringLiteral("runtime_symbol_missing");
        errorMessage_ = QStringLiteral("ONNX Runtime library does not export OrtGetApiBase");
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

} // namespace agplayer::separation
