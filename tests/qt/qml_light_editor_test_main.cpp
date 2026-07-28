#include "audio_tools_controller.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "light_editor_controller.hpp"
#include "metadata_editor.hpp"
#include "pitch_shifter.hpp"
#include "playback_controller.hpp"
#include "qml_registration.hpp"
#include "settings_controller.hpp"
#include "speed_adjuster.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

class QmlLightEditorSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlLightEditorSetup() override
    {
        if (core_ != nullptr) {
            ag_player_destroy(core_);
        }
    }

public slots:
    void applicationAvailable()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AgPlayer");
        QCoreApplication::setApplicationName("AgPlayer-test-light-editor");

        ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
        if (ag_player_create_with_config(&config, &core_) != AG_OK) {
            return;
        }

        library_ = std::make_unique<LibraryModel>();
        playback_ = std::make_unique<PlaybackController>(core_, library_.get());
        importer_ = std::make_unique<ImportController>(library_.get());
        windows_ = std::make_unique<WindowController>();
        audioTools_ = std::make_unique<AudioToolsController>();
        metadataEditor_ = std::make_unique<MetadataEditor>();
        formatConverter_ = std::make_unique<FormatConverter>();
        pitchShifter_ = std::make_unique<PitchShifter>();
        speedAdjuster_ = std::make_unique<SpeedAdjuster>();
        lightEditor_ = std::make_unique<LightEditor>();
        settings_ = std::make_unique<SettingsController>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get(), pitchShifter_.get(),
                                    speedAdjuster_.get(), lightEditor_.get(),
                                    settings_.get(), waveformProvider_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");
    }

private:
    ag_player* core_ = nullptr;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<ImportController> importer_;
    std::unique_ptr<WindowController> windows_;
    std::unique_ptr<AudioToolsController> audioTools_;
    std::unique_ptr<MetadataEditor> metadataEditor_;
    std::unique_ptr<FormatConverter> formatConverter_;
    std::unique_ptr<PitchShifter> pitchShifter_;
    std::unique_ptr<SpeedAdjuster> speedAdjuster_;
    std::unique_ptr<LightEditor> lightEditor_;
    std::unique_ptr<SettingsController> settings_;
    std::unique_ptr<WaveformProvider> waveformProvider_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_light_editor, QmlLightEditorSetup)

#include "qml_light_editor_test_main.moc"
