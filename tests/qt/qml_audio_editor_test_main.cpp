#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_editor_waveform_item.hpp"
#include "audio_tools_controller.hpp"
#include "manual_recording_capture.hpp"
#include "settings_controller.hpp"

#include <QCoreApplication>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>
#include <vector>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

class RecordingTestDriver final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    void setCapture(ManualRecordingCapture* capture) noexcept
    {
        capture_ = capture;
    }

    Q_INVOKABLE QUrl nextOutputUrl()
    {
        return temporary_.isValid()
            ? QUrl::fromLocalFile(temporary_.filePath(
                QStringLiteral("active-%1.wav").arg(++output_index_)))
            : QUrl{};
    }

    Q_INVOKABLE bool feedActive(const int frames)
    {
        if (capture_ == nullptr || frames <= 0) return false;
        std::vector<float> samples(static_cast<std::size_t>(frames) * 2U);
        for (int frame = 0; frame < frames; ++frame) {
            samples[static_cast<std::size_t>(frame) * 2U] =
                frame % 2 == 0 ? -0.8F : 0.3F;
            samples[static_cast<std::size_t>(frame) * 2U + 1U] =
                frame % 2 == 0 ? -0.2F : 0.6F;
        }
        return capture_->feed(samples, static_cast<std::size_t>(frames))
            == static_cast<std::size_t>(frames);
    }

    Q_INVOKABLE bool feedQuiet(const int frames)
    {
        if (capture_ == nullptr || frames <= 0) return false;
        const std::vector<float> samples(
            static_cast<std::size_t>(frames) * 2U, 0.0F);
        return capture_->feed(samples, static_cast<std::size_t>(frames))
            == static_cast<std::size_t>(frames);
    }

private:
    ManualRecordingCapture* capture_{};
    QTemporaryDir temporary_;
    int output_index_{};
};

RecordingTestDriver* recording_test_driver = nullptr;

class QmlAudioEditorSetup final : public QObject {
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
        QCoreApplication::setApplicationName(
            QStringLiteral("AgPlayer-test-audio-editor"));
        recording_test_driver = new RecordingTestDriver(qApp);
        qmlRegisterSingletonInstance(
            "AgPlayer.Test", 1, 0, "RecordingTestDriver",
            recording_test_driver);
        qmlRegisterSingletonType<AudioEditorController>(
            "AgPlayer", 1, 0, "AudioEditorController",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                auto capture = std::make_unique<ManualRecordingCapture>();
                recording_test_driver->setCapture(capture.get());
                return new AudioEditorController(
                    AG_AUDIO_BACKEND_NULL, std::move(capture));
            });
        qmlRegisterSingletonType<AudioToolsController>(
            "AgPlayer", 1, 0, "AudioToolsController",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new AudioToolsController();
            });
        qmlRegisterSingletonType<SettingsController>(
            "AgPlayer", 1, 0, "SettingsController",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new SettingsController();
            });
        qmlRegisterType<AudioEditorWaveformItem>(
            "AgPlayer", 1, 0, "AudioEditorWaveformItem");
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_editor, QmlAudioEditorSetup)

#include "qml_audio_editor_test_main.moc"
