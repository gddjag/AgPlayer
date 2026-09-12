#pragma once

#include <QObject>
#include <QString>

class LibraryModel;

struct ReplayGainAnalysis final {
    bool success = false;
    double loudnessLufs = 0.0;
    double gainDb = 0.0;
    double peak = 0.0;
    bool clipping = false;
    QString error;
};

class ReplayGainScanner final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit ReplayGainScanner(LibraryModel* library = nullptr,
                               QObject* parent = nullptr);

    bool running() const noexcept;
    double progress() const noexcept;
    QString errorMessage() const;
    void setLibraryModel(LibraryModel* library) noexcept;

    static ReplayGainAnalysis analyzeFile(const QString& path);
    Q_INVOKABLE bool scanTrack(const QString& trackId);
    Q_INVOKABLE bool scanAll();
    bool applyResult(const QString& trackId, const ReplayGainAnalysis& result);

signals:
    void runningChanged();
    void progressChanged();
    void errorMessageChanged();
    void trackScanned(const QString& trackId, double gainDb, double peak,
                      bool clipping);

private:
    LibraryModel* library_ = nullptr;
    bool running_ = false;
    double progress_ = 0.0;
    QString errorMessage_;
};
