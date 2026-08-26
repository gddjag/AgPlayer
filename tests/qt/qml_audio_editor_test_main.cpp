#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_editor_waveform_item.hpp"
#include "audio_tools_controller.hpp"
#include "settings_controller.hpp"
#include "theme_manager.hpp"

#include <QCoreApplication>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

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
        settings_ = std::make_unique<SettingsController>();
        themeManager_ = std::make_unique<ThemeManager>(*qGuiApp);
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SettingsController",
                                     settings_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "ThemeManager",
                                     themeManager_.get());
        qmlRegisterSingletonType<AudioEditorController>(
            "AgPlayer", 1, 0, "AudioEditorController",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new AudioEditorController(AG_AUDIO_BACKEND_NULL);
            });
        qmlRegisterSingletonType<AudioToolsController>(
            "AgPlayer", 1, 0, "AudioToolsController",
            [](QQmlEngine*, QJSEngine*) -> QObject* {
                return new AudioToolsController();
            });
        qmlRegisterType<AudioEditorWaveformItem>(
            "AgPlayer", 1, 0, "AudioEditorWaveformItem");
    }

private:
    std::unique_ptr<SettingsController> settings_;
    std::unique_ptr<ThemeManager> themeManager_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_editor, QmlAudioEditorSetup)

#include "qml_audio_editor_test_main.moc"
