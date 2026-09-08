#include "update_checker.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <cstring>

class ManifestReply final : public QNetworkReply {
public:
    ManifestReply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly | Unbuffered);
    }
    void respond(QByteArray body, int status = 200) {
        body_ = std::move(body);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setFinished(true); emit readyRead(); emit finished();
    }
    void abort() override {
        aborted = true;
        setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished();
    }
    bool aborted = false;
protected:
    qint64 readData(char* target, qint64 size) override {
        const qint64 count = std::min<qint64>(size, body_.size() - position_);
        if (count <= 0) return -1;
        std::memcpy(target, body_.constData() + position_, size_t(count));
        position_ += count; return count;
    }
private:
    QByteArray body_;
    qint64 position_ = 0;
};
class ManifestNetwork final : public QNetworkAccessManager {
public:
    QList<ManifestReply*> replies;
    QNetworkRequest lastRequest;
protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                QIODevice* body) override {
        Q_ASSERT(operation == GetOperation && !body);
        lastRequest = request;
        auto* reply = new ManifestReply(request, this);
        replies.append(reply); return reply;
    }
};
class UpdateCheckerTest final : public QObject {
    Q_OBJECT
private slots:
    void missingOrInsecureEndpointDoesNotRequest() {
        ManifestNetwork network;
        UpdateChecker empty({}, "1.1.1", nullptr, &network);
        empty.check(); QCOMPARE(empty.state(), "unconfigured");
        for (const auto& url : {"http://www.agplayer.com/latest.json", "https://user:pass@www.agplayer.com/x"}) {
            UpdateChecker invalid(QUrl(QString::fromUtf8(url)), "1.1.1", nullptr, &network);
            invalid.check(); QCOMPARE(invalid.state(), "error");
        }
        QVERIFY(network.replies.isEmpty());
    }
    void comparesVersionsNumerically_data() {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<QString>("expected");
        QTest::newRow("new") << QByteArray(R"({"schemaVersion":1,"version":"1.10.0"})") << QString("available");
        QTest::newRow("same") << QByteArray(R"({"schemaVersion":1,"version":"1.9.0"})") << QString("current");
        QTest::newRow("older") << QByteArray(R"({"schemaVersion":1,"version":"1.8.99"})") << QString("current");
        QTest::newRow("html") << QByteArray("<html>404</html>") << QString("error");
        QTest::newRow("schema") << QByteArray(R"({"schemaVersion":2,"version":"2.0.0"})") << QString("error");
        QTest::newRow("prerelease") << QByteArray(R"({"schemaVersion":1,"version":"2.0.0-beta"})") << QString("error");
        QTest::newRow("missing") << QByteArray(R"({"schemaVersion":1})") << QString("error");
        QTest::newRow("overflow") << QByteArray(R"({"schemaVersion":1,"version":"9999999999999.0.0"})") << QString("error");
    }
    void comparesVersionsNumerically() {
        QFETCH(QByteArray, body); QFETCH(QString, expected);
        ManifestNetwork network;
        UpdateChecker checker(QUrl("https://www.agplayer.com/updates/latest.json"), "1.9.0", nullptr, &network);
        checker.check(); QVERIFY(checker.busy()); checker.check();
        QCOMPARE(network.replies.size(), 1); // Re-entrant clicks do not create parallel requests.
        QCOMPARE(network.lastRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(network.lastRequest.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(), int(QNetworkRequest::Manual));
        network.replies.last()->respond(body);
        QCOMPARE(checker.state(), expected); QVERIFY(!checker.busy());
        QCOMPARE(checker.updateAvailable(), expected == "available");
        if (expected == "error") QVERIFY(checker.latestVersion().isEmpty());
    }
    void errorsClearStaleAvailableStateAndCanRetry() {
        ManifestNetwork network;
        UpdateChecker checker(QUrl("https://www.agplayer.com/latest.json"), "1.1.1", nullptr, &network);
        checker.check(); network.replies.last()->respond(R"({"schemaVersion":1,"version":"2.0.0"})");
        QVERIFY(checker.updateAvailable());
        checker.check(); network.replies.last()->respond("{}", 302);
        QCOMPARE(checker.state(), "error"); QVERIFY(!checker.updateAvailable()); QVERIFY(checker.latestVersion().isEmpty());
        QVERIFY(checker.statusText().contains(QStringLiteral("其他下载线路")));
        checker.check(); network.replies.last()->respond(R"({"schemaVersion":1,"version":"1.1.1"})");
        QCOMPARE(checker.state(), "current");
    }
    void missingManifestIsUnavailableRatherThanNetworkTimeout() {
        ManifestNetwork network;
        UpdateChecker checker(QUrl("https://download.agplayer.com/updates/latest.json"), "1.0.0", nullptr, &network);
        checker.check();
        network.replies.last()->respond("<html>Object not found</html>", 404);
        QCOMPARE(checker.state(), "unavailable");
        QVERIFY(!checker.busy());
        QVERIFY(!checker.updateAvailable());
        QVERIFY(checker.latestVersion().isEmpty());
        QVERIFY(!checker.statusText().contains(QStringLiteral("超时")));
        checker.check();
        network.replies.last()->respond(R"({"schemaVersion":1,"version":"1.0.1"})");
        QCOMPARE(checker.state(), "available");
    }
    void excessiveResponseIsAborted() {
        ManifestNetwork network;
        UpdateChecker checker(QUrl("https://www.agplayer.com/latest.json"), "1.1.1", nullptr, &network);
        checker.check(); auto* reply = network.replies.last();
        reply->respond(QByteArray(65537, 'x'));
        QCOMPARE(checker.state(), "error"); QVERIFY(reply->aborted);
    }
    void destructionCancelsExternalTransport() {
        ManifestNetwork network;
        auto* checker = new UpdateChecker(QUrl("https://www.agplayer.com/latest.json"), "1.1.1", nullptr, &network);
        checker->check(); auto* reply = network.replies.last();
        delete checker; QVERIFY(reply->aborted);
    }
    void stalledRequestTimesOutAndAllowsRetry() {
        ManifestNetwork network;
        UpdateChecker checker(QUrl("https://www.agplayer.com/latest.json"), "1.1.1", nullptr, &network);
        checker.check();
        QPointer<ManifestReply> stalled = network.replies.last();
        QTRY_COMPARE_WITH_TIMEOUT(checker.state(), QString("error"), 12000);
        QVERIFY(!checker.busy());
        QVERIFY(!stalled || stalled->aborted);
        checker.check();
        network.replies.last()->respond(R"({"schemaVersion":1,"version":"1.2.0"})");
        QVERIFY(checker.updateAvailable());
    }
};
QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "update_checker_test.moc"
