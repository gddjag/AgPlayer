#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class AgPlayerVoiceClonePluginInterface;
class QPluginLoader;

class VoiceCloneHostController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(QString pluginVersion READ pluginVersion NOTIFY stateChanged)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY stateChanged)
    Q_PROPERTY(bool pluginLoaded READ pluginLoaded NOTIFY stateChanged)
    Q_PROPERTY(QObject* pluginController READ pluginController NOTIFY stateChanged)
    Q_PROPERTY(QUrl mainQmlUrl READ mainQmlUrl NOTIFY stateChanged)

public:
    enum State {
        Absent,
        Invalid,
        Compatible,
        Loaded,
        UpdateAvailable,
        Failed,
    };
    Q_ENUM(State)

    explicit VoiceCloneHostController(QObject* parent = nullptr);
    ~VoiceCloneHostController() override;

    State state() const;
    QString errorString() const;
    QString pluginVersion() const;
    QString availableVersion() const;
    bool pluginLoaded() const;
    QObject* pluginController() const;
    QUrl mainQmlUrl() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool openPlugin();
    Q_INVOKABLE void closePlugin();

signals:
    void stateChanged();

private:
    QString pluginRoot() const;
    bool unloadLibrary(QString* error);
    void resetDiscovery();
    void setState(State state, const QString& error = {});

    State state_ = Absent;
    QString error_;
    QString libraryPath_;
    QString pluginVersion_;
    QString availableVersion_;
    int manifestProtocolVersion_ = 0;
    std::unique_ptr<QPluginLoader> loader_;
    AgPlayerVoiceClonePluginInterface* plugin_ = nullptr;
    QObject* pluginController_ = nullptr;
    QUrl mainQmlUrl_;
};
