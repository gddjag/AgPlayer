#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_editor_waveform_item.hpp"
#include "audio_tools_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_filter_model.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"
#include "playback_controller.hpp"
#include "qml_registration.hpp"
#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>
#include <qqml.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

class QmlAudioEditorSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlAudioEditorSetup() override
    {
        audioEditor_.reset();
        playback_.reset();
        if (core_ != nullptr) ag_player_destroy(core_);
    }

public slots:
    void applicationAvailable()
    {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
        QCoreApplication::setApplicationName(
            QStringLiteral("AgPlayer-test-audio-editor"));

        ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
        if (ag_player_create_with_config(&config, &core_) != AG_OK) return;

        library_ = std::make_unique<LibraryModel>();
        playback_ = std::make_unique<PlaybackController>(core_, library_.get());
        importer_ = std::make_unique<ImportController>(library_.get());
        windows_ = std::make_unique<WindowController>();
        audioTools_ = std::make_unique<AudioToolsController>();
        metadataEditor_ = std::make_unique<MetadataEditor>();
        filenameProcessor_ = std::make_unique<FilenameProcessor>();
        formatConverter_ = std::make_unique<FormatConverter>();
        audioEditor_ = std::make_unique<AudioEditorController>();
        audioEditor_->setPlaybackController(playback_.get());
        settings_ = std::make_unique<SettingsController>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());
        register_agplayer_qml_types(
            library_.get(), playback_.get(), importer_.get(), windows_.get(),
            audioTools_.get(), metadataEditor_.get(), formatConverter_.get(),
            filenameProcessor_.get(), settings_.get(), waveformProvider_.get(),
            nullptr, nullptr, audioEditor_.get(),
            AgPlayerQmlRuntimeModels{});
        qmlRegisterType<LibraryFilterModel>("AgPlayer", 1, 0,
                                            "LibraryFilterModel");
    }

private:
    ag_player* core_ = nullptr;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<ImportController> importer_;
    std::unique_ptr<WindowController> windows_;
    std::unique_ptr<AudioToolsController> audioTools_;
    std::unique_ptr<MetadataEditor> metadataEditor_;
    std::unique_ptr<FilenameProcessor> filenameProcessor_;
    std::unique_ptr<FormatConverter> formatConverter_;
    std::unique_ptr<AudioEditorController> audioEditor_;
    std::unique_ptr<SettingsController> settings_;
    std::unique_ptr<WaveformProvider> waveformProvider_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_editor, QmlAudioEditorSetup)

#include "qml_audio_editor_test_main.moc"
