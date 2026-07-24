#include "bpm_analyzer.hpp"

#include "runtime_log.hpp"

#include <agplayer/c_api.h>

BpmAnalyzeResult analyze_bpm(const QString& filePath)
{
    BpmAnalyzeResult result;
    ag_bpm_result api_result{};
    const QByteArray path = filePath.toUtf8();
    const ag_result api_status = ag_bpm_analyze(path.constData(), &api_result);
    if (api_status != AG_OK) {
        RuntimeLog::log(api_status, QStringLiteral("BPM"),
            QStringLiteral("Failed to analyze BPM for %1").arg(filePath));
        return result;
    }
    result.bpm = api_result.bpm;
    result.confidence = api_result.confidence;
    return result;
}
