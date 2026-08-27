#include <onnxruntime_c_api.h>

#include <cstring>
#include <cstdint>

namespace {

enum class Marker : std::uintptr_t {
    PathSession = 1,
    MissingStrictDml = 2,
    StrictArraySession = 3,
};

OrtStatus* marker(Marker value)
{
    return reinterpret_cast<OrtStatus*>(static_cast<std::uintptr_t>(value));
}

bool strictDml = false;

const char* ORT_API_CALL getErrorMessage(const OrtStatus* status) noexcept
{
    switch (static_cast<Marker>(reinterpret_cast<std::uintptr_t>(status))) {
    case Marker::PathSession: return "path-session-was-used";
    case Marker::MissingStrictDml: return "directml-cpu-fallback-was-not-disabled";
    case Marker::StrictArraySession: return "strict-array-session";
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

OrtStatus* ORT_API_CALL createArraySession(const OrtEnv*, const void*, size_t,
                                            const OrtSessionOptions*,
                                            OrtSession**) noexcept
{
    return strictDml ? marker(Marker::StrictArraySession)
                     : marker(Marker::MissingStrictDml);
}

void ORT_API_CALL releaseEnv(OrtEnv*) noexcept {}
void ORT_API_CALL releaseSession(OrtSession*) noexcept {}
void ORT_API_CALL releaseSessionOptions(OrtSessionOptions*) noexcept {}

OrtApi api{};

const OrtApi* ORT_API_CALL getApi(uint32_t version) noexcept
{
    if (version != ORT_API_VERSION) return nullptr;
    api.CreateEnv = createEnv;
    api.CreateSessionOptions = createSessionOptions;
    api.SetSessionExecutionMode = okExecutionMode;
    api.DisableMemPattern = okSessionOption;
    api.SetIntraOpNumThreads = okThreadCount;
    api.SetInterOpNumThreads = okThreadCount;
    api.AddSessionConfigEntry = addSessionConfig;
    api.CreateSession = createPathSession;
    api.CreateSessionFromArray = createArraySession;
    api.GetErrorMessage = getErrorMessage;
    api.ReleaseStatus = releaseStatus;
    api.ReleaseEnv = releaseEnv;
    api.ReleaseSession = releaseSession;
    api.ReleaseSessionOptions = releaseSessionOptions;
    return &api;
}

const char* ORT_API_CALL getVersion() noexcept { return "1.24.4-test"; }
const OrtApiBase apiBase{getApi, getVersion};

} // namespace

const OrtApiBase* ORT_API_CALL
OrtGetApiBase(void) noexcept
{
    return &apiBase;
}

extern "C" OrtStatus* ORT_API_CALL
OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions*, int) noexcept
{
    return nullptr;
}
