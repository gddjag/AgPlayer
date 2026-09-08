#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Read-only release notification. Never downloads or executes an installer.
class UpdateChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
public:
    explicit UpdateChecker(QUrl endpoint, QString currentVersion,
                           QObject* parent = nullptr,
                           QNetworkAccessManager* network = nullptr);
    ~UpdateChecker() override;
    QString state() const { return state_; }
    QString statusText() const;
    QString latestVersion() const { return latestVersion_; }
    QString currentVersion() const { return currentVersion_; }
    bool busy() const { return state_ == QStringLiteral("checking"); }
    bool updateAvailable() const { return state_ == QStringLiteral("available"); }
    Q_INVOKABLE void check();
signals:
    void changed();
private:
    void fail(const QString& state = QStringLiteral("error"));
    const QUrl endpoint_;
    const QString currentVersion_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QString state_;
    QString latestVersion_;
    QByteArray payload_;
};
