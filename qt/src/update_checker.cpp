#include "update_checker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QVersionNumber>

namespace {
constexpr qint64 kMaxManifestBytes = 64 * 1024;
bool validEndpoint(const QUrl& url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && !url.hasFragment();
}
QVersionNumber stableVersion(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})$"));
    return pattern.match(value).hasMatch() ? QVersionNumber::fromString(value)
                                          : QVersionNumber{};
}
}

UpdateChecker::UpdateChecker(QUrl endpoint, QString currentVersion,
                             QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), endpoint_(std::move(endpoint)),
      currentVersion_(std::move(currentVersion)), network_(network),
      state_(endpoint_.isEmpty() ? QStringLiteral("unconfigured")
                                : QStringLiteral("idle"))
{
}

QString UpdateChecker::statusText() const
{
    if (state_ == "unconfigured") return tr("更新服务尚未配置，可前往官网查看版本。");
    if (state_ == "unavailable") return tr("官网尚未提供可用的更新信息，请前往官网下载页查看。");
    if (state_ == "checking") return tr("正在检查更新…");
    if (state_ == "available") return tr("发现新版本 %1，请前往官网下载。").arg(latestVersion_);
    if (state_ == "current") return tr("当前已是最新版本。");
    if (state_ == "error") return tr("更新检查超时或失败，请前往官网选择其他下载线路（GitHub / R2）。");
    return tr("检查官网发布的最新版本。");
}

UpdateChecker::~UpdateChecker()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
    }
}

void UpdateChecker::fail(const QString& state)
{
    state_ = state;
    latestVersion_.clear();
    auto reply = reply_;
    reply_.clear(); // Abort may synchronously emit finished; invalidate first.
    if (reply) { reply->abort(); reply->deleteLater(); }
    payload_.clear();
    emit changed();
}

void UpdateChecker::check()
{
    if (busy()) return;
    latestVersion_.clear();
    payload_.clear();
    if (endpoint_.isEmpty()) {
        state_ = QStringLiteral("unconfigured");
        emit changed();
        return;
    }
    if (!validEndpoint(endpoint_) || stableVersion(currentVersion_).isNull()) {
        fail();
        return;
    }
    if (!network_) network_ = new QNetworkAccessManager(this);
    QNetworkRequest request(endpoint_);
    // No redirects: the future deployment must provide a direct HTTPS JSON URL.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "AgPlayer-UpdateCheck");
    request.setTransferTimeout(10000);
    state_ = QStringLiteral("checking");
    auto* reply = network_->get(request);
    reply_ = reply;
    reply->setReadBufferSize(kMaxManifestBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply_ != reply) return;
        payload_.append(reply->read(kMaxManifestBytes + 1 - payload_.size()));
        if (payload_.size() > kMaxManifestBytes) fail();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply_ != reply) return;
        payload_.append(reply->read(kMaxManifestBytes + 1 - payload_.size()));
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
            fail(QStringLiteral("unavailable"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
            || payload_.size() > kMaxManifestBytes) { fail(); return; }
        const auto document = QJsonDocument::fromJson(payload_);
        const auto object = document.object();
        const QString latest = object.value(QStringLiteral("version")).toString();
        const auto parsed = stableVersion(latest);
        if (!document.isObject()
            || object.value(QStringLiteral("schemaVersion")).toDouble(-1) != 1
            || parsed.isNull()) { fail(); return; }
        reply_.clear();
        reply->deleteLater();
        payload_.clear();
        latestVersion_ = latest;
        state_ = QVersionNumber::compare(parsed, stableVersion(currentVersion_)) > 0
            ? QStringLiteral("available") : QStringLiteral("current");
        emit changed();
    });
    // A total deadline also covers slow trickle responses (transfer timeout alone does not).
    QTimer::singleShot(10000, this, [this, guarded = QPointer<QNetworkReply>(reply)] {
        if (guarded && reply_ == guarded) fail();
    });
    emit changed();
}
