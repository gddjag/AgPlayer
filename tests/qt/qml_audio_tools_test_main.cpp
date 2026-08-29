#include "audio_tools_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"
#include "native_drop_router.hpp"
#include "playback_controller.hpp"
#include "playlist_model.hpp"
#include "qml_registration.hpp"
#include "settings_controller.hpp"
#include "audio_preview_controller.hpp"
#include "vocal_separation_controller.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTest>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

class NativeDropHelper final : public QObject {
    Q_OBJECT
public:
    void bind(AudioToolsController* tools, FormatConverter* format,
              AudioEditorController* editor, MetadataEditor* metadata,
              FilenameProcessor* filenames,
              VocalSeparationController* separation)
    {
        tools_ = tools;
        format_ = format;
        editor_ = editor;
        metadata_ = metadata;
        filenames_ = filenames;
        separation_ = separation;
        connect(&router_, &NativeDropRouter::pathsDropped, this,
                [this](NativeDropRouter::Target target,
                       const QStringList& paths) {
            delivered_ = false;
            if (target != NativeDropRouter::Target::AudioTools
                || tools_ == nullptr || paths.isEmpty()) {
                return;
            }
            QList<QUrl> urls;
            urls.reserve(paths.size());
            for (const QString& path : paths) {
                urls.append(QUrl::fromLocalFile(path));
            }
            switch (tools_->currentTool()) {
            case 0: editor_->openFile(urls.constFirst()); break;
            case 1: format_->loadFiles(urls); break;
            case 2: metadata_->loadFiles(urls); break;
            case 3: filenames_->loadFiles(urls); break;
            case 4: separation_->dropInput(urls); break;
            default: return;
            }
            delivered_ = true;
        });
    }

    Q_INVOKABLE bool sendUrls(QObject* target, const QList<QUrl>& urls)
    {
        if (target == nullptr || urls.isEmpty()) {
            return false;
        }
        QWindow* window = qobject_cast<QWindow*>(target);
        if (window == nullptr) {
            const auto* item = qobject_cast<QQuickItem*>(target);
            window = item == nullptr ? nullptr : item->window();
        }
        if (window == nullptr || !window->isVisible()) {
            return false;
        }
        delivered_ = false;
        router_.registerWindow(window, NativeDropRouter::Target::AudioTools);

        QMimeData mimeData;
        mimeData.setUrls(urls);
        QDragEnterEvent enter(QPoint(12, 12), Qt::CopyAction, &mimeData,
                              Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        if (!enter.isAccepted()) {
            return false;
        }
        QDropEvent drop(QPointF(12.0, 12.0), Qt::CopyAction, &mimeData,
                        Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        return delivered_ && drop.isAccepted();
    }

    Q_INVOKABLE QUrl copyForNativeDrop(const QUrl& sourceUrl)
    {
        const QString source = sourceUrl.toLocalFile();
        if (source.isEmpty() || !QFile::exists(source)) return {};
        const QString destination = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation)
            + QStringLiteral("/agplayer-metadata-ui-%1.%2")
                .arg(QCoreApplication::applicationPid())
                .arg(QFileInfo(source).suffix());
        QFile::remove(destination);
        return QFile::copy(source, destination)
            ? QUrl::fromLocalFile(destination) : QUrl{};
    }

    Q_INVOKABLE bool dragItem(QObject* target, qreal x, qreal y,
                              qreal deltaX, qreal deltaY)
    {
        return dragItemWithModifiers(target, x, y, deltaX, deltaY,
                                     static_cast<int>(Qt::NoModifier));
    }

    Q_INVOKABLE bool dragItemWithModifiers(QObject* target, qreal x, qreal y,
                                           qreal deltaX, qreal deltaY,
                                           int modifiers)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QQuickWindow* window = item == nullptr ? nullptr : item->window();
        if (window == nullptr || !window->isVisible()) {
            return false;
        }
        const QPoint start = item->mapToScene(QPointF(x, y)).toPoint();
        const QPoint end = start + QPoint(qRound(deltaX), qRound(deltaY));
        const auto keyboardModifiers = Qt::KeyboardModifiers(modifiers);
        QTest::mousePress(window, Qt::LeftButton, keyboardModifiers,
                          start, 20);
        constexpr int steps = 6;
        for (int step = 1; step <= steps; ++step) {
            const QPoint point = start + (end - start) * step / steps;
            QTest::mouseMove(window, point, 10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, keyboardModifiers,
                            end, 20);
        return true;
    }

private:
    NativeDropRouter router_;
    AudioToolsController* tools_ = nullptr;
    FormatConverter* format_ = nullptr;
    AudioEditorController* editor_ = nullptr;
    MetadataEditor* metadata_ = nullptr;
    FilenameProcessor* filenames_ = nullptr;
    VocalSeparationController* separation_ = nullptr;
    bool delivered_ = false;
};

class QmlAudioToolsSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlAudioToolsSetup() override
    {
        if (core_ != nullptr) {
            ag_player_destroy(core_);
        }
    }

public slots:
    void applicationAvailable()
    {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AgPlayer");
        QCoreApplication::setApplicationName("AgPlayer-test-audio-tools");

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
        filenameProcessor_ = std::make_unique<FilenameProcessor>();
        formatConverter_ = std::make_unique<FormatConverter>();
        audioEditor_ = std::make_unique<AudioEditorController>(AG_AUDIO_BACKEND_NULL);
        settings_ = std::make_unique<SettingsController>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());
        playlists_ = std::make_unique<PlaylistModel>();
        audioPreview_ = std::make_unique<AudioPreviewController>(
            AG_AUDIO_BACKEND_NULL, playback_.get());
        VocalSeparationControllerOptions separationOptions;
        separationOptions.dataRoot = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation)).filePath(QStringLiteral("separation"));
        separationOptions.outputDirectory = QDir(QStandardPaths::writableLocation(
            QStandardPaths::MusicLocation)).filePath(QStringLiteral("AgPlayer Separation"));
        vocalSeparation_ = std::make_unique<VocalSeparationController>(
            audioPreview_.get(), waveformProvider_.get(), library_.get(),
            importer_.get(), playlists_.get(), separationOptions);
        nativeDropHelper_.bind(audioTools_.get(), formatConverter_.get(),
                               audioEditor_.get(), metadataEditor_.get(),
                               filenameProcessor_.get(), vocalSeparation_.get());

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get(), filenameProcessor_.get(),
                                    settings_.get(), waveformProvider_.get(),
                                    playlists_.get(), nullptr, audioEditor_.get(),
                                    audioPreview_.get(), vocalSeparation_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");
        engine->rootContext()->setContextProperty("nativeDropHelper",
                                                  &nativeDropHelper_);
        engine->rootContext()->setContextProperty(
            "testAudioUrl",
            QUrl::fromLocalFile(QString::fromLocal8Bit(
                qgetenv("AGPLAYER_TEST_AUDIO"))));
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
    std::unique_ptr<PlaylistModel> playlists_;
    std::unique_ptr<AudioPreviewController> audioPreview_;
    std::unique_ptr<VocalSeparationController> vocalSeparation_;
    NativeDropHelper nativeDropHelper_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_tools, QmlAudioToolsSetup)

#include "qml_audio_tools_test_main.moc"
