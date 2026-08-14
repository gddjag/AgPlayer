#include "voice_clone_runtime_package.hpp"
#include "voice_clone_runtime_package_manager.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVersionNumber>

using namespace agplayer::voice_clone;

namespace {

quint32 crc32(const QByteArray& bytes)
{
    quint32 crc = 0xffffffffU;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

void append16(QByteArray& output, quint16 value)
{
    output.append(char(value & 0xffU));
    output.append(char((value >> 8U) & 0xffU));
}

void append32(QByteArray& output, quint32 value)
{
    append16(output, quint16(value & 0xffffU));
    append16(output, quint16((value >> 16U) & 0xffffU));
}

QByteArray storedZip(const QList<QPair<QByteArray, QByteArray>>& entries)
{
    QByteArray archive;
    struct Central { QByteArray name; QByteArray bytes; quint32 crc; quint32 offset; };
    QList<Central> central;
    for (const auto& entry : entries) {
        Central item{entry.first, entry.second, crc32(entry.second), quint32(archive.size())};
        append32(archive, 0x04034b50U);
        append16(archive, 20); append16(archive, 0); append16(archive, 0);
        append16(archive, 0); append16(archive, 0);
        append32(archive, item.crc);
        append32(archive, quint32(item.bytes.size()));
        append32(archive, quint32(item.bytes.size()));
        append16(archive, quint16(item.name.size())); append16(archive, 0);
        archive += item.name;
        archive += item.bytes;
        central.append(item);
    }
    const quint32 centralOffset = quint32(archive.size());
    for (const Central& item : central) {
        append32(archive, 0x02014b50U);
        append16(archive, 20); append16(archive, 20); append16(archive, 0); append16(archive, 0);
        append16(archive, 0); append16(archive, 0); append32(archive, item.crc);
        append32(archive, quint32(item.bytes.size())); append32(archive, quint32(item.bytes.size()));
        append16(archive, quint16(item.name.size())); append16(archive, 0); append16(archive, 0);
        append16(archive, 0); append16(archive, 0); append32(archive, 0); append32(archive, item.offset);
        archive += item.name;
    }
    const quint32 centralBytes = quint32(archive.size()) - centralOffset;
    append32(archive, 0x06054b50U);
    append16(archive, 0); append16(archive, 0);
    append16(archive, quint16(central.size())); append16(archive, quint16(central.size()));
    append32(archive, centralBytes); append32(archive, centralOffset); append16(archive, 0);
    return archive;
}

void overwrite32(QByteArray& output, const qsizetype offset, const quint32 value)
{
    QVERIFY(offset >= 0 && offset + 4 <= output.size());
    output[offset] = char(value & 0xffU);
    output[offset + 1] = char((value >> 8U) & 0xffU);
    output[offset + 2] = char((value >> 16U) & 0xffU);
    output[offset + 3] = char((value >> 24U) & 0xffU);
}

void overwrite16(QByteArray& output, const qsizetype offset, const quint16 value)
{
    QVERIFY(offset >= 0 && offset + 2 <= output.size());
    output[offset] = char(value & 0xffU);
    output[offset + 1] = char((value >> 8U) & 0xffU);
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject manifestObject(const QUrl& url, const QByteArray& archive,
                           const QByteArray& payload = QByteArray("worker-runtime"))
{
    const QJsonObject metadata{
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
        {QStringLiteral("publisher"), QJsonObject{
             {QStringLiteral("name"), QStringLiteral("AG Player")},
             {QStringLiteral("url"), QStringLiteral("https://agplayer.cn")}}},
        {QStringLiteral("licenseNotices"), QJsonArray{QJsonObject{
             {QStringLiteral("name"), QStringLiteral("Python")},
             {QStringLiteral("spdx"), QStringLiteral("PSF-2.0")},
             {QStringLiteral("url"), QStringLiteral("https://docs.python.org/3/license.html")}}}},
        {QStringLiteral("signature"), QJsonObject{
             {QStringLiteral("status"), QStringLiteral("unsigned-test")},
             {QStringLiteral("algorithm"), QJsonValue::Null},
             {QStringLiteral("keyId"), QJsonValue::Null}}},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
             {QStringLiteral("path"), QStringLiteral("python.exe")},
             {QStringLiteral("bytes"), payload.size()},
             {QStringLiteral("sha256"), sha256(payload)}}}}
    };
    QJsonObject result = metadata;
    result.insert(QStringLiteral("packageUrl"), url.toString());
    result.insert(QStringLiteral("packageBytes"), archive.size());
    result.insert(QStringLiteral("packageSha256"), sha256(archive));
    result.insert(QStringLiteral("installedBytes"), payload.size());
    result.insert(QStringLiteral("entryCount"), 1);
    return result;
}

class HttpArchiveServer final : public QTcpServer {
public:
    explicit HttpArchiveServer(QByteArray body, int bodyDelayMs = 0, QObject* parent = nullptr)
        : QTcpServer(parent), body_(std::move(body)), bodyDelayMs_(bodyDelayMs)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    if (!request.contains("\r\n\r\n")) return;
                    const bool head = request.startsWith("HEAD ");
                    qint64 offset = 0;
                    const qsizetype range = request.indexOf("Range: bytes=");
                    if (range >= 0) {
                        const qsizetype begin = range + qsizetype(sizeof("Range: bytes=") - 1);
                        const qsizetype end = request.indexOf('-', begin);
                        offset = request.mid(begin, end - begin).toLongLong();
                        sawRange_ = offset > 0;
                    }
                    const QByteArray responseBody = body_.mid(offset);
                    QByteArray headers = offset > 0 ? "HTTP/1.1 206 Partial Content\r\n"
                                                    : "HTTP/1.1 200 OK\r\n";
                    headers += "Content-Length: " + QByteArray::number(responseBody.size()) + "\r\n";
                    if (offset > 0) headers += "Content-Range: bytes " + QByteArray::number(offset)
                                               + "-" + QByteArray::number(body_.size() - 1)
                                               + "/" + QByteArray::number(body_.size()) + "\r\n";
                    headers += "Connection: close\r\n\r\n";
                    socket->write(headers);
                    if (head) {
                        socket->disconnectFromHost();
                    } else if (bodyDelayMs_ > 0 && responseBody.size() > 512) {
                        socket->write(responseBody.left(512));
                        socket->flush();
                        const QPointer<QTcpSocket> guarded(socket);
                        QTimer::singleShot(bodyDelayMs_, this, [guarded, responseBody] {
                            if (guarded == nullptr) return;
                            guarded->write(responseBody.mid(512));
                            guarded->disconnectFromHost();
                        });
                    } else {
                        socket->write(responseBody);
                        socket->disconnectFromHost();
                    }
                });
            }
        });
        Q_ASSERT(listen(QHostAddress::LocalHost));
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/runtime.zip").arg(serverPort())); }
    bool sawRange() const { return sawRange_; }

private:
    QByteArray body_;
    int bodyDelayMs_ = 0;
    bool sawRange_ = false;
};

bool awaitInstalledResolution(VoiceCloneRuntimePackageManager* manager,
                              const QString& runtimeId,
                              VoiceCloneRuntimeResolution* result)
{
    QSignalSpy resolved(manager, &VoiceCloneRuntimePackageManager::installedResolved);
    const quint64 requestId = manager->resolveInstalledAsync(
        runtimeId, QStringLiteral("qwen"), QStringLiteral("1.0.0"), 1);
    if (requestId == 0 || !resolved.wait(10000) || resolved.count() != 1
        || resolved.at(0).at(0).toULongLong() != requestId) return false;
    result->root = resolved.at(0).at(1).toString();
    result->error = resolved.at(0).at(2).toString();
    return true;
}

}

class VoiceCloneRuntimePackageTest final : public QObject {
    Q_OBJECT

private slots:
    void requiresDeclaredInstalledSizeAndEntryCount();
    void rejectsFuturePlayerVersionAndAcceptsCurrentBoundary();
    void productionPolicyRejectsSignedLookingMetadataWithoutVerification();
    void strictManifestRejectsWrongCompatibilityAndUnsignedProduction();
    void acceptsWindowsSafeSpacesAndRejectsUnsafeCharacters();
    void resolvesInstalledAsynchronouslyWithoutBlockingHeartbeat();
    void cancelsVerificationWithoutStaleCommit();
    void downloadsVerifiesExtractsAndCommitsStoredZip();
    void rejectsCorruptArchiveAndTraversalWithoutReplacingInstalledRuntime();
    void rejectsZipSymlinkBeforeExtraction();
    void rejectsUnsafeZipMetadataBeforeExtraction_data();
    void rejectsUnsafeZipMetadataBeforeExtraction();
    void rejectsRuntimeWhenVerifiedInstallWouldExceedFreeSpace();
    void resumesPartialAndRejectsUnsafeInstalledFolder();
    void disabledFeedIsHonestAndContainsNoFakeRelease();
    void failedAtomicSwapRestoresPreviousRuntime();
};

void VoiceCloneRuntimePackageTest::resolvesInstalledAsynchronouslyWithoutBlockingHeartbeat()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload(16 * 1024 * 1024, 'r');
    const QByteArray archive("fixture-archive");
    QJsonObject object = manifestObject(QUrl(QStringLiteral("http://127.0.0.1/fixture")),
                                        archive, payload);
    const QString versionRoot = QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0"));
    QVERIFY(QDir().mkpath(versionRoot));
    QFile runtime(QDir(versionRoot).filePath(QStringLiteral("python.exe")));
    QVERIFY(runtime.open(QIODevice::WriteOnly));
    QCOMPARE(runtime.write(payload), payload.size());
    runtime.close();
    QFile marker(QDir(versionRoot).filePath(QStringLiteral("agplayer-runtime.json")));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    QVERIFY(marker.write(QJsonDocument(object).toJson()) > 0);
    marker.close();

    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QSignalSpy resolved(&manager, &VoiceCloneRuntimePackageManager::installedResolved);
    int heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    connect(&heartbeat, &QTimer::timeout, this, [&heartbeats] { ++heartbeats; });
    heartbeat.start();
    QElapsedTimer elapsed;
    elapsed.start();
    const quint64 requestId = manager.resolveInstalledAsync(
        QStringLiteral("qwen-shared"), QStringLiteral("qwen"), QStringLiteral("1.0.0"), 1);
    QVERIFY(requestId > 0);
    QVERIFY2(elapsed.elapsed() < 50, "resolveInstalledAsync blocked its caller");
    QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 10000);
    QCOMPARE(resolved.at(0).at(0).toULongLong(), requestId);
    QCOMPARE(resolved.at(0).at(1).toString(), QDir::fromNativeSeparators(versionRoot));
    QVERIFY(resolved.at(0).at(2).toString().isEmpty());
    QVERIFY2(heartbeats > 0, "UI heartbeat did not run while installed Runtime was hashed");
}

void VoiceCloneRuntimePackageTest::cancelsVerificationWithoutStaleCommit()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload(8 * 1024 * 1024, 'v');
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        manifestObject(server.url(), archive, payload),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    int heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    connect(&heartbeat, &QTimer::timeout, this, [&heartbeats] { ++heartbeats; });
    connect(&manager, &VoiceCloneRuntimePackageManager::stateChanged, this, [&manager] {
        if (manager.state() == VoiceCloneRuntimePackageManager::Verifying)
            QTimer::singleShot(0, &manager, &VoiceCloneRuntimePackageManager::cancel);
    });
    heartbeat.start();
    manager.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceCloneRuntimePackageManager::Canceled, 10000);
    QTest::qWait(250);
    QCOMPARE(manager.state(), VoiceCloneRuntimePackageManager::Canceled);
    QVERIFY(heartbeats > 0);
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0"))));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(
        QStringLiteral("qwen-shared/.runtime-qwen-1.0.0.staging"))));
}

void VoiceCloneRuntimePackageTest::acceptsWindowsSafeSpacesAndRejectsUnsafeCharacters()
{
    const QByteArray payload("template");
    const QString safePath = QStringLiteral("Lib/site-packages/setuptools/script (dev).tmpl");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload},
                                          {safePath.toUtf8(), payload}});
    HttpArchiveServer server(archive);
    QJsonObject object = manifestObject(server.url(), archive, payload);
    object.insert(QStringLiteral("files"), QJsonArray{
        QJsonObject{{QStringLiteral("path"), QStringLiteral("python.exe")},
                    {QStringLiteral("bytes"), payload.size()},
                    {QStringLiteral("sha256"), sha256(payload)}},
        QJsonObject{{QStringLiteral("path"), safePath},
                    {QStringLiteral("bytes"), payload.size()},
                    {QStringLiteral("sha256"), sha256(payload)}}});
    object.insert(QStringLiteral("installedBytes"), payload.size() * 2);
    object.insert(QStringLiteral("entryCount"), 2);
    const auto safe = VoiceCloneRuntimePackageManifest::fromJson(
        object, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY2(safe.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest),
             qPrintable(safe.errorString(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest)));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(safe);
    QTRY_VERIFY_WITH_TIMEOUT(manager.state() == VoiceCloneRuntimePackageManager::Completed
                             || manager.state() == VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY2(manager.state() == VoiceCloneRuntimePackageManager::Completed,
             qPrintable(manager.errorString()));

    const QStringList unsafePaths{
        QStringLiteral("python.exe:stream"),
        QStringLiteral("bad\x01name.txt"),
        QStringLiteral("trailing-dot."),
        QStringLiteral("folder/trailing-space "),
        QStringLiteral("CON.txt"),
        QStringLiteral("../escape.exe")};
    for (const QString& unsafePath : unsafePaths) {
        QJsonObject unsafeObject = object;
        unsafeObject.insert(QStringLiteral("files"), QJsonArray{
            QJsonObject{{QStringLiteral("path"), QStringLiteral("python.exe")},
                        {QStringLiteral("bytes"), payload.size()},
                        {QStringLiteral("sha256"), sha256(payload)}},
            QJsonObject{{QStringLiteral("path"), unsafePath},
                        {QStringLiteral("bytes"), payload.size()},
                        {QStringLiteral("sha256"), sha256(payload)}}});
        const auto unsafe = VoiceCloneRuntimePackageManifest::fromJson(
            unsafeObject, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
        QVERIFY2(!unsafe.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest),
                 qPrintable(unsafePath));
    }
}

void VoiceCloneRuntimePackageTest::requiresDeclaredInstalledSizeAndEntryCount()
{
    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    QJsonObject object = manifestObject(server.url(), archive);
    object.remove(QStringLiteral("installedBytes"));
    object.remove(QStringLiteral("entryCount"));
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        object,
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);

    QVERIFY(!manifest.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest));
    QVERIFY(manifest.errorString(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest)
                .contains(QStringLiteral("installed"), Qt::CaseInsensitive));
}

void VoiceCloneRuntimePackageTest::rejectsFuturePlayerVersionAndAcceptsCurrentBoundary()
{
    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    const QVersionNumber current = QVersionNumber::fromString(QStringLiteral(AGPLAYER_VERSION));
    QVERIFY(current.segmentCount() == 3);

    QJsonObject boundary = manifestObject(server.url(), archive);
    boundary.insert(QStringLiteral("minimumPlayerVersion"), current.toString());
    const auto accepted = VoiceCloneRuntimePackageManifest::fromJson(
        boundary, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY2(accepted.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest),
             qPrintable(accepted.errorString(
                 VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest)));

    QJsonObject future = boundary;
    future.insert(QStringLiteral("minimumPlayerVersion"),
                  QVersionNumber(current.majorVersion(), current.minorVersion(),
                                 current.microVersion() + 1).toString());
    const auto rejected = VoiceCloneRuntimePackageManifest::fromJson(
        future, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY(!rejected.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest));
    QVERIFY(rejected.errorString(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest)
                .contains(QStringLiteral("player version"), Qt::CaseInsensitive));
}

void VoiceCloneRuntimePackageTest::productionPolicyRejectsSignedLookingMetadataWithoutVerification()
{
    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    QJsonObject package = manifestObject(
        QUrl(QStringLiteral("https://downloads.agplayer.cn/runtime/runtime-qwen.zip")), archive);
    package.insert(QStringLiteral("signature"), QJsonObject{
        {QStringLiteral("status"), QStringLiteral("signed")},
        {QStringLiteral("algorithm"), QStringLiteral("ed25519")},
        {QStringLiteral("keyId"), QStringLiteral("release-1")}});
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        package, VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly);
    QVERIFY(!manifest.isValid(VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly));
    QVERIFY(manifest.errorString(VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly)
                .contains(QStringLiteral("verification unavailable"), Qt::CaseInsensitive));

    const QJsonObject feed{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("reason"), QStringLiteral("signed-looking release")},
        {QStringLiteral("signature"), QJsonObject{
             {QStringLiteral("status"), QStringLiteral("signed")},
             {QStringLiteral("algorithm"), QStringLiteral("ed25519")},
             {QStringLiteral("keyId"), QStringLiteral("release-1")}}},
        {QStringLiteral("runtimes"), QJsonArray{package}}};
    const auto parsedFeed = VoiceCloneRuntimeFeed::fromJson(
        QJsonDocument(feed).toJson(), VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly);
    QVERIFY(!parsedFeed.isValid());
    QVERIFY(parsedFeed.error.contains(QStringLiteral("verification unavailable"),
                                      Qt::CaseInsensitive));
}

void VoiceCloneRuntimePackageTest::strictManifestRejectsWrongCompatibilityAndUnsignedProduction()
{
    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    const auto accepted = VoiceCloneRuntimePackageManifest::fromJson(
        manifestObject(server.url(), archive), VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY2(accepted.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest),
             qPrintable(accepted.errorString(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest)));
    QVERIFY(!accepted.isValid(VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly));

    QJsonObject wrong = manifestObject(server.url(), archive);
    wrong.insert(QStringLiteral("protocolVersion"), 2);
    QVERIFY(!VoiceCloneRuntimePackageManifest::fromJson(
        wrong, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest).isValid(
            VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest));
    wrong = manifestObject(server.url(), archive);
    wrong.insert(QStringLiteral("runtimeId"), QStringLiteral("indextts25"));
    QVERIFY(!VoiceCloneRuntimePackageManifest::fromJson(
        wrong, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest).isValid(
            VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest));

    const QJsonObject feedObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("reason"), QStringLiteral("injected test feed")},
        {QStringLiteral("signature"), QJsonObject{
             {QStringLiteral("status"), QStringLiteral("unsigned-test")},
             {QStringLiteral("algorithm"), QJsonValue::Null},
             {QStringLiteral("keyId"), QJsonValue::Null}}},
        {QStringLiteral("runtimes"), QJsonArray{manifestObject(server.url(), archive)}}};
    const auto feed = VoiceCloneRuntimeFeed::fromJson(
        QJsonDocument(feedObject).toJson(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY2(feed.isValid(), qPrintable(feed.error));
    QCOMPARE(feed.runtimes.size(), 1);
}

void VoiceCloneRuntimePackageTest::downloadsVerifiesExtractsAndCommitsStoredZip()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        manifestObject(server.url(), archive), VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(manifest);
    QTRY_VERIFY_WITH_TIMEOUT(manager.state() == VoiceCloneRuntimePackageManager::Completed
                             || manager.state() == VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY2(manager.state() == VoiceCloneRuntimePackageManager::Completed,
             qPrintable(manager.errorString()));
    QCOMPARE(manager.phase(), QStringLiteral("ready"));
    QFile installed(QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0/python.exe")));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), payload);
    VoiceCloneRuntimeResolution resolution;
    QVERIFY(awaitInstalledResolution(&manager, QStringLiteral("qwen-shared"), &resolution));
    QVERIFY2(resolution.isValid(), qPrintable(resolution.error));
}

void VoiceCloneRuntimePackageTest::rejectsCorruptArchiveAndTraversalWithoutReplacingInstalledRuntime()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0"));
    QVERIFY(QDir().mkpath(target));
    QFile old(QDir(target).filePath(QStringLiteral("old.marker")));
    QVERIFY(old.open(QIODevice::WriteOnly)); QVERIFY(old.write("old") == 3); old.close();

    QByteArray archive = storedZip({{QByteArray("../escape.exe"), QByteArray("bad")}});
    HttpArchiveServer server(archive);
    QJsonObject object = manifestObject(server.url(), archive, QByteArray("bad"));
    QJsonArray files{QJsonObject{{QStringLiteral("path"), QStringLiteral("../escape.exe")},
                                 {QStringLiteral("bytes"), 3},
                                 {QStringLiteral("sha256"), sha256(QByteArray("bad"))}}};
    object.insert(QStringLiteral("files"), files);
    const auto traversal = VoiceCloneRuntimePackageManifest::fromJson(
        object, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY(!traversal.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest));
    QVERIFY(QFileInfo::exists(old.fileName()));

    archive = storedZip({{QByteArray("python.exe"), QByteArray("new")}});
    HttpArchiveServer corruptServer(archive);
    object = manifestObject(corruptServer.url(), archive, QByteArray("new"));
    object.insert(QStringLiteral("packageSha256"), QString(64, QLatin1Char('0')));
    const auto corrupt = VoiceCloneRuntimePackageManifest::fromJson(
        object, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(corrupt);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY(QFileInfo::exists(old.fileName()));
}

void VoiceCloneRuntimePackageTest::rejectsZipSymlinkBeforeExtraction()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload("worker-runtime");
    QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    const qsizetype central = archive.indexOf(QByteArray::fromHex("504b0102"));
    QVERIFY(central >= 0);
    archive[central + 5] = char(3); // Unix creator OS.
    overwrite32(archive, central + 38, quint32(0120777U << 16U));

    HttpArchiveServer server(archive);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        manifestObject(server.url(), archive, payload),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY2(manager.errorString().contains(QStringLiteral("unsafe ZIP entry type")),
             qPrintable(manager.errorString()));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0"))));
}

void VoiceCloneRuntimePackageTest::rejectsUnsafeZipMetadataBeforeExtraction_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("errorFragment");
    QTest::newRow("encrypted") << QStringLiteral("encrypted") << QStringLiteral("encrypted");
    QTest::newRow("zip64") << QStringLiteral("zip64") << QStringLiteral("ZIP64");
    QTest::newRow("unsupported-method") << QStringLiteral("method") << QStringLiteral("method");
    QTest::newRow("compression-ratio") << QStringLiteral("ratio") << QStringLiteral("ratio");
    QTest::newRow("overlapping-local-records") << QStringLiteral("overlap")
                                                << QStringLiteral("overlap");
    QTest::newRow("truncated-eocd") << QStringLiteral("truncated")
                                     << QStringLiteral("central directory");
    QTest::newRow("installed-size-mismatch") << QStringLiteral("installed")
                                              << QStringLiteral("installed size");
}

void VoiceCloneRuntimePackageTest::rejectsUnsafeZipMetadataBeforeExtraction()
{
    QFETCH(QString, kind);
    QFETCH(QString, errorFragment);
    const QByteArray payload("worker-runtime");
    QByteArray archive;
    QJsonArray files;
    qint64 installedBytes = payload.size();
    int entryCount = 1;
    if (kind == QStringLiteral("overlap")) {
        archive = storedZip({{QByteArray("python.exe"), QByteArray("a")},
                             {QByteArray("helper.txt"), QByteArray("b")}});
        const qsizetype central = archive.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central >= 0);
        overwrite32(archive, 18, 2);
        overwrite32(archive, 22, 2);
        overwrite32(archive, central + 20, 2);
        overwrite32(archive, central + 24, 2);
        files = QJsonArray{
            QJsonObject{{QStringLiteral("path"), QStringLiteral("python.exe")},
                        {QStringLiteral("bytes"), 2},
                        {QStringLiteral("sha256"), sha256(QByteArray("a"))}},
            QJsonObject{{QStringLiteral("path"), QStringLiteral("helper.txt")},
                        {QStringLiteral("bytes"), 1},
                        {QStringLiteral("sha256"), sha256(QByteArray("b"))}}};
        installedBytes = 3;
        entryCount = 2;
    } else {
        archive = storedZip({{QByteArray("python.exe"), payload}});
        const qsizetype central = archive.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central >= 0);
        if (kind == QStringLiteral("encrypted")) {
            overwrite16(archive, 6, 1);
            overwrite16(archive, central + 8, 1);
        } else if (kind == QStringLiteral("zip64")) {
            overwrite32(archive, central + 20, 0xffffffffU);
        } else if (kind == QStringLiteral("method")) {
            overwrite16(archive, 8, 99);
            overwrite16(archive, central + 10, 99);
        } else if (kind == QStringLiteral("ratio")) {
            constexpr quint32 expanded = 4U * 1024U * 1024U;
            overwrite16(archive, 8, 8);
            overwrite16(archive, central + 10, 8);
            overwrite32(archive, 22, expanded);
            overwrite32(archive, central + 24, expanded);
            installedBytes = expanded;
            files = QJsonArray{QJsonObject{
                {QStringLiteral("path"), QStringLiteral("python.exe")},
                {QStringLiteral("bytes"), installedBytes},
                {QStringLiteral("sha256"), sha256(payload)}}};
        } else if (kind == QStringLiteral("truncated")) {
            archive.chop(1);
        } else if (kind == QStringLiteral("installed")) {
            overwrite16(archive, 8, 8);
            overwrite16(archive, central + 10, 8);
            overwrite32(archive, 22, quint32(payload.size() + 1));
            overwrite32(archive, central + 24, quint32(payload.size() + 1));
        }
    }

    HttpArchiveServer server(archive);
    QJsonObject object = manifestObject(server.url(), archive, payload);
    if (!files.isEmpty()) object.insert(QStringLiteral("files"), files);
    object.insert(QStringLiteral("installedBytes"), installedBytes);
    object.insert(QStringLiteral("entryCount"), entryCount);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        object, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY2(manifest.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest),
             qPrintable(manifest.errorString(
                 VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest)));
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceCloneRuntimePackageManager manager(
        root.path(), VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY2(manager.errorString().contains(errorFragment, Qt::CaseInsensitive),
             qPrintable(manager.errorString()));
    QVERIFY(!QFileInfo::exists(
        QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0"))));
}

void VoiceCloneRuntimePackageTest::rejectsRuntimeWhenVerifiedInstallWouldExceedFreeSpace()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    QJsonObject object = manifestObject(server.url(), archive, payload);
    object.insert(QStringLiteral("packageBytes"), 100.0 * 1024 * 1024 * 1024 * 1024);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        object, VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    QVERIFY(manifest.isValid(VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest));
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(manifest);
    QCOMPARE(manager.state(), VoiceCloneRuntimePackageManager::Failed);
    QVERIFY(manager.errorString().contains(QStringLiteral("disk space")));
}

void VoiceCloneRuntimePackageTest::resumesPartialAndRejectsUnsafeInstalledFolder()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload(4096, 'r');
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive, 1500);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        manifestObject(server.url(), archive, payload),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneRuntimePackageManager manager(root.path(),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(manifest);
    QTRY_VERIFY_WITH_TIMEOUT(manager.state() == VoiceCloneRuntimePackageManager::Downloading
                             && manager.transferredBytes() >= 512, 2000);
    QVERIFY(manager.pause());
    QCOMPARE(manager.state(), VoiceCloneRuntimePackageManager::Paused);
    QVERIFY(manager.retry());
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceCloneRuntimePackageManager::Downloading, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.sawRange(), 2000);
    manager.cancel();
    QCOMPARE(manager.state(), VoiceCloneRuntimePackageManager::Canceled);
    QVERIFY(manager.retry());
    QTRY_VERIFY_WITH_TIMEOUT(manager.state() == VoiceCloneRuntimePackageManager::Completed
                             || manager.state() == VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY2(manager.state() == VoiceCloneRuntimePackageManager::Completed,
             qPrintable(manager.errorString()));
    QVERIFY(server.sawRange());

#ifdef Q_OS_WIN
    const QString external = root.filePath(QStringLiteral("external"));
    const QString junction = QDir(root.path()).filePath(QStringLiteral("qwen-shared/2.0.0"));
    QVERIFY(QDir().mkpath(external));
    QVERIFY(QProcess::execute(QStringLiteral("cmd.exe"),
        {QStringLiteral("/d"), QStringLiteral("/c"), QStringLiteral("mklink"),
         QStringLiteral("/J"), QDir::toNativeSeparators(junction),
         QDir::toNativeSeparators(external)}) == 0);
    VoiceCloneRuntimeResolution unsafe;
    QVERIFY(awaitInstalledResolution(&manager, QStringLiteral("qwen-shared"), &unsafe));
    QVERIFY(unsafe.isValid());
    QVERIFY(unsafe.root.endsWith(QStringLiteral("/qwen-shared/1.0.0")));
#endif
}

void VoiceCloneRuntimePackageTest::failedAtomicSwapRestoresPreviousRuntime()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = QDir(root.path()).filePath(QStringLiteral("qwen-shared/1.0.0"));
    QVERIFY(QDir().mkpath(target));
    QFile old(QDir(target).filePath(QStringLiteral("old.marker")));
    QVERIFY(old.open(QIODevice::WriteOnly)); QVERIFY(old.write("old") == 3); old.close();

    const QByteArray payload("worker-runtime");
    const QByteArray archive = storedZip({{QByteArray("python.exe"), payload}});
    HttpArchiveServer server(archive);
    const auto manifest = VoiceCloneRuntimePackageManifest::fromJson(
        manifestObject(server.url(), archive),
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    VoiceCloneRuntimePackageManager::DeploymentOperations operations;
    operations.renameDirectory = [](const QString& source, const QString& destination) {
        if (source.endsWith(QStringLiteral(".staging"))) return false;
        return QDir().rename(source, destination);
    };
    operations.removeDirectory = [](const QString& path) { return QDir(path).removeRecursively(); };
    VoiceCloneRuntimePackageManager manager(
        root.path(), nullptr, operations,
        VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest);
    manager.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceCloneRuntimePackageManager::Failed, 5000);
    QVERIFY(QFileInfo::exists(old.fileName()));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".rollback")));
}

void VoiceCloneRuntimePackageTest::disabledFeedIsHonestAndContainsNoFakeRelease()
{
    QFile file(QStringLiteral(AGPLAYER_VOICE_CLONE_RUNTIME_FEED));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto feed = VoiceCloneRuntimeFeed::fromJson(file.readAll());
    QVERIFY2(feed.isValid(), qPrintable(feed.error));
    QVERIFY(!feed.enabled);
    QCOMPARE(feed.signatureStatus, QStringLiteral("unsigned-test"));
    QVERIFY(feed.runtimes.isEmpty());
}

QTEST_GUILESS_MAIN(VoiceCloneRuntimePackageTest)
#include "voice_clone_runtime_package_test.moc"
