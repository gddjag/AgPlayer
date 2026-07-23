#include "audio_tools_controller.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"
#include "playback_controller.hpp"
#include "qml_registration.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

class QmlMainWindowSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlMainWindowSetup() override
    {
        if (mainWindow_) {
            mainWindow_->deleteLater();
        }
        if (core_) {
            ag_player_destroy(core_);
        }
    }

public slots:
    void applicationAvailable()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AgPlayer");
        QCoreApplication::setApplicationName("AgPlayer-test");

        if (ag_player_create(&core_) != AG_OK) {
            return;
        }
        library_ = std::make_unique<LibraryModel>();
        playback_ = std::make_unique<PlaybackController>(core_, library_.get());
        importer_ = std::make_unique<ImportController>(library_.get());
        windows_ = std::make_unique<WindowController>();
        audioTools_ = std::make_unique<AudioToolsController>();
        metadataEditor_ = std::make_unique<MetadataEditor>();
        formatConverter_ = std::make_unique<FormatConverter>();

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");

        component_ = std::make_unique<QQmlComponent>(engine);
        component_->loadFromModule("AgPlayer", "Main");
        if (component_->isError()) {
            return;
        }

        mainWindow_ = component_->create();
        if (mainWindow_) {
            engine->rootContext()->setContextProperty("testMainWindow", mainWindow_);
        }
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
    std::unique_ptr<QQmlComponent> component_;
    QObject* mainWindow_ = nullptr;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_main_window, QmlMainWindowSetup)

#include "qml_main_window_test_main.moc"
