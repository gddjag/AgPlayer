#include "audio_tools_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "metadata_editor.hpp"
#include "native_drop_router.hpp"
#include "playback_controller.hpp"
#include "qml_registration.hpp"
#include "settings_controller.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"
#include "voice_clone/voice_clone_host_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTest>
#include <QTemporaryDir>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_PLUGIN(AgPlayerPlugin)

namespace {

bool writeJsonFile(const QString& path, const QJsonObject& object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.write(bytes) == bytes.size();
}

bool stageVoiceClonePlugin(const QString& root)
{
    const QString sourceLibrary = QString::fromUtf8(AGPLAYER_VOICE_CLONE_PLUGIN);
    const QString libraryName = QFileInfo(sourceLibrary).fileName();
    const QString libraryPath = QDir(root).filePath(libraryName);
    if (!QDir().mkpath(root) || !QFile::copy(sourceLibrary, libraryPath)) return false;

    const QString registry = QDir(root).filePath(QStringLiteral("registry/models.json"));
    if (!QDir().mkpath(QFileInfo(registry).absolutePath())
        || !QFile::copy(QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), registry)) {
        return false;
    }

    const QString modelRoot = QDir(root).filePath(
        QStringLiteral("models/voice-clone/qwen/main"));
    if (!QDir().mkpath(modelRoot)) return false;
    QFile config(QDir(modelRoot).filePath(QStringLiteral("config.json")));
    if (!config.open(QIODevice::WriteOnly) || config.write("{}") != 2) return false;
    config.close();
    const QString modelId = QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    const QString modelUrl = QStringLiteral(
        "https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    if (!writeJsonFile(QDir(modelRoot).filePath(QStringLiteral("agplayer-model.json")),
                       QJsonObject{
                           {QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("stableId"), modelId},
                           {QStringLiteral("displayName"), QStringLiteral("Qwen3-TTS 0.6B Base")},
                           {QStringLiteral("description"), QStringLiteral("QML integration model")},
                           {QStringLiteral("adapterId"), QStringLiteral("qwen")},
                           {QStringLiteral("runtimeId"), QStringLiteral("qwen")},
                           {QStringLiteral("revision"), QStringLiteral("main")},
                           {QStringLiteral("source"), QJsonObject{
                                {QStringLiteral("provider"), QStringLiteral("official")},
                                {QStringLiteral("url"), modelUrl}}},
                           {QStringLiteral("license"), QJsonObject{
                                {QStringLiteral("name"), QStringLiteral("Apache-2.0")},
                                {QStringLiteral("url"), modelUrl}}},
                           {QStringLiteral("files"), QJsonArray{QJsonObject{
                                {QStringLiteral("path"), QStringLiteral("config.json")}}}}})) {
        return false;
    }

    const QString adapterRoot = QDir(root).filePath(
        QStringLiteral("adapters/qwen/1.0.0"));
    const QString workerRelative = QStringLiteral("workers/test-worker.exe");
    const QString workerPath = QDir(adapterRoot).filePath(workerRelative);
    if (!QDir().mkpath(QFileInfo(workerPath).absolutePath())
        || !QFile::copy(QString::fromUtf8(AGPLAYER_VOICE_CLONE_TEST_WORKER), workerPath)) {
        return false;
    }
    if (!writeJsonFile(QDir(adapterRoot).filePath(QStringLiteral("adapter.json")),
                       QJsonObject{
                           {QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("adapterId"), QStringLiteral("qwen")},
                           {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")},
                           {QStringLiteral("protocolVersion"), 1},
                           {QStringLiteral("runtime"), QJsonObject{
                                {QStringLiteral("id"), QStringLiteral("test-runtime")},
                                {QStringLiteral("root"), QStringLiteral("runtime")},
                                {QStringLiteral("shared"), false}}},
                           {QStringLiteral("defaultLauncherId"), QStringLiteral("test")},
                           {QStringLiteral("launchers"), QJsonArray{QJsonObject{
                                {QStringLiteral("id"), QStringLiteral("test")},
                                {QStringLiteral("kind"), QStringLiteral("executable")},
                                {QStringLiteral("path"), workerRelative},
                                {QStringLiteral("shared"), false}}}}})) {
        return false;
    }

    return writeJsonFile(QDir(root).filePath(QStringLiteral("agplayer-voice-clone.json")),
                         QJsonObject{
                             {QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("pluginId"), QStringLiteral("agplayer.voice-clone")},
                             {QStringLiteral("version"), QStringLiteral("1.0.0")},
                             {QStringLiteral("availableVersion"), QStringLiteral("1.0.0")},
                             {QStringLiteral("platform"), QStringLiteral("windows")},
                             {QStringLiteral("architecture"), QStringLiteral("x86_64")},
                             {QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.0.0")},
                             {QStringLiteral("protocolVersion"), 1},
                             {QStringLiteral("library"), libraryName}});
}

} // namespace

class NativeDropHelper final : public QObject {
    Q_OBJECT
public:
    void bind(AudioToolsController* tools, FormatConverter* format,
              AudioEditorController* editor, MetadataEditor* metadata,
              FilenameProcessor* filenames)
    {
        tools_ = tools;
        format_ = format;
        editor_ = editor;
        metadata_ = metadata;
        filenames_ = filenames;
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
    bool delivered_ = false;
};

class QmlAudioToolsSetup final : public QObject {
    Q_OBJECT

public:
    ~QmlAudioToolsSetup() override
    {
        voiceCloneHost_.reset();
        qunsetenv("AGPLAYER_VOICE_CLONE_ROOT");
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

        if (!voiceCloneRoot_.isValid()
            || !stageVoiceClonePlugin(voiceCloneRoot_.path())) {
            voiceCloneStageError_ = QStringLiteral("Could not stage real voice-clone plugin test root");
        } else {
            qputenv("AGPLAYER_VOICE_CLONE_ROOT", voiceCloneRoot_.path().toUtf8());
            voiceCloneHost_ = std::make_unique<VoiceCloneHostController>();
        }

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
        nativeDropHelper_.bind(audioTools_.get(), formatConverter_.get(),
                               audioEditor_.get(), metadataEditor_.get(),
                               filenameProcessor_.get());

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get(), filenameProcessor_.get(),
                                    settings_.get(), waveformProvider_.get(),
                                    nullptr, nullptr, audioEditor_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");
        engine->rootContext()->setContextProperty("nativeDropHelper",
                                                  &nativeDropHelper_);
        engine->rootContext()->setContextProperty("realVoiceCloneHost",
                                                  voiceCloneHost_.get());
        engine->rootContext()->setContextProperty("realVoiceCloneStageError",
                                                  voiceCloneStageError_);
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
    QTemporaryDir voiceCloneRoot_;
    std::unique_ptr<VoiceCloneHostController> voiceCloneHost_;
    QString voiceCloneStageError_;
    NativeDropHelper nativeDropHelper_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_audio_tools, QmlAudioToolsSetup)

#include "qml_audio_tools_test_main.moc"
