#include "import_controller.hpp"
#include "library_model.hpp"
#include "playback_controller.hpp"
#include "waveform_item.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QtPlugin>
#include <QtQml/qqml.h>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

// Harness for tst_mini_player.qml. Mirrors qml_main_window_test_main.cpp but
// loads BOTH Main and MiniPlayerWindow components and exposes them as
// `testMainWindow` and `testMiniWindow` context properties. The QML test
// overrides `playback`/`windows` properties on these windows with fake
// QtObjects declared inline, so no real ag_player audio device is touched.
class QmlMiniPlayerSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlMiniPlayerSetup() override
    {
        if (miniWindow_) {
            miniWindow_->deleteLater();
        }
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

        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel", library_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController", playback_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ImportController", importer_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WindowController", windows_.get());
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");

        // Main window — same as qml_main_window_test harness.
        mainComponent_ = std::make_unique<QQmlComponent>(engine);
        mainComponent_->loadFromModule("AgPlayer", "Main");
        if (!mainComponent_->isError()) {
            mainWindow_ = mainComponent_->create();
            if (mainWindow_) {
                engine->rootContext()->setContextProperty("testMainWindow", mainWindow_);
            }
        }

        // Mini player window — loaded from the same module so it shares the
        // registered singletons. The QML test overrides `playback` and
        // `windows` with fake QtObjects in initTestCase.
        miniComponent_ = std::make_unique<QQmlComponent>(engine);
        miniComponent_->loadFromModule("AgPlayer", "MiniPlayerWindow");
        if (!miniComponent_->isError()) {
            miniWindow_ = miniComponent_->create();
            if (miniWindow_) {
                engine->rootContext()->setContextProperty("testMiniWindow", miniWindow_);
            }
        }
    }

private:
    ag_player* core_ = nullptr;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<ImportController> importer_;
    std::unique_ptr<WindowController> windows_;
    std::unique_ptr<QQmlComponent> mainComponent_;
    std::unique_ptr<QQmlComponent> miniComponent_;
    QObject* mainWindow_ = nullptr;
    QObject* miniWindow_ = nullptr;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_mini_player, QmlMiniPlayerSetup)

#include "qml_mini_player_test_main.moc"
