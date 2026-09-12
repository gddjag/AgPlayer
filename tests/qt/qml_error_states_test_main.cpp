#include "audio_tools_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"
#include "playback_controller.hpp"
#include "qml_registration.hpp"
#include "runtime_log.hpp"
#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

// Test harness exposed to QML as a context property so the QML test can
// trigger device loss, retry, and access a corrupt fixture file without
// reaching into the C ABI directly. This keeps test-only glue out of
// production code while letting the QML test exercise real error paths.
class ErrorStatesHarness final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString corruptFilePath READ corruptFilePath CONSTANT)
    Q_PROPERTY(QString missingFilePath READ missingFilePath CONSTANT)

public:
    ~ErrorStatesHarness() override
    {
        if (core_ != nullptr) {
            ag_player_destroy(core_);
        }
    }

    QString corruptFilePath() const { return corruptFile_->fileName(); }
    QString missingFilePath() const { return missingFilePath_; }

    Q_INVOKABLE void simulateDeviceLoss()
    {
        if (core_ != nullptr) {
            ag_player_simulate_device_loss(core_);
        }
    }

    Q_INVOKABLE void retryDevice()
    {
        if (core_ != nullptr) {
            ag_player_retry_device(core_);
        }
    }

    Q_INVOKABLE bool loadFile(const QString& path)
    {
        if (core_ == nullptr) {
            return false;
        }
        const QByteArray utf8 = path.toUtf8();
        return ag_player_load(core_, utf8.constData()) == AG_OK;
    }

    void initialize()
    {
        ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
        if (ag_player_create_with_config(&config, &core_) != AG_OK) {
            return;
        }

        corruptFile_ = std::make_unique<QTemporaryFile>();
        if (corruptFile_->open()) {
            corruptFile_->write(QByteArray(256, '\xff'));
            corruptFile_->close();
        }

        missingFilePath_ = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/agplayer-missing-test.wav");
    }

    ag_player* core_ = nullptr;
    std::unique_ptr<QTemporaryFile> corruptFile_;
    QString missingFilePath_;
};

class QmlErrorStatesSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlErrorStatesSetup() override
    {
        if (harness_) {
            harness_->deleteLater();
        }
    }

public slots:
    void applicationAvailable()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AgPlayer");
        QCoreApplication::setApplicationName("AgPlayer-test-error");

        harness_ = std::make_unique<ErrorStatesHarness>();
        harness_->initialize();
        if (harness_->core_ == nullptr) {
            return;
        }

        library_ = std::make_unique<LibraryModel>();
        playback_ = std::make_unique<PlaybackController>(harness_->core_, library_.get());
        importer_ = std::make_unique<ImportController>(library_.get());
        windows_ = std::make_unique<WindowController>();
        audioTools_ = std::make_unique<AudioToolsController>();
        metadataEditor_ = std::make_unique<MetadataEditor>();
        filenameProcessor_ = std::make_unique<FilenameProcessor>();
        formatConverter_ = std::make_unique<FormatConverter>();
        settings_ = std::make_unique<SettingsController>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get(), filenameProcessor_.get(),
                                    settings_.get(), waveformProvider_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");
        if (harness_ != nullptr) {
            engine->rootContext()->setContextProperty("testHarness", harness_.get());
        }
    }

private:
    std::unique_ptr<ErrorStatesHarness> harness_;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<ImportController> importer_;
    std::unique_ptr<WindowController> windows_;
    std::unique_ptr<AudioToolsController> audioTools_;
    std::unique_ptr<MetadataEditor> metadataEditor_;
    std::unique_ptr<FilenameProcessor> filenameProcessor_;
    std::unique_ptr<FormatConverter> formatConverter_;
    std::unique_ptr<SettingsController> settings_;
    std::unique_ptr<WaveformProvider> waveformProvider_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_error_states, QmlErrorStatesSetup)

#include "qml_error_states_test_main.moc"
