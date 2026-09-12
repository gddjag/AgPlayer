#include "audio_tools_controller.hpp"
#include "equalizer_controller.hpp"
#include "filename_processor.hpp"
#include "format_converter.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "lyrics_service.hpp"
#include "resource_folder_controller.hpp"
#include "library_navigation_model.hpp"
#include "metadata_editor.hpp"
#include "native_drop_router.hpp"
#include "playback_controller.hpp"
#include "playlist_model.hpp"
#include "qml_registration.hpp"
#include "settings_controller.hpp"
#include "translation_manager.hpp"
#include "tag_model.hpp"
#include "track_waveform_thumbnail_provider.hpp"
#include "video_playback_controller.hpp"
#include "waveform_provider.hpp"
#include "window_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMimeData>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <string>
#endif

Q_IMPORT_PLUGIN(AgPlayerPlugin)

// UI contracts exercise the real service without depending on public servers.
class NoMatchLyricsProvider final : public LyricsProvider {
public:
    void requestExact(quint64 id, const Track&) override { enqueue(id); }
    void requestSearch(quint64 id, const Track&) override { enqueue(id); }
    void cancel(quint64 id) override { pending_.remove(id); }

private:
    void enqueue(quint64 id)
    {
        pending_.insert(id);
        QTimer::singleShot(0, this, [this, id] {
            if (pending_.remove(id)) complete(id, Result::notFound());
        });
    }
    QSet<quint64> pending_;
};

class NativeDropHelper final : public QObject {
    Q_OBJECT
public:
    void setLibraryModel(LibraryModel* library) { library_ = library; }

    Q_INVOKABLE QStringList ensureSortableTracks()
    {
        if (library_ == nullptr) return {};
        const QString fixture = QString::fromLocal8Bit(
            qgetenv("AGPLAYER_TEST_AUDIO"));
        if (!QFileInfo(fixture).isFile()) return {};
        QList<TrackRecord> tracks;
        QStringList ids;
        // QML tests may run against a persisted temporary library.  A stable
        // literal id can then resolve to a record made unavailable by an
        // earlier test.  Use one per-process namespace so the drag test
        // always exercises real, enabled rows instead of stale state.
        static const QString testRunId =
            QString::number(QDateTime::currentMSecsSinceEpoch());
        for (int index = 0; index < 3; ++index) {
            const QString id = QStringLiteral("drag-test-%1-%2")
                                   .arg(testRunId)
                                   .arg(index);
            ids.append(id);
            if (library_->indexForTrackId(id) >= 0) continue;
            const QString path = dropDirectory_.filePath(id + QStringLiteral(".wav"));
            if (!QFileInfo::exists(path) && !QFile::copy(fixture, path)) return {};
            TrackRecord track;
            track.trackId = id;
            track.path = path;
            track.title = QStringLiteral("Drag test %1").arg(index);
            track.artist = QStringLiteral("AgPlayer QA");
            track.available = true;
            tracks.append(track);
        }
        library_->appendBatch(std::move(tracks));
        return ids;
    }

    Q_INVOKABLE void clearTracks()
    {
        if (library_ == nullptr) return;
        while (library_->rowCount() > 0) {
            const QModelIndex index = library_->index(0, 0);
            const QString trackId = library_->data(
                index, LibraryModel::TrackIdRole).toString();
            if (trackId.isEmpty() || !library_->removeTrack(trackId)) return;
        }
    }

    Q_INVOKABLE void clearLibrary()
    {
        if (library_ != nullptr) {
            library_->replaceAll({});
        }
    }

    Q_INVOKABLE QString ensureLongTitleTrack()
    {
        if (library_ == nullptr) return {};
        const QString id = QStringLiteral("long-title-track");
        if (library_->indexForTrackId(id) < 0) {
            TrackRecord track;
            track.trackId = id;
            track.path = QStringLiteral("C:/virtual/long-title-track.mp3");
            track.title = QStringLiteral(
                "This is an intentionally very long song title for hover marquee verification");
            track.artist = QStringLiteral("AgPlayer QA");
            track.available = true;
            library_->appendBatch({track});
        }
        return id;
    }

    Q_INVOKABLE QString ensureLongAlbumArtistTrack()
    {
        if (library_ == nullptr) return {};
        const QString id = QStringLiteral("long-album-artist-track");
        if (library_->indexForTrackId(id) < 0) {
            const QString fixture = QString::fromLocal8Bit(
                qgetenv("AGPLAYER_TEST_AUDIO"));
            if (!QFileInfo(fixture).isFile()) return {};
            const QString path = dropDirectory_.filePath(id + QStringLiteral(".wav"));
            if (!QFileInfo::exists(path) && !QFile::copy(fixture, path)) return {};
            TrackRecord track;
            track.trackId = id;
            track.path = path;
            track.title = QStringLiteral("Column alignment QA");
            track.artist = QStringLiteral(
                "An intentionally long artist name for hover marquee verification");
            track.album = QStringLiteral(
                "An intentionally long album name for hover marquee verification");
            track.available = true;
            library_->appendBatch({track});
        }
        return id;
    }

    Q_INVOKABLE bool sendUrls(QObject* target, const QList<QUrl>& urls)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        auto* window = qobject_cast<QWindow*>(target);
        if (item != nullptr) {
            window = item->window();
        }
        if (window == nullptr || urls.isEmpty()) {
            return false;
        }
        QMimeData mime;
        mime.setUrls(urls);
        const QPointF scenePosition = item != nullptr
            ? item->mapToScene(QPointF(item->width() / 2.0,
                                       item->height() / 2.0))
            : QPointF(window->width() / 2.0, window->height() / 2.0);
        QDragEnterEvent enter(scenePosition.toPoint(), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QDropEvent drop(scenePosition, Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        return enter.isAccepted() && drop.isAccepted();
    }

    Q_INVOKABLE bool sendTrackIds(QObject* target, const QStringList& trackIds)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QWindow* window = item == nullptr ? qobject_cast<QWindow*>(target)
                                           : item->window();
        if (window == nullptr || trackIds.isEmpty()) return false;

        QJsonArray values;
        for (const QString& trackId : trackIds) values.append(trackId);
        QMimeData mime;
        mime.setData("application/x-agplayer-track-ids",
                     QJsonDocument(values).toJson(QJsonDocument::Compact));
        const QPointF scenePosition = item != nullptr
            ? item->mapToScene(QPointF(item->width() / 2.0,
                                       item->height() / 2.0))
            : QPointF(window->width() / 2.0, window->height() / 2.0);
        QDragEnterEvent enter(scenePosition.toPoint(), Qt::MoveAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QDropEvent drop(scenePosition, Qt::MoveAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        return enter.isAccepted() && drop.isAccepted();
    }

    Q_INVOKABLE bool sendKey(QObject* target, int key)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QWindow* window = item == nullptr ? qobject_cast<QWindow*>(target)
                                           : item->window();
        if (window == nullptr) return false;

        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    }

    Q_INVOKABLE bool registerListDropWindow(QObject* target)
    {
        auto* window = qobject_cast<QWindow*>(target);
        if (window == nullptr) return false;
        listDrops_ = std::make_unique<NativeDropRouter>();
        listDrops_->registerWindow(window, NativeDropRouter::Target::List);
        const QPointer<QObject> guardedWindow = window;
        listDrops_->registerHitTarget(
            window, NativeDropRouter::Target::ResourceFolder,
            [guardedWindow](const QPointF& position) {
                if (guardedWindow == nullptr) return false;
                QVariant hit;
                return QMetaObject::invokeMethod(
                           guardedWindow, "resourceDropContainsPoint",
                           Q_RETURN_ARG(QVariant, hit),
                           Q_ARG(QVariant, position.x()),
                           Q_ARG(QVariant, position.y()))
                    && hit.toBool();
            });
        connect(listDrops_.get(), &NativeDropRouter::pathsDropped, window,
                [guardedWindow](const NativeDropRouter::Target dropTarget,
                                const QStringList& paths) {
            if (guardedWindow == nullptr) return;
            QList<QUrl> urls;
            urls.reserve(paths.size());
            for (const QString& path : paths) {
                urls.append(QUrl::fromLocalFile(path));
            }
            const char* method = dropTarget
                    == NativeDropRouter::Target::ResourceFolder
                ? "handleResourceDropUrls" : "handleListDropUrls";
            QMetaObject::invokeMethod(
                guardedWindow, method,
                Q_ARG(QVariant, QVariant::fromValue(urls)));
        });
        return true;
    }

    Q_INVOKABLE bool sendWindowsDropFiles(QObject* target,
                                          const QList<QUrl>& urls)
    {
#ifdef Q_OS_WIN
        auto* item = qobject_cast<QQuickItem*>(target);
        auto* window = qobject_cast<QWindow*>(target);
        QPoint dropPoint;
        if (item != nullptr) {
            window = item->window();
            const QPointF scenePoint = item->mapToScene(
                QPointF(item->width() / 2.0, item->height() / 2.0));
            dropPoint = QPoint(qRound(scenePoint.x()), qRound(scenePoint.y()));
        }
        if (window == nullptr || !window->isVisible()) {
            return false;
        }

        QStringList paths;
        paths.reserve(urls.size());
        qsizetype characterCount = 1; // Final multi-string terminator.
        for (const QUrl& url : urls) {
            if (!url.isLocalFile()) {
                continue;
            }
            const QString path = QDir::toNativeSeparators(url.toLocalFile());
            if (path.isEmpty()) {
                continue;
            }
            paths.append(path);
            characterCount += path.size() + 1;
        }
        if (paths.isEmpty()) {
            return false;
        }

        const SIZE_T byteCount = sizeof(DROPFILES)
            + static_cast<SIZE_T>(characterCount) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GHND, byteCount);
        if (memory == nullptr) {
            return false;
        }
        auto* payload = static_cast<unsigned char*>(GlobalLock(memory));
        if (payload == nullptr) {
            GlobalFree(memory);
            return false;
        }

        auto* header = reinterpret_cast<DROPFILES*>(payload);
        header->pFiles = sizeof(DROPFILES);
        header->pt.x = dropPoint.x();
        header->pt.y = dropPoint.y();
        header->fNC = FALSE;
        header->fWide = TRUE;
        auto* destination = reinterpret_cast<wchar_t*>(payload
                                                       + sizeof(DROPFILES));
        for (const QString& path : paths) {
            const std::wstring nativePath = path.toStdWString();
            std::copy(nativePath.cbegin(), nativePath.cend(), destination);
            destination += nativePath.size();
            *destination++ = L'\0';
        }
        *destination = L'\0';
        GlobalUnlock(memory);

        const HWND handle = reinterpret_cast<HWND>(window->winId());
        if (!PostMessageW(handle, WM_DROPFILES,
                          reinterpret_cast<WPARAM>(memory), 0)) {
            GlobalFree(memory);
            return false;
        }
        return true;
#else
        Q_UNUSED(target)
        Q_UNUSED(urls)
        return false;
#endif
    }

    Q_INVOKABLE bool supportsWindowsDropFiles() const
    {
#ifdef Q_OS_WIN
        return QGuiApplication::platformName().compare(
                   QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
#else
        return false;
#endif
    }

    Q_INVOKABLE QUrl copyForNativeDrop(const QUrl& source)
    {
        if (!source.isLocalFile() || !dropDirectory_.isValid()) {
            return {};
        }
        const QFileInfo sourceInfo(source.toLocalFile());
        if (!sourceInfo.isFile()) {
            return {};
        }
        const QString copyPath = dropDirectory_.filePath(
            QStringLiteral("native-drop-%1.%2")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces),
                     sourceInfo.suffix()));
        return QFile::copy(sourceInfo.absoluteFilePath(), copyPath)
            ? QUrl::fromLocalFile(copyPath) : QUrl{};
    }

    Q_INVOKABLE QUrl createDropDirectory()
    {
        if (!dropDirectory_.isValid()) return {};
        const QString path = dropDirectory_.filePath(
            QStringLiteral("resource-%1").arg(
                QUuid::createUuid().toString(QUuid::WithoutBraces)));
        return QDir().mkpath(path) ? QUrl::fromLocalFile(path) : QUrl{};
    }

    Q_INVOKABLE QUrl createNestedDropDirectory()
    {
        const QUrl root = createDropDirectory();
        if (!root.isLocalFile()) return {};
        return QDir().mkpath(QDir(root.toLocalFile()).filePath(
                   QStringLiteral("child")))
            ? root : QUrl{};
    }

    Q_INVOKABLE QUrl createAudioDropDirectory(const QUrl& source,
                                              int fileCount)
    {
        if (!source.isLocalFile() || fileCount <= 0
            || !dropDirectory_.isValid()) return {};
        const QFileInfo sourceInfo(source.toLocalFile());
        if (!sourceInfo.isFile()) return {};
        const QString path = dropDirectory_.filePath(
            QStringLiteral("audio-resource-%1").arg(
                QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().mkpath(path)) return {};
        for (int index = 0; index < fileCount; ++index) {
            const QString destination = QDir(path).filePath(
                QStringLiteral("track-%1.%2")
                    .arg(index, 2, 10, QLatin1Char('0'))
                    .arg(sourceInfo.suffix()));
            if (!QFile::copy(sourceInfo.absoluteFilePath(), destination))
                return {};
        }
        return QUrl::fromLocalFile(path);
    }

    Q_INVOKABLE QUrl createInvalidAudioDropDirectory()
    {
        const QUrl directory = createDropDirectory();
        if (!directory.isLocalFile()) return {};
        QFile file(QDir(directory.toLocalFile())
                       .filePath(QStringLiteral("invalid.wav")));
        return file.open(QIODevice::WriteOnly)
                && file.write("not an audio stream") > 0
            ? directory : QUrl{};
    }

    Q_INVOKABLE QUrl createNonAudioDropFile()
    {
        if (!dropDirectory_.isValid()) return {};
        const QString path = dropDirectory_.filePath(
            QStringLiteral("ignored-%1.txt").arg(
                QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("not audio") > 0
            ? QUrl::fromLocalFile(path) : QUrl{};
    }

    Q_INVOKABLE QUrl missingDropUrl() const
    {
        return dropDirectory_.isValid()
            ? QUrl::fromLocalFile(dropDirectory_.filePath(
                QStringLiteral("missing-%1.mp3").arg(
                    QUuid::createUuid().toString(QUuid::WithoutBraces))))
            : QUrl{};
    }

    Q_INVOKABLE bool pathExists(const QUrl& url) const
    {
        return url.isLocalFile() && QFileInfo::exists(url.toLocalFile());
    }

private:
    QPointer<LibraryModel> library_;
    QTemporaryDir dropDirectory_;
    std::unique_ptr<NativeDropRouter> listDrops_;
};

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
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("AgPlayer");
        QCoreApplication::setApplicationName("AgPlayer-test");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           settingsDirectory_.path());
        // Keep edit-session expectations independent of test order and avoid
        // touching the real Windows registry from an isolated UI test.
        QSettings().clear();

        if (ag_player_create(&core_) != AG_OK) {
            return;
        }
        library_ = std::make_unique<LibraryModel>();
        playlists_ = std::make_unique<PlaylistModel>(
            runtimeDataDirectory_.filePath(QStringLiteral("playlists.json")));
        playlists_->load();
        tagModel_ = std::make_unique<TagModel>(
            library_.get(),
            runtimeDataDirectory_.filePath(QStringLiteral("tags.json")));
        nativeDropHelper_.setLibraryModel(library_.get());
        playback_ = std::make_unique<PlaybackController>(core_, library_.get());
        videoPlayback_ = std::make_unique<VideoPlaybackController>(
            library_.get(), playback_.get());
        equalizer_ = std::make_unique<EqualizerController>(core_);
        importer_ = std::make_unique<ImportController>(library_.get());
        windows_ = std::make_unique<WindowController>();
        audioTools_ = std::make_unique<AudioToolsController>();
        metadataEditor_ = std::make_unique<MetadataEditor>();
        filenameProcessor_ = std::make_unique<FilenameProcessor>();
        filenameProcessor_->setLibraryModel(library_.get());
        formatConverter_ = std::make_unique<FormatConverter>();
        settings_ = std::make_unique<SettingsController>();
        windows_->setMainWindowShellMode(settings_->playerShellMode());
        connect(settings_.get(), &SettingsController::playerShellModeChanged,
                windows_.get(), [this] {
                    windows_->setMainWindowShellMode(settings_->playerShellMode());
                });
        lyricsProvider_ = std::make_unique<NoMatchLyricsProvider>();
        lyrics_ = std::make_unique<LyricsService>(
            library_.get(), playback_.get(), settings_.get(), lyricsProvider_.get());
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());
        thumbnailProvider_ = std::make_unique<TrackWaveformThumbnailProvider>(
            settings_->cacheDirectory());
        QObject::connect(
            settings_.get(), &SettingsController::cacheDirectoryChanged,
            thumbnailProvider_.get(), [this] {
                thumbnailProvider_->setCacheDirectory(
                    settings_->cacheDirectory());
            });
        resourceFolders_ = std::make_unique<ResourceFolderController>();
        resourceFolders_->setStoragePath(runtimeDataDirectory_.filePath(
            QStringLiteral("resource-roots.json")));
        resourceFolders_->setLibraryModel(library_.get());
        resourceFolders_->setImportController(importer_.get());
        libraryNavigation_ = std::make_unique<LibraryNavigationModel>(
            library_.get(), playlists_.get(), tagModel_.get(),
            resourceFolders_.get());

        register_agplayer_qml_types(library_.get(), playback_.get(),
                                    importer_.get(), windows_.get(),
                                    audioTools_.get(), metadataEditor_.get(),
                                    formatConverter_.get(), filenameProcessor_.get(),
                                    settings_.get(), waveformProvider_.get(),
                                    playlists_.get(), equalizer_.get(), nullptr,
                                    AgPlayerQmlRuntimeModels{
                                        tagModel_.get(),
                                        libraryNavigation_.get(),
                                        resourceFolders_.get(),
                                        thumbnailProvider_.get(),
                                        nullptr,
                                        videoPlayback_.get()},
                                    nullptr, nullptr, lyrics_.get());
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath("qrc:/");
        engine->rootContext()->setContextProperty(
            "visualFixtureOutput",
            QString::fromLocal8Bit(qgetenv("AGPLAYER_VISUAL_FIXTURE_OUTPUT")));
        const bool testTranslations = qEnvironmentVariableIsSet("AGPLAYER_TEST_TRANSLATIONS");
        engine->rootContext()->setContextProperty("testTranslationsEnabled", testTranslations);
        if (testTranslations) {
            translations_ = std::make_unique<TranslationManager>();
            if (!translations_->setLanguage(settings_->language()))
                qFatal("Unable to load test translation catalog");
            connect(settings_.get(), &SettingsController::languageChanged, engine, [this] {
                if (!translations_->setLanguage(settings_->language()))
                    qFatal("Unable to switch test translation catalog");
            });
            connect(translations_.get(), &TranslationManager::languageChanged,
                    engine, &QQmlEngine::retranslate);
        }
        const QString fixture =
            QString::fromLocal8Bit(qgetenv("AGPLAYER_TEST_AUDIO"));
        engine->rootContext()->setContextProperty(
            "testAudioUrl", QUrl::fromLocalFile(fixture));
        engine->rootContext()->setContextProperty("nativeDropHelper",
                                                  &nativeDropHelper_);
        engine->rootContext()->setContextProperty(
            "expectedTagModel", tagModel_.get());
        engine->rootContext()->setContextProperty(
            "expectedLibraryNavigationModel", libraryNavigation_.get());
        engine->rootContext()->setContextProperty(
            "expectedResourceFolderController", resourceFolders_.get());
        engine->rootContext()->setContextProperty(
            "expectedThumbnailProvider", thumbnailProvider_.get());

        component_ = std::make_unique<QQmlComponent>(engine);
        component_->loadFromModule("AgPlayer", "Main");
        if (component_->isError()) {
            qWarning().noquote() << component_->errorString();
            return;
        }

        mainWindow_ = component_->create();
        if (mainWindow_ == nullptr) {
            qWarning().noquote() << component_->errorString();
        }
        if (mainWindow_) {
            auto* nativeWindow = qobject_cast<QWindow*>(mainWindow_);
            if (nativeWindow != nullptr) {
                nativeDrops_ = std::make_unique<NativeDropRouter>();
                nativeDrops_->registerWindow(nativeWindow,
                                             NativeDropRouter::Target::Main);
                const QPointer<QObject> mainDropTarget = mainWindow_;
                nativeDrops_->registerHitTarget(
                    nativeWindow, NativeDropRouter::Target::ResourceFolder,
                    [mainDropTarget](const QPointF& position) {
                        if (mainDropTarget == nullptr) return false;
                        bool hit = false;
                        return QMetaObject::invokeMethod(
                                   mainDropTarget, "resourceDropContainsPoint",
                                   Q_RETURN_ARG(bool, hit),
                                   Q_ARG(QVariant, position.x()),
                                   Q_ARG(QVariant, position.y()))
                            && hit;
                    });
                QObject::connect(
                    nativeDrops_.get(), &NativeDropRouter::pathsDropped,
                    mainWindow_, [this](NativeDropRouter::Target target,
                                        const QStringList& paths) {
                        if (target == NativeDropRouter::Target::Main
                            || target == NativeDropRouter::Target::ResourceFolder) {
                            QList<QUrl> urls;
                            urls.reserve(paths.size());
                            for (const QString& path : paths)
                                urls.append(QUrl::fromLocalFile(path));
                            bool accepted = false;
                            const bool invoked = QMetaObject::invokeMethod(
                                mainWindow_, target == NativeDropRouter::Target::Main
                                    ? "handleShellDropUrls" : "handleResourceDropUrls",
                                Qt::DirectConnection,
                                Q_RETURN_ARG(bool, accepted),
                                Q_ARG(QVariant, QVariant::fromValue(urls)));
                            if (!invoked)
                                qWarning() << "Unable to invoke classified test drop";
                            Q_UNUSED(accepted)
                        }
                    });
            }
            engine->rootContext()->setContextProperty("testMainWindow", mainWindow_);
        }
    }

private:
    QTemporaryDir settingsDirectory_;
    ag_player* core_ = nullptr;
    QTemporaryDir runtimeDataDirectory_;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<PlaylistModel> playlists_;
    std::unique_ptr<TagModel> tagModel_;
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<VideoPlaybackController> videoPlayback_;
    std::unique_ptr<EqualizerController> equalizer_;
    std::unique_ptr<ImportController> importer_;
    std::unique_ptr<WindowController> windows_;
    std::unique_ptr<AudioToolsController> audioTools_;
    std::unique_ptr<MetadataEditor> metadataEditor_;
    std::unique_ptr<FilenameProcessor> filenameProcessor_;
    std::unique_ptr<FormatConverter> formatConverter_;
    std::unique_ptr<SettingsController> settings_;
    std::unique_ptr<TranslationManager> translations_;
    std::unique_ptr<NoMatchLyricsProvider> lyricsProvider_;
    std::unique_ptr<LyricsService> lyrics_;
    std::unique_ptr<WaveformProvider> waveformProvider_;
    std::unique_ptr<TrackWaveformThumbnailProvider> thumbnailProvider_;
    std::unique_ptr<ResourceFolderController> resourceFolders_;
    std::unique_ptr<LibraryNavigationModel> libraryNavigation_;
    std::unique_ptr<NativeDropRouter> nativeDrops_;
    std::unique_ptr<QQmlComponent> component_;
    QObject* mainWindow_ = nullptr;
    NativeDropHelper nativeDropHelper_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_main_window, QmlMainWindowSetup)

#include "qml_main_window_test_main.moc"
