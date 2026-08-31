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

#include "../core/bpm_fixture.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QAbstractTableModel>
#include <QDragEnterEvent>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeData>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QThread>
#include <QTest>
#include <QUuid>
#include <QtPlugin>
#include <QtQuickTest/quicktest.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <memory>

namespace {

bool waitForConverterIdle(FormatConverter& converter, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (converter.busy() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    return !converter.busy();
}

int runFormatConversionEvidence(const QString& rootPath)
{
    const QDir root(rootPath);
    if (!QDir().mkpath(root.absolutePath())) {
        qCritical("Unable to create conversion-evidence directory");
        return 2;
    }
    const QString input = root.filePath(QStringLiteral("short-reference.wav"));
    if (!agplayer::test::writeClickTrackWav(input, 120, 2)) {
        qCritical("Unable to generate short WAV input");
        return 3;
    }

    QVariantMap results;
    const auto convert = [&](const QString& format, int bitRate,
                             int sampleRate) -> bool {
        const QString outputDir = root.filePath(format);
        if (!QDir().mkpath(outputDir)) return false;

        FormatConverter converter;
        converter.loadFiles({QUrl::fromLocalFile(input)});
        if (!waitForConverterIdle(converter, 5'000) || converter.fileCount() != 1) {
            return false;
        }

        const QVariantMap request{
            {QStringLiteral("outputFormat"), format},
            {QStringLiteral("outputDir"), outputDir},
            {QStringLiteral("bitRate"), bitRate},
            {QStringLiteral("sampleRate"), sampleRate},
            {QStringLiteral("channels"), 2},
            {QStringLiteral("bitrateMode"), QStringLiteral("cbr")},
            {QStringLiteral("conflictPolicy"), QStringLiteral("auto-number")},
            {QStringLiteral("keepMetadata"), true},
            {QStringLiteral("keepCover"), false},
            {QStringLiteral("preserveDirectories"), false},
            {QStringLiteral("extractAudio"), false}};
        const QVariantMap plan = converter.buildPreflight(request);
        if (!plan.value(QStringLiteral("ready")).toBool()) {
            qCritical().noquote() << format << "preflight failed:"
                                  << plan.value(QStringLiteral("error")).toString();
            return false;
        }

        QSignalSpy completed(&converter, &FormatConverter::transcodeCompleted);
        converter.confirmPendingPlan();
        if (completed.isEmpty() && !completed.wait(30'000)) {
            qCritical().noquote() << format << "conversion timed out";
            return false;
        }
        if (!waitForConverterIdle(converter, 5'000) || converter.failedCount() != 0) {
            qCritical().noquote() << format << "conversion reported failures";
            return false;
        }
        const QVariantMap row = converter.files().value(0).toMap();
        const QString outputPath = row.value(QStringLiteral("outputPath")).toString();
        if (row.value(QStringLiteral("status")).toString() != QStringLiteral("Done")
            || !QFileInfo::exists(outputPath) || QFileInfo(outputPath).size() <= 0) {
            qCritical().noquote() << format << "did not publish a completed output";
            return false;
        }

        ag_metadata* metadata = nullptr;
        if (ag_metadata_open(outputPath.toUtf8().constData(), &metadata) != AG_OK
            || metadata == nullptr) {
            qCritical().noquote() << format << "output could not be reopened";
            return false;
        }
        const qint64 durationMs = ag_metadata_duration_ms(metadata);
        const int decodedSampleRate = ag_metadata_sample_rate(metadata);
        ag_metadata_destroy(metadata);
        if (durationMs <= 0 || decodedSampleRate <= 0) {
            qCritical().noquote() << format << "output probe/decode data was invalid";
            return false;
        }

        QVariantMap result;
        result.insert(QStringLiteral("request"), request);
        result.insert(QStringLiteral("plan"), plan);
        result.insert(QStringLiteral("row"), row);
        result.insert(QStringLiteral("outputBytes"), QFileInfo(outputPath).size());
        result.insert(QStringLiteral("probe"), QVariantMap{
            {QStringLiteral("durationMs"), durationMs},
            {QStringLiteral("sampleRate"), decodedSampleRate}});
        results.insert(format, result);
        return true;
    };

    if (!convert(QStringLiteral("flac"), 0, 44'100)
        || !convert(QStringLiteral("mp3"), 192'000, 44'100)) {
        return 4;
    }

    QFile report(root.filePath(QStringLiteral("conversion-evidence.json")));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCritical("Unable to write conversion-evidence.json");
        return 5;
    }
    report.write(QJsonDocument::fromVariant(results).toJson(QJsonDocument::Indented));
    report.close();
    return 0;
}

} // namespace

class VisualFormatTaskModel final : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY statusFilterChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
public:
    struct Row {
        QString taskId;
        QString fileName;
        QString sourceFormat;
        int durationMs;
        int sampleRate;
        int bitRate;
        QString outputFormat;
        QString status;
        double progress;
        QString errorDetail;
    };

    explicit VisualFormatTaskModel(QObject* parent = nullptr)
        : QAbstractTableModel(parent)
        , rows_({
              {"fixture-01", "Neon City.flac", "FLAC", 265000, 44100, 1054000, "MP3", "Converting", 0.68, {}},
              {"fixture-02", QString::fromUtf8("梦边晚风.wav"), "WAV", 230000, 48000, 1536000, "FLAC", "Converting", 0.42, {}},
              {"fixture-03", "Night Drive.mp3", "MP3", 199000, 44100, 320000, "MP3", "Done", 1.0, {}},
              {"fixture-04", QString::fromUtf8("旅程开始.m4a"), "AAC / M4A", 312000, 44100, 256000, "AAC", "Done", 1.0, {}},
              {"fixture-05", "Tokyo Lights.opus", "Opus", 241000, 48000, 192000, "OPUS", "Converting", 0.12, {}},
              {"fixture-06", "Live Mix 01.aac", "AAC", 405000, 44100, 256000, "MP3", "Ready", 0.0, {}},
              {"fixture-07", QString::fromUtf8("深夜电台.ogg"), "OGG", 178000, 44100, 160000, "OGG", "Ready", 0.0, {}},
              {"fixture-08", QString::fromUtf8("步履城市.flac"), "FLAC", 217000, 96000, 2304000, "FLAC", "Ready", 0.0, {}},
              {"fixture-09", QString::fromUtf8("回忆片段.wav"), "WAV", 288000, 44100, 1411000, "WAV", "Cancelled", 0.0, {}},
              {"fixture-10", "Lost In Tokyo.mp3", "MP3", 225000, 44100, 320000, "MP3", "Error", 0.0, QString::fromUtf8("测试专用失败状态")},
              {"fixture-11", QString::fromUtf8("雨后晴空.m4a"), "AAC / M4A", 123000, 48000, 256000, "AAC", "Ready", 0.0, {}},
              {"fixture-12", "Sunset Boulevard.flac", "FLAC", 273000, 44100, 1141000, "MP3", "Ready", 0.0, {}}
          })
    {
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 9;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
        const Row& row = rows_.at(index.row());
        switch (role) {
        case Qt::UserRole + 1: return row.taskId;
        case Qt::UserRole + 2: return row.fileName;
        case Qt::UserRole + 3: return row.sourceFormat;
        case Qt::UserRole + 4: return row.durationMs;
        case Qt::UserRole + 5: return row.sampleRate;
        case Qt::UserRole + 6: return row.bitRate;
        case Qt::UserRole + 7: return row.outputFormat;
        case Qt::UserRole + 8: return row.status;
        case Qt::UserRole + 9: return row.progress;
        case Qt::UserRole + 10: return row.errorDetail;
        case Qt::UserRole + 11: return true;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {Qt::UserRole + 1, "taskId"}, {Qt::UserRole + 2, "fileName"},
            {Qt::UserRole + 3, "sourceFormat"}, {Qt::UserRole + 4, "durationMs"},
            {Qt::UserRole + 5, "sampleRate"}, {Qt::UserRole + 6, "bitRate"},
            {Qt::UserRole + 7, "outputFormat"}, {Qt::UserRole + 8, "status"},
            {Qt::UserRole + 9, "progress"}, {Qt::UserRole + 10, "errorDetail"},
            {Qt::UserRole + 11, "checked"}
        };
    }

    QString statusFilter() const { return statusFilter_; }
    void setStatusFilter(const QString& value)
    {
        if (statusFilter_ == value) return;
        statusFilter_ = value;
        emit statusFilterChanged();
    }
    QString query() const { return query_; }
    void setQuery(const QString& value)
    {
        if (query_ == value) return;
        query_ = value;
        emit queryChanged();
    }

signals:
    void statusFilterChanged();
    void queryChanged();

private:
    QList<Row> rows_;
    QString statusFilter_ = QStringLiteral("All");
    QString query_;
};

Q_IMPORT_PLUGIN(AgPlayerPlugin)

class NativeDropHelper final : public QObject {
    Q_OBJECT
public:
    ~NativeDropHelper() override { unlockFiles(); }

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
            case 0: editor_->openDroppedUrls(urls); break;
            case 1: format_->loadFiles(urls); break;
            case 2: metadata_->loadFiles(urls); break;
            case 3: filenames_->loadFiles(urls); break;
            case 4: separation_->dropInput(urls); break;
            default: return;
            }
            delivered_ = true;
        });
    }

    void clearBindings()
    {
        tools_ = nullptr;
        format_ = nullptr;
        editor_ = nullptr;
        metadata_ = nullptr;
        filenames_ = nullptr;
    }

    Q_INVOKABLE bool prepareItem(QObject* target) const
    {
        QWindow* window = windowForTarget(target);
        if (window == nullptr) return false;
        if (!window->isVisible()) window->show();
        if (!QTest::qWaitForWindowExposed(window, 2'000)) return false;

        if (!window->isActive()) {
            window->raise();
            window->requestActivate();
#ifdef Q_OS_WIN
            const HWND handle = reinterpret_cast<HWND>(window->winId());
            if (handle != nullptr) {
                ShowWindow(handle, SW_RESTORE);
                BringWindowToTop(handle);
                SetForegroundWindow(handle);
                SetActiveWindow(handle);
            }
#endif
        }
        return QTest::qWaitForWindowActive(window, 2'000)
            && window->isExposed();
    }

    Q_INVOKABLE bool destroyItem(QObject* target) const
    {
        if (target == nullptr) return true;
        QPointer<QObject> guard(target);
        target->deleteLater();
        QElapsedTimer timer;
        timer.start();
        while (!guard.isNull() && timer.elapsed() < 1'000) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
        return guard.isNull();
    }

    Q_INVOKABLE bool sendUrls(QObject* target, const QList<QUrl>& urls)
    {
        if (target == nullptr || urls.isEmpty()) {
            return false;
        }
        if (!prepareItem(target)) {
            return false;
        }
        QWindow* window = windowForTarget(target);
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
            + QStringLiteral("/agplayer-audio-tools-ui-%1-%2.%3")
                .arg(QCoreApplication::applicationPid())
                .arg(QUuid::createUuid().toString(QUuid::Id128))
                .arg(QFileInfo(source).suffix());
        return QFile::copy(source, destination)
            ? QUrl::fromLocalFile(destination) : QUrl{};
    }

    Q_INVOKABLE QUrl copyForNativeDropWithFileName(const QUrl& sourceUrl,
                                                   const QString& fileName)
    {
        const QString source = sourceUrl.toLocalFile();
        if (source.isEmpty() || !QFile::exists(source) || fileName.isEmpty()) {
            return {};
        }
        const QString directory = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation)
            + QStringLiteral("/AgPlayer-元数据-%1-%2")
                  .arg(QCoreApplication::applicationPid())
                  .arg(QUuid::createUuid().toString(QUuid::Id128));
        if (!QDir().mkpath(directory)) return {};
        const QString destination = QDir(directory).filePath(
            QFileInfo(fileName).fileName());
        return QFile::copy(source, destination)
            ? QUrl::fromLocalFile(destination) : QUrl{};
    }

    Q_INVOKABLE QVariantMap probeMedia(const QString& path) const
    {
        QVariantMap result{{QStringLiteral("readable"), false},
                           {QStringLiteral("durationMs"), 0},
                           {QStringLiteral("sampleRate"), 0}};
        ag_metadata* metadata = nullptr;
        if (ag_metadata_open(path.toUtf8().constData(), &metadata) != AG_OK
            || metadata == nullptr) {
            return result;
        }
        result.insert(QStringLiteral("readable"), true);
        result.insert(QStringLiteral("durationMs"),
                      ag_metadata_duration_ms(metadata));
        result.insert(QStringLiteral("sampleRate"),
                      ag_metadata_sample_rate(metadata));
        ag_metadata_destroy(metadata);
        return result;
    }

    Q_INVOKABLE QString probeMetadataTitle(const QUrl& url) const
    {
        const QString path = url.toLocalFile();
        ag_metadata* metadata = nullptr;
        if (path.isEmpty()
            || ag_metadata_open(path.toUtf8().constData(), &metadata) != AG_OK
            || metadata == nullptr) {
            return {};
        }
        const QString title = QString::fromUtf8(ag_metadata_title(metadata));
        ag_metadata_destroy(metadata);
        return title;
    }

    Q_INVOKABLE QString probeMetadataText(const QUrl& url,
                                          const QString& field) const
    {
        const QString path = url.toLocalFile();
        ag_metadata* metadata = nullptr;
        if (path.isEmpty()
            || ag_metadata_open(path.toUtf8().constData(), &metadata) != AG_OK
            || metadata == nullptr) {
            return {};
        }
        const char* value = nullptr;
        if (field == QLatin1String("title")) value = ag_metadata_title(metadata);
        else if (field == QLatin1String("artist")) value = ag_metadata_artist(metadata);
        else if (field == QLatin1String("album")) value = ag_metadata_album(metadata);
        else if (field == QLatin1String("customTag")) value = ag_metadata_custom_tag(metadata);
        const QString decoded = QString::fromUtf8(value == nullptr ? "" : value);
        ag_metadata_destroy(metadata);
        return decoded;
    }

    Q_INVOKABLE bool lockFileExclusive(const QUrl& url)
    {
#ifdef Q_OS_WIN
        const QString path = url.toLocalFile();
        if (path.isEmpty()) return false;
        HANDLE handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        lockedFiles_.append(handle);
        return true;
#else
        Q_UNUSED(url);
        return false;
#endif
    }

    Q_INVOKABLE void unlockFiles()
    {
#ifdef Q_OS_WIN
        for (HANDLE handle : std::as_const(lockedFiles_)) {
            CloseHandle(handle);
        }
        lockedFiles_.clear();
#endif
    }

    Q_INVOKABLE bool dragItem(QObject* target, qreal x, qreal y,
                              qreal deltaX, qreal deltaY)
    {
        return dragItemWithModifiers(target, x, y, deltaX, deltaY,
                                     static_cast<int>(Qt::NoModifier));
    }

    Q_INVOKABLE bool keyClickItem(QObject* target, int key, int modifiers)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QQuickWindow* window = item == nullptr ? nullptr : item->window();
        if (window == nullptr || !prepareItem(target)) return false;
        QTest::keyClick(window, static_cast<Qt::Key>(key),
                        Qt::KeyboardModifiers(modifiers));
        return true;
    }

    Q_INVOKABLE bool doubleClickItem(QObject* target, qreal x, qreal y)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QQuickWindow* window = item == nullptr ? nullptr : item->window();
        if (window == nullptr || !prepareItem(target)) return false;
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier,
                           item->mapToScene(QPointF(x, y)).toPoint(), 20);
        return true;
    }

    Q_INVOKABLE bool doubleClickItemWithButton(QObject* target, qreal x,
                                               qreal y, int button)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QQuickWindow* window = item == nullptr ? nullptr : item->window();
        if (window == nullptr || !prepareItem(target)) return false;
        QTest::mouseDClick(window, static_cast<Qt::MouseButton>(button),
                           Qt::NoModifier,
                           item->mapToScene(QPointF(x, y)).toPoint(), 20);
        return true;
    }

    Q_INVOKABLE bool clickItem(QObject* target, qreal x, qreal y, int button)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QQuickWindow* window = item == nullptr ? nullptr : item->window();
        if (window == nullptr || !prepareItem(target)) return false;
        QTest::mouseClick(window, static_cast<Qt::MouseButton>(button),
                          Qt::NoModifier,
                          item->mapToScene(QPointF(x, y)).toPoint(), 20);
        return true;
    }

    Q_INVOKABLE bool dragItemWithModifiers(QObject* target, qreal x, qreal y,
                                           qreal deltaX, qreal deltaY,
                                           int modifiers)
    {
        auto* item = qobject_cast<QQuickItem*>(target);
        QQuickWindow* window = item == nullptr ? nullptr : item->window();
        if (window == nullptr || !prepareItem(target)) {
            return false;
        }
        const QPoint start = item->mapToScene(QPointF(x, y)).toPoint();
        const QPoint end = start + QPoint(qRound(deltaX), qRound(deltaY));
        const auto keyboardModifiers = Qt::KeyboardModifiers(modifiers);
        const bool controlHeld = keyboardModifiers.testFlag(Qt::ControlModifier);
        if (controlHeld) QTest::keyPress(window, Qt::Key_Control);
        QTest::mousePress(window, Qt::LeftButton, keyboardModifiers,
                          start, 20);
        constexpr int steps = 6;
        for (int step = 1; step <= steps; ++step) {
            const QPoint point = start + (end - start) * step / steps;
            QTest::mouseMove(window, point, 10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, keyboardModifiers,
                            end, 20);
        if (controlHeld) QTest::keyRelease(window, Qt::Key_Control);
        return true;
    }

private:
    static QWindow* windowForTarget(QObject* target)
    {
        QWindow* window = qobject_cast<QWindow*>(target);
        if (window != nullptr) return window;
        const auto* item = qobject_cast<QQuickItem*>(target);
        return item == nullptr ? nullptr : item->window();
    }

    NativeDropRouter router_;
    AudioToolsController* tools_ = nullptr;
    FormatConverter* format_ = nullptr;
    AudioEditorController* editor_ = nullptr;
    MetadataEditor* metadata_ = nullptr;
    FilenameProcessor* filenames_ = nullptr;
    VocalSeparationController* separation_ = nullptr;
    bool delivered_ = false;
#ifdef Q_OS_WIN
    QList<HANDLE> lockedFiles_;
#endif
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
        nativeDropHelper_.clearBindings();
        // Test controllers own workers and several of them retain the player
        // handle.  Destroy them before the C core so parallel/serial QML test
        // processes cannot race their teardown against an already freed core.
        visualFormatTaskModel_.reset();
        waveformProvider_.reset();
        settings_.reset();
        audioEditor_.reset();
        formatConverter_.reset();
        filenameProcessor_.reset();
        metadataEditor_.reset();
        audioTools_.reset();
        windows_.reset();
        importer_.reset();
        playback_.reset();
        library_.reset();
        if (core_ != nullptr) {
            ag_player_destroy(core_);
            core_ = nullptr;
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
        audioEditor_ = std::make_unique<AudioEditorController>();
        audioEditor_->setPlaybackController(playback_.get());
        settings_ = std::make_unique<SettingsController>();
        waveformProvider_ = std::make_unique<WaveformProvider>(settings_.get());
        visualFormatTaskModel_ = std::make_unique<VisualFormatTaskModel>();
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
                                     AgPlayerQmlRuntimeModels{},
                                     nullptr, nullptr, nullptr,
                                     audioPreview_.get(),
                                     vocalSeparation_.get());
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
        engine->rootContext()->setContextProperty(
            "visualFixtureOutput",
            QString::fromLocal8Bit(qgetenv("AGPLAYER_VISUAL_FIXTURE_OUTPUT")));
        engine->rootContext()->setContextProperty(
            "visualFormatTaskModel", visualFormatTaskModel_.get());
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
    std::unique_ptr<VisualFormatTaskModel> visualFormatTaskModel_;
    std::unique_ptr<PlaylistModel> playlists_;
    std::unique_ptr<AudioPreviewController> audioPreview_;
    std::unique_ptr<VocalSeparationController> vocalSeparation_;
    NativeDropHelper nativeDropHelper_;
    VocalSeparationControllerTestDriver separationTestDriver_;
};

int main(int argc, char* argv[])
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index])
                == QStringLiteral("--format-conversion-evidence")) {
            QCoreApplication application(argc, argv);
            return runFormatConversionEvidence(
                QString::fromLocal8Bit(argv[index + 1]));
        }
    }

    QTEST_SET_MAIN_SOURCE_PATH
    QmlAudioToolsSetup setup;
    return quick_test_main_with_setup(argc, argv, "qml_audio_tools",
                                      QUICK_TEST_SOURCE_DIR, &setup);
}

#include "qml_audio_tools_test_main.moc"
