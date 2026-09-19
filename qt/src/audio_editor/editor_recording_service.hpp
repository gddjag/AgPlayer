#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace agplayer::editor {

struct EditorInputDevice final {
    QString id;
    QString name;
    bool isDefault{};
};

// Hardware boundary. open/start/stop and enumeration run outside the GUI thread.
// stop() must quiesce all callbacks before returning. PCM is interleaved float
// in the requested project format. Callbacks must never throw.
class IEditorCaptureBackend {
public:
    using Capture = void (*)(void*, const float*, std::size_t) noexcept;
    virtual ~IEditorCaptureBackend() = default;
    virtual std::vector<EditorInputDevice> devices(QString& error) = 0;
    virtual bool open(const QString& deviceId, int sampleRate, int channels,
                      Capture capture, void* user, QString& actualDevice,
                      QString& error) = 0;
    virtual bool start(QString& error) = 0;
    virtual void stop() noexcept = 0;
    // Called by the writer, never from the capture callback.
    virtual QString failure() const = 0;
};

using EditorCaptureFactory = std::function<std::unique_ptr<IEditorCaptureBackend>()>;

class EditorRecordingService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY inputDevicesChanged)
    Q_PROPERTY(QString selectedInputDeviceId READ selectedInputDeviceId WRITE setSelectedInputDeviceId NOTIFY selectedInputDeviceIdChanged)
    Q_PROPERTY(QString actualInputDeviceName READ actualInputDeviceName NOTIFY actualInputDeviceNameChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 recordedFrames READ recordedFrames NOTIFY recordedFramesChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(double inputGain READ inputGain WRITE setInputGain NOTIFY inputGainChanged)
    Q_PROPERTY(double inputLevel READ inputLevel NOTIFY inputLevelChanged)
    Q_PROPERTY(QString partialRecordingPath READ partialRecordingPath NOTIFY partialRecordingPathChanged)

public:
    enum State { Idle, Recording, Paused, Stopping, Error };
    Q_ENUM(State)

    explicit EditorRecordingService(QObject* parent = nullptr);
    explicit EditorRecordingService(EditorCaptureFactory factory, QObject* parent = nullptr);
    ~EditorRecordingService() override;

    [[nodiscard]] QVariantList inputDevices() const;
    [[nodiscard]] QString selectedInputDeviceId() const;
    void setSelectedInputDeviceId(const QString& id);
    [[nodiscard]] QString actualInputDeviceName() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] qint64 recordedFrames() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] double inputGain() const;
    [[nodiscard]] double inputLevel() const;
    void setInputGain(double gain);
    [[nodiscard]] QString partialRecordingPath() const;
    // Hardware-boundary injection for integration tests; never replace an
    // active capture or permission request.
    bool setCaptureFactoryForTesting(EditorCaptureFactory factory);

    Q_INVOKABLE void refreshInputDevices();
    Q_INVOKABLE QVariantList waveformPeaks(qint64 start, qint64 end, int pixels) const;
    // true means accepted, not that hardware has started; observe actual device,
    // state and failed. The output path must not already exist.
    Q_INVOKABLE bool start(const QString& outputPath, int sampleRate, int channels);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void cancel();

signals:
    void inputDevicesChanged();
    void selectedInputDeviceIdChanged();
    void actualInputDeviceNameChanged();
    void stateChanged();
    void recordedFramesChanged();
    void errorChanged();
    void inputGainChanged();
    void inputLevelChanged();
    void partialRecordingPathChanged();
    void finished(const QString& path, qint64 frames);
    void failed(const QString& message, const QString& partialPath, qint64 frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void startWorker(const QString& outputPath, int sampleRate, int channels);
    void setState(State state);
    void reportFailure(const QString& message, const QString& partialPath = {}, qint64 frames = 0);
};

} // namespace agplayer::editor
