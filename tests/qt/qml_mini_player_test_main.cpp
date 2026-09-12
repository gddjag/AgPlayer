#include "audio_tools_controller.hpp"
#include "audio_visual_feature_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"
#include "playback_controller.hpp"
#include "player_experience_controller.hpp"
#include "qml_registration.hpp"
#include "settings_controller.hpp"
#include "lyrics_service.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QEvent>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtPlugin>
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
    Q_INVOKABLE QStringList prepareTransportQueue(int durationSeconds = 1)
    {
        if (!transportDirectory_.isValid() || !playback_ || !library_) return {};
        durationSeconds = qBound(1, durationSeconds, 60);
        const quint32 dataBytes = 192000U * quint32(durationSeconds);
        QStringList ids;
        for (int index = 0; index < 3; ++index) {
            const QString id = QStringLiteral("shortcut-track-%1").arg(index)
                + (durationSeconds == 1 ? QString{} : QStringLiteral("-%1s").arg(durationSeconds));
            const QString path = transportDirectory_.filePath(id + QStringLiteral(".wav"));
            if (library_->indexForTrackId(id) < 0) {
                QFile file(path);
                if (!file.open(QIODevice::WriteOnly)) return {};
                // Bounded real PCM silence; only the output device is
                // null. Decoder, queue, transport and snapshots remain real.
                QDataStream wav(&file);
                wav.setByteOrder(QDataStream::LittleEndian);
                wav.writeRawData("RIFF", 4);
                wav << quint32(36 + dataBytes);
                wav.writeRawData("WAVEfmt ", 8);
                wav << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
                    << quint32(192000) << quint16(4) << quint16(16);
                wav.writeRawData("data", 4);
                wav << dataBytes;
                const QByteArray pcm(dataBytes, '\0');
                wav.writeRawData(pcm.constData(), pcm.size());
                if (wav.status() != QDataStream::Ok) return {};
                file.close();
                TrackRecord track;
                track.trackId = id;
                track.path = path;
                track.title = id;
                track.available = true;
                if (!library_->append(track)) return {};
            }
            ids.append(id);
        }
        return playback_->restoreQueue(ids, ids.at(1)) ? ids : QStringList{};
    }

    ~QmlMiniPlayerSetup() override
    {
        // Quick Test may destroy the engine (and its QML objects) before this
        // setup object. QPointer makes that ordering observable and prevents
        // a second delete through a stale raw pointer.
        delete miniWindow_.data();
        miniWindow_ = nullptr;
        delete mainWindow_.data();
        mainWindow_ = nullptr;

        miniComponent_.reset();
        mainComponent_.reset();
        waveformProvider_.reset();
        formatConverter_.reset();
        filenameProcessor_.reset();
        metadataEditor_.reset();
        audioTools_.reset();
        windows_.reset();
        importer_.reset();
        lyricsService_.reset();
        audioFeatures_.reset();
        experience_.reset();
        playback_.reset();
        library_.reset();
        settings_.reset();
        if (core_) {
            ag_player_destroy(core_);
            core_ = nullptr;
        }
    }

public slots:
    void cleanupTestCase()
    {
        if (experience_) {
            experience_->setImmersiveMode(PlayerExperienceController::Off);
            experience_->setHostMode(PlayerExperienceController::Windowed);
        }
        if (audioFeatures_) {
            audioFeatures_->setActive(false);
        }

        delete miniWindow_.data();
        miniWindow_ = nullptr;
        delete mainWindow_.data();
        mainWindow_ = nullptr;
        miniComponent_.reset();
        mainComponent_.reset();

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
    }

    void applicationAvailable()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AgPlayer");
        QCoreApplication::setApplicationName("AgPlayer-test");

        const ag_player_config config{AG_AUDIO_BACKEND_NULL, 2048};
        if (ag_player_create_with_config(&config, &core_) != AG_OK) {
            return;
        }
        library_ = std::make_unique<LibraryModel>();
        miniMetadataTrackId_ = QStringLiteral("mini-metadata-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        TrackRecord metadataTrack;
        metadataTrack.trackId = miniMetadataTrackId_;
        metadataTrack.path = QStringLiteral("C:/virtual/%1.mp3").arg(miniMetadataTrackId_);
        metadataTrack.title = QStringLiteral("Mini metadata QA");
        metadataTrack.artist = QStringLiteral("Mini Artist");
        metadataTrack.album = QStringLiteral("Mini Album");
        metadataTrack.available = true;
        library_->appendBatch({metadataTrack});
        playback_ = std::make_unique<PlaybackController>(core_, library_.get());
        importer_ = std::make_unique<ImportController>(library_.get());
        windows_ = std::make_unique<WindowController>();
        audioTools_ = std::make_unique<AudioToolsController>();
        metadataEditor_ = std::make_unique<MetadataEditor>();
        filenameProcessor_ = std::make_unique<FilenameProcessor>();
        formatConverter_ = std::make_unique<FormatConverter>();
        settings_ = std::make_unique<SettingsController>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());
        experience_ = std::make_unique<PlayerExperienceController>(settings_.get());
        experience_->setImmersiveMode(PlayerExperienceController::Off);
        experience_->setHostMode(PlayerExperienceController::Windowed);
        audioFeatures_ = std::make_unique<AudioVisualFeatureController>(playback_.get());
        lyricsService_ = std::make_unique<LyricsService>(
            library_.get(), playback_.get(), settings_.get());

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get(), filenameProcessor_.get(),
                                    settings_.get(), waveformProvider_.get(),
                                    nullptr, nullptr, nullptr, {},
                                    experience_.get(), audioFeatures_.get(),
                                    lyricsService_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");
        engine->rootContext()->setContextProperty("miniMetadataTrackId",
                                                  miniMetadataTrackId_);
        engine->rootContext()->setContextProperty("transportTestSetup", this);
        engine->rootContext()->setContextProperty(
            "testFloatingControlsScreenshotPath",
            qEnvironmentVariable("AGPLAYER_FLOATING_CONTROLS_SCREENSHOT"));

        // Main window — same as qml_main_window_test harness.
        mainComponent_ = std::make_unique<QQmlComponent>(engine);
        mainComponent_->loadFromModule("AgPlayer", "Main");
        if (!mainComponent_->isError()) {
            mainWindow_ = mainComponent_->create();
            if (mainWindow_) {
                mainWindow_->setProperty("immersiveRenderingEnabled", false);
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
                if (mainWindow_) {
                    miniWindow_->setProperty(
                        "waveformSession",
                        mainWindow_->property("waveformSession"));
                }
                engine->rootContext()->setContextProperty("testMiniWindow", miniWindow_);
            }
        }
    }

private:
    QTemporaryDir transportDirectory_;
    ag_player* core_ = nullptr;
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
    std::unique_ptr<PlayerExperienceController> experience_;
    std::unique_ptr<AudioVisualFeatureController> audioFeatures_;
    std::unique_ptr<LyricsService> lyricsService_;
    QString miniMetadataTrackId_;
    std::unique_ptr<QQmlComponent> mainComponent_;
    std::unique_ptr<QQmlComponent> miniComponent_;
    QPointer<QObject> mainWindow_;
    QPointer<QObject> miniWindow_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_mini_player, QmlMiniPlayerSetup)

#include "qml_mini_player_test_main.moc"
