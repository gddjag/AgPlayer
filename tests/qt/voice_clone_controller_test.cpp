#include "voice_clone_controller.hpp"
#include "voice_clone_package_manager.hpp"
#include "voice_clone_runtime_package_manager.hpp"
#include "voice_clone_worker_protocol.hpp"
#include "voice_clone_host_controller.hpp"
#include "audio_tools_controller.hpp"
#include "audio_preview_controller.hpp"
#include "audio_editor/audio_editor_controller.hpp"
#include "settings_controller.hpp"
#include "waveform_item.hpp"
#include "waveform_provider.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QPointer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QtPlugin>
#include <qqml.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace agplayer::voice_clone;

Q_IMPORT_PLUGIN(AgPlayerPlugin)

namespace {

class ModelDownloadServer final : public QTcpServer {
public:
    explicit ModelDownloadServer(QByteArray body, int bodyDelayMs = 0,
                                 QObject* parent = nullptr)
        : QTcpServer(parent), body_(std::move(body)), bodyDelayMs_(bodyDelayMs)
    {
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket] {
                    const QByteArray request = socket->readAll();
                    if (!request.contains("\r\n\r\n")) return;
                    if (socket->property("counted").toBool()) return;
                    socket->setProperty("counted", true);
                    const bool head = request.startsWith("HEAD ");
                    ++requestCount_;
                    if (head) ++headCount_;
                    else ++getCount_;
                    QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Length: ";
                    headers += QByteArray::number(body_.size());
                    headers += "\r\nConnection: close\r\n\r\n";
                    socket->write(headers);
                    if (head) {
                        socket->disconnectFromHost();
                    } else if (bodyDelayMs_ > 0) {
                        const QPointer<QTcpSocket> guarded(socket);
                        QTimer::singleShot(bodyDelayMs_, this, [this, guarded] {
                            if (guarded == nullptr) return;
                            guarded->write(body_);
                            guarded->disconnectFromHost();
                        });
                    } else {
                        socket->write(body_);
                        socket->disconnectFromHost();
                    }
                });
            }
        });
        QVERIFY(listen(QHostAddress::LocalHost));
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/config.json").arg(serverPort()));
    }
    int requestCount() const { return requestCount_; }
    int headCount() const { return headCount_; }
    int getCount() const { return getCount_; }

private:
    QByteArray body_;
    int bodyDelayMs_ = 0;
    int requestCount_ = 0;
    int headCount_ = 0;
    int getCount_ = 0;
};

QJsonObject liveSchema()
{
    auto control = [](const QString& key, const QString& group, const QString& type,
                      const QJsonValue& defaultValue) {
        return QJsonObject{{QStringLiteral("key"), key},
                           {QStringLiteral("label"), key},
                           {QStringLiteral("description"), key + QStringLiteral(" parameter")},
                           {QStringLiteral("group"), group},
                           {QStringLiteral("type"), type},
                           {QStringLiteral("default"), defaultValue},
                           {QStringLiteral("required"), false}};
    };
    QJsonObject seed = control(QStringLiteral("seed"), QStringLiteral("basic"),
                               QStringLiteral("int"), 1);
    seed.insert(QStringLiteral("minimum"), 0);
    seed.insert(QStringLiteral("maximum"), 99);
    QJsonObject mode = control(QStringLiteral("mode"), QStringLiteral("basic"),
                               QStringLiteral("enum"), QStringLiteral("fast"));
    mode.insert(QStringLiteral("options"), QJsonArray{QStringLiteral("fast"), QStringLiteral("hq")});
    QJsonObject style = control(QStringLiteral("style"), QStringLiteral("advanced"),
                                QStringLiteral("string"), QStringLiteral(""));
    style.insert(QStringLiteral("visibleWhen"),
                 QJsonObject{{QStringLiteral("key"), QStringLiteral("mode")},
                             {QStringLiteral("equals"), QStringLiteral("hq")}});
    return {{QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("groups"),
             QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("basic")},
                                    {QStringLiteral("label"), QStringLiteral("Basic")}},
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("advanced")},
                                    {QStringLiteral("label"), QStringLiteral("Advanced")}}}},
            {QStringLiteral("parameters"),
             QJsonArray{seed, mode, style}}};
}

VoiceCloneWorkerMessage responseFor(const VoiceCloneWorkerMessage& request)
{
    VoiceCloneWorkerMessage response;
    response.kind = WorkerMessageKind::Response;
    response.operation = request.operation;
    response.requestId = request.requestId;
    response.adapterId = request.adapterId;
    response.adapterVersion = request.adapterVersion;
    response.protocolVersion = request.protocolVersion;
    switch (request.operation) {
    case WorkerOperation::Hello:
        response.payload = {{QStringLiteral("workerVersion"), QStringLiteral("test-1")}};
        break;
    case WorkerOperation::Capabilities:
        response.payload = {{QStringLiteral("schema"), liveSchema()}};
        break;
    case WorkerOperation::Load:
        response.payload = {{QStringLiteral("loaded"), true}};
        break;
    case WorkerOperation::Generate:
        response.payload = {{QStringLiteral("outputPath"),
                             request.payload.value(QStringLiteral("outputPath"))}};
        break;
    default: break;
    }
    return response;
}

int runWorker(const QStringList& arguments)
{
    const auto valueAfter = [&arguments](const QString& option) {
        const int index = arguments.indexOf(option);
        return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString{};
    };
    const QString socketName = valueAfter(QStringLiteral("--socket"));
    const QString outputRoot = valueAfter(QStringLiteral("--output-root"));
    const QString modelRoot = valueAfter(QStringLiteral("--model-root"));
    QLocalSocket socket;
    socket.connectToServer(socketName);
    if (!socket.waitForConnected(5000)) return 90;

    QByteArray input;
    QString delayed;
    QString cancelFailureTarget;
    VoiceCloneWorkerMessage delayedLoad;
    QObject::connect(&socket, &QLocalSocket::readyRead, &socket, [&] {
        input += socket.readAll();
        while (true) {
            const qsizetype newline = input.indexOf('\n');
            if (newline < 0) break;
            const QByteArray frame = input.left(newline);
            input.remove(0, newline + 1);
            const auto decoded = decodeWorkerMessage(frame, outputRoot);
            if (!decoded.isValid()) {
                QCoreApplication::exit(91);
                return;
            }
            const auto& request = decoded.message;
            const QString text = request.payload.value(QStringLiteral("text")).toString();
            if (request.operation == WorkerOperation::Shutdown) {
                QFile marker(QDir(outputRoot).filePath(QStringLiteral("shutdown.marker")));
                if (marker.open(QIODevice::WriteOnly)) marker.write("shutdown");
                socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
                socket.flush();
                QCoreApplication::exit(0);
                return;
            }
            if (request.operation == WorkerOperation::Load
                && QFileInfo(request.payload.value(QStringLiteral("modelRoot")).toString()).fileName()
                       == QStringLiteral("slow-load")) {
                delayedLoad = request;
                continue;
            }
            if (request.operation == WorkerOperation::Load && !delayedLoad.requestId.isEmpty()) {
                socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
                socket.write(encodeWorkerMessage(responseFor(delayedLoad)) + '\n');
                delayedLoad = {};
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Load
                && QFileInfo(modelRoot).fileName() == QStringLiteral("reject-load")) {
                VoiceCloneWorkerMessage error = responseFor(request);
                error.kind = WorkerMessageKind::Error;
                error.workerError = {QStringLiteral("model-invalid"),
                                     QStringLiteral("adapter rejected local model"), false, {}};
                socket.write(encodeWorkerMessage(error) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Load) {
                const QJsonObject parameters = request.payload.value(
                    QStringLiteral("parameters")).toObject();
                QFile marker(QDir(outputRoot).filePath(QStringLiteral("load-parameters.json")));
                if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QCoreApplication::exit(95);
                    return;
                }
                marker.write(QJsonDocument(parameters).toJson(QJsonDocument::Compact));
                marker.close();
                if (parameters.isEmpty()) {
                    VoiceCloneWorkerMessage error = responseFor(request);
                    error.kind = WorkerMessageKind::Error;
                    error.workerError = {QStringLiteral("missing-load-parameters"),
                                         QStringLiteral("load parameters are empty"), false, {}};
                    socket.write(encodeWorkerMessage(error) + '\n');
                    socket.flush();
                    continue;
                }
            }
            if (request.operation == WorkerOperation::Generate && text == QStringLiteral("crash")) {
                QCoreApplication::exit(23);
                return;
            }
            if (request.operation == WorkerOperation::Generate && text == QStringLiteral("hang")) continue;
            if (request.operation == WorkerOperation::Cancel
                && request.payload.value(QStringLiteral("targetRequestId")).toString()
                       == cancelFailureTarget) {
                VoiceCloneWorkerMessage error = responseFor(request);
                error.kind = WorkerMessageKind::Error;
                error.workerError = {QStringLiteral("cancel-failed"),
                                     QStringLiteral("worker rejected cancellation"), false, {}};
                socket.write(encodeWorkerMessage(error) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate && text == QStringLiteral("oom")) {
                VoiceCloneWorkerMessage error = responseFor(request);
                error.kind = WorkerMessageKind::Error;
                error.workerError = {QStringLiteral("out-of-memory"), QStringLiteral("worker OOM"), true, {}};
                socket.write(encodeWorkerMessage(error) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate
                && text == QStringLiteral("bad-identity")) {
                VoiceCloneWorkerMessage bad = responseFor(request);
                bad.adapterId = QStringLiteral("other-adapter");
                socket.write(encodeWorkerMessage(bad) + '\n');
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate
                && text == QStringLiteral("oversize-frame")) {
                socket.write(QByteArray(1024 * 1024 + 1, 'x'));
                socket.flush();
                continue;
            }
            if (request.operation == WorkerOperation::Generate) {
                const QString relative = request.payload.value(QStringLiteral("outputPath")).toString();
                const QString path = QDir(outputRoot).filePath(relative);
                if (text == QStringLiteral("symlink-part")) {
                    const QString target = QDir(outputRoot).filePath(QStringLiteral("symlink-target.wav"));
                    QFile targetFile(target);
                    if (!targetFile.open(QIODevice::WriteOnly) || targetFile.write("RIFF-link") < 0) {
                        QCoreApplication::exit(93);
                        return;
                    }
                    targetFile.close();
#ifdef Q_OS_WIN
                    bool linked = CreateSymbolicLinkW(
                        QDir::toNativeSeparators(path).toStdWString().c_str(),
                        QDir::toNativeSeparators(target).toStdWString().c_str(), 0) != 0;
                    if (!linked) {
                        const QString targetDirectory = QDir(outputRoot).filePath(
                            QStringLiteral("symlink-target-directory"));
                        QDir().mkpath(targetDirectory);
                        linked = QProcess::execute(QStringLiteral("cmd.exe"),
                                                   {QStringLiteral("/d"), QStringLiteral("/c"),
                                                    QStringLiteral("mklink"), QStringLiteral("/J"),
                                                    QDir::toNativeSeparators(path),
                                                    QDir::toNativeSeparators(targetDirectory)}) == 0;
                    }
#else
                    const bool linked = QFile::link(target, path);
#endif
                    if (!linked) {
                        VoiceCloneWorkerMessage error = responseFor(request);
                        error.kind = WorkerMessageKind::Error;
                        error.workerError = {QStringLiteral("symlink-unavailable"),
                                             QStringLiteral("symlink unavailable"), false, {}};
                        socket.write(encodeWorkerMessage(error) + '\n');
                    } else {
                        socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
                    }
                    socket.flush();
                    continue;
                }
#ifdef Q_OS_WIN
                if (text == QStringLiteral("hold-cancel")) {
                    const HANDLE held = CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(),
                                                    GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                                    FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (held == INVALID_HANDLE_VALUE) {
                        QCoreApplication::exit(94);
                        return;
                    }
                    DWORD written = 0;
                    WriteFile(held, "RIFF-held", 9, &written, nullptr);
                    continue;
                }
#endif
                QDir().mkpath(QFileInfo(path).absolutePath());
                const QString qaResultSource = qEnvironmentVariable(
                    "AGPLAYER_VOICE_CLONE_QA_RESULT_SOURCE");
                if (!qaResultSource.isEmpty()) {
                    if (!QFileInfo(qaResultSource).isFile()
                        || !QFile::copy(qaResultSource, path)) {
                        QCoreApplication::exit(96);
                        return;
                    }
                } else {
                    QFile wav(path);
                    if (!wav.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        QCoreApplication::exit(92);
                        return;
                    }
                    wav.write("RIFFtest-WAVE");
                    wav.close();
                }
                if (text == QStringLiteral("slow") || text == QStringLiteral("cancel-error")) {
                    delayed = request.requestId;
                    if (text == QStringLiteral("cancel-error")) cancelFailureTarget = request.requestId;
                    continue;
                }
            }
            socket.write(encodeWorkerMessage(responseFor(request)) + '\n');
            if (request.operation == WorkerOperation::Generate
                && text == QStringLiteral("fast") && !delayed.isEmpty()) {
                auto late = request;
                late.requestId = delayed;
                late.payload.insert(QStringLiteral("outputPath"),
                                    QStringLiteral("requests/%1/generated.wav.part").arg(delayed));
                socket.write(encodeWorkerMessage(responseFor(late)) + '\n');
                delayed.clear();
            }
            socket.flush();
        }
    });
    return QCoreApplication::exec();
}

bool writeAdapterPack(const QString& pluginRoot, const QString& adapterId = QStringLiteral("qwen"))
{
    const QString root = QDir(pluginRoot).filePath(
        QStringLiteral("adapters/%1/1.0.0").arg(adapterId));
    const QString launcherRelative = QStringLiteral("workers/test-worker.exe");
    if (!QDir(root).mkpath(QStringLiteral("workers"))) return false;
    const QString launcher = QDir(root).filePath(launcherRelative);
    QFile::remove(launcher);
    if (!QFile::copy(QCoreApplication::applicationFilePath(), launcher)) return false;
    QFile manifest(QDir(root).filePath(QStringLiteral("adapter.json")));
    if (!manifest.open(QIODevice::WriteOnly)) return false;
    manifest.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("runtime"), QJsonObject{{QStringLiteral("id"), QStringLiteral("test-runtime")},
                                                {QStringLiteral("root"), QStringLiteral("runtime")},
                                                {QStringLiteral("shared"), false}}},
        {QStringLiteral("defaultLauncherId"), QStringLiteral("test")},
        {QStringLiteral("launchers"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("test")},
                                                             {QStringLiteral("kind"), QStringLiteral("executable")},
                                                             {QStringLiteral("path"), launcherRelative},
                                                             {QStringLiteral("shared"), false}}}}
    }).toJson());
    return true;
}

quint32 zipCrc32(const QByteArray& bytes)
{
    quint32 crc = 0xffffffffU;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

void zip16(QByteArray& output, const quint16 value)
{
    output.append(char(value & 0xffU)); output.append(char((value >> 8U) & 0xffU));
}

void zip32(QByteArray& output, const quint32 value)
{
    zip16(output, quint16(value & 0xffffU)); zip16(output, quint16(value >> 16U));
}

QByteArray singleFileStoredZip(const QByteArray& name, const QByteArray& bytes)
{
    QByteArray archive;
    const quint32 crc = zipCrc32(bytes);
    zip32(archive, 0x04034b50U); zip16(archive, 20); zip16(archive, 0); zip16(archive, 0);
    zip16(archive, 0); zip16(archive, 0); zip32(archive, crc);
    zip32(archive, quint32(bytes.size())); zip32(archive, quint32(bytes.size()));
    zip16(archive, quint16(name.size())); zip16(archive, 0); archive += name; archive += bytes;
    const quint32 centralOffset = quint32(archive.size());
    zip32(archive, 0x02014b50U); zip16(archive, 20); zip16(archive, 20);
    zip16(archive, 0); zip16(archive, 0); zip16(archive, 0); zip16(archive, 0);
    zip32(archive, crc); zip32(archive, quint32(bytes.size())); zip32(archive, quint32(bytes.size()));
    zip16(archive, quint16(name.size())); zip16(archive, 0); zip16(archive, 0);
    zip16(archive, 0); zip16(archive, 0); zip32(archive, 0); zip32(archive, 0); archive += name;
    const quint32 centralBytes = quint32(archive.size()) - centralOffset;
    zip32(archive, 0x06054b50U); zip16(archive, 0); zip16(archive, 0); zip16(archive, 1);
    zip16(archive, 1); zip32(archive, centralBytes); zip32(archive, centralOffset); zip16(archive, 0);
    return archive;
}

QString bytesSha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool writePythonAdapterPack(const QString& pluginRoot,
                            const QString& adapterId = QStringLiteral("qwen"),
                            const QString& runtimeId = QStringLiteral("qwen-shared"))
{
    const QString root = QDir(pluginRoot).filePath(
        QStringLiteral("adapters/%1/1.0.0").arg(adapterId));
    const QString workerPath = QStringLiteral("workers/%1/worker.py").arg(adapterId);
    if (!QDir(root).mkpath(QFileInfo(workerPath).path())) return false;
    QFile worker(QDir(root).filePath(workerPath));
    if (!worker.open(QIODevice::WriteOnly) || worker.write("# test launcher\n") < 0) return false;
    QFile manifest(QDir(root).filePath(QStringLiteral("adapter.json")));
    if (!manifest.open(QIODevice::WriteOnly)) return false;
    return manifest.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("runtime"), QJsonObject{{QStringLiteral("id"), runtimeId},
                                                {QStringLiteral("root"), QStringLiteral("unused")},
                                                {QStringLiteral("shared"), true}}},
        {QStringLiteral("defaultLauncherId"), QStringLiteral("python-module")},
        {QStringLiteral("launchers"), QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("python-module")},
             {QStringLiteral("kind"), QStringLiteral("pythonModule")},
             {QStringLiteral("path"), workerPath},
             {QStringLiteral("shared"), true}}}}
    }).toJson()) > 0;
}

bool copyRegistry(const QString& pluginRoot)
{
    const QString target = QDir(pluginRoot).filePath(QStringLiteral("registry/models.json"));
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) return false;
    return QFile::copy(QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH), target);
}

QString writeModel(const QString& modelsRoot,
                   const QString& stableId,
                   const QString& adapterId,
                   const QString& leaf = QStringLiteral("model"))
{
    const QString directory = QDir(modelsRoot).filePath(
        QStringLiteral("installed/%1").arg(leaf));
    if (!QDir().mkpath(directory)) return {};
    QFile data(QDir(directory).filePath(QStringLiteral("config.json")));
    if (!data.open(QIODevice::WriteOnly) || data.write("{}") != 2) return {};
    const bool index = adapterId == QStringLiteral("indextts25");
    const QString url = index
                            ? QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE")
                            : QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    QJsonObject license{{QStringLiteral("name"),
                         index ? QStringLiteral("bilibili Model Use License Agreement")
                               : QStringLiteral("Apache-2.0")},
                        {QStringLiteral("url"), url}};
    if (index) license.insert(QStringLiteral("revision"),
                              QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63"));
    QJsonObject manifest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("stableId"), stableId},
        {QStringLiteral("displayName"), stableId},
        {QStringLiteral("description"), QStringLiteral("test model")},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("runtimeId"), adapterId},
        {QStringLiteral("revision"), index
                                         ? QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63")
                                         : QStringLiteral("main")},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("provider"), QStringLiteral("official")},
                                                {QStringLiteral("url"), url}}},
        {QStringLiteral("license"), license},
        {QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("config.json")}}}}
    };
    if (index) {
        manifest.insert(QStringLiteral("licenses"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("bilibili-model-use-license")},
                        {QStringLiteral("name"), QStringLiteral("bilibili Model Use License Agreement")},
                        {QStringLiteral("url"), QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE")},
                        {QStringLiteral("revision"), QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63")},
                        {QStringLiteral("spdx"), QStringLiteral("LicenseRef-Bilibili-Model-Use")},
                        {QStringLiteral("requiredAcceptance"), true},
                        {QStringLiteral("useRestriction"), QStringLiteral("custom-terms")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("maskgct-cc-by-nc-4.0")},
                        {QStringLiteral("name"), QStringLiteral("CC-BY-NC-4.0")},
                        {QStringLiteral("url"), QStringLiteral("https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md")},
                        {QStringLiteral("revision"), QStringLiteral("265c6cef07625665d0c28d2faafb1415562379dc")},
                        {QStringLiteral("spdx"), QStringLiteral("CC-BY-NC-4.0")},
                        {QStringLiteral("requiredAcceptance"), true},
                        {QStringLiteral("useRestriction"), QStringLiteral("non-commercial-only")}}
        });
    }
    QFile file(QDir(directory).filePath(QStringLiteral("agplayer-model.json")));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(manifest).toJson());
    return directory;
}

struct TestLayout {
    QTemporaryDir root;
    QString pluginRoot;
    QString modelsRoot;
    QString packagesRoot;
    QString runtimeRoot;

    TestLayout()
        : pluginRoot(root.filePath(QStringLiteral("plugin"))),
          modelsRoot(root.filePath(QStringLiteral("data/models/voice-clone"))),
          packagesRoot(root.filePath(QStringLiteral("packages"))),
          runtimeRoot(root.filePath(QStringLiteral("runtime")))
    {
        QDir().mkpath(pluginRoot);
        QDir().mkpath(modelsRoot);
        QDir().mkpath(runtimeRoot);
        copyRegistry(pluginRoot);
    }
};

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), previous_(qgetenv(name)), hadPrevious_(!previous_.isNull())
    {
        qputenv(name_, value);
    }
    ~ScopedEnvironment()
    {
        if (hadPrevious_) qputenv(name_, previous_);
        else qunsetenv(name_);
    }

private:
    const char* name_;
    QByteArray previous_;
    bool hadPrevious_;
};

} // namespace

class VoiceCloneControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void activateModelResolvesInstalledAdapterAndLoadsWorker();
    void qaWorkerPublishesConfiguredFixture();
    void activateModelReportsMissingRuntimeWithoutPretendingReady();
    void downloadsInstallsRefreshesAndSelectsModel();
    void oneClickDownloadInstallsRuntimeThenModelAndProbesWorker();
    void indexLicensesGateRuntimeAndModelNetworkBeforeDownload();
    void missingRuntimeFeedIsReportedAsRetryableDownloadFailure();
    void installedModelWithoutRuntimeRequiresRuntimeDownload();
    void activateModelClearsLiveSchemaBeforeMissingRuntime();
    void resultFileUrlEncodesReservedCharacters();
    void selectedIndexLicenseAcceptanceUsesExactIdentity();
    void lifecycleCorrelationAndCleanup();
    void rejectsInvalidDynamicParametersAndUnacceptedIndexLicense();
    void handlesOomCrashTimeoutAndRestartWithoutKillingHost();
    void lazyLoadProbeMarksRejectedModelInvalid();
    void rejectsExternalAdapterAndModelRoots();
    void canceledLateResponseAndStaleLoadAreIgnored();
    void lockedTemporaryOutputIsCleanedAfterWorkerStops();
    void rejectsOversizedFrameAndSymlinkPart();
    void shutdownUsesProtocolAndModuleLoadsThroughHost();
};

void VoiceCloneControllerTest::downloadsInstallsRefreshesAndSelectsModel()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    const QByteArray body("downloaded-config");
    ModelDownloadServer server(body, 350);
    const QString manifestPath = QDir(layout.pluginRoot).filePath(
        QStringLiteral("registry/downloads/qwen3-tts-0.6b.json"));
    const QString revision = QStringLiteral("5d83992436eae1d760afd27aff78a71d676296fc");
    const QString licenseRevision = QStringLiteral("022e286b98fbec7e1e916cb940cdf532cd9f488e");
    const QJsonObject package{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("packageId"), QStringLiteral("controller-http-fixture")},
        {QStringLiteral("modelId"), QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")},
        {QStringLiteral("adapterId"), QStringLiteral("qwen")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("model"), QJsonObject{
             {QStringLiteral("displayName"), QStringLiteral("Qwen3-TTS 0.6B Base")},
             {QStringLiteral("description"), QStringLiteral("Local HTTP protocol fixture")}}},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("provider"), QStringLiteral("hugging-face")},
             {QStringLiteral("repository"), QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")},
             {QStringLiteral("url"), QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base")}}},
        {QStringLiteral("licenses"), QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("apache-2.0")},
             {QStringLiteral("name"), QStringLiteral("Apache-2.0")},
             {QStringLiteral("url"), QStringLiteral("https://github.com/QwenLM/Qwen3-TTS/blob/%1/LICENSE").arg(licenseRevision)},
             {QStringLiteral("revision"), licenseRevision},
             {QStringLiteral("spdx"), QStringLiteral("Apache-2.0")},
             {QStringLiteral("requiredAcceptance"), false},
             {QStringLiteral("useRestriction"), QString{}}}}},
        {QStringLiteral("totalBytes"), body.size()},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("config.json")},
             {QStringLiteral("url"), server.url().toString()},
             {QStringLiteral("sha256"), QString::fromLatin1(
                  QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex())},
             {QStringLiteral("sizeBytes"), body.size()}}}}};
    QVERIFY(QDir().mkpath(QFileInfo(manifestPath).absolutePath()));
    QFile manifestFile(manifestPath);
    QVERIFY(manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(manifestFile.write(QJsonDocument(package).toJson()) > 0);
    manifestFile.close();

    VoiceClonePackageManager packages(
        layout.modelsRoot, VoiceClonePackageValidationPolicy::AllowLoopback);
    VoiceCloneController controller(
        layout.pluginRoot, layout.modelsRoot, &packages,
        VoiceClonePackageValidationPolicy::AllowLoopback);
    QVERIFY2(controller.downloadModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")),
             qPrintable(controller.errorString()));
    QTRY_COMPARE_WITH_TIMEOUT(packages.state(), VoiceClonePackageManager::Downloading, 2000);
    QCOMPARE(controller.downloadModelId(),
             QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"));
    QCOMPARE(controller.downloadState(), QStringLiteral("downloading"));
    QVERIFY(controller.pauseDownload());
    QCOMPARE(controller.downloadState(), QStringLiteral("paused"));
    QVERIFY(controller.resumeDownload());
    QTRY_COMPARE_WITH_TIMEOUT(packages.state(), VoiceClonePackageManager::Downloading, 2000);
    QVERIFY(controller.cancelDownload());
    QCOMPARE(controller.downloadState(), QStringLiteral("canceled"));
    QVERIFY(controller.retryDownload());
    QTRY_COMPARE_WITH_TIMEOUT(packages.state(), VoiceClonePackageManager::Completed, 3000);
    QTRY_VERIFY_WITH_TIMEOUT([&controller] {
        for (const QVariant& value : controller.models()) {
            const QVariantMap model = value.toMap();
            if (model.value(QStringLiteral("stableId")).toString()
                    == QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")) {
                return model.value(QStringLiteral("installState")).toString()
                       == QStringLiteral("ready");
            }
        }
        return false;
    }(), 3000);
    QVERIFY2(controller.selectModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")),
             qPrintable(controller.errorString()));
    QVERIFY(!controller.modelLoaded());

    QJsonObject failedPackage = package;
    failedPackage.insert(QStringLiteral("packageId"),
                         QStringLiteral("controller-failure-fixture"));
    QJsonArray failedFiles = failedPackage.value(QStringLiteral("files")).toArray();
    QJsonObject failedFile = failedFiles.first().toObject();
    failedFile.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('0')));
    failedFiles[0] = failedFile;
    failedPackage.insert(QStringLiteral("files"), failedFiles);
    QVERIFY(manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(manifestFile.write(QJsonDocument(failedPackage).toJson()) > 0);
    manifestFile.close();
    QVERIFY(controller.downloadModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")));
    QTRY_COMPARE_WITH_TIMEOUT(packages.state(), VoiceClonePackageManager::Failed, 3000);
    QCOMPARE(controller.downloadState(), QStringLiteral("failed"));
    QVERIFY(!controller.downloadError().isEmpty());
    QCOMPARE(controller.errorString(), controller.downloadError());
    QVERIFY(controller.retryDownload());
    QTRY_COMPARE_WITH_TIMEOUT(packages.state(), VoiceClonePackageManager::Downloading, 2000);
    QVERIFY(controller.cancelDownload());
}

void VoiceCloneControllerTest::oneClickDownloadInstallsRuntimeThenModelAndProbesWorker()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writePythonAdapterPack(layout.pluginRoot));

    QFile executable(QCoreApplication::applicationFilePath());
    QVERIFY(executable.open(QIODevice::ReadOnly));
    const QByteArray runtimeExecutable = executable.readAll();
    const QByteArray runtimeZip = singleFileStoredZip("python.exe", runtimeExecutable);
    ModelDownloadServer runtimeServer(runtimeZip);

    const QJsonObject runtimeManifest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("packageId"), QStringLiteral("runtime-qwen")},
        {QStringLiteral("runtimeId"), QStringLiteral("qwen-shared")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("compatibleAdapters"), QJsonArray{QJsonObject{
             {QStringLiteral("adapterId"), QStringLiteral("qwen")},
             {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")}}}},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("platform"), QStringLiteral("windows")},
        {QStringLiteral("architecture"), QStringLiteral("x86_64")},
        {QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("archiveFormat"), QStringLiteral("zip")},
        {QStringLiteral("installRoot"), QStringLiteral("runtime/qwen-shared/1.0.0")},
        {QStringLiteral("publisher"), QJsonObject{{QStringLiteral("name"), QStringLiteral("AG Player")},
                                                  {QStringLiteral("url"), QStringLiteral("https://agplayer.cn")}}},
        {QStringLiteral("licenseNotices"), QJsonArray{QJsonObject{
             {QStringLiteral("name"), QStringLiteral("Python")},
             {QStringLiteral("spdx"), QStringLiteral("PSF-2.0")},
             {QStringLiteral("url"), QStringLiteral("https://docs.python.org/3/license.html")}}}},
        {QStringLiteral("signature"), QJsonObject{{QStringLiteral("status"), QStringLiteral("unsigned-test")},
                                                  {QStringLiteral("algorithm"), QJsonValue::Null},
                                                  {QStringLiteral("keyId"), QJsonValue::Null}}},
        {QStringLiteral("packageUrl"), runtimeServer.url().toString()},
        {QStringLiteral("packageBytes"), runtimeZip.size()},
        {QStringLiteral("packageSha256"), bytesSha256(runtimeZip)},
        {QStringLiteral("installedBytes"), runtimeExecutable.size()},
        {QStringLiteral("entryCount"), 1},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("python.exe")},
             {QStringLiteral("bytes"), runtimeExecutable.size()},
             {QStringLiteral("sha256"), bytesSha256(runtimeExecutable)}}}}
    };
    const QString feedPath = QDir(layout.pluginRoot).filePath(QStringLiteral("config/runtime-feed.json"));
    QVERIFY(QDir().mkpath(QFileInfo(feedPath).absolutePath()));
    QFile feed(feedPath);
    QVERIFY(feed.open(QIODevice::WriteOnly));
    QVERIFY(feed.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("reason"), QStringLiteral("test")},
        {QStringLiteral("signature"), QJsonObject{{QStringLiteral("status"), QStringLiteral("unsigned-test")},
                                                  {QStringLiteral("algorithm"), QJsonValue::Null},
                                                  {QStringLiteral("keyId"), QJsonValue::Null}}},
        {QStringLiteral("runtimes"), QJsonArray{runtimeManifest}}
    }).toJson()) > 0);
    feed.close();

    const QByteArray modelBytes("downloaded-config");
    ModelDownloadServer modelServer(modelBytes);
    const QString modelManifestPath = QDir(layout.pluginRoot).filePath(
        QStringLiteral("registry/downloads/qwen3-tts-0.6b.json"));
    QVERIFY(QDir().mkpath(QFileInfo(modelManifestPath).absolutePath()));
    const QString revision = QStringLiteral("5d83992436eae1d760afd27aff78a71d676296fc");
    const QString licenseRevision = QStringLiteral("022e286b98fbec7e1e916cb940cdf532cd9f488e");
    const QJsonObject modelPackage{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("packageId"), QStringLiteral("controller-runtime-chain")},
        {QStringLiteral("modelId"), QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")},
        {QStringLiteral("adapterId"), QStringLiteral("qwen")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("model"), QJsonObject{{QStringLiteral("displayName"), QStringLiteral("Qwen3-TTS 0.6B Base")},
                                              {QStringLiteral("description"), QStringLiteral("runtime chain")}}},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("provider"), QStringLiteral("hugging-face")},
                                               {QStringLiteral("repository"), QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")},
                                               {QStringLiteral("url"), QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base")}}},
        {QStringLiteral("licenses"), QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("apache-2.0")},
             {QStringLiteral("name"), QStringLiteral("Apache-2.0")},
             {QStringLiteral("url"), QStringLiteral("https://github.com/QwenLM/Qwen3-TTS/blob/%1/LICENSE").arg(licenseRevision)},
             {QStringLiteral("revision"), licenseRevision},
             {QStringLiteral("spdx"), QStringLiteral("Apache-2.0")},
             {QStringLiteral("requiredAcceptance"), false},
             {QStringLiteral("useRestriction"), QString{}}}}},
        {QStringLiteral("totalBytes"), modelBytes.size()},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("config.json")},
             {QStringLiteral("url"), modelServer.url().toString()},
             {QStringLiteral("sha256"), bytesSha256(modelBytes)},
             {QStringLiteral("sizeBytes"), modelBytes.size()}}}}
    };
    QFile modelManifest(modelManifestPath);
    QVERIFY(modelManifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(modelManifest.write(QJsonDocument(modelPackage).toJson()) > 0);
    modelManifest.close();

    VoiceClonePackageManager models(layout.modelsRoot,
        VoiceClonePackageValidationPolicy::AllowLoopback);
    VoiceCloneRuntimePackageManager runtimes(layout.runtimeRoot,
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneController controller(
        layout.pluginRoot, layout.modelsRoot, &models, &runtimes,
        VoiceClonePackageValidationPolicy::AllowLoopback);
    QVERIFY2(controller.downloadModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")),
             qPrintable(controller.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.downloadPhase() == QStringLiteral("runtime"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 10000);
    QCOMPARE(controller.downloadPhase(), QStringLiteral("ready"));
    QCOMPARE(controller.activationState(), QStringLiteral("ready"));
}

void VoiceCloneControllerTest::indexLicensesGateRuntimeAndModelNetworkBeforeDownload()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writePythonAdapterPack(layout.pluginRoot, QStringLiteral("indextts25"),
                                   QStringLiteral("indextts25-isolated")));

    const QByteArray runtimePayload("runtime-python");
    const QByteArray runtimeZip = singleFileStoredZip("python.exe", runtimePayload);
    ModelDownloadServer runtimeServer(runtimeZip, 1000);
    const QJsonObject runtimeManifest{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("packageId"), QStringLiteral("runtime-indextts25")},
        {QStringLiteral("runtimeId"), QStringLiteral("indextts25-isolated")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("compatibleAdapters"), QJsonArray{QJsonObject{
             {QStringLiteral("adapterId"), QStringLiteral("indextts25")},
             {QStringLiteral("adapterVersion"), QStringLiteral("1.0.0")}}}},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("platform"), QStringLiteral("windows")},
        {QStringLiteral("architecture"), QStringLiteral("x86_64")},
        {QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("archiveFormat"), QStringLiteral("zip")},
        {QStringLiteral("installRoot"), QStringLiteral("runtime/indextts25-isolated/1.0.0")},
        {QStringLiteral("publisher"), QJsonObject{{QStringLiteral("name"), QStringLiteral("AG Player")},
                                                  {QStringLiteral("url"), QStringLiteral("https://agplayer.cn")}}},
        {QStringLiteral("licenseNotices"), QJsonArray{QJsonObject{
             {QStringLiteral("name"), QStringLiteral("Python")},
             {QStringLiteral("spdx"), QStringLiteral("PSF-2.0")},
             {QStringLiteral("url"), QStringLiteral("https://docs.python.org/3/license.html")}}}},
        {QStringLiteral("signature"), QJsonObject{{QStringLiteral("status"), QStringLiteral("unsigned-test")},
                                                  {QStringLiteral("algorithm"), QJsonValue::Null},
                                                  {QStringLiteral("keyId"), QJsonValue::Null}}},
        {QStringLiteral("packageUrl"), runtimeServer.url().toString()},
        {QStringLiteral("packageBytes"), runtimeZip.size()},
        {QStringLiteral("packageSha256"), bytesSha256(runtimeZip)},
        {QStringLiteral("installedBytes"), runtimePayload.size()},
        {QStringLiteral("entryCount"), 1},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("python.exe")},
             {QStringLiteral("bytes"), runtimePayload.size()},
             {QStringLiteral("sha256"), bytesSha256(runtimePayload)}}}}
    };
    const QString feedPath = QDir(layout.pluginRoot).filePath(QStringLiteral("config/runtime-feed.json"));
    QVERIFY(QDir().mkpath(QFileInfo(feedPath).absolutePath()));
    QFile feed(feedPath);
    QVERIFY(feed.open(QIODevice::WriteOnly));
    QVERIFY(feed.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("reason"), QStringLiteral("test")},
        {QStringLiteral("signature"), QJsonObject{{QStringLiteral("status"), QStringLiteral("unsigned-test")},
                                                  {QStringLiteral("algorithm"), QJsonValue::Null},
                                                  {QStringLiteral("keyId"), QJsonValue::Null}}},
        {QStringLiteral("runtimes"), QJsonArray{runtimeManifest}}
    }).toJson()) > 0);
    feed.close();

    const QByteArray modelPayload("index-config");
    ModelDownloadServer modelServer(modelPayload, 5000);
    const QString sourceManifest = QDir(
        QFileInfo(QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH)).absolutePath())
                                       .filePath(QStringLiteral("downloads/indextts-2.5.json"));
    QFile source(sourceManifest);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QJsonObject modelPackage = QJsonDocument::fromJson(source.readAll()).object();
    modelPackage.insert(QStringLiteral("packageId"), QStringLiteral("controller-index-license-fixture"));
    modelPackage.remove(QStringLiteral("fileGraphSha256"));
    modelPackage.insert(QStringLiteral("totalBytes"), modelPayload.size());
    modelPackage.insert(QStringLiteral("files"), QJsonArray{QJsonObject{
        {QStringLiteral("path"), QStringLiteral("config.yaml")},
        {QStringLiteral("url"), modelServer.url().toString()},
        {QStringLiteral("sha256"), bytesSha256(modelPayload)},
        {QStringLiteral("sizeBytes"), modelPayload.size()}}});
    const QString modelManifestPath = QDir(layout.pluginRoot).filePath(
        QStringLiteral("registry/downloads/indextts-2.5.json"));
    QVERIFY(QDir().mkpath(QFileInfo(modelManifestPath).absolutePath()));
    QFile modelManifest(modelManifestPath);
    QVERIFY(modelManifest.open(QIODevice::WriteOnly));
    QVERIFY(modelManifest.write(QJsonDocument(modelPackage).toJson()) > 0);
    modelManifest.close();

    VoiceClonePackageManager models(layout.modelsRoot,
        VoiceClonePackageValidationPolicy::AllowLoopback);
    VoiceCloneRuntimePackageManager runtimes(layout.runtimeRoot,
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &models, &runtimes,
                                    VoiceClonePackageValidationPolicy::AllowLoopback);
    const QString modelId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    QVERIFY(!controller.downloadModel(modelId));
    QVERIFY2(controller.downloadPhase() == QStringLiteral("license"),
             qPrintable(controller.errorString()));
    QCOMPARE(controller.downloadState(), QStringLiteral("license-required"));
    QVERIFY(controller.licenseAcceptanceRequired());
    QTest::qWait(100);
    QCOMPARE(runtimeServer.requestCount(), 0);
    QCOMPARE(modelServer.requestCount(), 0);

    QVERIFY(controller.acceptSelectedLicenses({QStringLiteral("bilibili-model-use-license"),
                                               QStringLiteral("maskgct-cc-by-nc-4.0")}));
    QVERIFY2(controller.downloadModel(modelId), qPrintable(controller.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(runtimeServer.headCount() > 0, 3000);
    QCOMPARE(modelServer.requestCount(), 0);
    controller.cancelDownload();
}

void VoiceCloneControllerTest::missingRuntimeFeedIsReportedAsRetryableDownloadFailure()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writePythonAdapterPack(layout.pluginRoot));
    const QString downloadsRoot = QDir(layout.pluginRoot).filePath(
        QStringLiteral("registry/downloads"));
    QVERIFY(QDir().mkpath(downloadsRoot));
    const QString sourceManifest = QDir(
        QFileInfo(QString::fromUtf8(AGPLAYER_VOICE_CLONE_REGISTRY_PATH)).absolutePath())
                                       .filePath(QStringLiteral(
                                           "downloads/qwen3-tts-0.6b.json"));
    QVERIFY(QFile::copy(sourceManifest,
                        QDir(downloadsRoot).filePath(QStringLiteral(
                            "qwen3-tts-0.6b.json"))));

    VoiceClonePackageManager models(layout.modelsRoot,
        VoiceClonePackageValidationPolicy::AllowLoopback);
    VoiceCloneRuntimePackageManager runtimes(layout.runtimeRoot,
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneController controller(
        layout.pluginRoot, layout.modelsRoot, &models, &runtimes,
        VoiceClonePackageValidationPolicy::AllowLoopback);

    QVERIFY(controller.downloadModel(
        QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")));
    QCOMPARE(controller.downloadPhase(), QStringLiteral("runtime"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.downloadState(), QStringLiteral("failed"), 3000);
    QVERIFY(controller.downloadError().contains(QStringLiteral("feed"),
                                                 Qt::CaseInsensitive));
}

void VoiceCloneControllerTest::installedModelWithoutRuntimeRequiresRuntimeDownload()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writePythonAdapterPack(layout.pluginRoot, QStringLiteral("indextts25"),
                                   QStringLiteral("indextts25-isolated")));
    const QString modelId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("indextts25")).isEmpty());
    VoiceClonePackageManager models(layout.modelsRoot);
    VoiceCloneRuntimePackageManager runtimes(layout.runtimeRoot,
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneController controller(
        layout.pluginRoot, layout.modelsRoot, &models, &runtimes,
        VoiceClonePackageValidationPolicy::OfficialOnly);

    QVERIFY(controller.activateModel(modelId));
    QTRY_VERIFY_WITH_TIMEOUT(controller.activationState() == QStringLiteral("needs-download"), 3000);
    QVERIFY(controller.runtimeDownloadRequired());
}

void VoiceCloneControllerTest::activateModelResolvesInstalledAdapterAndLoadsWorker()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/activated-qwen");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);

    QSignalSpy activationChanged(&controller, &VoiceCloneController::activationChanged);
    QVERIFY2(controller.activateModel(modelId), qPrintable(controller.errorString()));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.workerReady(), qPrintable(controller.errorString()), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    QCOMPARE(controller.activationState(), QStringLiteral("ready"));
    QVERIFY(activationChanged.count() >= 2);
    QFile loadParameters(QDir(layout.pluginRoot).filePath(
        QStringLiteral("cache/load-parameters.json")));
    QVERIFY(loadParameters.open(QIODevice::ReadOnly));
    const QJsonObject loaded = QJsonDocument::fromJson(loadParameters.readAll()).object();
    QCOMPARE(loaded.value(QStringLiteral("seed")).toInt(), 1);
    QCOMPARE(loaded.value(QStringLiteral("mode")).toString(), QStringLiteral("fast"));

}

void VoiceCloneControllerTest::qaWorkerPublishesConfiguredFixture()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qa-fixture-qwen");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    const QString sourcePath = layout.root.filePath(QStringLiteral("authorized-fixture.wav"));
    const QByteArray sourceBytes("RIFF-authorized-fixture-WAVE-data");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(source.write(sourceBytes), sourceBytes.size());
    source.close();
    ScopedEnvironment fixture("AGPLAYER_VOICE_CLONE_QA_RESULT_SOURCE",
                              QFile::encodeName(sourcePath));

    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY2(controller.activateModel(modelId), qPrintable(controller.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 5000);
    QSignalSpy finished(&controller, &VoiceCloneController::generationFinished);
    const QString requestId = controller.generate(
        QStringLiteral("qa-fixture"), sourcePath,
        {{QStringLiteral("seed"), 1},
         {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(!requestId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QFile output(finished.constFirst().at(1).toString());
    QVERIFY(output.open(QIODevice::ReadOnly));
    QCOMPARE(output.readAll(), sourceBytes);
}

void VoiceCloneControllerTest::activateModelReportsMissingRuntimeWithoutPretendingReady()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    const QString modelId = QStringLiteral("local/missing-runtime");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);

    QVERIFY(!controller.activateModel(
        QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base")));
    QCOMPARE(controller.activationState(), QStringLiteral("needs-download"));
    QVERIFY(controller.activationMessage().contains(QStringLiteral("not ready"),
                                                     Qt::CaseInsensitive));
    QVERIFY(!controller.activateModel(modelId));
    QCOMPARE(controller.activationState(), QStringLiteral("needs-download"));
    QVERIFY(controller.activationMessage().contains(QStringLiteral("not ready"),
                                                     Qt::CaseInsensitive));
    QVERIFY(!controller.workerReady());
    QVERIFY(!controller.modelLoaded());
}

void VoiceCloneControllerTest::activateModelClearsLiveSchemaBeforeMissingRuntime()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString readyId = QStringLiteral("local/schema-ready");
    const QString missingRuntimeId = QStringLiteral("local/schema-missing-runtime");
    QVERIFY(!writeModel(layout.modelsRoot, readyId, QStringLiteral("qwen"),
                        QStringLiteral("schema-ready")).isEmpty());
    QVERIFY(!writeModel(layout.modelsRoot, missingRuntimeId,
                        QStringLiteral("qwen"),
                        QStringLiteral("schema-missing")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);

    QVERIFY2(controller.activateModel(readyId), qPrintable(controller.errorString()));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.workerReady(), qPrintable(controller.errorString()), 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(controller.modelLoaded(), qPrintable(controller.errorString()), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.basicParameters().size(), 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.advancedParameters().size(), 1, 3000);
    QVERIFY(controller.advancedSettingsAvailable());
    controller.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(QFile::remove(QDir(layout.pluginRoot).filePath(
        QStringLiteral("adapters/qwen/1.0.0/workers/test-worker.exe"))));

    QSignalSpy capabilitiesChanged(&controller, &VoiceCloneController::capabilitiesChanged);
    QVERIFY(!controller.activateModel(missingRuntimeId));
    QVERIFY2(controller.activationState() == QStringLiteral("needs-download"),
             qPrintable(controller.activationMessage()));
    QCOMPARE(controller.basicParameters(), QVariantList{});
    QCOMPARE(controller.advancedParameters(), QVariantList{});
    QVERIFY(!controller.advancedSettingsAvailable());
    QCOMPARE(capabilitiesChanged.count(), 1);
}

void VoiceCloneControllerTest::resultFileUrlEncodesReservedCharacters()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    const QString path = QDir(layout.root.path()).filePath(
        QStringLiteral("results/audio clip #50%.wav"));

    const QUrl url = controller.resultFileUrl(path);

    QCOMPARE(url, QUrl::fromLocalFile(path));
    QVERIFY(url.toEncoded().contains("audio%20clip%20%2350%25.wav"));
    QCOMPARE(url.toLocalFile(), path);
}

void VoiceCloneControllerTest::selectedIndexLicenseAcceptanceUsesExactIdentity()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot, QStringLiteral("indextts25")));
    const QString modelId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("indextts25"),
                        QStringLiteral("index")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);

    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.licenseAcceptanceRequired());
    QCOMPARE(controller.currentLicenseUrl(),
             QUrl(QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE")));
    QCOMPARE(controller.currentLicenseRevision(), QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63"));
    const QVariantList requirements = controller.currentLicenseRequirements();
    QCOMPARE(requirements.size(), 2);
    QCOMPARE(requirements[1].toMap().value(QStringLiteral("spdx")).toString(),
             QStringLiteral("CC-BY-NC-4.0"));
    QCOMPARE(requirements[1].toMap().value(QStringLiteral("useRestriction")).toString(),
             QStringLiteral("non-commercial-only"));
    QVERIFY(!controller.acceptSelectedLicenseIdentity(
        QStringLiteral("wrong/model"), QStringLiteral("indextts25"),
        controller.currentLicenseUrl(), controller.currentLicenseRevision()));
    QVERIFY(controller.licenseAcceptanceRequired());
    QVERIFY(!controller.acceptSelectedLicense());
    QVERIFY(!controller.acceptSelectedLicenses(
        {requirements[0].toMap().value(QStringLiteral("id")).toString()}));
    QVERIFY(controller.acceptSelectedLicenses(
        {requirements[0].toMap().value(QStringLiteral("id")).toString(),
         requirements[1].toMap().value(QStringLiteral("id")).toString()}));
    QVERIFY(!controller.licenseAcceptanceRequired());
    QVERIFY(licenses.hasRequiredLicenseAcceptances(modelId, QStringLiteral("indextts25")));

    QFile acceptance(licenses.licenseAcceptancePath());
    QVERIFY(acceptance.open(QIODevice::ReadOnly));
    const QJsonArray records = QJsonDocument::fromJson(acceptance.readAll())
                                   .object().value(QStringLiteral("records")).toArray();
    QCOMPARE(records.size(), 2);
    const QJsonObject record = records.at(0).toObject();
    QCOMPARE(record.value(QStringLiteral("modelId")).toString(), modelId);
    QCOMPARE(record.value(QStringLiteral("adapterId")).toString(),
             QStringLiteral("indextts25"));
    QCOMPARE(record.value(QStringLiteral("licenseId")).toString(),
             QStringLiteral("bilibili-model-use-license"));
    QCOMPARE(record.value(QStringLiteral("licenseUrl")).toString(),
             controller.currentLicenseUrl().toString());
    QCOMPARE(record.value(QStringLiteral("revision")).toString(),
             controller.currentLicenseRevision());
    QVERIFY(QDateTime::fromString(record.value(QStringLiteral("acceptedAt")).toString(),
                                  Qt::ISODateWithMs).isValid());
}

void VoiceCloneControllerTest::lifecycleCorrelationAndCleanup()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qwen-test");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    const QString invalidDirectory = QDir(layout.modelsRoot).filePath(
        QStringLiteral("invalid/model"));
    QVERIFY(QDir().mkpath(invalidDirectory));
    QFile invalidManifest(QDir(invalidDirectory).filePath(QStringLiteral("agplayer-model.json")));
    QVERIFY(invalidManifest.open(QIODevice::WriteOnly));
    invalidManifest.write("{}");
    invalidManifest.close();
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.metaObject()->indexOfMethod("selectModel(QString)") >= 0);
    QVERIFY(controller.metaObject()->indexOfMethod(
                "generate(QString,QString,QJsonObject)") >= 0);
    bool foundInvalid = false;
    bool foundModelMetadata = false;
    for (const QVariant& value : controller.models()) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("installState")) == QStringLiteral("invalid"))
            foundInvalid = true;
        if (model.value(QStringLiteral("stableId")) == modelId) {
            QVERIFY(!model.value(QStringLiteral("description")).toString().isEmpty());
            QVERIFY(!model.value(QStringLiteral("licenseName")).toString().isEmpty());
            QVERIFY(model.contains(QStringLiteral("officialProjectUrl")));
            QVERIFY(model.contains(QStringLiteral("huggingFaceUrl")));
            QVERIFY(model.contains(QStringLiteral("modelScopeUrl")));
            QVERIFY(model.contains(QStringLiteral("capabilityPreview")));
            QVERIFY(model.contains(QStringLiteral("requiresLicenseAcceptance")));
            foundModelMetadata = true;
        }
    }
    QVERIFY(foundInvalid);
    QVERIFY(foundModelMetadata);
    controller.setRequestTimeoutMs(2000);
    QVERIFY2(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")),
             qPrintable(controller.errorString()));
    QVERIFY(controller.selectModel(modelId));

    QSignalSpy ready(&controller, &VoiceCloneController::workerReadyChanged);
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(ready.count() > 0);
    QCOMPARE(controller.basicParameters().size(), 2);
    QCOMPARE(controller.advancedParameters().size(), 1);
    QVERIFY(controller.advancedSettingsAvailable());
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    QSignalSpy finished(&controller, &VoiceCloneController::generationFinished);
    const QString slow = controller.generate(QStringLiteral("slow"), {},
                                             {{QStringLiteral("seed"), 2},
                                              {QStringLiteral("mode"), QStringLiteral("fast")}});
    const QString fast = controller.generate(QStringLiteral("fast"), {},
                                             {{QStringLiteral("seed"), 3},
                                              {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(!slow.isEmpty());
    QVERIFY(!fast.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
    QCOMPARE(finished.at(0).at(0).toString(), fast);
    QCOMPARE(finished.at(1).at(0).toString(), slow);
    for (const auto& arguments : finished) {
        const QString output = arguments.at(1).toString();
        QVERIFY(QFileInfo::exists(output));
        QVERIFY(!QFileInfo::exists(output + QStringLiteral(".part")));
    }
    const QString managedOutput = finished.at(0).at(1).toString();
    const QString savedCopy = layout.root.filePath(QStringLiteral("saved-result.wav"));
    QVERIFY(controller.saveResult(managedOutput, savedCopy));
    QVERIFY(QFileInfo::exists(savedCopy));
    QCOMPARE(QFileInfo(savedCopy).size(), QFileInfo(managedOutput).size());
    QVERIFY(!controller.deleteResult(savedCopy));
    QVERIFY(controller.deleteResult(managedOutput));
    QVERIFY(!QFileInfo::exists(managedOutput));

    const QString canceled = controller.generate(QStringLiteral("hang"), {},
                                                  {{QStringLiteral("seed"), 4},
                                                   {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(controller.cancel(canceled));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(canceled), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(controller.requestDirectory(canceled)), 3000);
    QVERIFY(controller.unloadModel());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.modelLoaded(), 3000);
    controller.shutdown();
    QVERIFY(!controller.workerRunning());
}

void VoiceCloneControllerTest::rejectsInvalidDynamicParametersAndUnacceptedIndexLicense()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot, QStringLiteral("indextts25")));
    const QString indexId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    QVERIFY(!writeModel(layout.modelsRoot, indexId, QStringLiteral("indextts25"),
                        QStringLiteral("index")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("indextts25"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(indexId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("unknown"), 1}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("seed"), 100},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("slow")}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("x"), {},
                                {{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")},
                                 {QStringLiteral("style"), QStringLiteral("hidden")}}).isEmpty());
    QVERIFY(controller.generate(QStringLiteral("license-blocked"), {},
                                {{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}}).isEmpty());
    QVERIFY(controller.errorString().contains(QStringLiteral("license"), Qt::CaseInsensitive));

    QVERIFY(controller.licenseAcceptanceRequired());
    const QVariantList requirements = controller.currentLicenseRequirements();
    QVERIFY(controller.acceptSelectedLicenses(
        {requirements[0].toMap().value(QStringLiteral("id")).toString(),
         requirements[1].toMap().value(QStringLiteral("id")).toString()}));
    QVERIFY(!controller.licenseAcceptanceRequired());
    const QString accepted = controller.generate(QStringLiteral("accepted"), {},
                                                 {{QStringLiteral("seed"), 1},
                                                  {QStringLiteral("mode"), QStringLiteral("fast")}});
    QVERIFY(!accepted.isEmpty());
}

void VoiceCloneControllerTest::handlesOomCrashTimeoutAndRestartWithoutKillingHost()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qwen-failure-test");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    controller.setRequestTimeoutMs(250);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    const QString oom = controller.generate(QStringLiteral("oom"), {},
                                            {{QStringLiteral("seed"), 1},
                                             {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(oom), 3000);
    QVERIFY(failed.last().at(1).toString().contains(QStringLiteral("memory")));
    QVERIFY(controller.workerRunning());

    const QString mismatched = controller.generate(QStringLiteral("bad-identity"), {},
                                                   {{QStringLiteral("seed"), 1},
                                                    {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(mismatched)));
    QVERIFY(controller.restartWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    const QString crash = controller.generate(QStringLiteral("crash"), {},
                                              {{QStringLiteral("seed"), 1},
                                               {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(crash)));
    QVERIFY(QCoreApplication::instance() != nullptr);
    QVERIFY(controller.restartWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);

    const QString timedOut = controller.generate(QStringLiteral("hang"), {},
                                                 {{QStringLiteral("seed"), 1},
                                                  {QStringLiteral("mode"), QStringLiteral("fast")}});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPendingRequest(timedOut), 3000);
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(timedOut)));
    QVERIFY(!controller.workerRunning());
    QVERIFY(QCoreApplication::instance() != nullptr);
}

void VoiceCloneControllerTest::lazyLoadProbeMarksRejectedModelInvalid()
{
    TestLayout layout;
    QVERIFY(layout.root.isValid());
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/qwen-reject-test");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen"),
                        QStringLiteral("reject-load")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    QVERIFY(controller.loadModel());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(!controller.modelLoaded());
    bool invalid = false;
    for (const QVariant& value : controller.models()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("stableId")) == modelId
            && row.value(QStringLiteral("installState")) == QStringLiteral("invalid")) {
            invalid = true;
            QVERIFY(row.value(QStringLiteral("diagnostic")).toString().contains(
                QStringLiteral("rejected")));
        }
    }
    QVERIFY(invalid);
}

void VoiceCloneControllerTest::rejectsExternalAdapterAndModelRoots()
{
    TestLayout layout;
    QTemporaryDir external;
    QVERIFY(layout.root.isValid());
    QVERIFY(external.isValid());
    QVERIFY(writeAdapterPack(external.path()));
    QVERIFY(!writeModel(external.path(), QStringLiteral("local/external"),
                        QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(!controller.configureAdapter(QStringLiteral("../external"), QStringLiteral("1.0.0")));
    QVERIFY(!controller.selectModel(QStringLiteral("local/external")));
}

void VoiceCloneControllerTest::canceledLateResponseAndStaleLoadAreIgnored()
{
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString slowId = QStringLiteral("local/slow-load");
    const QString fastId = QStringLiteral("local/fast-load");
    QVERIFY(!writeModel(layout.modelsRoot, slowId, QStringLiteral("qwen"),
                        QStringLiteral("slow-load")).isEmpty());
    QVERIFY(!writeModel(layout.modelsRoot, fastId, QStringLiteral("qwen"),
                        QStringLiteral("fast-load")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(slowId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QVERIFY(controller.selectModel(fastId));
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    QVERIFY(controller.workerRunning());

    QSignalSpy finished(&controller, &VoiceCloneController::generationFinished);
    const QJsonObject parameters{{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}};
    const QString retired = controller.generate(QStringLiteral("slow"), {}, parameters);
    QVERIFY(controller.cancel(retired));
    const QString current = controller.generate(QStringLiteral("fast"), {}, parameters);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QCOMPARE(finished.last().at(0).toString(), current);
    QVERIFY(controller.workerRunning());
    QVERIFY(!QFileInfo::exists(controller.requestDirectory(retired)));

    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    const QString cancelError = controller.generate(QStringLiteral("cancel-error"), {}, parameters);
    const QString cancelErrorDirectory = controller.requestDirectory(cancelError);
    QVERIFY(controller.cancel(cancelError));
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(cancelErrorDirectory), 3000);
}

void VoiceCloneControllerTest::lockedTemporaryOutputIsCleanedAfterWorkerStops()
{
#ifndef Q_OS_WIN
    QSKIP("Windows locked-file semantics test");
#else
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/held-output");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    const QJsonObject parameters{{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}};
    const QString request = controller.generate(QStringLiteral("hold-cancel"), {}, parameters);
    const QString directory = controller.requestDirectory(request);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(directory), 1000);
    QVERIFY(controller.cancel(request));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(directory), 3000);
    QCOMPARE(controller.pendingCleanupCount(), 0);
#endif
}

void VoiceCloneControllerTest::rejectsOversizedFrameAndSymlinkPart()
{
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/security-output");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    controller.setRequestTimeoutMs(2000);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    QSignalSpy failed(&controller, &VoiceCloneController::requestFailed);
    const QJsonObject parameters{{QStringLiteral("seed"), 1},
                                 {QStringLiteral("mode"), QStringLiteral("fast")}};
    controller.generate(QStringLiteral("oversize-frame"), {}, parameters);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.workerRunning(), 3000);
    QVERIFY(failed.last().at(1).toString().contains(QStringLiteral("frame")));

    QVERIFY(controller.restartWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    QVERIFY(controller.loadModel());
    QTRY_VERIFY_WITH_TIMEOUT(controller.modelLoaded(), 3000);
    const int before = failed.count();
    controller.generate(QStringLiteral("symlink-part"), {}, parameters);
    QTRY_VERIFY_WITH_TIMEOUT(failed.count() > before, 3000);
    if (failed.last().at(1).toString() == QStringLiteral("symlink-unavailable"))
        QSKIP("Symbolic-link privilege is unavailable");
    QCOMPARE(failed.last().at(1).toString(), QStringLiteral("unsafe-output"));
}

void VoiceCloneControllerTest::shutdownUsesProtocolAndModuleLoadsThroughHost()
{
    TestLayout layout;
    QVERIFY(writeAdapterPack(layout.pluginRoot));
    const QString modelId = QStringLiteral("local/shutdown");
    QVERIFY(!writeModel(layout.modelsRoot, modelId, QStringLiteral("qwen")).isEmpty());
    VoiceClonePackageManager licenses(layout.packagesRoot);
    VoiceCloneController controller(layout.pluginRoot, layout.modelsRoot, &licenses);
    QVERIFY(controller.configureAdapter(QStringLiteral("qwen"), QStringLiteral("1.0.0")));
    QVERIFY(controller.selectModel(modelId));
    QVERIFY(controller.startWorker());
    QTRY_VERIFY_WITH_TIMEOUT(controller.workerReady(), 5000);
    controller.shutdown();
    QVERIFY(QFileInfo::exists(QDir(layout.pluginRoot).filePath(
        QStringLiteral("cache/shutdown.marker"))));

    QTemporaryDir moduleRoot;
    QVERIFY(moduleRoot.isValid());
    const QString source = QString::fromUtf8(AGPLAYER_VOICE_CLONE_PLUGIN);
    const QString library = QFileInfo(source).fileName();
    QVERIFY(QFile::copy(source, moduleRoot.filePath(library)));
    QVERIFY(copyRegistry(moduleRoot.path()));
    QVERIFY(QDir().mkpath(moduleRoot.filePath(QStringLiteral("models/voice-clone"))));
    QFile manifest(moduleRoot.filePath(QStringLiteral("agplayer-voice-clone.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("pluginId"), QStringLiteral("agplayer.voice-clone")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("availableVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("platform"), QStringLiteral("windows")},
        {QStringLiteral("architecture"), QStringLiteral("x86_64")},
        {QStringLiteral("minimumPlayerVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("library"), library}}).toJson());
    manifest.close();
    qputenv("AGPLAYER_VOICE_CLONE_ROOT", moduleRoot.path().toUtf8());
    VoiceCloneHostController host;
    host.refresh();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    QVERIFY2(host.openPlugin(), qPrintable(host.errorString()));
    QVERIFY(host.pluginController() != nullptr);
    QVERIFY(host.mainQmlUrl().isValid());
    QCOMPARE(host.mainQmlUrl().scheme(), QStringLiteral("qrc"));
    QFile workspace(QStringLiteral(":") + host.mainQmlUrl().path());
    QVERIFY2(workspace.open(QIODevice::ReadOnly), qPrintable(workspace.errorString()));
    QVERIFY(workspace.readAll().contains("VoiceCloneModelBar"));

    AudioToolsController audioTools;
    AudioEditorController audioEditor(AG_AUDIO_BACKEND_NULL);
    SettingsController settings;
    WaveformProvider waveformProvider(&settings);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "AudioToolsController", &audioTools);
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "AudioEditorController", &audioEditor);
    qmlRegisterSingletonType<AudioPreviewController>(
        "AgPlayer", 1, 0, "AudioPreviewController",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return new AudioPreviewController(AG_AUDIO_BACKEND_NULL);
        });
    qmlRegisterSingletonInstance("AgPlayer", 1, 0, "WaveformProvider", &waveformProvider);
    qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/"));
        QQmlComponent loaderComponent(&engine);
        loaderComponent.setData(R"QML(
            import QtQuick
            Item {
                property url workspaceUrl
                property var injectedController
                Loader {
                    objectName: "realVoiceCloneWorkspaceLoader"
                    active: true
                    source: parent.workspaceUrl
                    onLoaded: item.controller = parent.injectedController
                }
            }
        )QML", QUrl(QStringLiteral("qrc:/tests/voice-clone-loader.qml")));
        QVERIFY2(loaderComponent.isReady(), qPrintable(loaderComponent.errorString()));
        QScopedPointer<QObject> loaderRoot(loaderComponent.createWithInitialProperties(
            {{QStringLiteral("workspaceUrl"), host.mainQmlUrl()},
             {QStringLiteral("injectedController"),
              QVariant::fromValue(host.pluginController())}}));
        QVERIFY2(loaderRoot, qPrintable(loaderComponent.errorString()));
        QObject* loader = loaderRoot->findChild<QObject*>(
            QStringLiteral("realVoiceCloneWorkspaceLoader"));
        QVERIFY(loader != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(loader->property("item").value<QObject*>() != nullptr, 3000);
        QObject* loadedWorkspace = loader->property("item").value<QObject*>();
        QCOMPARE(loadedWorkspace->property("controller").value<QObject*>(),
                 host.pluginController());
        loader->setProperty("active", false);
        QTRY_VERIFY_WITH_TIMEOUT(loader->property("item").value<QObject*>() == nullptr, 3000);
        loaderRoot.reset();
        engine.clearComponentCache();
    }
    host.closePlugin();
    QCOMPARE(host.state(), VoiceCloneHostController::Compatible);
    qunsetenv("AGPLAYER_VOICE_CLONE_ROOT");
    QVERIFY(QFile::rename(moduleRoot.filePath(library),
                          moduleRoot.filePath(library + QStringLiteral(".unloaded"))));
}

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--voice-clone-worker"))) {
        return runWorker(application.arguments());
    }
    VoiceCloneControllerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "voice_clone_controller_test.moc"
