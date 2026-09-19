#include "editor_recording_service.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QtEndian>
#ifdef Q_OS_MACOS
#include <QPermissions>
#define MA_NO_RUNTIME_LINKING
#endif

// Reuse the implementation in core/audio_engine.cpp; do not instantiate a
// second miniaudio implementation or its decoder/engine facilities here.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace agplayer::editor {
namespace {

QString nativeError(const char* action, const ma_result result)
{
    return QString::fromUtf8(action) + QStringLiteral(": ")
        + QString::fromUtf8(ma_result_description(result));
}

class MiniaudioCapture final : public IEditorCaptureBackend {
public:
    ~MiniaudioCapture() override
    {
        stop();
        if (deviceReady_) ma_device_uninit(&device_);
        if (contextReady_) ma_context_uninit(&context_);
    }

    std::vector<EditorInputDevice> devices(QString& error) override
    {
        if (!enumerate(error)) return {};
        std::vector<EditorInputDevice> result;
        result.reserve(inputs_.size());
        for (const auto& input : inputs_) result.push_back({input.id, input.name, input.isDefault});
        return result;
    }

    bool open(const QString& deviceId, const int sampleRate, const int channels,
              const Capture capture, void* const user, QString& actual,
              QString& error) override
    {
        if (!enumerate(error)) return false;
        const ma_device_id* selected = nullptr;
        if (!deviceId.isEmpty()) {
            const auto found = std::find_if(inputs_.cbegin(), inputs_.cend(),
                [&deviceId](const NativeInput& input) { return input.id == deviceId; });
            if (found == inputs_.cend()) {
                error = QStringLiteral("所选输入设备已不可用，请重新选择输入设备。");
                return false;
            }
            selected = &found->native;
        }
        capture_ = capture;
        user_ = user;
        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.pDeviceID = selected;
        config.capture.format = ma_format_f32;
        config.capture.channels = static_cast<ma_uint32>(channels);
        config.sampleRate = static_cast<ma_uint32>(sampleRate);
        config.dataCallback = &captureCallback;
        config.notificationCallback = &notificationCallback;
        config.pUserData = this;
        const ma_result result = ma_device_init(&context_, &config, &device_);
        if (result != MA_SUCCESS) {
            error = nativeError("无法打开麦克风，请检查系统麦克风隐私权限和设备占用", result);
            return false;
        }
        deviceReady_ = true;
        actual = QString::fromUtf8(device_.capture.name);
        if (actual.isEmpty()) actual = QStringLiteral("系统输入设备（名称不可用）");
        return true;
    }

    bool start(QString& error) override
    {
        if (!deviceReady_) { error = QStringLiteral("输入设备尚未打开。"); return false; }
        armed_.store(true, std::memory_order_release);
        const ma_result result = ma_device_start(&device_);
        if (result == MA_SUCCESS) return true;
        armed_.store(false, std::memory_order_release);
        error = nativeError("无法开始录音，请检查麦克风权限和设备", result);
        return false;
    }

    void stop() noexcept override
    {
        armed_.store(false, std::memory_order_release);
        if (deviceReady_) (void)ma_device_stop(&device_);
    }

    QString failure() const override
    {
        return interrupted_.load(std::memory_order_acquire)
            ? QStringLiteral("输入设备已停止、断开或切换；已保留此前录音。") : QString{};
    }

private:
    struct NativeInput { QString id; QString name; ma_device_id native; bool isDefault; };

    QString deviceId(const ma_device_id& id) const
    {
        // Encode only the active backend's identifier, never union padding.
        switch (context_.backend) {
        case ma_backend_wasapi:
            return QStringLiteral("wasapi:") + QString::fromWCharArray(
                reinterpret_cast<const wchar_t*>(id.wasapi));
        case ma_backend_dsound:
            return QStringLiteral("dsound:") + QString::fromLatin1(
                QByteArray(reinterpret_cast<const char*>(id.dsound), 16).toHex());
        case ma_backend_winmm: return QStringLiteral("winmm:") + QString::number(id.winmm);
        case ma_backend_coreaudio: return QStringLiteral("coreaudio:") + QString::fromUtf8(id.coreaudio);
        case ma_backend_alsa: return QStringLiteral("alsa:") + QString::fromUtf8(id.alsa);
        case ma_backend_pulseaudio: return QStringLiteral("pulse:") + QString::fromUtf8(id.pulse);
        case ma_backend_jack: return QStringLiteral("jack:") + QString::number(id.jack);
        default: return {};
        }
    }

    bool enumerate(QString& error)
    {
        if (!contextReady_) {
            const ma_result result = ma_context_init(nullptr, 0, nullptr, &context_);
            if (result != MA_SUCCESS) { error = nativeError("无法初始化输入设备", result); return false; }
            contextReady_ = true;
        }
        if (context_.backend == ma_backend_null) {
            error = QStringLiteral("未找到可用的真实音频输入设备。");
            return false;
        }
        ma_device_info* inputs = nullptr;
        ma_uint32 count = 0;
        const ma_result result = ma_context_get_devices(&context_, nullptr, nullptr, &inputs, &count);
        if (result != MA_SUCCESS) { error = nativeError("无法读取输入设备列表", result); return false; }
        inputs_.clear();
        inputs_.reserve(count);
        for (ma_uint32 index = 0; index < count; ++index) {
            const QString id = deviceId(inputs[index].id);
            if (!id.isEmpty()) inputs_.push_back({id, QString::fromUtf8(inputs[index].name), inputs[index].id, inputs[index].isDefault != 0});
        }
        if (inputs_.empty()) { error = QStringLiteral("未找到可用的音频输入设备。"); return false; }
        return true;
    }

    static void captureCallback(ma_device* const device, void*, const void* input,
                                const ma_uint32 frames) noexcept
    {
        auto* const self = static_cast<MiniaudioCapture*>(device->pUserData);
        if (self && self->capture_) self->capture_(self->user_, static_cast<const float*>(input), frames);
    }

    static void notificationCallback(const ma_device_notification* notification) noexcept
    {
        if (!notification || !notification->pDevice) return;
        auto* const self = static_cast<MiniaudioCapture*>(notification->pDevice->pUserData);
        if (!self || !self->armed_.load(std::memory_order_acquire)) return;
        if (notification->type == ma_device_notification_type_stopped
            || notification->type == ma_device_notification_type_interruption_began
            || notification->type == ma_device_notification_type_rerouted) {
            self->interrupted_.store(true, std::memory_order_release);
        }
    }

    ma_context context_{};
    ma_device device_{};
    bool contextReady_{};
    bool deviceReady_{};
    Capture capture_{};
    void* user_{};
    std::vector<NativeInput> inputs_;
    std::atomic_bool armed_{false};
    std::atomic_bool interrupted_{false};
};

struct CaptureSession final {
    explicit CaptureSession(const int rate, const int channelCount)
        : ring(static_cast<std::size_t>(rate) * static_cast<std::size_t>(channelCount) * 2U),
          channels(static_cast<std::size_t>(channelCount)) {}

    static void capture(void* const user, const float* const samples, const std::size_t frames) noexcept
    {
        auto& session = *static_cast<CaptureSession*>(user);
        if (session.paused.load(std::memory_order_acquire)
            || session.stop.load(std::memory_order_acquire)
            || session.overflow.load(std::memory_order_relaxed)) return;
        if (!samples && frames != 0) { session.inputLost.store(true, std::memory_order_release); return; }
        const auto write = session.write.load(std::memory_order_relaxed);
        const auto read = session.read.load(std::memory_order_acquire);
        if (frames > session.ring.size() / session.channels
            || frames * session.channels > session.ring.size() - (write - read)) {
            session.overflow.store(true, std::memory_order_release);
            return;
        }
        const std::size_t count = frames * session.channels;
        const auto offset = static_cast<std::size_t>(write % session.ring.size());
        const auto first = std::min(count, session.ring.size() - offset);
        if (count != 0) {
            std::memcpy(session.ring.data() + offset, samples, first * sizeof(float));
            std::memcpy(session.ring.data(), samples + first, (count - first) * sizeof(float));
        }
        session.write.store(write + count, std::memory_order_release);
    }

    std::vector<float> ring;
    const std::size_t channels;
    std::atomic<double> gain{1.0};
    std::atomic<double> level{0.0};
    std::atomic<std::uint64_t> read{0}, write{0};
    std::atomic<qint64> writtenFrames{0};
    std::atomic_bool paused{false}, stop{false}, cancelled{false};
    std::atomic_bool overflow{false}, inputLost{false};
    // Set/read by the worker, then by GUI only after worker.join().
    bool ownsFile{};
    QString path;
    // Only the writer and GUI touch this bounded overview, never the capture callback.
    mutable std::mutex peakMutex;
    std::vector<std::pair<float, float>> peaks;
    qint64 peakFrames{256};
    qint64 pendingFrames{};
    float pendingMin{1}, pendingMax{-1};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Capture indices require lock-free 64-bit atomics on PC targets");

QByteArray waveHeader(const int rate, const int channels, const quint32 dataBytes)
{
    QByteArray header(44, '\0');
    std::memcpy(header.data(), "RIFF", 4);
    qToLittleEndian<quint32>(36U + dataBytes + (dataBytes & 1U), header.data() + 4);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, header.data() + 16);
    qToLittleEndian<quint16>(1, header.data() + 20);
    qToLittleEndian<quint16>(static_cast<quint16>(channels), header.data() + 22);
    qToLittleEndian<quint32>(static_cast<quint32>(rate), header.data() + 24);
    qToLittleEndian<quint32>(static_cast<quint32>(rate * channels * 3), header.data() + 28);
    qToLittleEndian<quint16>(static_cast<quint16>(channels * 3), header.data() + 32);
    qToLittleEndian<quint16>(24, header.data() + 34);
    std::memcpy(header.data() + 36, "data", 4);
    qToLittleEndian<quint32>(dataBytes, header.data() + 40);
    return header;
}

bool validateWave(const QString& path, const int rate, const int channels, const qint64 frames)
{
    QFile file(path);
    if (frames <= 0 || !file.open(QIODevice::ReadOnly)) return false;
    const auto bytes = static_cast<quint32>(frames * channels * 3);
    return file.size() == 44LL + bytes + (bytes & 1U)
        && file.read(44) == waveHeader(rate, channels, bytes);
}

bool writeAvailable(QFile& file, CaptureSession& session, QString& error)
{
    constexpr std::uint64_t blockFrames = 4096;
    const auto read = session.read.load(std::memory_order_relaxed);
    const auto available = session.write.load(std::memory_order_acquire) - read;
    const auto frames = std::min(blockFrames, available / session.channels);
    if (frames == 0) return true;
    const qint64 previousFrames = session.writtenFrames.load(std::memory_order_relaxed);
    constexpr std::uint64_t maxDataBytes = std::numeric_limits<quint32>::max() - 37ULL;
    if (static_cast<std::uint64_t>(previousFrames) + frames > maxDataBytes / (session.channels * 3U)) {
        error = QStringLiteral("录音达到 WAV 文件容量上限，已保留此前录音。");
        return false;
    }
    std::array<char, blockFrames * 2U * 3U> pcm{};
    const auto count = static_cast<std::size_t>(frames * session.channels);
    const double gain = session.gain.load(std::memory_order_relaxed);
    double blockPeak = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const float raw = session.ring[static_cast<std::size_t>((read + index) % session.ring.size())];
        const double bounded = std::isfinite(raw) ? std::clamp(static_cast<double>(raw) * gain, -1.0, 1.0) : 0.0;
        blockPeak = std::max(blockPeak, std::abs(bounded));
        const auto value = static_cast<std::int32_t>(std::clamp(std::llround(bounded * 8'388'608.0),
                                                              -8'388'608LL, 8'388'607LL));
        const auto bits = static_cast<std::uint32_t>(value);
        pcm[index * 3] = static_cast<char>(bits & 0xffU);
        pcm[index * 3 + 1] = static_cast<char>((bits >> 8U) & 0xffU);
        pcm[index * 3 + 2] = static_cast<char>((bits >> 16U) & 0xffU);
    }
    const qint64 bytes = static_cast<qint64>(count * 3U);
    double previousPeak = session.level.load(std::memory_order_relaxed);
    while (previousPeak < blockPeak
           && !session.level.compare_exchange_weak(previousPeak, blockPeak, std::memory_order_relaxed)) {}
    const qint64 written = file.write(pcm.data(), bytes);
    const qint64 completeFrames = std::max<qint64>(0, written) / static_cast<qint64>(session.channels * 3U);
    {
        std::lock_guard<std::mutex> lock(session.peakMutex);
        for (qint64 frame = 0; frame < completeFrames; ++frame) {
            for (std::size_t channel = 0; channel < session.channels; ++channel) {
                const auto raw = session.ring[static_cast<std::size_t>((read + frame * session.channels + channel) % session.ring.size())];
                const float value = std::isfinite(raw) ? static_cast<float>(std::clamp(static_cast<double>(raw) * gain, -1.0, 1.0)) : 0.0F;
                session.pendingMin = std::min(session.pendingMin, value);
                session.pendingMax = std::max(session.pendingMax, value);
            }
            if (++session.pendingFrames == session.peakFrames) {
                session.peaks.emplace_back(session.pendingMin, session.pendingMax);
                session.pendingFrames = 0; session.pendingMin = 1; session.pendingMax = -1;
                if (session.peaks.size() == 8192) {
                    for (std::size_t i = 0; i < 4096; ++i)
                        session.peaks[i] = {std::min(session.peaks[2*i].first, session.peaks[2*i+1].first),
                                            std::max(session.peaks[2*i].second, session.peaks[2*i+1].second)};
                    session.peaks.resize(4096);
                    session.peakFrames *= 2;
                }
            }
        }
    }
    session.writtenFrames.store(previousFrames + completeFrames, std::memory_order_release);
    session.read.store(read + static_cast<std::uint64_t>(completeFrames) * session.channels,
                       std::memory_order_release);
    if (written == bytes) return true;
    error = QStringLiteral("录音文件写入失败：") + file.errorString();
    return false;
}

} // namespace

struct EditorRecordingService::Impl final {
    explicit Impl(EditorCaptureFactory value) : factory(std::move(value)) {}
    EditorCaptureFactory factory;
    QVariantList devices;
    QString selected;
    bool selectionInitialized{};
    double inputGain{1.0};
    double inputLevel{};
    QString actual;
    QString error;
    QString partialPath;
    State state{Idle};
    qint64 lastFrames{};
    quint64 generation{};
    bool permissionPending{};
    bool needsPermission{};
    bool enumerating{};
    std::shared_ptr<CaptureSession> session;
    std::thread worker;
    std::thread enumerationWorker;
    QTimer progress;
};

EditorRecordingService::EditorRecordingService(QObject* parent)
    : EditorRecordingService([] { return std::make_unique<MiniaudioCapture>(); }, parent)
{ impl_->needsPermission = true; }

EditorRecordingService::EditorRecordingService(EditorCaptureFactory factory, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(std::move(factory)))
{
    impl_->devices.append(QVariantMap{{QStringLiteral("id"), QString{}},
                                     {QStringLiteral("name"), QStringLiteral("系统默认输入")}});
    impl_->progress.setInterval(50);
    connect(&impl_->progress, &QTimer::timeout, this, [this] {
        const double captured = impl_->session ? impl_->session->level.exchange(0.0, std::memory_order_relaxed) : 0.0;
        const double level = state() == Recording ? captured : 0.0;
        if (impl_->inputLevel != level) { impl_->inputLevel = level; emit inputLevelChanged(); }
        const qint64 current = recordedFrames();
        if (current != impl_->lastFrames) { impl_->lastFrames = current; emit recordedFramesChanged(); }
    });
    QTimer::singleShot(0, this, &EditorRecordingService::refreshInputDevices);
}

EditorRecordingService::~EditorRecordingService()
{
    ++impl_->generation;
    if (impl_->session) {
        impl_->session->cancelled.store(true, std::memory_order_release);
        impl_->session->stop.store(true, std::memory_order_release);
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->session && impl_->session->ownsFile) QFile::remove(impl_->session->path);
    if (impl_->enumerationWorker.joinable()) impl_->enumerationWorker.join();
}

QVariantList EditorRecordingService::inputDevices() const { return impl_->devices; }
QString EditorRecordingService::selectedInputDeviceId() const { return impl_->selected; }

void EditorRecordingService::setSelectedInputDeviceId(const QString& id)
{
    if (state() != Idle && state() != Error) return;
    impl_->selectionInitialized = true;
    if (impl_->selected == id) return;
    impl_->selected = id;
    emit selectedInputDeviceIdChanged();
}

QString EditorRecordingService::actualInputDeviceName() const { return impl_->actual; }
EditorRecordingService::State EditorRecordingService::state() const { return impl_->state; }
qint64 EditorRecordingService::recordedFrames() const
{ return impl_->session ? impl_->session->writtenFrames.load(std::memory_order_acquire) : impl_->lastFrames; }
QString EditorRecordingService::error() const { return impl_->error; }
double EditorRecordingService::inputGain() const { return impl_->inputGain; }
double EditorRecordingService::inputLevel() const { return impl_->inputLevel; }
void EditorRecordingService::setInputGain(double gain)
{
    if (!std::isfinite(gain)) return;
    gain = std::clamp(gain, 1.0, 8.0);
    if (impl_->inputGain == gain) return;
    impl_->inputGain = gain;
    if (impl_->session) impl_->session->gain.store(gain, std::memory_order_relaxed);
    emit inputGainChanged();
}
QString EditorRecordingService::partialRecordingPath() const { return impl_->partialPath; }

QVariantList EditorRecordingService::waveformPeaks(qint64 start, qint64 end, int pixels) const
{
    const auto session = impl_->session;
    if (!session || end <= start) return {};
    std::lock_guard<std::mutex> lock(session->peakMutex);
    end = std::min(end, session->writtenFrames.load(std::memory_order_acquire));
    start = std::max<qint64>(0, start);
    if (end <= start) return {};
    pixels = std::clamp(pixels, 1, 2048);
    QVariantList values;
    values.reserve(pixels * 2);
    for (int i = 0; i < pixels; ++i) {
        const auto first = (start + (end - start) * i / pixels) / session->peakFrames;
        const auto last = (start + (end - start) * (i + 1) / pixels - 1) / session->peakFrames;
        float lo = 0, hi = 0;
        for (qint64 bucket = first; bucket <= std::max(first, last); ++bucket) {
            if (bucket < static_cast<qint64>(session->peaks.size())) {
                lo = std::min(lo, session->peaks[bucket].first);
                hi = std::max(hi, session->peaks[bucket].second);
            } else if (session->pendingFrames > 0) {
                lo = std::min(lo, session->pendingMin); hi = std::max(hi, session->pendingMax);
            }
        }
        values.append(lo); values.append(hi);
    }
    QVariantList channels;
    channels.append(QVariant::fromValue(values));
    return channels;
}

bool EditorRecordingService::setCaptureFactoryForTesting(EditorCaptureFactory factory)
{
    if (!factory || (state() != Idle && state() != Error) || impl_->session
        || impl_->permissionPending || impl_->enumerating) return false;
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->enumerationWorker.joinable()) impl_->enumerationWorker.join();
    impl_->factory = std::move(factory);
    impl_->needsPermission = false;
    return true;
}

void EditorRecordingService::refreshInputDevices()
{
    if (impl_->enumerating || (state() != Idle && state() != Error)) return;
    if (impl_->enumerationWorker.joinable()) impl_->enumerationWorker.join();
    impl_->enumerating = true;
    const EditorCaptureFactory factory = impl_->factory;
    impl_->enumerationWorker = std::thread([this, factory] {
        QVariantList result;
        QString error;
        try {
            auto backend = factory();
            if (backend) {
                for (const auto& input : backend->devices(error)) {
                    result.append(QVariantMap{{QStringLiteral("id"), input.id},
                                               {QStringLiteral("name"), input.name},
                                               {QStringLiteral("isDefault"), input.isDefault}});
                }
            } else error = QStringLiteral("无法创建录音设备接口。");
        } catch (...) { error = QStringLiteral("无法读取录音设备列表。"); }
        QMetaObject::invokeMethod(this, [this, result, error] {
            impl_->enumerating = false;
            impl_->devices = {QVariantMap{{QStringLiteral("id"), QString{}},
                                         {QStringLiteral("name"), QStringLiteral("系统默认输入")}}};
            impl_->devices.append(result);
            emit inputDevicesChanged();
            if (!impl_->selectionInitialized && !result.isEmpty()
                && (state() == Idle || state() == Error)) {
                QString selected = result.front().toMap().value(QStringLiteral("id")).toString();
                for (const auto& device : result) {
                    const auto fields = device.toMap();
                    if (fields.value(QStringLiteral("isDefault")).toBool()) {
                        selected = fields.value(QStringLiteral("id")).toString();
                        break;
                    }
                }
                setSelectedInputDeviceId(selected);
            }
            if (!error.isEmpty() && (state() == Idle || state() == Error)) {
                impl_->error = error;
                emit errorChanged();
            }
        }, Qt::QueuedConnection);
    });
}

bool EditorRecordingService::start(const QString& outputPath, const int sampleRate, const int channels)
{
    if (state() != Idle && state() != Error) return false;
    if (outputPath.isEmpty() || sampleRate < 8'000 || sampleRate > 384'000
        || channels < 1 || channels > 2 || QFileInfo::exists(outputPath)) {
        reportFailure(QStringLiteral("录音参数无效或目标文件已存在；请选择新的 WAV 文件路径。"));
        return false;
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    ++impl_->generation;
    impl_->session.reset();
    impl_->lastFrames = 0;
    impl_->error.clear();
    impl_->partialPath.clear();
    impl_->actual.clear();
    emit recordedFramesChanged();
    emit errorChanged();
    emit partialRecordingPathChanged();
    emit actualInputDeviceNameChanged();
    setState(Recording);
#ifdef Q_OS_MACOS
    if (impl_->needsPermission) {
        const QMicrophonePermission permission;
        const auto status = qApp->checkPermission(permission);
        if (status == Qt::PermissionStatus::Denied) {
            reportFailure(QStringLiteral("麦克风权限被拒绝，请在系统设置的隐私与安全性中允许 AgPlayer 使用麦克风。"));
            return false;
        }
        if (status == Qt::PermissionStatus::Undetermined) {
            impl_->permissionPending = true;
            const quint64 generation = impl_->generation;
            qApp->requestPermission(permission, this, [this, outputPath, sampleRate, channels, generation](const QPermission& result) {
                if (generation != impl_->generation || !impl_->permissionPending) return;
                impl_->permissionPending = false;
                if (result.status() != Qt::PermissionStatus::Granted) {
                    reportFailure(QStringLiteral("未获得麦克风访问权限，录音未开始。"));
                    return;
                }
                startWorker(outputPath, sampleRate, channels);
            });
            return true;
        }
    }
#endif
    startWorker(outputPath, sampleRate, channels);
    return true;
}

void EditorRecordingService::startWorker(const QString& outputPath, const int sampleRate, const int channels)
{
    std::shared_ptr<CaptureSession> session;
    try { session = std::make_shared<CaptureSession>(sampleRate, channels); }
    catch (...) { reportFailure(QStringLiteral("无法分配录音缓冲区。")); return; }
    session->path = outputPath;
    session->paused.store(state() == Paused, std::memory_order_release);
    impl_->session = session;
    session->gain.store(impl_->inputGain, std::memory_order_relaxed);
    const EditorCaptureFactory factory = impl_->factory;
    const QString selected = impl_->selected;
    const quint64 generation = impl_->generation;
    impl_->progress.start();
    impl_->worker = std::thread([this, session, factory, selected, generation, sampleRate, channels] {
        QFile file(session->path);
        QString error;
        bool writeFailed = false;
        std::unique_ptr<IEditorCaptureBackend> backend;
        try {
            if (!session->cancelled.load(std::memory_order_acquire)) {
                if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                    error = QStringLiteral("无法创建录音文件：") + file.errorString();
                } else {
                    session->ownsFile = true;
                    if (file.write(waveHeader(sampleRate, channels, 0)) != 44) {
                        error = QStringLiteral("无法写入录音文件头：") + file.errorString();
                    }
                }
                if (error.isEmpty()) {
                    backend = factory();
                    QString actual;
                    if (!backend) error = QStringLiteral("无法创建录音设备接口。");
                    else if (backend->open(selected, sampleRate, channels, &CaptureSession::capture,
                                           session.get(), actual, error)
                             && backend->start(error)) {
                        QMetaObject::invokeMethod(this, [this, actual, generation] {
                            if (generation != impl_->generation) return;
                            impl_->actual = actual;
                            emit actualInputDeviceNameChanged();
                        }, Qt::QueuedConnection);
                    } else if (error.isEmpty()) error = QStringLiteral("输入设备无法开始录音。");
                }
            }
            while (error.isEmpty() && !session->stop.load(std::memory_order_acquire)) {
                if (session->overflow.load(std::memory_order_acquire)) {
                    error = QStringLiteral("磁盘写入未能跟上录音，缓冲区已满；已保留此前录音。");
                    break;
                }
                if (session->inputLost.load(std::memory_order_acquire)) {
                    error = QStringLiteral("输入设备未提供有效采集数据，已停止录音。");
                    break;
                }
                error = backend ? backend->failure() : QStringLiteral("输入设备不可用。");
                if (!error.isEmpty()) break;
                if (!writeAvailable(file, *session, error)) { writeFailed = true; break; }
                if (session->read.load(std::memory_order_relaxed) == session->write.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        } catch (...) { error = QStringLiteral("录音过程中发生错误，已尝试保留此前录音。"); }

        session->stop.store(true, std::memory_order_release);
        if (backend) backend->stop();
        // A user stop may race the next worker iteration. Inspect capture and
        // device failures after callbacks are quiescent so stop cannot turn a
        // truncated recording into a successful one.
        if (error.isEmpty() && session->overflow.load(std::memory_order_acquire))
            error = QStringLiteral("磁盘写入未能跟上录音，缓冲区已满；已保留此前录音。");
        if (error.isEmpty() && session->inputLost.load(std::memory_order_acquire))
            error = QStringLiteral("输入设备未提供有效采集数据，已停止录音。");
        if (error.isEmpty() && backend) error = backend->failure();
        backend.reset(); // Quiesce all producer callbacks before draining/finalizing.
        if (!writeFailed && file.isOpen() && !session->cancelled.load(std::memory_order_acquire)) {
            QString drainError;
            while (session->read.load(std::memory_order_relaxed) != session->write.load(std::memory_order_acquire)) {
                if (!writeAvailable(file, *session, drainError)) { error = drainError; break; }
            }
        }
        const qint64 frames = session->writtenFrames.load(std::memory_order_acquire);
        bool valid = false;
        if (file.isOpen() && frames > 0 && !session->cancelled.load(std::memory_order_acquire)) {
            const auto bytes = static_cast<quint32>(frames * channels * 3);
            const qint64 dataEnd = 44LL + bytes;
            const bool headerWritten = file.resize(dataEnd) && file.seek(dataEnd)
                && ((bytes & 1U) == 0 || file.write("\0", 1) == 1)
                && file.seek(0) && file.write(waveHeader(sampleRate, channels, bytes)) == 44 && file.flush();
            file.close();
            valid = headerWritten && validateWave(session->path, sampleRate, channels, frames);
            if (!valid) error = QStringLiteral("录音文件未能完成校验；已保留部分文件供恢复。");
        } else file.close();
        if (session->ownsFile && (frames == 0 || session->cancelled.load(std::memory_order_acquire))) {
            QFile::remove(session->path);
            session->ownsFile = false;
        }
        if (frames == 0 && error.isEmpty()) error = QStringLiteral("没有录到音频，录音未添加到轨道。");
        QMetaObject::invokeMethod(this, [this, session, generation, error, valid, frames] {
            if (generation != impl_->generation) return;
            if (impl_->worker.joinable()) impl_->worker.join();
            impl_->progress.stop();
            impl_->lastFrames = frames;
            impl_->session.reset();
            emit recordedFramesChanged();
            if (session->cancelled.load(std::memory_order_acquire)) {
                if (session->ownsFile) QFile::remove(session->path);
                impl_->lastFrames = 0;
                emit recordedFramesChanged();
                setState(Idle);
            } else if (!error.isEmpty() || !valid) {
                reportFailure(error.isEmpty() ? QStringLiteral("录音文件校验失败。") : error,
                              session->ownsFile ? session->path : QString{}, frames);
            } else {
                setState(Idle);
                emit finished(session->path, frames);
            }
        }, Qt::QueuedConnection);
    });
}

void EditorRecordingService::pause()
{
    if (state() != Recording) return;
    if (impl_->session) impl_->session->paused.store(true, std::memory_order_release);
    setState(Paused);
}

void EditorRecordingService::resume()
{
    if (state() != Paused) return;
    if (impl_->session) impl_->session->paused.store(false, std::memory_order_release);
    setState(Recording);
}

void EditorRecordingService::stop()
{
    if (state() != Recording && state() != Paused) return;
    if (impl_->permissionPending) {
        impl_->permissionPending = false;
        ++impl_->generation;
        reportFailure(QStringLiteral("录音尚未开始，未生成音频。"));
        return;
    }
    if (impl_->session) {
        setState(Stopping);
        impl_->session->stop.store(true, std::memory_order_release);
    }
}

void EditorRecordingService::cancel()
{
    if (impl_->permissionPending) {
        impl_->permissionPending = false;
        ++impl_->generation;
        setState(Idle);
        return;
    }
    if (!impl_->session) return;
    setState(Stopping);
    impl_->session->cancelled.store(true, std::memory_order_release);
    impl_->session->stop.store(true, std::memory_order_release);
}

void EditorRecordingService::setState(const State state)
{
    if (impl_->state == state) return;
    impl_->state = state;
    if (state != Recording) {
        if (impl_->session) impl_->session->level.store(0.0, std::memory_order_relaxed);
        if (impl_->inputLevel != 0.0) { impl_->inputLevel = 0.0; emit inputLevelChanged(); }
    }
    emit stateChanged();
}

void EditorRecordingService::reportFailure(const QString& message, const QString& partialPath, const qint64 frames)
{
    impl_->error = message;
    impl_->partialPath = partialPath;
    impl_->lastFrames = frames;
    emit errorChanged();
    emit partialRecordingPathChanged();
    setState(Error);
    emit failed(message, partialPath, frames);
}

} // namespace agplayer::editor
