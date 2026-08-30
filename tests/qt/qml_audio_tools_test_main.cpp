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
#include <QDir>
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

// This driver exists only in the QML test executable.  It emits the production
// controller's real notify signals after arranging deterministic state so the
// page is exercised without adding a production-only mock mode.
class VocalSeparationControllerTestDriver final : public QObject {
    Q_OBJECT

public:
    void bind(VocalSeparationController* controller)
    {
        controller_ = controller;
        if (controller_ != nullptr)
            runtimeLibraryPath_ = controller_->options_.runtimeLibraryPath;
    }

    Q_INVOKABLE void reset()
    {
        if (controller_ == nullptr) return;
        controller_->activeRequest_.reset();
        controller_->failedRequest_.reset();
        controller_->setJobState(VocalSeparationController::JobState::Idle);
        controller_->clearInput();
        controller_->downloadQueue_.clear();
        controller_->downloadingModelId_.clear();
        controller_->failedDownloadModelId_.clear();
        controller_->downloadProgress_ = 0.0;
        controller_->verifiedModelIds_.clear();
        controller_->verifiedOrRejectedModelIds_.clear();
        controller_->runtimeVerified_ = false;
        controller_->options_.runtimeLibraryPath = runtimeLibraryPath_;
        controller_->inputInfo_.clear();
        controller_->error_.clear();
        controller_->progress_ = 0.0;
        controller_->deviceMode_ = VocalSeparationController::DeviceMode::Auto;
        controller_->availableDevices_ = standardDevices();
        controller_->selectedModelId_ = controller_->options_.catalog.isEmpty()
            ? QString{} : controller_->options_.catalog.constFirst().id;
        controller_->refreshModels();
        controller_->rebuildStems();
        emit controller_->inputInfoChanged();
        emit controller_->errorChanged();
        emit controller_->progressChanged();
        emit controller_->downloadProgressChanged();
        emit controller_->downloadStateChanged();
        emit controller_->availableDevicesChanged();
        emit controller_->selectedModelIdChanged();
    }

    Q_INVOKABLE bool setInput(const QUrl& input)
    {
        return controller_ != nullptr && controller_->selectInput(input);
    }

    Q_INVOKABLE void setHistoryRecord()
    {
        if (controller_ == nullptr) return;
        controller_->history_ = {QVariantMap{
            {QStringLiteral("id"), QStringLiteral("history-layout-test")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-08-30T20:00:00Z")},
            {QStringLiteral("inputPath"), QStringLiteral("C:/音乐/历史输入 #100%.wav")},
            {QStringLiteral("inputName"), QStringLiteral("历史输入 #100%.wav")},
            {QStringLiteral("modelId"), QStringLiteral("uvr-mdxnet-kara")},
            {QStringLiteral("status"), QStringLiteral("completed")},
            {QStringLiteral("outputPath"), QStringLiteral("C:/音乐/分离结果")},
        }};
        emit controller_->historyChanged();
    }

    Q_INVOKABLE void clearHistory()
    {
        if (controller_ == nullptr) return;
        controller_->history_.clear();
        emit controller_->historyChanged();
    }

    Q_INVOKABLE void markSelectedModelInstalled()
    {
        if (controller_ == nullptr) return;
        controller_->verifiedModelIds_.insert(controller_->selectedModelId_);
        controller_->verifiedOrRejectedModelIds_.insert(
            controller_->selectedModelId_);
        controller_->refreshModels();
    }

    Q_INVOKABLE void setRuntimeMissing()
    {
        if (controller_ == nullptr) return;
        controller_->options_.runtimeLibraryPath = QDir(
            QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("agplayer-test-missing-onnxruntime.dll"));
        emit controller_->startEligibilityChanged();
    }

    Q_INVOKABLE bool setReady(const QUrl& input)
    {
        if (controller_ == nullptr) return false;
        reset();
        const QString runtimePath = QDir(
            QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("agplayer-test-onnxruntime.dll"));
        QFile runtime(runtimePath);
        if (!runtime.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        runtime.write("test-runtime");
        runtime.close();
        controller_->options_.runtimeLibraryPath = runtimePath;
        if (!controller_->selectInput(input)) return false;
        markSelectedModelInstalled();
        controller_->availableDevices_ = standardDevices();
        emit controller_->availableDevicesChanged();
        emit controller_->startEligibilityChanged();
        return controller_->canStart();
    }

    Q_INVOKABLE void setDevices(const QString& scenario)
    {
        if (controller_ == nullptr) return;
        if (scenario == QStringLiteral("fallback")) {
            controller_->availableDevices_ = {
                device(VocalSeparationController::DeviceMode::Auto, "Auto", true,
                       "自动选择已验证的可用设备"),
                device(VocalSeparationController::DeviceMode::CPU, "CPU", true, ""),
                device(VocalSeparationController::DeviceMode::GPU, "DirectML", false,
                       "DirectML 提供程序不可用，已回退 CPU"),
            };
        } else if (scenario == QStringLiteral("candidate")) {
            controller_->availableDevices_ = {
                device(VocalSeparationController::DeviceMode::Auto, "Auto", true,
                       "自动选择可用设备"),
                device(VocalSeparationController::DeviceMode::CPU, "CPU", true, ""),
                device(VocalSeparationController::DeviceMode::GPU, "DirectML", true,
                       "检测到 NVIDIA GeForce RTX 4070 Ti SUPER；开始分离时验证 DirectML"),
            };
        } else if (scenario == QStringLiteral("none")) {
            controller_->availableDevices_ = {
                device(VocalSeparationController::DeviceMode::Auto, "Auto", false,
                       "CPU 和 GPU 均未通过设备探测"),
                device(VocalSeparationController::DeviceMode::CPU, "CPU", false,
                       "CPU 提供程序不可用"),
                device(VocalSeparationController::DeviceMode::GPU, "DirectML", false,
                       "DirectML 提供程序不可用"),
            };
        } else {
            controller_->availableDevices_ = standardDevices();
        }
        emit controller_->availableDevicesChanged();
        emit controller_->startEligibilityChanged();
    }

    Q_INVOKABLE void setDownloadState(const QString& modelId, int state,
                                      double progress, const QString& error)
    {
        if (controller_ == nullptr) return;
        const auto modelState = static_cast<VocalSeparationController::ModelState>(state);
        controller_->downloadProgress_ = progress;
        controller_->failedDownloadModelId_.clear();
        if (modelState == VocalSeparationController::ModelState::ModelFailed) {
            controller_->failedDownloadModelId_ = modelId;
            controller_->downloadingModelId_.clear();
        } else {
            controller_->downloadingModelId_ = modelId;
        }
        for (int index = 0; index < controller_->models_.size(); ++index) {
            QVariantMap model = controller_->models_.at(index).toMap();
            if (model.value(QStringLiteral("id")).toString() == modelId) {
                model.insert(QStringLiteral("state"), state);
                controller_->models_[index] = model;
                break;
            }
        }
        controller_->setError(error);
        emit controller_->modelsChanged();
        emit controller_->downloadProgressChanged();
        emit controller_->downloadStateChanged();
    }

    Q_INVOKABLE void setJobState(int state, const QString& stage)
    {
        if (controller_ == nullptr) return;
        controller_->setJobState(
            static_cast<VocalSeparationController::JobState>(state), stage);
    }

    Q_INVOKABLE void setCompleted()
    {
        if (controller_ == nullptr) return;
        for (int index = 0; index < controller_->stems_.size(); ++index) {
            QVariantMap stem = controller_->stems_.at(index).toMap();
            if (stem.value(QStringLiteral("supported")).toBool()) {
                stem.insert(QStringLiteral("selected"), true);
                stem.insert(QStringLiteral("available"), true);
                stem.insert(QStringLiteral("path"), QStringLiteral("C:/test/%1.wav")
                    .arg(stem.value(QStringLiteral("name")).toString()));
                stem.insert(QStringLiteral("waveform"), QVariantList{0.2, 0.8, 0.4});
            }
            controller_->stems_[index] = stem;
        }
        controller_->progress_ = 1.0;
        emit controller_->stemsChanged();
        emit controller_->progressChanged();
        controller_->setJobState(VocalSeparationController::JobState::Completed,
                                 QStringLiteral("completed"));
    }

    Q_INVOKABLE QVariantList activeResultMixKinds() const
    {
        QVariantList kinds;
        if (controller_ == nullptr) return kinds;
        kinds.reserve(controller_->resultPreviewMixKinds_.size());
        for (const VocalSeparationController::StemKind kind
             : controller_->resultPreviewMixKinds_) {
            kinds.push_back(int(kind));
        }
        return kinds;
    }

    Q_INVOKABLE bool setCompletedWithAudio(const QUrl& source)
    {
        if (controller_ == nullptr || !source.isLocalFile()) return false;
        const QString sourcePath = source.toLocalFile();
        if (!QFileInfo::exists(sourcePath)) return false;
        const quint64 generation = ++audioResultGeneration_;
        for (int index = 0; index < controller_->stems_.size(); ++index) {
            QVariantMap stem = controller_->stems_.at(index).toMap();
            if (stem.value(QStringLiteral("supported")).toBool()) {
                const int kind = stem.value(QStringLiteral("kind")).toInt();
                const QString suffix = QFileInfo(sourcePath).suffix();
                const QString destination = QDir(
                    QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                    .filePath(QStringLiteral("agplayer-qml-result-%1-%2.%3")
                                  .arg(generation)
                                  .arg(kind)
                                  .arg(suffix));
                QFile::remove(destination);
                if (!QFile::copy(sourcePath, destination)) return false;
                stem.insert(QStringLiteral("selected"), true);
                stem.insert(QStringLiteral("available"), true);
                stem.insert(QStringLiteral("path"), destination);
                stem.insert(QStringLiteral("waveform"), QVariantList{0.2, 0.8, 0.4});
            }
            controller_->stems_[index] = stem;
        }
        controller_->progress_ = 1.0;
        emit controller_->stemsChanged();
        emit controller_->progressChanged();
        controller_->setJobState(VocalSeparationController::JobState::Completed,
                                 QStringLiteral("completed"));
        return true;
    }

    Q_INVOKABLE void setJobProgress(double progress, const QString& stage)
    {
        if (controller_ == nullptr) return;
        controller_->progress_ = qBound(0.0, progress, 1.0);
        emit controller_->progressChanged();
        controller_->setJobState(VocalSeparationController::JobState::Running,
                                 stage);
    }

    Q_INVOKABLE void setJobFailure(const QString& error)
    {
        if (controller_ == nullptr) return;
        controller_->failedRequest_ =
            VocalSeparationController::ActiveRequestContext{};
        controller_->setError(error);
        controller_->setJobState(VocalSeparationController::JobState::JobFailed,
                                 QStringLiteral("failed"));
    }

    Q_INVOKABLE bool selectModel(const QString& modelId)
    {
        return controller_ != nullptr && controller_->selectModel(modelId);
    }

private:
    static QVariantMap device(VocalSeparationController::DeviceMode mode,
                              const QString& name, bool available,
                              const QString& reason)
    {
        return {{QStringLiteral("mode"), int(mode)},
                {QStringLiteral("name"), name},
                {QStringLiteral("available"), available},
                {QStringLiteral("reason"), reason}};
    }

    static QVariantList standardDevices()
    {
        return {
            device(VocalSeparationController::DeviceMode::Auto, "Auto", true,
                   "自动选择已验证的可用设备"),
            device(VocalSeparationController::DeviceMode::CPU, "CPU", true, ""),
            device(VocalSeparationController::DeviceMode::GPU, "DirectML", false,
                   "尚未探测"),
        };
    }

    VocalSeparationController* controller_ = nullptr;
    QString runtimeLibraryPath_;
    quint64 audioResultGeneration_ = 0;
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
        separationTestDriver_.bind(vocalSeparation_.get());

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
        engine->rootContext()->setContextProperty("separationTestDriver",
                                                  &separationTestDriver_);
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
    VocalSeparationControllerTestDriver separationTestDriver_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_tools, QmlAudioToolsSetup)

#include "qml_audio_tools_test_main.moc"
