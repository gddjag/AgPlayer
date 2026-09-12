#include <onnxruntime_c_api.h>

#include <cstring>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <cstddef>
#ifdef __APPLE__
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

enum class Marker : std::uintptr_t {
    PathSession = 1,
    MissingStrictDml = 2,
    StrictArraySession = 3,
    LoadCancelled = 4,
};

OrtStatus* marker(Marker value)
{
    return reinterpret_cast<OrtStatus*>(static_cast<std::uintptr_t>(value));
}

bool strictDml = false;
std::atomic_bool loadEntered{false};
std::atomic_bool loadCancelled{false};
std::atomic_int loadCancellationCalls{0};
std::mutex loadMutex;
std::condition_variable loadWake;

const char* ORT_API_CALL getErrorMessage(const OrtStatus* status) noexcept
{
    switch (static_cast<Marker>(reinterpret_cast<std::uintptr_t>(status))) {
    case Marker::PathSession: return "path-session-was-used";
    case Marker::MissingStrictDml: return "directml-cpu-fallback-was-not-disabled";
    case Marker::StrictArraySession: return "strict-array-session";
    case Marker::LoadCancelled: return "session-load-cancelled";
    }
    return "unknown-test-status";
}

void ORT_API_CALL releaseStatus(OrtStatus*) noexcept {}

OrtStatus* ORT_API_CALL createEnv(OrtLoggingLevel, const char*, OrtEnv** out) noexcept
{
    *out = reinterpret_cast<OrtEnv*>(std::uintptr_t{1});
    return nullptr;
}

OrtStatus* ORT_API_CALL createSessionOptions(OrtSessionOptions** out) noexcept
{
    strictDml = false;
    loadEntered.store(false);
    loadCancelled.store(false);
    *out = reinterpret_cast<OrtSessionOptions*>(std::uintptr_t{1});
    return nullptr;
}

OrtStatus* ORT_API_CALL okExecutionMode(OrtSessionOptions*, ExecutionMode) noexcept
{
    return nullptr;
}

OrtStatus* ORT_API_CALL okSessionOption(OrtSessionOptions*) noexcept
{
    return nullptr;
}

OrtStatus* ORT_API_CALL okThreadCount(OrtSessionOptions*, int) noexcept
{
    return nullptr;
}

OrtStatus* ORT_API_CALL addSessionConfig(OrtSessionOptions*, const char* key,
                                         const char* value) noexcept
{
    strictDml = std::strcmp(key, "session.disable_cpu_ep_fallback") == 0
        && std::strcmp(value, "1") == 0;
    return nullptr;
}

OrtStatus* ORT_API_CALL createPathSession(const OrtEnv*, const ORTCHAR_T*,
                                           const OrtSessionOptions*,
                                           OrtSession**) noexcept
{
    return marker(Marker::PathSession);
}

OrtStatus* ORT_API_CALL createArraySession(const OrtEnv*, const void* modelData,
                                            size_t modelDataLength,
                                            const OrtSessionOptions*,
                                            OrtSession**) noexcept
{
    const char* model = static_cast<const char*>(modelData);
    if (modelDataLength == std::strlen("block-load")
        && std::memcmp(model, "block-load", modelDataLength) == 0) {
        loadEntered.store(true);
        loadWake.notify_all();
        std::unique_lock lock(loadMutex);
        loadWake.wait(lock, [] { return loadCancelled.load(); });
        return marker(Marker::LoadCancelled);
    }
    return strictDml ? marker(Marker::StrictArraySession)
                     : marker(Marker::MissingStrictDml);
}

OrtStatus* ORT_API_CALL setLoadCancellation(OrtSessionOptions*, bool cancel) noexcept
{
    if (cancel) {
        ++loadCancellationCalls;
        loadCancelled.store(true);
        loadWake.notify_all();
    }
    return nullptr;
}

void ORT_API_CALL releaseEnv(OrtEnv*) noexcept {}
void ORT_API_CALL releaseSession(OrtSession*) noexcept {}
void ORT_API_CALL releaseSessionOptions(OrtSessionOptions*) noexcept {}

OrtApi api{};

const OrtApi* ORT_API_CALL getApi(uint32_t version) noexcept
{
#ifdef __APPLE__
    if (version != 18) return nullptr;
#else
    if (version != ORT_API_VERSION) return nullptr;
#endif
    api.CreateEnv = createEnv;
    api.CreateSessionOptions = createSessionOptions;
    api.SetSessionExecutionMode = okExecutionMode;
    api.DisableMemPattern = okSessionOption;
    api.SetIntraOpNumThreads = okThreadCount;
    api.SetInterOpNumThreads = okThreadCount;
    api.AddSessionConfigEntry = addSessionConfig;
    api.CreateSession = createPathSession;
    api.CreateSessionFromArray = createArraySession;
    api.SessionOptionsSetLoadCancellationFlag = setLoadCancellation;
    api.GetErrorMessage = getErrorMessage;
    api.ReleaseStatus = releaseStatus;
    api.ReleaseEnv = releaseEnv;
    api.ReleaseSession = releaseSession;
    api.ReleaseSessionOptions = releaseSessionOptions;
#ifdef __APPLE__
    // The real 1.18 table ends here. Put its end against an unreadable page:
    // any accidental access to the v24 cancellation tail fails this test.
    constexpr size_t prefixBytes = offsetof(OrtApi, AddExternalInitializersFromFilesInMemory)
        + sizeof(api.AddExternalInitializersFromFilesInMemory);
    const size_t pageBytes = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    static void* pages = ::mmap(nullptr, pageBytes * 2, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
    if (pages == MAP_FAILED || prefixBytes > pageBytes) return nullptr;
    ::mprotect(static_cast<char*>(pages) + pageBytes, pageBytes, PROT_NONE);
    void* prefix = static_cast<char*>(pages) + pageBytes - prefixBytes;
    std::memcpy(prefix, &api, prefixBytes);
    return static_cast<const OrtApi*>(prefix);
#else
    return &api;
#endif
}

const char* ORT_API_CALL getVersion() noexcept { return "1.24.4-test"; }
const OrtApiBase apiBase{getApi, getVersion};

} // namespace

const OrtApiBase* ORT_API_CALL
OrtGetApiBase(void) noexcept
{
    return &apiBase;
}

extern "C" bool AgSeparationFakeOrtLoadEntered() noexcept
{
    return loadEntered.load();
}

extern "C" int AgSeparationFakeOrtLoadCancellationCalls() noexcept
{
    return loadCancellationCalls.load();
}

extern "C" void AgSeparationFakeOrtResetLoadState() noexcept
{
    loadEntered.store(false);
    loadCancelled.store(false);
    loadCancellationCalls.store(0);
}

extern "C" OrtStatus* ORT_API_CALL
OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions*, int) noexcept
{
    return nullptr;
}
