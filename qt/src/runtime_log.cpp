#include "runtime_log.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QtGlobal>

#include <cstdio>
#include <fstream>
#include <utility>

RuntimeLog* RuntimeLog::instance_ = nullptr;

namespace {

QtMessageHandler previousMessageHandler = nullptr;

QString resultToken(ag_result result)
{
    return QStringLiteral("rc=%1").arg(static_cast<int>(result));
}

} // namespace

RuntimeLog::RuntimeLog(QString logPath)
    : logPath_(std::move(logPath))
{
    if (logPath_.isEmpty()) {
        logPath_ = defaultLogPath();
    }
    QFileInfo info(logPath_);
    QDir().mkpath(info.absolutePath());
    rotateIfNeeded();
    std::ofstream(logPath_.toStdString(), std::ios::binary | std::ios::app);
    installed_ = true;
}

RuntimeLog::~RuntimeLog()
{
    std::lock_guard<std::mutex> lock(mutex_);
    installed_ = false;
    if (!fileBuffer_.empty()) {
        std::ofstream stream(logPath_.toStdString(),
                             std::ios::binary | std::ios::app);
        if (stream) {
            stream.write(fileBuffer_.data(),
                         static_cast<std::streamsize>(fileBuffer_.size()));
            stream.flush();
        }
        fileBuffer_.clear();
    }
}

QString RuntimeLog::defaultLogPath()
{
    // AppLocalDataLocation resolves to <AppData>/<ApplicationName> on Windows,
    // matching the path used by LibraryStore in main.cpp. The application name
    // is set in main() before RuntimeLog::install() is called.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QStringLiteral(".");
    }
    return QDir(base).filePath(QStringLiteral("logs/agplayer.log"));
}

void RuntimeLog::install(const QString& logPath)
{
    if (instance_ != nullptr) {
        return;
    }
    QString resolved = logPath;
    if (resolved.isEmpty()) {
        // Honor AGPLAYER_LOG_DIR so QA tooling can redirect logs to a
        // sandbox-writable location without touching QStandardPaths.
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString envDir = env.value(QStringLiteral("AGPLAYER_LOG_DIR"));
        if (!envDir.isEmpty()) {
            resolved = QDir(envDir).filePath(QStringLiteral("agplayer.log"));
        }
    }
    instance_ = new RuntimeLog(resolved.isEmpty() ? defaultLogPath() : resolved);
    previousMessageHandler = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext& context, const QString& message) {
            if (RuntimeLog::instance_ != nullptr) {
                RuntimeLog::instance_->handleMessage(type, message);
            }
            if (previousMessageHandler != nullptr) {
                previousMessageHandler(type, context, message);
            }
        });
}

void RuntimeLog::uninstall()
{
    if (instance_ == nullptr) {
        return;
    }
    qInstallMessageHandler(previousMessageHandler);
    previousMessageHandler = nullptr;
    delete instance_;
    instance_ = nullptr;
}

void RuntimeLog::log(ag_result result, const QString& component, const QString& detail)
{
    if (instance_ == nullptr) {
        return;
    }
    instance_->writeLine(QStringLiteral("ERROR"), component, detail, result);
}

QString RuntimeLog::mapResult(ag_result result)
{
    switch (result) {
    case AG_OK:
        return {};
    case AG_INVALID_ARGUMENT:
        return QStringLiteral("参数无效");
    case AG_IO_ERROR:
        return QStringLiteral("无法读取文件");
    case AG_UNSUPPORTED_FORMAT:
        return QStringLiteral("不支持的音频格式");
    case AG_DECODE_ERROR:
        return QStringLiteral("解码失败");
    case AG_DEVICE_ERROR:
        return QStringLiteral("音频设备错误");
    case AG_CANCELLED:
        return QStringLiteral("操作已取消");
    case AG_INTERNAL_ERROR:
        return QStringLiteral("内部错误");
    }
    return QStringLiteral("未知错误");
}

QString RuntimeLog::severityFor(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}

void RuntimeLog::handleMessage(QtMsgType type, const QString& message)
{
    const QString severity = severityFor(type);
    writeLine(severity, QStringLiteral("Qt"), message, AG_OK);
}

void RuntimeLog::writeLine(const QString& severity, const QString& component,
                           const QString& message, ag_result result)
{
    if (!installed_) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyy-MM-ddThh:mm:ss.zzzZ"));
    // Sanitize C0 control chars (including NUL) and DEL: replace with U+FFFD
    // so binary payloads (PCM slabs, cover art) never leak raw bytes into the
    // log. Tab/LF/CR are preserved.
    auto sanitize = [](const QString& text) {
        QString out;
        out.reserve(text.size());
        for (const QChar ch : text) {
            const ushort code = ch.unicode();
            if (code == 0x0009 || code == 0x000A || code == 0x000D
                || (code >= 0x0020 && code != 0x007F)) {
                out.append(ch);
            } else {
                out.append(QChar::ReplacementCharacter);
            }
        }
        return out;
    };
    const QString mapped = mapResult(result);
    const QString displayMessage =
        sanitize(message.isEmpty() ? mapped : message);
    QString line = QStringLiteral("[%1] [%2] [%3] %4")
                       .arg(timestamp, severity, component, displayMessage);
    if (result != AG_OK) {
        // Append the mapped Chinese error text when it differs from the
        // display message so the log always carries the human-readable
        // result alongside any caller-provided detail.
        if (!mapped.isEmpty() && displayMessage != mapped) {
            line += QStringLiteral(" | ") + mapped;
        }
        line += QStringLiteral(" | ") + resultToken(result);
    }
    line += QStringLiteral("\n");

    const QByteArray utf8 = line.toUtf8();
    std::lock_guard<std::mutex> lock(mutex_);
    fileBuffer_.append(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    // Cap the in-memory buffer so a persistently unwritable log path doesn't
    // cause unbounded memory growth. The cap is well above the rotation
    // threshold (2 MiB) so normal operation never truncates.
    if (fileBuffer_.size() > MaxBufferBytes) {
        fileBuffer_.erase(0, fileBuffer_.size() - MaxBufferBytes);
    }
    rotateIfNeeded();
    if (!fileBuffer_.empty()) {
        std::ofstream stream(logPath_.toStdString(),
                             std::ios::binary | std::ios::app);
        if (stream) {
            stream.write(fileBuffer_.data(),
                         static_cast<std::streamsize>(fileBuffer_.size()));
            stream.flush();
            fileBuffer_.clear();
        }
    }
}

void RuntimeLog::rotateIfNeeded()
{
    if (logPath_.isEmpty()) {
        return;
    }
    QFileInfo info(logPath_);
    if (!info.exists()) {
        return;
    }
    const qint64 size = info.size();
    if (size <= static_cast<qint64>(RotationThresholdBytes)) {
        return;
    }
    const QString oldPath = logPath_ + QStringLiteral(".old");
    if (QFile::exists(oldPath)) {
        QFile::remove(oldPath);
    }
    QFile file(logPath_);
    file.rename(oldPath);
    fileBuffer_.clear();
}
