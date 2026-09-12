#pragma once

#include "agplayer/c_api.h"

#include <QString>
#include <QtGlobal>

#include <mutex>
#include <string>

class RuntimeLog final {
public:
    static constexpr std::size_t RotationThresholdBytes = 2U * 1024U * 1024U;
    static constexpr std::size_t MaxBufferBytes = 4U * 1024U * 1024U;

    static void install(const QString& logPath = {});
    static void uninstall();
    static void log(ag_result result, const QString& component, const QString& detail);
    static QString mapResult(ag_result result);
    static QString defaultLogPath();

    RuntimeLog(const RuntimeLog&) = delete;
    RuntimeLog& operator=(const RuntimeLog&) = delete;

private:
    RuntimeLog(QString logPath);
    ~RuntimeLog();
    static RuntimeLog* instance_;

    void handleMessage(QtMsgType type, const QString& message);
    void writeLine(const QString& severity, const QString& component,
                   const QString& message, ag_result result);
    void rotateIfNeeded();
    static QString severityFor(QtMsgType type);

    QString logPath_;
    std::string fileBuffer_;
    std::mutex mutex_;
    bool installed_ = false;
};
