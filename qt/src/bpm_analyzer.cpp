#include "bpm_analyzer.hpp"

#include "runtime_log.hpp"

#include <agplayer/c_api.h>

BpmAnalyzeResult analyze_bpm(const QString& filePath)
{
    BpmAnalyzeResult result;
    ag_bpm_result api_result{};
    const QByteArray path = filePath.toUtf8();
    if (ag_bpm_analyze(path.constData(), &api_result) != AG_OK) {
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("BPM"),
            QStringLiteral("Failed to analyze BPM for %1").arg(filePath));
        return result;
    }
    result.bpm = api_result.bpm;
    result.confidence = api_result.confidence;
    return result;
}
