#include "audio_editor/audio_editor_controller.hpp"
#include "audio_editor/audio_editor_waveform_item.hpp"
#include "audio_tools_controller.hpp"

#include <QCoreApplication>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

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
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_editor, QmlAudioEditorSetup)

#include "qml_audio_editor_test_main.moc"
