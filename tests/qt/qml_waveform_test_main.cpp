#include "audio_visual_feature_controller.hpp"
#include "library_model.hpp"
#include "playback_controller.hpp"
#include "settings_controller.hpp"
#include "waveform_item.hpp"
#include "waveform_provider.hpp"

#include <QCoreApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QtQml/qqml.h>
#include <QtQuickTest/quicktest.h>

#include <memory>

class QmlWaveformSetup final : public QObject {
    Q_OBJECT

public slots:
    void feedSpectrum(const QVariantList& spectrum) {
        features_->processSpectrum(spectrum);
    }
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty(QStringLiteral("spectrumTestDriver"), this);
    }
    void applicationAvailable() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
        QCoreApplication::setApplicationName(
            QStringLiteral("AgPlayer-qml-waveform-test"));
        playback_ = std::make_unique<PlaybackController>();
        settings_ = std::make_unique<SettingsController>();
        features_ = std::make_unique<AudioVisualFeatureController>(playback_.get());
        library_ = std::make_unique<LibraryModel>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "PlaybackController",
                                     playback_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "SettingsController",
                                     settings_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0,
                                     "AudioVisualFeatureController",
                                     features_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "LibraryModel",
                                     library_.get());
        qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WaveformProvider",
                                     waveformProvider_.get());
    }

private:
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<SettingsController> settings_;
    std::unique_ptr<AudioVisualFeatureController> features_;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<WaveformProvider> waveformProvider_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_waveform, QmlWaveformSetup)

#include "qml_waveform_test_main.moc"
