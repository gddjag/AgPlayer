#include "voice_clone_package_manager.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace agplayer::voice_clone;

namespace {

QByteArray sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(bytes) == bytes.size();
}

bool markHidden(const QString& path)
{
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
           && SetFileAttributesW(native.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
#else
    return QFileInfo(path).fileName().startsWith(QLatin1Char('.'));
#endif
}

struct HttpResource {
    QByteArray body;
    qint64 advertisedLength = -1;
    bool omitLength = false;
    bool delayed = false;
    bool dropFirstGet = false;
};

class LocalHttpFixture final : public QObject {
    Q_OBJECT

public:
    explicit LocalHttpFixture(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                buffers_.insert(socket, {});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray& buffer = buffers_[socket];
                    buffer += socket->readAll();
                    if (!buffer.contains("\r\n\r\n")) return;
                    const QByteArray request = buffer;
                    buffers_.remove(socket);
                    serve(socket, request);
                });
            }
        });
        QVERIFY(server_.listen(QHostAddress::LocalHost));
    }

    QUrl url(const QString& path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(server_.serverPort())
                        .arg(path));
    }

    void add(const QString& path, HttpResource resource)
    {
        resources_.insert(path.toUtf8(), std::move(resource));
    }

    int headCount(const QString& path) const { return headCounts_.value(path.toUtf8()); }
    int getCount(const QString& path) const { return getCounts_.value(path.toUtf8()); }
    QList<qint64> ranges(const QString& path) const { return ranges_.value(path.toUtf8()); }

private:
    void serve(QTcpSocket* socket, const QByteArray& request)
    {
        const QList<QByteArray> lines = request.split('\n');
        const QList<QByteArray> requestParts = lines.value(0).trimmed().split(' ');
        if (requestParts.size() < 2 || !resources_.contains(requestParts.at(1))) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QByteArray method = requestParts.at(0);
        const QByteArray path = requestParts.at(1);
        const HttpResource resource = resources_.value(path);
        const qint64 total = resource.advertisedLength >= 0
                                 ? resource.advertisedLength
                                 : resource.body.size();
        qint64 rangeOffset = 0;
        for (const QByteArray& rawLine : lines) {
            const QByteArray line = rawLine.trimmed();
            if (line.toLower().startsWith("range: bytes=")) {
                bool ok = false;
                const qint64 parsed = line.mid(line.indexOf('=') + 1)
                                          .split('-').value(0).toLongLong(&ok);
                if (ok) rangeOffset = parsed;
            }
        }

        if (method == "HEAD") {
            ++headCounts_[path];
            QByteArray response = "HTTP/1.1 200 OK\r\nETag: fixture-v1\r\n";
            if (!resource.omitLength) {
                response += "Content-Length: " + QByteArray::number(total) + "\r\n";
            }
            response += "Connection: close\r\n\r\n";
            socket->write(response);
            socket->disconnectFromHost();
            return;
        }

        ++getCounts_[path];
        ranges_[path].append(rangeOffset);
        const QByteArray body = resource.body.mid(rangeOffset);
        const bool partial = rangeOffset > 0;
        QByteArray response = partial ? "HTTP/1.1 206 Partial Content\r\n"
                                      : "HTTP/1.1 200 OK\r\n";
        response += "ETag: fixture-v1\r\n";
        if (partial) {
            response += "Content-Range: bytes " + QByteArray::number(rangeOffset) + "-"
                        + QByteArray::number(resource.body.size() - 1) + "/"
                        + QByteArray::number(resource.body.size()) + "\r\n";
        }
        if (resource.omitLength) response += "Transfer-Encoding: chunked\r\n";
        else response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        socket->write(response);

        auto writePayload = [socket](const QByteArray& payload, const bool chunked, const bool finish) {
            if (!socket || socket->state() == QAbstractSocket::UnconnectedState) return;
            if (chunked) {
                if (!payload.isEmpty()) {
                    socket->write(QByteArray::number(payload.size(), 16) + "\r\n" + payload + "\r\n");
                }
                if (finish) socket->write("0\r\n\r\n");
            } else {
                socket->write(payload);
            }
            if (finish) socket->disconnectFromHost();
        };

        if (resource.dropFirstGet && getCounts_.value(path) == 1) {
            writePayload(body.left(qMax<qsizetype>(1, body.size() / 3)), false, false);
            socket->flush();
            QPointer<QTcpSocket> guarded(socket);
            QTimer::singleShot(25, this, [guarded] {
                if (guarded) guarded->abort();
            });
            return;
        }
        if (resource.delayed && body.size() > 1) {
            const qsizetype split = qMax<qsizetype>(1, body.size() / 3);
            writePayload(body.left(split), resource.omitLength, false);
            QPointer<QTcpSocket> guarded(socket);
            QTimer::singleShot(250, this, [guarded, tail = body.mid(split), chunked = resource.omitLength] {
                if (!guarded || guarded->state() == QAbstractSocket::UnconnectedState) return;
                if (chunked) {
                    guarded->write(QByteArray::number(tail.size(), 16) + "\r\n" + tail
                                   + "\r\n0\r\n\r\n");
                } else {
                    guarded->write(tail);
                }
                guarded->disconnectFromHost();
            });
            return;
        }
        writePayload(body, resource.omitLength, true);
    }

    QTcpServer server_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    QHash<QByteArray, HttpResource> resources_;
    QHash<QByteArray, int> headCounts_;
    QHash<QByteArray, int> getCounts_;
    QHash<QByteArray, QList<qint64>> ranges_;
};

class ControlledReply final : public QNetworkReply {
public:
    ControlledReply(QNetworkAccessManager::Operation operation,
                    const QNetworkRequest& request,
                    QByteArray body,
                    qint64 contentLength,
                    int completionDelayMs,
                    int abortDelayMs,
                    QObject* parent)
        : QNetworkReply(parent), body_(std::move(body)), abortDelayMs_(abortDelayMs)
    {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setHeader(QNetworkRequest::ContentLengthHeader, contentLength);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(completionDelayMs, this, [this] {
            if (aborted_ || finished_) return;
            if (!body_.isEmpty()) emit readyRead();
            finished_ = true;
            emit finished();
        });
    }

    void abort() override
    {
        if (aborted_ || finished_) return;
        aborted_ = true;
        QTimer::singleShot(abortDelayMs_, this, [this] {
            if (finished_) return;
            setError(QNetworkReply::OperationCanceledError, QStringLiteral("deferred abort"));
            finished_ = true;
            emit finished();
        });
    }

    qint64 bytesAvailable() const override
    {
        return body_.size() - offset_ + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maximum) override
    {
        const qint64 count = qMin<qint64>(maximum, body_.size() - offset_);
        if (count <= 0) return -1;
        std::memcpy(data, body_.constData() + offset_, static_cast<size_t>(count));
        offset_ += count;
        return count;
    }

private:
    QByteArray body_;
    qint64 offset_ = 0;
    int abortDelayMs_ = 0;
    bool aborted_ = false;
    bool finished_ = false;
};

class ControlledNetworkManager final : public QNetworkAccessManager {
public:
    void add(const QUrl& url, const QByteArray& body) { bodies_.insert(url, body); }

protected:
    QNetworkReply* createRequest(Operation operation,
                                 const QNetworkRequest& request,
                                 QIODevice* outgoingData) override
    {
        Q_UNUSED(outgoingData)
        const QByteArray body = bodies_.value(request.url());
        const bool stale = request.url().path().contains(QStringLiteral("stale-old"));
        return new ControlledReply(operation, request,
                                   operation == GetOperation ? body : QByteArray{},
                                   body.size(), stale ? 1000 : 0, stale ? 200 : 0, this);
    }

private:
    QHash<QUrl, QByteArray> bodies_;
};

VoiceClonePackageManifest packageManifest(const QString& id,
                                          const QString& relativePath,
                                          const QUrl& url,
                                          const QByteArray& body)
{
    VoiceClonePackageManifest manifest;
    manifest.packageId = id;
    manifest.modelId = QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    manifest.adapterId = QStringLiteral("qwen");
    manifest.version = QStringLiteral("1.0.0");
    manifest.revision = QStringLiteral("fixture-r1");
    manifest.licenseUrl = QUrl(QStringLiteral("https://example.test/license"));
    manifest.licenseRevision = QStringLiteral("license-r1");
    manifest.files.append({relativePath, url, sha256(body)});
    return manifest;
}

bool createWindowsJunction(const QString& link, const QString& target)
{
#ifdef Q_OS_WIN
    return QProcess::execute(QStringLiteral("cmd.exe"),
                             {QStringLiteral("/d"), QStringLiteral("/c"),
                              QStringLiteral("mklink"), QStringLiteral("/J"),
                              QDir::toNativeSeparators(link),
                              QDir::toNativeSeparators(target)}) == 0;
#else
    Q_UNUSED(link)
    Q_UNUSED(target)
    return false;
#endif
}

} // namespace

class VoiceClonePackageManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesStrictManifestAndRejectsUnsafeOrDuplicateTargets();
    void exposesRealLengthAndIndeterminateProgress();
    void resumesPersistedPartialDownloadWithHttpRange();
    void cancelRemovesPartialAndRetryContinuesIt();
    void rejectsInsufficientDiskBeforeGet();
    void hashMismatchNeverReplacesInstalledVersion();
    void commitsCompleteStagingAndRollsBackFailedSwap();
    void recordsExactIndexLicenseAcceptanceOnly();
    void rejectsReparseStagingAndTargetDirectories();
    void rejectsWindowsAliasedTargets();
    void removesUnlistedStagingContentBeforeCommit();
    void derivesLicenseGateFromIndexLicenseIdentity();
    void rejectsCorruptLicenseAcceptanceStoreWithoutOverwrite();
    void rejectsHiddenUnlistedContentAddedDuringDownload();
    void stalePausedReplyCannotCorruptNewOperation();
    void staleCanceledReplyCannotCorruptNewOperation();
    void rejectsNonexistentInstallRootBelowReparseAncestorWithoutWriting();
    void restartsUnknownLengthPartialFromZero();
    void restoresOldTargetWhenActivatedRenameFails();
    void rejectsTargetDescendantReparseBeforeSwap();
    void keepsCompletedStateWhenBackupCleanupFails();
    void finalizesAlreadyCompletePartialWithoutEofRange();
    void recoversInterruptedSwapFromRollbackDirectory();
};

void VoiceClonePackageManagerTest::parsesStrictManifestAndRejectsUnsafeOrDuplicateTargets()
{
    const auto file = [](const QString& path) {
        return QJsonObject{{QStringLiteral("path"), path},
                           {QStringLiteral("url"), QStringLiteral("https://example.test/file")},
                           {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))}};
    };
    QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("packageId"), QStringLiteral("qwen-model")},
                       {QStringLiteral("modelId"), QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")},
                       {QStringLiteral("adapterId"), QStringLiteral("qwen")},
                       {QStringLiteral("version"), QStringLiteral("1.0.0")},
                       {QStringLiteral("revision"), QStringLiteral("r1")},
                       {QStringLiteral("license"),
                        QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.test/license")},
                                    {QStringLiteral("revision"), QStringLiteral("license-r1")}}},
                       {QStringLiteral("files"), QJsonArray{file(QStringLiteral("weights/model.bin"))}}};

    auto parsed = VoiceClonePackageManifest::fromJson(object);
    QVERIFY2(parsed.isValid(), qPrintable(parsed.errorString()));
    QCOMPARE(parsed.files.size(), 1);
    QCOMPARE(parsed.files.front().relativePath, QStringLiteral("weights/model.bin"));
    QCOMPARE(parsed.modelId, QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"));
    QCOMPARE(parsed.adapterId, QStringLiteral("qwen"));
    QVERIFY(!parsed.requiresLicenseAcceptance);
    QCOMPARE(parsed.licenseUrl, QUrl(QStringLiteral("https://example.test/license")));

    QJsonArray files{file(QStringLiteral("../escape.bin"))};
    object.insert(QStringLiteral("files"), files);
    QVERIFY(!VoiceClonePackageManifest::fromJson(object).isValid());
    files = {file(QDir::tempPath() + QStringLiteral("/absolute.bin"))};
    object.insert(QStringLiteral("files"), files);
    QVERIFY(!VoiceClonePackageManifest::fromJson(object).isValid());
    files = {file(QStringLiteral("weights/model.bin")),
             file(QStringLiteral("weights/./model.bin"))};
    object.insert(QStringLiteral("files"), files);
    const auto duplicate = VoiceClonePackageManifest::fromJson(object);
    QVERIFY(!duplicate.isValid());
    QVERIFY(duplicate.errorString().contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
}

void VoiceClonePackageManagerTest::exposesRealLengthAndIndeterminateProgress()
{
    LocalHttpFixture server;
    const QByteArray known(300, 'k');
    const QByteArray unknown(300, 'u');
    server.add(QStringLiteral("/known"), {known, -1, false, true, false});
    server.add(QStringLiteral("/unknown"), {unknown, -1, true, true, false});

    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManager knownManager(root.path());
    knownManager.start(packageManifest(QStringLiteral("known"), QStringLiteral("file.bin"),
                                       server.url(QStringLiteral("/known")), known));
    QTRY_VERIFY_WITH_TIMEOUT(knownManager.state() == VoiceClonePackageManager::Downloading, 2000);
    QCOMPARE(knownManager.totalBytes(), qint64(known.size()));
    QVERIFY(!knownManager.isProgressIndeterminate());
    QVERIFY(knownManager.progressPercent() >= 0);
    QTRY_COMPARE_WITH_TIMEOUT(knownManager.state(), VoiceClonePackageManager::Completed, 3000);
    QCOMPARE(knownManager.progressPercent(), 100);

    VoiceClonePackageManager unknownManager(root.path());
    unknownManager.start(packageManifest(QStringLiteral("unknown"), QStringLiteral("file.bin"),
                                         server.url(QStringLiteral("/unknown")), unknown));
    QTRY_VERIFY_WITH_TIMEOUT(unknownManager.state() == VoiceClonePackageManager::Downloading, 2000);
    QCOMPARE(unknownManager.totalBytes(), qint64(-1));
    QVERIFY(unknownManager.isProgressIndeterminate());
    QCOMPARE(unknownManager.progressPercent(), -1);
    QTRY_COMPARE_WITH_TIMEOUT(unknownManager.state(), VoiceClonePackageManager::Completed, 3000);
}

void VoiceClonePackageManagerTest::resumesPersistedPartialDownloadWithHttpRange()
{
    LocalHttpFixture server;
    const QByteArray body(600, 'r');
    server.add(QStringLiteral("/resume"), {body, -1, false, true, false});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto manifest = packageManifest(QStringLiteral("resume"), QStringLiteral("file.bin"),
                                          server.url(QStringLiteral("/resume")), body);
    {
        VoiceClonePackageManager manager(root.path());
        manager.start(manifest);
        QTRY_VERIFY_WITH_TIMEOUT(manager.transferredBytes() > 0, 2000);
        QVERIFY(manager.pause());
        QCOMPARE(manager.state(), VoiceClonePackageManager::Paused);
        QVERIFY(QFileInfo::exists(manager.partialFilePath()));
        QVERIFY(QFileInfo::exists(manager.resumeMetadataPath()));
    }

    VoiceClonePackageManager resumed(root.path());
    resumed.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(resumed.state(), VoiceClonePackageManager::Completed, 3000);
    QVERIFY(server.getCount(QStringLiteral("/resume")) >= 2);
    const QList<qint64> ranges = server.ranges(QStringLiteral("/resume"));
    QVERIFY(std::any_of(ranges.cbegin(), ranges.cend(), [](qint64 value) { return value > 0; }));
    QFile installed(root.filePath(QStringLiteral("resume/file.bin")));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), body);
}

void VoiceClonePackageManagerTest::cancelRemovesPartialAndRetryContinuesIt()
{
    LocalHttpFixture server;
    const QByteArray canceledBody(600, 'c');
    server.add(QStringLiteral("/cancel"), {canceledBody, -1, false, true, false});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManager canceled(root.path());
    canceled.start(packageManifest(QStringLiteral("cancel"), QStringLiteral("file.bin"),
                                   server.url(QStringLiteral("/cancel")), canceledBody));
    QTRY_VERIFY_WITH_TIMEOUT(canceled.transferredBytes() > 0, 2000);
    canceled.cancel();
    QCOMPARE(canceled.state(), VoiceClonePackageManager::Canceled);
    QVERIFY(!QFileInfo::exists(canceled.stagingDirectory()));

    const QByteArray retryBody(600, 'x');
    server.add(QStringLiteral("/retry"), {retryBody, -1, false, false, true});
    VoiceClonePackageManager retried(root.path());
    retried.start(packageManifest(QStringLiteral("retry"), QStringLiteral("file.bin"),
                                  server.url(QStringLiteral("/retry")), retryBody));
    QTRY_COMPARE_WITH_TIMEOUT(retried.state(), VoiceClonePackageManager::Failed, 2000);
    QVERIFY(QFileInfo::exists(retried.partialFilePath()));
    QVERIFY(retried.retry());
    QTRY_COMPARE_WITH_TIMEOUT(retried.state(), VoiceClonePackageManager::Completed, 3000);
    const QList<qint64> retryRanges = server.ranges(QStringLiteral("/retry"));
    QVERIFY(std::any_of(retryRanges.cbegin(), retryRanges.cend(),
                        [](qint64 value) { return value > 0; }));
}

void VoiceClonePackageManagerTest::rejectsInsufficientDiskBeforeGet()
{
    LocalHttpFixture server;
    const QByteArray body("small");
    server.add(QStringLiteral("/huge"), {body, (std::numeric_limits<qint64>::max)() / 2,
                                         false, false, false});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManager manager(root.path());
    manager.start(packageManifest(QStringLiteral("huge"), QStringLiteral("file.bin"),
                                  server.url(QStringLiteral("/huge")), body));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Failed, 2000);
    QVERIFY(manager.errorString().contains(QStringLiteral("disk"), Qt::CaseInsensitive)
            || manager.errorString().contains(QStringLiteral("space"), Qt::CaseInsensitive));
    QCOMPARE(server.getCount(QStringLiteral("/huge")), 0);
}

void VoiceClonePackageManagerTest::hashMismatchNeverReplacesInstalledVersion()
{
    LocalHttpFixture server;
    const QByteArray served("tampered");
    const QByteArray expected("expected");
    server.add(QStringLiteral("/bad"), {served});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath(QStringLiteral("model/file.bin")), "old-version"));
    VoiceClonePackageManager manager(root.path());
    manager.start(packageManifest(QStringLiteral("model"), QStringLiteral("file.bin"),
                                  server.url(QStringLiteral("/bad")), expected));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Failed, 2000);
    QVERIFY(manager.errorString().contains(QStringLiteral("SHA"), Qt::CaseInsensitive)
            || manager.errorString().contains(QStringLiteral("hash"), Qt::CaseInsensitive));
    QFile installed(root.filePath(QStringLiteral("model/file.bin")));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), QByteArray("old-version"));
}

void VoiceClonePackageManagerTest::commitsCompleteStagingAndRollsBackFailedSwap()
{
    LocalHttpFixture server;
    const QByteArray first(300, '1');
    const QByteArray second(300, '2');
    server.add(QStringLiteral("/first"), {first});
    server.add(QStringLiteral("/second"), {second, -1, false, true, false});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManifest manifest = packageManifest(
        QStringLiteral("atomic"), QStringLiteral("one.bin"),
        server.url(QStringLiteral("/first")), first);
    manifest.files.append({QStringLiteral("nested/two.bin"),
                           server.url(QStringLiteral("/second")), sha256(second)});
    VoiceClonePackageManager manager(root.path());
    manager.start(manifest);
    QTRY_VERIFY_WITH_TIMEOUT(manager.transferredBytes() >= first.size(), 2000);
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("atomic/one.bin"))));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 3000);
    QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("atomic/one.bin"))));
    QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("atomic/nested/two.bin"))));

    const QString target = root.filePath(QStringLiteral("rollback"));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("old.bin")), "old"));
    QString error;
    QVERIFY(!VoiceClonePackageManager::commitStagingDirectory(
        root.filePath(QStringLiteral("missing-staging")), target, &error));
    QVERIFY(!error.isEmpty());
    QFile old(QDir(target).filePath(QStringLiteral("old.bin")));
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("old"));

    const QString invalidTarget = root.filePath(QStringLiteral("target-is-file"));
    const QString safeStaging = root.filePath(QStringLiteral("safe-staging"));
    QVERIFY(writeFile(invalidTarget, "old-file"));
    QVERIFY(writeFile(QDir(safeStaging).filePath(QStringLiteral("new.bin")), "new"));
    error.clear();
    QVERIFY(!VoiceClonePackageManager::commitStagingDirectory(
        safeStaging, invalidTarget, &error));
    QFile preserved(invalidTarget);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray("old-file"));
    QVERIFY(QFileInfo::exists(QDir(safeStaging).filePath(QStringLiteral("new.bin"))));
}

void VoiceClonePackageManagerTest::recordsExactIndexLicenseAcceptanceOnly()
{
    LocalHttpFixture server;
    const QByteArray body("licensed");
    server.add(QStringLiteral("/index"), {body});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    auto index = packageManifest(QStringLiteral("index"), QStringLiteral("file.bin"),
                                 server.url(QStringLiteral("/index")), body);
    index.requiresLicenseAcceptance = true;
    index.modelId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    index.adapterId = QStringLiteral("indextts25");
    index.licenseUrl = QUrl(QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5"));
    index.licenseRevision = QStringLiteral("license-2026-08-13");

    VoiceClonePackageManager manager(root.path());
    manager.start(index);
    QCOMPARE(manager.state(), VoiceClonePackageManager::LicenseRequired);
    QCOMPARE(server.headCount(QStringLiteral("/index")), 0);
    QVERIFY(!manager.acceptLicense(index.licenseUrl, QStringLiteral("wrong-revision")));
    QVERIFY(manager.acceptLicense(index.licenseUrl, index.licenseRevision));
    QVERIFY(manager.hasLicenseAcceptance(index.modelId, index.adapterId, index.licenseUrl,
                                         index.licenseRevision));
    QVERIFY(!manager.hasLicenseAcceptance(index.modelId, index.adapterId, index.licenseUrl,
                                          QStringLiteral("other-revision")));
    QVERIFY(manager.retry());
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 2000);

    QFile record(manager.licenseAcceptancePath());
    QVERIFY(record.open(QIODevice::ReadOnly));
    const QJsonObject rootObject = QJsonDocument::fromJson(record.readAll()).object();
    QCOMPARE(rootObject.value(QStringLiteral("schemaVersion")).toInt(), 1);
    const QJsonObject acceptance = rootObject.value(QStringLiteral("records"))
                                       .toArray().first().toObject();
    QCOMPARE(acceptance.value(QStringLiteral("modelId")).toString(), index.modelId);
    QCOMPARE(acceptance.value(QStringLiteral("adapterId")).toString(), index.adapterId);
    QCOMPARE(acceptance.value(QStringLiteral("licenseUrl")).toString(),
             index.licenseUrl.toString());
    QCOMPARE(acceptance.value(QStringLiteral("revision")).toString(),
             index.licenseRevision);
    QVERIFY(!acceptance.value(QStringLiteral("acceptedAt")).toString().isEmpty());

    auto qwen = packageManifest(QStringLiteral("qwen"), QStringLiteral("file.bin"),
                                server.url(QStringLiteral("/index")), body);
    qwen.requiresLicenseAcceptance = false;
    qwen.licenseUrl = QUrl(QStringLiteral("https://github.com/QwenLM/Qwen3-TTS/blob/main/LICENSE"));
    VoiceClonePackageManager qwenManager(root.path());
    qwenManager.start(qwen);
    QVERIFY(qwenManager.state() != VoiceClonePackageManager::LicenseRequired);
    QCOMPARE(qwenManager.currentManifest().licenseUrl, qwen.licenseUrl);
}

void VoiceClonePackageManagerTest::rejectsReparseStagingAndTargetDirectories()
{
#ifdef Q_OS_WIN
    LocalHttpFixture server;
    const QByteArray body("safe");
    server.add(QStringLiteral("/safe"), {body});
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    const auto manifest = packageManifest(QStringLiteral("linked"), QStringLiteral("file.bin"),
                                          server.url(QStringLiteral("/safe")), body);
    QVERIFY(createWindowsJunction(root.filePath(QStringLiteral("linked")), outside.path()));
    VoiceClonePackageManager targetManager(root.path());
    targetManager.start(manifest);
    QCOMPARE(targetManager.state(), VoiceClonePackageManager::Failed);
    QVERIFY(targetManager.errorString().contains(QStringLiteral("reparse"), Qt::CaseInsensitive)
            || targetManager.errorString().contains(QStringLiteral("link"), Qt::CaseInsensitive));
    QVERIFY(QDir().rmdir(root.filePath(QStringLiteral("linked"))));

    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral(".staging"))));
    QVERIFY(createWindowsJunction(root.filePath(QStringLiteral(".staging/linked")), outside.path()));
    VoiceClonePackageManager stagingManager(root.path());
    stagingManager.start(manifest);
    QCOMPARE(stagingManager.state(), VoiceClonePackageManager::Failed);
    QVERIFY(stagingManager.errorString().contains(QStringLiteral("reparse"), Qt::CaseInsensitive)
            || stagingManager.errorString().contains(QStringLiteral("link"), Qt::CaseInsensitive));
    QVERIFY(QDir().rmdir(root.filePath(QStringLiteral(".staging/linked"))));
#else
    QSKIP("Reparse-point contract is Windows-specific.");
#endif
}

void VoiceClonePackageManagerTest::rejectsWindowsAliasedTargets()
{
    auto manifest = packageManifest(QStringLiteral("paths"), QStringLiteral("model.bin"),
                                    QUrl(QStringLiteral("https://example.test/model")), "x");
    manifest.files.append({QStringLiteral("model.bin."),
                           QUrl(QStringLiteral("https://example.test/model2")), sha256("y")});
    QVERIFY(!manifest.isValid());
    manifest.files[1].relativePath = QStringLiteral("weights/model.bin:stream");
    QVERIFY(!manifest.isValid());
    manifest.files[1].relativePath = QStringLiteral("CON/config.json");
    QVERIFY(!manifest.isValid());

    const QStringList unsafePackageIds{
        QStringLiteral("CON"), QStringLiteral("model."), QStringLiteral("model "),
        QStringLiteral(".staging"), QStringLiteral(".rollback"),
        QStringLiteral("other.rollback"), QStringLiteral("model:stream"),
        QStringLiteral("license-acceptances.json")};
    for (const QString& packageId : unsafePackageIds) {
        auto unsafePackage = packageManifest(packageId, QStringLiteral("file.bin"),
                                             QUrl(QStringLiteral("https://example.test/file")),
                                             QByteArray("x"));
        QVERIFY2(!unsafePackage.isValid(), qPrintable(packageId));
    }
}

void VoiceClonePackageManagerTest::removesUnlistedStagingContentBeforeCommit()
{
    LocalHttpFixture server;
    const QByteArray body("listed");
    server.add(QStringLiteral("/listed"), {body});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath(QStringLiteral(".staging/clean/unlisted.bin")), "stale"));
    const QString hidden = root.filePath(QStringLiteral(".staging/clean/.hidden-stale.bin"));
    QVERIFY(writeFile(hidden, "hidden-stale"));
    QVERIFY(markHidden(hidden));
    VoiceClonePackageManager manager(root.path());
    manager.start(packageManifest(QStringLiteral("clean"), QStringLiteral("listed.bin"),
                                  server.url(QStringLiteral("/listed")), body));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 2000);
    QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("clean/listed.bin"))));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("clean/unlisted.bin"))));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("clean/.hidden-stale.bin"))));
}

void VoiceClonePackageManagerTest::derivesLicenseGateFromIndexLicenseIdentity()
{
    LocalHttpFixture server;
    const QByteArray body("index");
    server.add(QStringLiteral("/index-derived"), {body});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    auto index = packageManifest(QStringLiteral("indextts-model"), QStringLiteral("file.bin"),
                                 server.url(QStringLiteral("/index-derived")), body);
    index.requiresLicenseAcceptance = false;
    index.modelId = QStringLiteral("IndexTeam/IndexTTS-2.5");
    index.adapterId = QStringLiteral("indextts25");
    index.licenseUrl = QUrl(QStringLiteral("https://licenses.example.test/not-an-index-url"));
    VoiceClonePackageManager manager(root.path());
    manager.start(index);
    QCOMPARE(manager.state(), VoiceClonePackageManager::LicenseRequired);

    auto qwen = packageManifest(QStringLiteral("qwen-model"), QStringLiteral("file.bin"),
                                server.url(QStringLiteral("/index-derived")), body);
    qwen.requiresLicenseAcceptance = true;
    qwen.licenseUrl = QUrl(QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5"));
    VoiceClonePackageManager qwenManager(root.path());
    qwenManager.start(qwen);
    QVERIFY(qwenManager.state() != VoiceClonePackageManager::LicenseRequired);

    auto evilSuffix = index;
    evilSuffix.modelId = QStringLiteral("IndexTeam/IndexTTS-2.5-evil");
    QVERIFY(!evilSuffix.isValid());
    auto wrongAdapter = index;
    wrongAdapter.adapterId = QStringLiteral("qwen");
    QVERIFY(!wrongAdapter.isValid());

    const QVector<QPair<QString, QString>> approvedIdentities{
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"), QStringLiteral("qwen")},
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"), QStringLiteral("qwen")},
        {QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
         QStringLiteral("cosyvoice3")}};
    for (const auto& identity : approvedIdentities) {
        auto approved = qwen;
        approved.modelId = identity.first;
        approved.adapterId = identity.second;
        QVERIFY2(approved.isValid(), qPrintable(approved.errorString()));
        approved.adapterId = QStringLiteral("indextts25");
        QVERIFY(!approved.isValid());
    }
}

void VoiceClonePackageManagerTest::rejectsCorruptLicenseAcceptanceStoreWithoutOverwrite()
{
    LocalHttpFixture server;
    const QByteArray body("licensed");
    server.add(QStringLiteral("/strict-license"), {body});
    const auto indexManifest = [&] {
        auto manifest = packageManifest(QStringLiteral("strict-license"),
                                        QStringLiteral("file.bin"),
                                        server.url(QStringLiteral("/strict-license")), body);
        manifest.modelId = QStringLiteral("IndexTeam/IndexTTS-2.5");
        manifest.adapterId = QStringLiteral("indextts25");
        manifest.licenseUrl = QUrl(QStringLiteral("https://license.example.test/index"));
        manifest.licenseRevision = QStringLiteral("license-r7");
        return manifest;
    }();
    const QJsonObject validRecord{
        {QStringLiteral("modelId"), QStringLiteral("IndexTeam/IndexTTS-2.5")},
        {QStringLiteral("adapterId"), QStringLiteral("indextts25")},
        {QStringLiteral("licenseUrl"), QStringLiteral("https://license.example.test/index")},
        {QStringLiteral("revision"), QStringLiteral("old-revision")},
        {QStringLiteral("acceptedAt"), QStringLiteral("2026-08-14T01:02:03.000Z")}};
    const QVector<QJsonObject> corruptStores{
        QJsonObject{{QStringLiteral("schemaVersion"), 2},
                    {QStringLiteral("records"), QJsonArray{}}},
        QJsonObject{{QStringLiteral("schemaVersion"), 1},
                    {QStringLiteral("records"),
                     QJsonArray{QJsonObject{{QStringLiteral("modelId"), 7}}}}},
        QJsonObject{{QStringLiteral("schemaVersion"), 1},
                    {QStringLiteral("records"),
                     QJsonArray{validRecord,
                                QJsonObject{{QStringLiteral("modelId"),
                                             QStringLiteral("broken")}}}}},
        QJsonObject{{QStringLiteral("schemaVersion"), 1},
                    {QStringLiteral("records"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("modelId"), QStringLiteral("IndexTeam/IndexTTS-2.5")},
                         {QStringLiteral("adapterId"), QStringLiteral("indextts25")},
                         {QStringLiteral("licenseUrl"), QStringLiteral("https://license.example.test/index")},
                         {QStringLiteral("revision"), QStringLiteral("old-revision")},
                         {QStringLiteral("acceptedAt"), QStringLiteral("not-a-timestamp")}}}}},
        QJsonObject{{QStringLiteral("schemaVersion"), 1},
                    {QStringLiteral("records"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("modelId"), QStringLiteral("IndexTeam/IndexTTS-2.5")},
                         {QStringLiteral("adapterId"), QStringLiteral("indextts25")},
                         {QStringLiteral("licenseUrl"), QStringLiteral("relative-license")},
                         {QStringLiteral("revision"), QStringLiteral("old-revision")},
                         {QStringLiteral("acceptedAt"), QStringLiteral("2026-08-14T01:02:03.000Z")}}}}}};

    for (qsizetype index = 0; index < corruptStores.size(); ++index) {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString acceptancePath = root.filePath(QStringLiteral("license-acceptances.json"));
        const QByteArray original = QJsonDocument(corruptStores.at(index))
                                        .toJson(QJsonDocument::Compact);
        QVERIFY(writeFile(acceptancePath, original));
        VoiceClonePackageManager manager(root.path());
        manager.start(indexManifest);
        QCOMPARE(manager.state(), VoiceClonePackageManager::LicenseRequired);
        QVERIFY(manager.errorString().contains(QStringLiteral("license"), Qt::CaseInsensitive));
        QVERIFY(!manager.acceptLicense(indexManifest.licenseUrl,
                                       indexManifest.licenseRevision));
        QVERIFY(manager.errorString().contains(QStringLiteral("invalid"), Qt::CaseInsensitive)
                || manager.errorString().contains(QStringLiteral("corrupt"), Qt::CaseInsensitive));
        QFile unchanged(acceptancePath);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), original);
    }
}

void VoiceClonePackageManagerTest::rejectsHiddenUnlistedContentAddedDuringDownload()
{
    LocalHttpFixture server;
    const QByteArray body(600, 'h');
    server.add(QStringLiteral("/hidden"), {body, -1, false, true, false});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManager manager(root.path());
    manager.start(packageManifest(QStringLiteral("hidden"), QStringLiteral("file.bin"),
                                  server.url(QStringLiteral("/hidden")), body));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Downloading, 2000);
    const QString hidden = root.filePath(QStringLiteral(".staging/hidden/.unlisted.bin"));
    QVERIFY(writeFile(hidden, "unlisted"));
    QVERIFY(markHidden(hidden));
    QVERIFY(QFileInfo(hidden).isHidden());
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Failed, 3000);
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("hidden/file.bin"))));
}

void VoiceClonePackageManagerTest::stalePausedReplyCannotCorruptNewOperation()
{
    ControlledNetworkManager network;
    const QUrl oldUrl(QStringLiteral("https://fixture.test/stale-old-pause"));
    const QUrl newUrl(QStringLiteral("https://fixture.test/new-after-pause"));
    network.add(oldUrl, QByteArray("old"));
    network.add(newUrl, QByteArray("new-after-pause"));
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManager manager(root.path(), &network, nullptr);
    manager.start(packageManifest(QStringLiteral("old-pause"), QStringLiteral("file.bin"),
                                  oldUrl, QByteArray("old")));
    QCOMPARE(manager.state(), VoiceClonePackageManager::Resolving);
    QVERIFY(manager.pause());
    manager.start(packageManifest(QStringLiteral("new-pause"), QStringLiteral("file.bin"),
                                  newUrl, QByteArray("new-after-pause")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 1000);
    QTest::qWait(300);
    QCOMPARE(manager.state(), VoiceClonePackageManager::Completed);
    QFile installed(root.filePath(QStringLiteral("new-pause/file.bin")));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), QByteArray("new-after-pause"));
}

void VoiceClonePackageManagerTest::staleCanceledReplyCannotCorruptNewOperation()
{
    ControlledNetworkManager network;
    const QUrl oldUrl(QStringLiteral("https://fixture.test/stale-old-cancel"));
    const QUrl newUrl(QStringLiteral("https://fixture.test/new-after-cancel"));
    network.add(oldUrl, QByteArray("old"));
    network.add(newUrl, QByteArray("new-after-cancel"));
    QTemporaryDir root;
    QVERIFY(root.isValid());
    VoiceClonePackageManager manager(root.path(), &network, nullptr);
    manager.start(packageManifest(QStringLiteral("old-cancel"), QStringLiteral("file.bin"),
                                  oldUrl, QByteArray("old")));
    QCOMPARE(manager.state(), VoiceClonePackageManager::Resolving);
    manager.cancel();
    QCOMPARE(manager.state(), VoiceClonePackageManager::Canceled);
    manager.start(packageManifest(QStringLiteral("new-cancel"), QStringLiteral("file.bin"),
                                  newUrl, QByteArray("new-after-cancel")));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 1000);
    QTest::qWait(300);
    QCOMPARE(manager.state(), VoiceClonePackageManager::Completed);
    QFile installed(root.filePath(QStringLiteral("new-cancel/file.bin")));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), QByteArray("new-after-cancel"));
}

void VoiceClonePackageManagerTest::rejectsNonexistentInstallRootBelowReparseAncestorWithoutWriting()
{
#ifdef Q_OS_WIN
    LocalHttpFixture server;
    const QByteArray body("safe");
    server.add(QStringLiteral("/junction-root"), {body});
    QTemporaryDir anchor;
    QTemporaryDir outside;
    QVERIFY(anchor.isValid());
    QVERIFY(outside.isValid());
    const QString junction = anchor.filePath(QStringLiteral("linked"));
    QVERIFY(createWindowsJunction(junction, outside.path()));
    const QString installRoot = QDir(junction).filePath(QStringLiteral("not-created/deep"));
    const QString escapedDirectory = outside.filePath(QStringLiteral("not-created"));
    {
        VoiceClonePackageManager manager(installRoot);
        manager.start(packageManifest(QStringLiteral("junction-root"),
                                      QStringLiteral("file.bin"),
                                      server.url(QStringLiteral("/junction-root")), body));
        QCOMPARE(manager.state(), VoiceClonePackageManager::Failed);
    }
    const bool wroteOutside = QFileInfo::exists(escapedDirectory);
    if (wroteOutside) QVERIFY(QDir(escapedDirectory).removeRecursively());
    QVERIFY(QDir().rmdir(junction));
    QVERIFY(!wroteOutside);
#else
    QSKIP("Junction ancestor contract is Windows-specific.");
#endif
}

void VoiceClonePackageManagerTest::restartsUnknownLengthPartialFromZero()
{
    LocalHttpFixture server;
    const QByteArray body("unknown-complete-partial");
    server.add(QStringLiteral("/unknown-partial"), {body, -1, true, false, false});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto manifest = packageManifest(QStringLiteral("unknown-partial"),
                                          QStringLiteral("file.bin"),
                                          server.url(QStringLiteral("/unknown-partial")), body);
    QVERIFY(writeFile(root.filePath(
                          QStringLiteral(".staging/unknown-partial/file.bin.part")),
                      body));
    QVERIFY(writeFile(
        root.filePath(QStringLiteral(
            ".staging/unknown-partial/file.bin.resume.json")),
        QJsonDocument(QJsonObject{{QStringLiteral("url"), manifest.files[0].url.toString()},
                                  {QStringLiteral("sha256"),
                                   QString::fromLatin1(manifest.files[0].sha256)},
                                  {QStringLiteral("revision"), manifest.revision},
                                  {QStringLiteral("totalBytes"), -1}})
            .toJson(QJsonDocument::Compact)));
    VoiceClonePackageManager manager(root.path());
    manager.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 2000);
    const QList<qint64> ranges = server.ranges(QStringLiteral("/unknown-partial"));
    QCOMPARE(ranges, QList<qint64>{0});
}

void VoiceClonePackageManagerTest::restoresOldTargetWhenActivatedRenameFails()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("rename-failure"));
    const QString staging = root.filePath(QStringLiteral("staging-rename-failure"));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("old.bin")), "old"));
    QVERIFY(writeFile(QDir(staging).filePath(QStringLiteral("new.bin")), "new"));
    VoiceClonePackageManager::DeploymentOperations operations;
    int renameCall = 0;
    operations.renameDirectory = [&renameCall](const QString& source,
                                               const QString& destination) {
        ++renameCall;
        if (renameCall == 2) return false;
        return QDir().rename(source, destination);
    };
    QString error;
    QVERIFY(!VoiceClonePackageManager::commitStagingDirectory(
        staging, target, &error, operations));
    QCOMPARE(renameCall, 3);
    QFile old(QDir(target).filePath(QStringLiteral("old.bin")));
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("old"));
    QVERIFY(QFileInfo::exists(QDir(staging).filePath(QStringLiteral("new.bin"))));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".rollback")));
}

void VoiceClonePackageManagerTest::rejectsTargetDescendantReparseBeforeSwap()
{
#ifdef Q_OS_WIN
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    const QString target = root.filePath(QStringLiteral("linked-target"));
    const QString staging = root.filePath(QStringLiteral("linked-staging"));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("old.bin")), "old"));
    QVERIFY(writeFile(QDir(staging).filePath(QStringLiteral("new.bin")), "new"));
    const QString junction = QDir(target).filePath(QStringLiteral("escape"));
    QVERIFY(createWindowsJunction(junction, outside.path()));
    QString error;
    const bool committed = VoiceClonePackageManager::commitStagingDirectory(
        staging, target, &error);
    const bool oldStillTarget = QFileInfo::exists(
        QDir(target).filePath(QStringLiteral("old.bin")));
    const bool stagingStillPresent = QFileInfo::exists(
        QDir(staging).filePath(QStringLiteral("new.bin")));
    const QString backup = target + QStringLiteral(".rollback");
    QDir().rmdir(QDir(target).filePath(QStringLiteral("escape")));
    QDir().rmdir(QDir(backup).filePath(QStringLiteral("escape")));
    if (QFileInfo::exists(backup)) QVERIFY(QDir(backup).removeRecursively());
    QVERIFY(!committed);
    QVERIFY(error.contains(QStringLiteral("reparse"), Qt::CaseInsensitive)
            || error.contains(QStringLiteral("link"), Qt::CaseInsensitive));
    QVERIFY(oldStillTarget);
    QVERIFY(stagingStillPresent);
#else
    QSKIP("Target descendant reparse contract is Windows-specific.");
#endif
}

void VoiceClonePackageManagerTest::keepsCompletedStateWhenBackupCleanupFails()
{
    LocalHttpFixture server;
    const QByteArray body("new-version");
    server.add(QStringLiteral("/cleanup-warning"), {body});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.filePath(QStringLiteral("cleanup-warning/old.bin")), "old"));
    VoiceClonePackageManager::DeploymentOperations operations;
    operations.renameDirectory = [](const QString& source, const QString& destination) {
        return QDir().rename(source, destination);
    };
    operations.removeDirectory = [](const QString&) { return false; };
    VoiceClonePackageManager manager(root.path(), nullptr, operations, nullptr);
    manager.start(packageManifest(QStringLiteral("cleanup-warning"),
                                  QStringLiteral("file.bin"),
                                  server.url(QStringLiteral("/cleanup-warning")), body));
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 2000);
    QVERIFY(manager.errorString().contains(QStringLiteral("cleanup"), Qt::CaseInsensitive)
            || manager.errorString().contains(QStringLiteral("backup"), Qt::CaseInsensitive));
    QFile installed(root.filePath(QStringLiteral("cleanup-warning/file.bin")));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), body);
    QVERIFY(QFileInfo::exists(root.filePath(
        QStringLiteral("cleanup-warning.rollback/old.bin"))));
}

void VoiceClonePackageManagerTest::finalizesAlreadyCompletePartialWithoutEofRange()
{
    LocalHttpFixture server;
    const QByteArray body("complete-partial");
    server.add(QStringLiteral("/complete-partial"), {body});
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto manifest = packageManifest(QStringLiteral("complete"), QStringLiteral("file.bin"),
                                          server.url(QStringLiteral("/complete-partial")), body);
    QVERIFY(writeFile(root.filePath(QStringLiteral(".staging/complete/file.bin.part")), body));
    QVERIFY(writeFile(root.filePath(QStringLiteral(".staging/complete/file.bin.resume.json")),
                      QJsonDocument(QJsonObject{{QStringLiteral("url"), manifest.files[0].url.toString()},
                                                {QStringLiteral("sha256"), QString::fromLatin1(manifest.files[0].sha256)},
                                                {QStringLiteral("revision"), manifest.revision},
                                                {QStringLiteral("totalBytes"), body.size()}})
                          .toJson(QJsonDocument::Compact)));
    VoiceClonePackageManager manager(root.path());
    manager.start(manifest);
    QTRY_COMPARE_WITH_TIMEOUT(manager.state(), VoiceClonePackageManager::Completed, 2000);
    QCOMPARE(server.getCount(QStringLiteral("/complete-partial")), 0);
}

void VoiceClonePackageManagerTest::recoversInterruptedSwapFromRollbackDirectory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString target = root.filePath(QStringLiteral("recover"));
    QVERIFY(writeFile(target + QStringLiteral(".rollback/old.bin"), "old"));
    QString error;
    QVERIFY(!VoiceClonePackageManager::commitStagingDirectory(
        root.filePath(QStringLiteral("missing")), target, &error));
    QFile restored(QDir(target).filePath(QStringLiteral("old.bin")));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("old"));
}

QTEST_GUILESS_MAIN(VoiceClonePackageManagerTest)

#include "voice_clone_package_manager_test.moc"
