#include "ort_session.hpp"

#include <QThread>
#include <QFileInfo>

#include <onnxruntime_c_api.h>

#ifdef Q_OS_WIN
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace agplayer::separation {
namespace {

QString statusMessage(const OrtApi* api, OrtStatus* status)
{
    if (status == nullptr) return {};
    const QString message = QString::fromUtf8(api->GetErrorMessage(status));
    api->ReleaseStatus(status);
    return message;
}

qsizetype elementCount(const QVector<qint64>& shape)
{
    qsizetype count = 1;
    for (const qint64 value : shape) {
        if (value <= 0 || value > 100'000'000
            || count > (std::numeric_limits<qsizetype>::max)() / value) {
            return -1;
        }
        count *= static_cast<qsizetype>(value);
    }
    return count;
}

TensorElementType tensorType(ONNXTensorElementDataType type)
{
    return type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        ? TensorElementType::Float16 : TensorElementType::Float32;
}

} // namespace

OrtOperationResult requestOrtRunTermination(const OrtApi* api,
                                             OrtRunOptions* runOptions)
{
    if (api == nullptr || runOptions == nullptr
        || api->RunOptionsSetTerminate == nullptr) {
        return {false, QStringLiteral("inference_cancel_failed"),
                QStringLiteral("ONNX Runtime cannot terminate this inference"), {}};
    }
    const QString error = statusMessage(api, api->RunOptionsSetTerminate(runOptions));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("inference_cancel_failed"), error, {}};
    }
    return {true, {}, {}, {}};
}

class OrtModelSession::Impl final {
public:
    ~Impl()
    {
        if (api != nullptr) {
            if (session != nullptr) api->ReleaseSession(session);
            if (environment != nullptr) api->ReleaseEnv(environment);
        }
    }

    OrtOperationResult inspect();
    TensorContract inspectTensor(bool input, size_t index,
                                 OrtOperationResult& result);

    DynamicOrtRuntime runtime;
    const OrtApi* api = nullptr;
    OrtEnv* environment = nullptr;
    OrtSession* session = nullptr;
    ModelMetadata metadata;
};

QVector<DxgiAdapterInfo> enumerateDxgiHardwareAdapters()
{
    QVector<DxgiAdapterInfo> result;
#ifdef Q_OS_WIN
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))
            || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        result.push_back({static_cast<int>(index),
                          QString::fromWCharArray(description.Description),
                          static_cast<quint64>(description.DedicatedVideoMemory)});
    }
#endif
    return result;
}

OrtModelSession::OrtModelSession() : impl_(std::make_unique<Impl>()) {}
OrtModelSession::~OrtModelSession() = default;

OrtOperationResult OrtModelSession::open(const QString& runtimePath,
                                         const QByteArray& approvedModelBytes,
                                         ExecutionProvider provider,
                                         int directMlDeviceId,
                                         const CancellationToken& cancelled)
{
    impl_ = std::make_unique<Impl>();
    if (cancelled.isCancelled()) {
        return {false, QStringLiteral("cancelled"),
                QStringLiteral("Separation cancelled"), {}};
    }
    if (!impl_->runtime.load(runtimePath)) {
        return {false, impl_->runtime.errorCode(), impl_->runtime.errorMessage(), {}};
    }
    using GetApiBaseFunction = const OrtApiBase*(ORT_API_CALL*)();
    const auto getApiBase = reinterpret_cast<GetApiBaseFunction>(
        impl_->runtime.ortGetApiBase());
    const OrtApiBase* apiBase = getApiBase == nullptr ? nullptr : getApiBase();
#ifdef Q_OS_MACOS
    constexpr uint32_t apiVersion = 18;
#else
    constexpr uint32_t apiVersion = ORT_API_VERSION;
#endif
    impl_->api = apiBase == nullptr ? nullptr : apiBase->GetApi(apiVersion);
    if (impl_->api == nullptr) {
        return {false, QStringLiteral("runtime_api_mismatch"),
                QStringLiteral("ONNX Runtime does not provide C API version %1").arg(apiVersion), {}};
    }
    QString error = statusMessage(
        impl_->api, impl_->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                                          "AgSeparationWorker",
                                          &impl_->environment));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("runtime_environment_failed"), error, {}};
    }
    OrtSessionOptions* options = nullptr;
    error = statusMessage(impl_->api, impl_->api->CreateSessionOptions(&options));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("session_options_failed"), error, {}};
    }
    const auto releaseOptions = qScopeGuard(
        [this, &options] { impl_->api->ReleaseSessionOptions(options); });
    for (OrtStatus* status : {
             impl_->api->SetSessionExecutionMode(options, ORT_SEQUENTIAL),
             impl_->api->DisableMemPattern(options),
             impl_->api->SetIntraOpNumThreads(options,
                 provider == ExecutionProvider::Cpu
                     ? std::clamp(QThread::idealThreadCount() / 2, 1, 4) : 1),
             impl_->api->SetInterOpNumThreads(options, 1)}) {
        error = statusMessage(impl_->api, status);
        if (!error.isEmpty()) {
            return {false, QStringLiteral("session_options_failed"), error, {}};
        }
    }
    if (provider == ExecutionProvider::DirectMl) {
        using AppendDmlFunction = OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*, int);
        const auto appendDml = reinterpret_cast<AppendDmlFunction>(
            impl_->runtime.resolve("OrtSessionOptionsAppendExecutionProvider_DML"));
        if (appendDml == nullptr) {
            return {false, QStringLiteral("directml_export_missing"),
                    QStringLiteral("The runtime does not export the DirectML provider factory"), {}};
        }
        error = statusMessage(impl_->api, appendDml(options, directMlDeviceId));
        if (!error.isEmpty()) {
            return {false, QStringLiteral("directml_session_failed"), error, {}};
        }
        error = statusMessage(impl_->api, impl_->api->AddSessionConfigEntry(
            options, "session.disable_cpu_ep_fallback", "1"));
        if (!error.isEmpty()) {
            return {false, QStringLiteral("directml_session_failed"), error, {}};
        }
    }
    if (provider == ExecutionProvider::Cuda) {
        OrtCUDAProviderOptionsV2* cuda = nullptr;
        error = statusMessage(impl_->api, impl_->api->CreateCUDAProviderOptions(&cuda));
        if (!error.isEmpty()) return {false, QStringLiteral("cuda_options_failed"), error, {}};
        const auto releaseCuda = qScopeGuard([this, cuda] { impl_->api->ReleaseCUDAProviderOptions(cuda); });
        const QByteArray device = QByteArray::number(directMlDeviceId);
        // Bound arena and convolution workspaces; never let exhaustive cuDNN
        // algorithm search consume all VRAM on the first fixed-size chunk.
        const char* keys[] = {"device_id", "gpu_mem_limit", "arena_extend_strategy",
                              "cudnn_conv_algo_search", "cudnn_conv_use_max_workspace"};
        const char* values[] = {device.constData(), "6442450944", "kSameAsRequested", "HEURISTIC", "0"};
        error = statusMessage(impl_->api, impl_->api->UpdateCUDAProviderOptions(cuda, keys, values, 5));
        if (error.isEmpty()) error = statusMessage(impl_->api,
            impl_->api->SessionOptionsAppendExecutionProvider_CUDA_V2(options, cuda));
        if (error.isEmpty()) error = statusMessage(impl_->api, impl_->api->AddSessionConfigEntry(
            options, "session.disable_cpu_ep_fallback", "1"));
        if (!error.isEmpty()) return {false, QStringLiteral("cuda_session_failed"), error, {}};
    }
    if (provider == ExecutionProvider::CoreMl) {
        using AppendCoreMlFunction = OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*, uint32_t);
        const auto appendCoreMl = reinterpret_cast<AppendCoreMlFunction>(
            impl_->runtime.resolve("OrtSessionOptionsAppendExecutionProvider_CoreML"));
        if (appendCoreMl == nullptr)
            return {false, QStringLiteral("coreml_export_missing"),
                    QStringLiteral("当前运行时未提供 CoreML；可继续使用 CPU"), {}};
        // ORT 1.18 flags: fixed shapes (0x08), CoreML MLProgram (0x10).
        // A graph that requires CPU EP fallback must not be advertised as GPU.
        error = statusMessage(impl_->api, appendCoreMl(options, 0x08 | 0x10));
        if (error.isEmpty()) error = statusMessage(impl_->api, impl_->api->AddSessionConfigEntry(
            options, "session.disable_cpu_ep_fallback", "1"));
        if (!error.isEmpty()) return {false, QStringLiteral("coreml_session_failed"), error, {}};
    }
    std::mutex cancellationMutex;
    QString cancellationError;
    CancellationToken::Subscription cancellationSubscription;
#ifndef Q_OS_MACOS
    if (impl_->api->SessionOptionsSetLoadCancellationFlag == nullptr) {
        return {false, QStringLiteral("runtime_api_mismatch"),
                QStringLiteral("ONNX Runtime does not support cancellable session loading"), {}};
    }
    error = statusMessage(impl_->api,
                          impl_->api->SessionOptionsSetLoadCancellationFlag(
                              options, false));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("session_options_failed"), error, {}};
    }

    const OrtApi* api = impl_->api;
    cancellationSubscription = cancelled.notifyOnCancel(
        [api, options, &cancellationMutex, &cancellationError] {
            const QString flagError = statusMessage(
                api, api->SessionOptionsSetLoadCancellationFlag(options, true));
            if (!flagError.isEmpty()) {
                const std::lock_guard lock(cancellationMutex);
                cancellationError = flagError;
            }
        });
#endif
    // API 18 has no session-load cancellation member. Never access the v24
    // struct tail on macOS; the isolated worker's process deadline cancels it.
    OrtStatus* loadStatus = impl_->api->CreateSessionFromArray(
        impl_->environment, approvedModelBytes.constData(),
        static_cast<size_t>(approvedModelBytes.size()), options, &impl_->session);
    cancellationSubscription = {};
    {
        const std::lock_guard lock(cancellationMutex);
        if (!cancellationError.isEmpty()) {
            (void)statusMessage(impl_->api, loadStatus);
            return {false, QStringLiteral("session_load_cancel_failed"),
                    cancellationError, {}};
        }
    }
    error = statusMessage(impl_->api, loadStatus);
    if (cancelled.isCancelled()) {
        return {false, QStringLiteral("cancelled"),
                QStringLiteral("Separation cancelled"), {}};
    }
    if (!error.isEmpty()) {
        return {false, QStringLiteral("model_open_failed"), error, {}};
    }
    OrtOperationResult inspected = impl_->inspect();
    if (!inspected.ok) return inspected;
    impl_->metadata.opset = readOnnxDefaultOpset(approvedModelBytes);
    if (impl_->metadata.opset < 0) {
        return {false, QStringLiteral("model_metadata_failed"),
                QStringLiteral("Could not read the default ONNX opset import"), {}};
    }
    return inspected;
}

TensorContract OrtModelSession::Impl::inspectTensor(
    bool input, size_t index, OrtOperationResult& result)
{
    OrtAllocator* allocator = nullptr;
    QString error = statusMessage(api, api->GetAllocatorWithDefaultOptions(&allocator));
    if (!error.isEmpty()) {
        result = {false, QStringLiteral("model_metadata_failed"), error, {}};
        return {};
    }
    char* rawName = nullptr;
    OrtTypeInfo* typeInfo = nullptr;
    OrtStatus* nameStatus = input
        ? api->SessionGetInputName(session, index, allocator, &rawName)
        : api->SessionGetOutputName(session, index, allocator, &rawName);
    error = statusMessage(api, nameStatus);
    if (error.isEmpty()) {
        error = statusMessage(api, input
            ? api->SessionGetInputTypeInfo(session, index, &typeInfo)
            : api->SessionGetOutputTypeInfo(session, index, &typeInfo));
    }
    if (!error.isEmpty() || rawName == nullptr || typeInfo == nullptr) {
        if (rawName != nullptr) allocator->Free(allocator, rawName);
        if (typeInfo != nullptr) api->ReleaseTypeInfo(typeInfo);
        result = {false, QStringLiteral("model_metadata_failed"), error, {}};
        return {};
    }
    const QString name = QString::fromUtf8(rawName);
    allocator->Free(allocator, rawName);
    const OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
    error = statusMessage(api, api->CastTypeInfoToTensorInfo(typeInfo, &tensorInfo));
    if (!error.isEmpty() || tensorInfo == nullptr) {
        api->ReleaseTypeInfo(typeInfo);
        result = {false, QStringLiteral("model_metadata_failed"),
                  QStringLiteral("Model input/output is not a tensor"), {}};
        return {};
    }
    ONNXTensorElementDataType elementType{};
    size_t dimensionCount = 0;
    error = statusMessage(api, api->GetTensorElementType(tensorInfo, &elementType));
    if (error.isEmpty()) {
        error = statusMessage(api, api->GetDimensionsCount(tensorInfo, &dimensionCount));
    }
    QVector<qint64> dimensions(static_cast<qsizetype>(dimensionCount));
    if (error.isEmpty() && dimensionCount > 0) {
        error = statusMessage(api, api->GetDimensions(
            tensorInfo, dimensions.data(), dimensionCount));
    }
    api->ReleaseTypeInfo(typeInfo);
    if (!error.isEmpty()) {
        result = {false, QStringLiteral("model_metadata_failed"), error, {}};
        return {};
    }
    if (elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
        && elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        result = {false, QStringLiteral("model_metadata_failed"),
                  QStringLiteral("Only FLOAT/FLOAT16 model tensors are supported"), {}};
        return {};
    }
    return {name, tensorType(elementType), dimensions};
}

OrtOperationResult OrtModelSession::Impl::inspect()
{
    size_t inputCount = 0;
    size_t outputCount = 0;
    QString error = statusMessage(api, api->SessionGetInputCount(session, &inputCount));
    if (error.isEmpty()) {
        error = statusMessage(api, api->SessionGetOutputCount(session, &outputCount));
    }
    if (!error.isEmpty()) {
        return {false, QStringLiteral("model_metadata_failed"), error, {}};
    }
    OrtOperationResult result{true, {}, {}, {}};
    for (size_t index = 0; index < inputCount && result.ok; ++index) {
        metadata.inputs.push_back(inspectTensor(true, index, result));
    }
    for (size_t index = 0; index < outputCount && result.ok; ++index) {
        metadata.outputs.push_back(inspectTensor(false, index, result));
    }
    return result;
}

const ModelMetadata& OrtModelSession::metadata() const
{
    return impl_->metadata;
}

OrtOperationResult OrtModelSession::run(const QVector<float>& input,
                                        const QVector<qint64>& inputShape,
                                        const QVector<qint64>& outputShape,
                                        const CancellationToken& cancelled)
{
    if (impl_->session == nullptr || impl_->api == nullptr) {
        return {false, QStringLiteral("session_not_open"),
                QStringLiteral("ONNX session is not open"), {}};
    }
    if (cancelled.isCancelled()) {
        return {false, QStringLiteral("cancelled"),
                QStringLiteral("Separation cancelled"), {}};
    }
    const qsizetype expectedInput = elementCount(inputShape);
    const qsizetype expectedOutput = elementCount(outputShape);
    if (expectedInput != input.size() || expectedOutput < 0) {
        return {false, QStringLiteral("tensor_shape_mismatch"),
                QStringLiteral("Inference tensor size does not match its shape"), {}};
    }
    OrtMemoryInfo* memoryInfo = nullptr;
    QString error = statusMessage(impl_->api, impl_->api->CreateCpuMemoryInfo(
        OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("inference_setup_failed"), error, {}};
    }
    const auto releaseMemory = qScopeGuard(
        [this, &memoryInfo] { impl_->api->ReleaseMemoryInfo(memoryInfo); });
    OrtValue* inputValue = nullptr;
    error = statusMessage(impl_->api, impl_->api->CreateTensorWithDataAsOrtValue(
        memoryInfo, const_cast<float*>(input.constData()),
        static_cast<size_t>(input.size()) * sizeof(float),
        inputShape.constData(), static_cast<size_t>(inputShape.size()),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputValue));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("inference_setup_failed"), error, {}};
    }
    const auto releaseInput = qScopeGuard(
        [this, &inputValue] { impl_->api->ReleaseValue(inputValue); });
    OrtRunOptions* runOptions = nullptr;
    error = statusMessage(impl_->api, impl_->api->CreateRunOptions(&runOptions));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("inference_setup_failed"), error, {}};
    }
    const auto releaseRunOptions = qScopeGuard(
        [this, &runOptions] { impl_->api->ReleaseRunOptions(runOptions); });

    std::mutex monitorMutex;
    std::condition_variable monitorWake;
    bool finished = false;
    OrtOperationResult terminationResult{true, {}, {}, {}};
    auto cancellationSubscription = cancelled.notifyOnCancel(
        [&monitorWake] { monitorWake.notify_all(); });
    std::thread monitor([&] {
        std::unique_lock lock(monitorMutex);
        monitorWake.wait(lock, [&] { return finished || cancelled.isCancelled(); });
        if (!finished && cancelled.isCancelled()) {
            terminationResult = requestOrtRunTermination(impl_->api, runOptions);
        }
    });
    OrtValue* outputValue = nullptr;
    const QByteArray inputName = impl_->metadata.inputs.front().name.toUtf8();
    const QByteArray outputName = impl_->metadata.outputs.front().name.toUtf8();
    const char* inputNames[]{inputName.constData()};
    const char* outputNames[]{outputName.constData()};
    OrtStatus* runStatus = impl_->api->Run(impl_->session, runOptions,
                                           inputNames, &inputValue, 1,
                                           outputNames, 1, &outputValue);
    {
        std::lock_guard lock(monitorMutex);
        finished = true;
    }
    monitorWake.notify_all();
    monitor.join();
    error = statusMessage(impl_->api, runStatus);
    if (!terminationResult.ok) {
        if (outputValue != nullptr) impl_->api->ReleaseValue(outputValue);
        return terminationResult;
    }
    if (!error.isEmpty()) {
        if (outputValue != nullptr) impl_->api->ReleaseValue(outputValue);
        return {false, cancelled.isCancelled() ? QStringLiteral("cancelled")
                                        : QStringLiteral("inference_failed"),
                cancelled.isCancelled() ? QStringLiteral("Separation cancelled") : error, {}};
    }
    const auto releaseOutput = qScopeGuard(
        [this, &outputValue] { impl_->api->ReleaseValue(outputValue); });
    OrtTensorTypeAndShapeInfo* outputInfo = nullptr;
    error = statusMessage(impl_->api,
                          impl_->api->GetTensorTypeAndShape(outputValue, &outputInfo));
    if (!error.isEmpty()) {
        return {false, QStringLiteral("inference_output_invalid"), error, {}};
    }
    const auto releaseOutputInfo = qScopeGuard(
        [this, &outputInfo] { impl_->api->ReleaseTensorTypeAndShapeInfo(outputInfo); });
    size_t dimensionCount = 0;
    error = statusMessage(impl_->api,
                          impl_->api->GetDimensionsCount(outputInfo, &dimensionCount));
    QVector<qint64> actualShape(static_cast<qsizetype>(dimensionCount));
    if (error.isEmpty()) {
        error = statusMessage(impl_->api, impl_->api->GetDimensions(
            outputInfo, actualShape.data(), dimensionCount));
    }
    if (!error.isEmpty() || actualShape != outputShape) {
        return {false, QStringLiteral("inference_output_invalid"),
                error.isEmpty() ? QStringLiteral("Inference output shape is invalid") : error, {}};
    }
    float* outputData = nullptr;
    error = statusMessage(impl_->api,
                          impl_->api->GetTensorMutableData(outputValue,
                                                           reinterpret_cast<void**>(&outputData)));
    if (!error.isEmpty() || outputData == nullptr) {
        return {false, QStringLiteral("inference_output_invalid"), error, {}};
    }
    QVector<float> output(expectedOutput);
    std::memcpy(output.data(), outputData,
                static_cast<size_t>(expectedOutput) * sizeof(float));
    return {true, {}, {}, std::move(output)};
}

} // namespace agplayer::separation
