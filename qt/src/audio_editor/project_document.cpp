#include "project_document.hpp"

#include "../../../core/src/audio_editor/audio_source_probe.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace agplayer::editor {

bool isValidProjectExportSettings(const ProjectExportSettings& settings) noexcept
{
    const bool validSampleRate = settings.sampleRate == 0
        || (settings.sampleRate >= 8'000 && settings.sampleRate <= 384'000);
    return validSampleRate && settings.bitDepth >= 8 && settings.bitDepth <= 32
        && settings.channels >= 0 && settings.channels <= 8
        && settings.bitRate >= 0 && settings.bitRate <= 1'536'000
        && settings.quality >= 0 && settings.quality <= 100;
}

namespace {

constexpr qint64 kMaxProjectJsonBytes = 16LL * 1024LL * 1024LL;
constexpr qsizetype kMaxProjectSources = 4'096;
constexpr qsizetype kMaxProjectEvents = 4'096;
constexpr qsizetype kMaxProjectMarkers = 4'096;
constexpr qsizetype kMaxProjectEnvelopePoints = 65'536;
constexpr qsizetype kMaxProjectSourceProbes = 128;
constexpr auto kProjectProbeBudget = std::chrono::seconds(5);
constexpr auto kResourceLimitMessage = "project resource limit exceeded";

struct SourceInspection final {
    bool exists{};
    qint64 fileSize{};
    qint64 modifiedUtcMs{};
    AudioSourceProbeResult probe;
};

QString sourceInspectionKey(const QString& absoluteSourcePath)
{
#ifdef Q_OS_WIN
    return absoluteSourcePath.toCaseFolded();
#else
    return absoluteSourcePath;
#endif
}

std::optional<quint64> nextAvailableSourceId(const std::unordered_set<quint64>& ids)
{
    for (quint64 candidate = 1; candidate < std::numeric_limits<quint64>::max(); ++candidate) {
        if (ids.count(candidate) == 0) return candidate;
    }
    return std::nullopt;
}

QString toQString(const std::filesystem::path& path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.u8string().c_str());
#endif
}

std::filesystem::path toPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

QString absolutePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool within(const QString& child, const QString& parent)
{
    const QString cleanChild = QDir::cleanPath(QDir::fromNativeSeparators(child));
    const QString cleanParent = QDir::cleanPath(QDir::fromNativeSeparators(parent));
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return cleanChild.compare(cleanParent, sensitivity) == 0
        || cleanChild.startsWith(cleanParent + QLatin1Char('/'), sensitivity);
}

bool withinResolvedBoundary(const QString& child, const QString& parent)
{
    if (!within(child, parent)) return false;
    const QString canonicalParent = QFileInfo(parent).canonicalFilePath();
    if (canonicalParent.isEmpty()) return false;

    QFileInfo childInfo(child);
    QString canonicalChild = childInfo.canonicalFilePath();
    if (!canonicalChild.isEmpty()) {
        return within(canonicalChild, canonicalParent);
    }

    QString ancestor = childInfo.absolutePath();
    while (!QFileInfo::exists(ancestor)) {
        const QString next = QFileInfo(ancestor).absolutePath();
        if (next == ancestor) return false;
        ancestor = next;
    }
    const QString canonicalAncestor = QFileInfo(ancestor).canonicalFilePath();
    return !canonicalAncestor.isEmpty()
        && within(canonicalAncestor, canonicalParent);
}

bool safeRelative(const QString& path)
{
    if (path.isEmpty() || QFileInfo(path).isAbsolute()) return false;
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"));
}

bool isValidProjectEditorSettings(const ProjectEditorSettings& settings) noexcept
{
    const auto validBpm = [](const double value) {
        return std::isfinite(value)
            && (value == 0.0 || (value >= 20.0 && value <= 400.0));
    };
    if (!validBpm(settings.originalBpm) || !validBpm(settings.targetBpm)
        || !std::isfinite(settings.speedPercent)
        || settings.speedPercent < 50.0 || settings.speedPercent > 200.0
        || settings.pitchCents < -1'200 || settings.pitchCents > 1'200
        || (settings.trackMuted && settings.trackSolo)
        || !std::isfinite(settings.trackGainDb)
        || settings.trackGainDb < -60.0 || settings.trackGainDb > 12.0) {
        return false;
    }
    if (settings.originalBpm == 0.0) return settings.targetBpm == 0.0;
    return std::abs(settings.targetBpm
                    - settings.originalBpm * settings.speedPercent / 100.0)
        < 0.001;
}

QString fadeCurveName(const FadeCurve curve)
{
    switch (curve) {
    case FadeCurve::Linear:
        return QStringLiteral("linear");
    case FadeCurve::Smooth:
        return QStringLiteral("smooth");
    case FadeCurve::Exponential:
        return QStringLiteral("exponential");
    }
    return {};
}

bool fadeCurve(const QJsonValue& value, FadeCurve& result)
{
    if (!value.isString()) return false;
    const QString name = value.toString();
    if (name == QStringLiteral("linear")) {
        result = FadeCurve::Linear;
        return true;
    }
    if (name == QStringLiteral("smooth")) {
        result = FadeCurve::Smooth;
        return true;
    }
    if (name == QStringLiteral("exponential")) {
        result = FadeCurve::Exponential;
        return true;
    }
    return false;
}

bool integer(const QJsonValue& value, qint64& output)
{
    if (value.isString()) {
        const QString text = value.toString();
        if (text.isEmpty()) return false;
        int first = 0;
        if (text.front() == QLatin1Char('-')) {
            if (text.size() == 1) return false;
            first = 1;
        }
        if ((text.size() - first) > 1 && text.at(first) == QLatin1Char('0')) return false;
        for (int index = first; index < text.size(); ++index) {
            if (!text.at(index).isDigit()) return false;
        }
        bool ok = false;
        output = text.toLongLong(&ok, 10);
        return ok;
    }
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < -9223372036854775808.0 || number >= 9223372036854775808.0) return false;
    output = static_cast<qint64>(number);
    return true;
}

bool positiveId(const QJsonValue& value, quint64& output)
{
    if (value.isString()) {
        const QString text = value.toString();
        if (text.isEmpty() || (text.size() > 1 && text.front() == QLatin1Char('0'))) return false;
        for (const QChar character : text) if (!character.isDigit()) return false;
        bool ok = false;
        output = text.toULongLong(&ok, 10);
        return ok && output != 0;
    }
    qint64 signedValue{};
    if (!integer(value, signedValue) || signedValue <= 0) return false;
    output = static_cast<quint64>(signedValue);
    return true;
}

bool validSourceId(const QJsonValue& value, quint64& output)
{
    return positiveId(value, output)
        && output != std::numeric_limits<quint64>::max();
}

QJsonValue integerJson(const qint64 value)
{
    return QString::number(value);
}

QJsonValue idJson(const quint64 value)
{
    return QString::number(value);
}

bool finiteFloat(const QJsonValue& value, float& output)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    const double number = value.toDouble();
    if (number < -std::numeric_limits<float>::max()
        || number > std::numeric_limits<float>::max()) return false;
    output = static_cast<float>(number);
    return true;
}

bool finiteDouble(const QJsonValue& value, double& output)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    output = value.toDouble();
    return true;
}

bool stringValue(const QJsonObject& object, const char* key, QString& output)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) return false;
    output = value.toString();
    return true;
}

bool boolValue(const QJsonObject& object, const char* key, bool& output)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isBool()) return false;
    output = value.toBool();
    return true;
}

bool validViewport(const ProjectSaveRequest& request)
{
    const SampleFrame total = request.document->totalFrames();
    if (total == 0) {
        return request.playheadFrame == 0 && request.visibleStartFrame == 0
            && request.visibleEndFrame == 0;
    }
    return request.playheadFrame >= 0 && request.playheadFrame <= total
        && request.visibleStartFrame >= 0 && request.visibleEndFrame > request.visibleStartFrame
        && request.visibleEndFrame <= total;
}

QJsonObject exportJson(const ProjectExportSettings& settings)
{
    return {{QStringLiteral("codecName"), settings.codecName},
            {QStringLiteral("sampleRate"), settings.sampleRate},
            {QStringLiteral("bitDepth"), settings.bitDepth},
            {QStringLiteral("channels"), settings.channels},
            {QStringLiteral("bitRate"), integerJson(settings.bitRate)},
            {QStringLiteral("keepMetadata"), settings.keepMetadata},
            {QStringLiteral("variableBitRate"), settings.variableBitRate},
            {QStringLiteral("quality"), settings.quality},
            {QStringLiteral("outputDirectory"), settings.outputDirectory}};
}

bool parseExport(const QJsonValue& value, ProjectExportSettings& output)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    ProjectExportSettings parsed;
    qint64 rate{}, bitDepth{parsed.bitDepth}, channels{}, bitRate{}, quality{};
    const QJsonValue bitDepthValue = object.value(QStringLiteral("bitDepth"));
    if (!stringValue(object, "codecName", parsed.codecName)
        || !integer(object.value(QStringLiteral("sampleRate")), rate)
        || (!bitDepthValue.isUndefined() && !integer(bitDepthValue, bitDepth))
        || !integer(object.value(QStringLiteral("channels")), channels)
        || !integer(object.value(QStringLiteral("bitRate")), bitRate)
        || !boolValue(object, "keepMetadata", parsed.keepMetadata)
        || !boolValue(object, "variableBitRate", parsed.variableBitRate)
        || !integer(object.value(QStringLiteral("quality")), quality)
        || !stringValue(object, "outputDirectory", parsed.outputDirectory)
        || rate < 0 || rate > std::numeric_limits<int>::max()
        || bitDepth < std::numeric_limits<int>::min()
        || bitDepth > std::numeric_limits<int>::max()
        || channels < 0 || channels > std::numeric_limits<int>::max()
        || quality < std::numeric_limits<int>::min()
        || quality > std::numeric_limits<int>::max()) return false;
    parsed.sampleRate = static_cast<int>(rate);
    parsed.bitDepth = static_cast<int>(bitDepth);
    parsed.channels = static_cast<int>(channels);
    parsed.bitRate = bitRate;
    parsed.quality = static_cast<int>(quality);
    if (!isValidProjectExportSettings(parsed)) return false;
    output = std::move(parsed);
    return true;
}

QJsonObject editorJson(const ProjectEditorSettings& settings)
{
    return {{QStringLiteral("originalBpm"), settings.originalBpm},
            {QStringLiteral("targetBpm"), settings.targetBpm},
            {QStringLiteral("speedPercent"), settings.speedPercent},
            {QStringLiteral("keepPitch"), settings.keepPitch},
            {QStringLiteral("formantPreservation"), settings.formantPreservation},
            {QStringLiteral("pitchCents"), settings.pitchCents},
            {QStringLiteral("trackMuted"), settings.trackMuted},
            {QStringLiteral("trackSolo"), settings.trackSolo},
            {QStringLiteral("trackGainDb"), settings.trackGainDb}};
}

bool parseEditor(const QJsonValue& value, ProjectEditorSettings& output)
{
    if (value.isUndefined()) {
        output = {};
        return true;
    }
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    ProjectEditorSettings parsed;
    qint64 pitch{};
    if (!finiteDouble(object.value(QStringLiteral("originalBpm")),
                      parsed.originalBpm)
        || !finiteDouble(object.value(QStringLiteral("targetBpm")),
                         parsed.targetBpm)
        || !finiteDouble(object.value(QStringLiteral("speedPercent")),
                         parsed.speedPercent)
        || !boolValue(object, "keepPitch", parsed.keepPitch)
        || !boolValue(object, "formantPreservation",
                      parsed.formantPreservation)
        || !integer(object.value(QStringLiteral("pitchCents")), pitch)
        || pitch < std::numeric_limits<int>::min()
        || pitch > std::numeric_limits<int>::max()
        || !boolValue(object, "trackMuted", parsed.trackMuted)
        || !boolValue(object, "trackSolo", parsed.trackSolo)
        || !finiteDouble(object.value(QStringLiteral("trackGainDb")),
                         parsed.trackGainDb)) {
        return false;
    }
    parsed.pitchCents = static_cast<int>(pitch);
    if (!isValidProjectEditorSettings(parsed)) return false;
    output = parsed;
    return true;
}

bool exceedsProjectResourceLimits(const QJsonObject& root)
{
    const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    const QJsonArray events = root.value(QStringLiteral("events")).toArray();
    const QJsonArray markers = root.value(QStringLiteral("markers")).toArray();
    if (sources.size() > kMaxProjectSources || events.size() > kMaxProjectEvents
        || markers.size() > kMaxProjectMarkers) {
        return true;
    }
    qsizetype envelopePoints = 0;
    for (const QJsonValue& value : events) {
        if (!value.isObject()) continue;
        const QJsonValue envelope = value.toObject().value(QStringLiteral("envelope"));
        if (!envelope.isArray()) continue;
        const qsizetype count = envelope.toArray().size();
        if (count > static_cast<qsizetype>(kMaxEnvelopePoints)
            || envelopePoints > kMaxProjectEnvelopePoints - count) {
            return true;
        }
        envelopePoints += count;
    }
    return false;
}

} // namespace

ProjectSaveResult ProjectDocument::save(const QString& path, const ProjectSaveRequest& request)
{
    if (path.isEmpty() || request.document == nullptr || !validViewport(request)
        || !isValidProjectExportSettings(request.exportSettings)
        || !isValidProjectEditorSettings(request.editorSettings)) {
        return {false, QStringLiteral("invalid project save request")};
    }
    const QString projectPath = absolutePath(path);
    const QString projectDir = QFileInfo(projectPath).dir().absolutePath();
    const TimelineSnapshot timeline = request.document->timelineSnapshot();
    if (timeline.events.size() > static_cast<std::size_t>(kMaxProjectEvents)
        || request.document->markers().size()
            > static_cast<std::size_t>(kMaxProjectMarkers)) {
        return {false, QString::fromLatin1(kResourceLimitMessage)};
    }
    qsizetype envelopePoints = 0;
    for (const AudioEvent& event : timeline.events) {
        if (event.envelope.size() > static_cast<std::size_t>(kMaxEnvelopePoints)
            || envelopePoints > kMaxProjectEnvelopePoints
                - static_cast<qsizetype>(event.envelope.size())) {
            return {false, QString::fromLatin1(kResourceLimitMessage)};
        }
        envelopePoints += static_cast<qsizetype>(event.envelope.size());
    }

    std::unordered_set<const AudioSource*> timelineSources;
    for (const AudioEvent& event : timeline.events) {
        if (event.source) timelineSources.insert(event.source.get());
    }
    std::unordered_map<const AudioSource*, ProjectSourceRecord> records;
    std::unordered_set<quint64> suppliedIds;
    if (request.sourceRecords != nullptr) {
        std::unordered_set<quint64> recordIds;
        std::unordered_set<const AudioSource*> recordSources;
        for (const ProjectSourceRecord& record : *request.sourceRecords) {
            if (record.sourceId == std::numeric_limits<quint64>::max()) {
                return {false, QStringLiteral("invalid source id")};
            }
            if (!record.source || record.sourceId == 0
                || !recordIds.insert(record.sourceId).second
                || !recordSources.insert(record.source.get()).second) {
                return {false, QStringLiteral("invalid source records")};
            }
            if (timelineSources.count(record.source.get()) == 0) continue;
            suppliedIds.insert(record.sourceId);
            records.emplace(record.source.get(), record);
        }
    }
    std::unordered_map<const AudioSource*, quint64> sourceIds;
    std::vector<ProjectSourceRecord> savedRecords;
    QJsonArray sources;
    for (const AudioEvent& event : timeline.events) {
        if (!event.source || sourceIds.count(event.source.get()) != 0) continue;
        const auto supplied = records.find(event.source.get());
        if (sourceIds.size() >= static_cast<std::size_t>(kMaxProjectSources)) {
            return {false, QString::fromLatin1(kResourceLimitMessage)};
        }
        const auto nextId = supplied == records.end()
            ? nextAvailableSourceId(suppliedIds) : std::optional<quint64>{};
        if (supplied == records.end() && !nextId) {
            return {false, QStringLiteral("invalid source id")};
        }
        const quint64 id = supplied == records.end() ? *nextId : supplied->second.sourceId;
        suppliedIds.insert(id);
        sourceIds.emplace(event.source.get(), id);
        const QString rawSourcePath = toQString(event.source->path);
        if (rawSourcePath.isEmpty()) return {false, QStringLiteral("source path is required")};
        const QString sourcePath = absolutePath(rawSourcePath);
        const QString relativePath = QDir(projectDir).relativeFilePath(sourcePath);
        const bool relative = safeRelative(relativePath)
            && withinResolvedBoundary(sourcePath, projectDir);
        const QFileInfo file(sourcePath);
        QString savedPath = relative ? relativePath : sourcePath;
        savedPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const qint64 savedSize = supplied == records.end()
            ? (file.exists() ? file.size() : -1) : supplied->second.fileSize;
        const qint64 savedModified = supplied == records.end()
            ? (file.exists() ? file.lastModified().toUTC().toMSecsSinceEpoch() : -1)
            : supplied->second.lastModifiedUtcMs;
        savedRecords.push_back({id, event.source, savedSize, savedModified});
        sources.append(QJsonObject{{QStringLiteral("sourceId"), idJson(id)},
                                   {QStringLiteral("pathKind"), relative ? QStringLiteral("relative") : QStringLiteral("absolute")},
                                   {QStringLiteral("path"), savedPath},
                                   {QStringLiteral("sampleRate"), static_cast<int>(event.source->sample_rate)},
                                   {QStringLiteral("channels"), static_cast<int>(event.source->channels)},
                                   {QStringLiteral("totalFrames"), integerJson(event.source->total_frames)},
                                   {QStringLiteral("fileSize"), integerJson(savedSize)},
                                   {QStringLiteral("lastModifiedUtcMs"), integerJson(savedModified)}});
    }
    QJsonArray events;
    for (const AudioEvent& event : timeline.events) {
        if (!isValid(event)) return {false, QStringLiteral("invalid audio event")};
        if (event.speedRatio != 1.0 || event.pitchSemitone != 0) {
            return {false, QStringLiteral(
                "per-event speed and pitch are not supported")};
        }
        QJsonArray envelope;
        for (const EnvelopePoint& point : event.envelope) {
            envelope.append(QJsonObject{{QStringLiteral("offset"), integerJson(point.offset)},
                                        {QStringLiteral("gain"), point.gain}});
        }
        events.append(QJsonObject{{QStringLiteral("id"), idJson(event.id)},
                       {QStringLiteral("sourceId"), idJson(sourceIds.at(event.source.get()))},
                       {QStringLiteral("sourceStart"), integerJson(event.sourceStart)},
                       {QStringLiteral("sourceEnd"), integerJson(event.sourceEnd)},
                       {QStringLiteral("timelineStart"), integerJson(event.timelineStart)},
                       {QStringLiteral("gain"), event.gain},
                       {QStringLiteral("fadeIn"), integerJson(event.fadeIn)},
                       {QStringLiteral("fadeOut"), integerJson(event.fadeOut)},
                       {QStringLiteral("fadeInCurve"), fadeCurveName(event.fadeInCurve)},
                       {QStringLiteral("fadeOutCurve"), fadeCurveName(event.fadeOutCurve)},
                       {QStringLiteral("speedRatio"), event.speedRatio},
                       {QStringLiteral("pitchSemitone"), event.pitchSemitone},
                       {QStringLiteral("mute"), event.mute},
                                  {QStringLiteral("envelope"), envelope}});
    }
    QJsonArray markers;
    for (const Marker& marker : request.document->markers()) {
        markers.append(QJsonObject{{QStringLiteral("name"), QString::fromUtf8(marker.name.data(), static_cast<int>(marker.name.size()))},
                                   {QStringLiteral("frame"), integerJson(marker.frame)}});
    }
    QJsonValue selection = QJsonValue::Null;
    if (request.document->selection()) {
        const Selection value = *request.document->selection();
        selection = QJsonObject{{QStringLiteral("start"), integerJson(value.start)},
                                {QStringLiteral("end"), integerJson(value.end)}};
    }
    const QJsonObject root{{QStringLiteral("schemaVersion"), schemaVersion()},
                           {QStringLiteral("sources"), sources}, {QStringLiteral("events"), events},
                           {QStringLiteral("markers"), markers}, {QStringLiteral("selection"), selection},
                           {QStringLiteral("playheadFrame"), integerJson(request.playheadFrame)},
                           {QStringLiteral("visibleStartFrame"), integerJson(request.visibleStartFrame)},
                           {QStringLiteral("visibleEndFrame"), integerJson(request.visibleEndFrame)},
                           {QStringLiteral("exportSettings"), exportJson(request.exportSettings)},
                           {QStringLiteral("editorSettings"), editorJson(request.editorSettings)}};
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (payload.size() > kMaxProjectJsonBytes) {
        return {false, QString::fromLatin1(kResourceLimitMessage)};
    }
    QSaveFile output(projectPath);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(payload) != payload.size()
        || !output.commit()) return {false, QStringLiteral("could not save project")};
    return {true, {}, std::move(savedRecords)};
}

ProjectLoadResult ProjectDocument::load(const QString& path,
                                        const std::atomic_bool* cancelled)
{
    ProjectLoadResult result;
    const auto cancelledNow = [cancelled] {
        return cancelled && cancelled->load(std::memory_order_acquire);
    };
    if (cancelledNow()) {
        result.message = QStringLiteral("project load cancelled");
        return result;
    }
    QFile input(absolutePath(path));
    if (!input.open(QIODevice::ReadOnly)) { result.message = QStringLiteral("could not open project"); return result; }
    if (input.size() < 0 || input.size() > kMaxProjectJsonBytes) {
        result.message = QString::fromLatin1(kResourceLimitMessage);
        return result;
    }
    const QByteArray payload = input.read(kMaxProjectJsonBytes + 1);
    if (payload.size() > kMaxProjectJsonBytes || !input.atEnd()) {
        result.message = QString::fromLatin1(kResourceLimitMessage);
        return result;
    }
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) { result.message = QStringLiteral("malformed project JSON"); return result; }
    const QJsonObject root = json.object();
    qint64 version{};
    if (!integer(root.value(QStringLiteral("schemaVersion")), version)
        || (version != 1 && version != schemaVersion())) {
        result.message = QStringLiteral("unsupported project schema");
        return result;
    }
    if (!root.value(QStringLiteral("sources")).isArray() || !root.value(QStringLiteral("events")).isArray() || !root.value(QStringLiteral("markers")).isArray()) { result.message = QStringLiteral("invalid project arrays"); return result; }
    if (exceedsProjectResourceLimits(root)) {
        result.message = QString::fromLatin1(kResourceLimitMessage);
        return result;
    }
    const QString projectDir = QFileInfo(input.fileName()).dir().absolutePath();
    const auto probeDeadline = std::chrono::steady_clock::now()
        + kProjectProbeBudget;
    std::unordered_map<quint64, std::shared_ptr<const AudioSource>> sourceMap;
    std::unordered_set<quint64> sourceIds;
    QHash<QString, bool> resolvedBoundaryCache;
    QHash<QString, SourceInspection> sourceInspectionCache;
    qsizetype sourceProbeCount = 0;
    bool probeBudgetExhausted = false;
    for (const QJsonValue& value : root.value(QStringLiteral("sources")).toArray()) {
        if (cancelledNow()) {
            result.message = QStringLiteral("project load cancelled");
            return result;
        }
        if (!value.isObject()) { result.message = QStringLiteral("invalid source"); return result; }
        const QJsonObject object = value.toObject();
        quint64 id{}; qint64 rate{}, channels{}, frames{}, fileSize{}, modified{}; QString kind, stored;
        if (!validSourceId(object.value(QStringLiteral("sourceId")), id) || !sourceIds.insert(id).second
            || !stringValue(object, "pathKind", kind) || !stringValue(object, "path", stored)
            || !integer(object.value(QStringLiteral("sampleRate")), rate) || !integer(object.value(QStringLiteral("channels")), channels)
            || !integer(object.value(QStringLiteral("totalFrames")), frames) || !integer(object.value(QStringLiteral("fileSize")), fileSize)
            || !integer(object.value(QStringLiteral("lastModifiedUtcMs")), modified)
            || rate <= 0 || rate > std::numeric_limits<std::uint32_t>::max() || channels <= 0 || channels > std::numeric_limits<std::uint32_t>::max()
            || frames <= 0 || fileSize < -1 || modified < -1) { result.message = QStringLiteral("invalid source metadata"); return result; }
        QString resolved;
        if (kind == QStringLiteral("relative")) {
            if (!safeRelative(stored)) { result.message = QStringLiteral("relative source escapes project"); return result; }
            resolved = absolutePath(QDir(projectDir).filePath(stored));
            const QString boundaryKey = sourceInspectionKey(resolved);
            auto boundaryIt = resolvedBoundaryCache.constFind(boundaryKey);
            if (boundaryIt == resolvedBoundaryCache.cend()) {
                resolvedBoundaryCache.insert(
                    boundaryKey, withinResolvedBoundary(resolved, projectDir));
                boundaryIt = resolvedBoundaryCache.constFind(boundaryKey);
            }
            if (!boundaryIt.value()) { result.message = QStringLiteral("relative source escapes project"); return result; }
        } else if (kind == QStringLiteral("absolute") && QFileInfo(stored).isAbsolute()) {
            resolved = absolutePath(stored);
        } else { result.message = QStringLiteral("invalid source path"); return result; }
        auto source = std::make_shared<const AudioSource>(AudioSource{toPath(resolved), static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(channels), frames});
        sourceMap.emplace(id, source);
        result.sources.push_back({id, source, fileSize, modified});
        const QString inspectionKey = sourceInspectionKey(resolved);
        auto inspectionIt = sourceInspectionCache.constFind(inspectionKey);
        if (inspectionIt == sourceInspectionCache.cend()) {
            const QFileInfo actual(resolved);
            if (!actual.exists()) {
                sourceInspectionCache.insert(inspectionKey, SourceInspection{});
                inspectionIt = sourceInspectionCache.constFind(inspectionKey);
            } else {
                if (probeBudgetExhausted
                    || sourceProbeCount >= kMaxProjectSourceProbes
                    || std::chrono::steady_clock::now() >= probeDeadline) {
                    probeBudgetExhausted = true;
                    result.issues.push_back({ProjectSourceIssueKind::Unavailable, id,
                                             resolved,
                                             QStringLiteral("project source probe budget exceeded")});
                    continue;
                }
                ++sourceProbeCount;
                AudioSourceProbeResult probe = AudioSourceProbe::probe(
                    toPath(resolved), probeDeadline, cancelled);
                if (cancelledNow()) {
                    result.message = QStringLiteral("project load cancelled");
                    return result;
                }
                if (probe.timed_out) {
                    probeBudgetExhausted = true;
                    result.issues.push_back({ProjectSourceIssueKind::Unavailable, id,
                                             resolved,
                                             QStringLiteral("project source probe budget exceeded")});
                    continue;
                }
                sourceInspectionCache.insert(inspectionKey, SourceInspection{
                    true, actual.size(),
                    actual.lastModified().toUTC().toMSecsSinceEpoch(),
                    std::move(probe)});
                inspectionIt = sourceInspectionCache.constFind(inspectionKey);
            }
        }
        if (!inspectionIt->exists) {
            result.issues.push_back({ProjectSourceIssueKind::Missing, id,
                                     resolved, QStringLiteral("source file is missing")});
        } else {
            const bool statMismatch = (fileSize >= 0
                                        && inspectionIt->fileSize != fileSize)
                || (modified >= 0
                    && inspectionIt->modifiedUtcMs != modified);
            const AudioSource expected{toPath(resolved), static_cast<std::uint32_t>(rate),
                                       static_cast<std::uint32_t>(channels), frames};
            const bool formatMismatch = !inspectionIt->probe.matchesFormat(expected);
            if (statMismatch || formatMismatch) {
                result.issues.push_back({ProjectSourceIssueKind::IdentityMismatch,
                    id, resolved, QStringLiteral("source identity changed or is unreadable")});
            }
        }
    }
    std::vector<AudioEvent> events;
    std::unordered_set<quint64> eventIds;
    for (const QJsonValue& value : root.value(QStringLiteral("events")).toArray()) {
        if (cancelledNow()) {
            result.message = QStringLiteral("project load cancelled");
            return result;
        }
        if (!value.isObject()) { result.message = QStringLiteral("invalid event"); return result; }
        const QJsonObject object = value.toObject(); AudioEvent event; quint64 sourceId{}; qint64 start{}, end{}, timeline{}, fadeIn{}, fadeOut{}, pitch{};
        if (!positiveId(object.value(QStringLiteral("id")), event.id)
            || event.id == std::numeric_limits<quint64>::max()
            || !eventIds.insert(event.id).second
            || !validSourceId(object.value(QStringLiteral("sourceId")), sourceId) || sourceMap.find(sourceId) == sourceMap.end()
            || !integer(object.value(QStringLiteral("sourceStart")), start) || !integer(object.value(QStringLiteral("sourceEnd")), end)
            || !integer(object.value(QStringLiteral("timelineStart")), timeline) || !finiteFloat(object.value(QStringLiteral("gain")), event.gain)
            || !integer(object.value(QStringLiteral("fadeIn")), fadeIn) || !integer(object.value(QStringLiteral("fadeOut")), fadeOut)
            || !finiteDouble(object.value(QStringLiteral("speedRatio")), event.speedRatio) || !integer(object.value(QStringLiteral("pitchSemitone")), pitch)
            || !boolValue(object, "mute", event.mute) || !object.value(QStringLiteral("envelope")).isArray()
            || pitch < std::numeric_limits<int>::min() || pitch > std::numeric_limits<int>::max()) { result.message = QStringLiteral("invalid event metadata"); return result; }
        if (version == 1) {
            event.fadeInCurve = FadeCurve::Linear;
            event.fadeOutCurve = FadeCurve::Linear;
        } else if (!fadeCurve(object.value(QStringLiteral("fadeInCurve")),
                              event.fadeInCurve)
                   || !fadeCurve(object.value(QStringLiteral("fadeOutCurve")),
                                 event.fadeOutCurve)) {
            result.message = QStringLiteral("invalid event metadata");
            return result;
        }
        event.source = sourceMap.at(sourceId); event.sourceStart = start; event.sourceEnd = end; event.timelineStart = timeline; event.fadeIn = fadeIn; event.fadeOut = fadeOut; event.pitchSemitone = static_cast<int>(pitch);
        for (const QJsonValue& pointValue : object.value(QStringLiteral("envelope")).toArray()) {
            if (!pointValue.isObject()) { result.message = QStringLiteral("invalid envelope"); return result; }
            const QJsonObject point = pointValue.toObject(); EnvelopePoint envelope;
            if (!integer(point.value(QStringLiteral("offset")), envelope.offset) || !finiteFloat(point.value(QStringLiteral("gain")), envelope.gain)) { result.message = QStringLiteral("invalid envelope"); return result; }
            event.envelope.push_back(envelope);
        }
        if (!isValid(event)) { result.message = QStringLiteral("invalid event range"); return result; }
        if (event.speedRatio != 1.0 || event.pitchSemitone != 0) {
            result.message = QStringLiteral(
                "per-event speed and pitch are not supported");
            return result;
        }
        events.push_back(std::move(event));
    }
    AudioDocument document = AudioDocument::fromEvents(std::move(events));
    if (document.timelineSnapshot().events.size() != static_cast<std::size_t>(root.value(QStringLiteral("events")).toArray().size())) { result.message = QStringLiteral("overlapping project events"); return result; }
    for (const QJsonValue& value : root.value(QStringLiteral("markers")).toArray()) {
        if (!value.isObject()) { result.message = QStringLiteral("invalid marker"); return result; }
        const QJsonObject marker = value.toObject(); QString name; qint64 frame{};
        if (!stringValue(marker, "name", name) || !integer(marker.value(QStringLiteral("frame")), frame) || !document.addMarker({name.toUtf8().toStdString(), frame})) { result.message = QStringLiteral("invalid marker"); return result; }
    }
    const QJsonValue selection = root.value(QStringLiteral("selection"));
    if (!selection.isNull()) {
        if (!selection.isObject()) { result.message = QStringLiteral("invalid selection"); return result; }
        const QJsonObject object = selection.toObject(); qint64 start{}, end{};
        if (!integer(object.value(QStringLiteral("start")), start) || !integer(object.value(QStringLiteral("end")), end) || !document.setSelection({start, end})) { result.message = QStringLiteral("invalid selection"); return result; }
    }
    if (!integer(root.value(QStringLiteral("playheadFrame")), result.playheadFrame) || !integer(root.value(QStringLiteral("visibleStartFrame")), result.visibleStartFrame) || !integer(root.value(QStringLiteral("visibleEndFrame")), result.visibleEndFrame)
        || result.playheadFrame < 0 || result.playheadFrame > document.totalFrames()
        || (document.totalFrames() == 0
                ? (result.playheadFrame != 0 || result.visibleStartFrame != 0
                   || result.visibleEndFrame != 0)
                : (result.visibleStartFrame < 0
                   || result.visibleEndFrame <= result.visibleStartFrame
                   || result.visibleEndFrame > document.totalFrames()))
        || !parseExport(root.value(QStringLiteral("exportSettings")), result.exportSettings)
        || !parseEditor(root.value(QStringLiteral("editorSettings")),
                        result.editorSettings)) {
        result.message = QStringLiteral("invalid editor state");
        return result;
    }
    result.document = std::make_unique<AudioDocument>(std::move(document));
    return result;
}

ProjectRelinkResult ProjectDocument::relink(
    AudioDocument& document, std::vector<ProjectSourceRecord>& sources,
    const quint64 sourceId, const QString& replacementPath,
    const std::atomic_bool* cancelled)
{
    const auto cancelledNow = [cancelled] {
        return cancelled && cancelled->load(std::memory_order_acquire);
    };
    if (cancelledNow()) return {false, QStringLiteral("project relink cancelled")};
    if (sourceId == 0 || sourceId == std::numeric_limits<quint64>::max()) {
        return {false, QStringLiteral("invalid source id")};
    }
    const auto record = std::find_if(sources.begin(), sources.end(), [sourceId](const ProjectSourceRecord& source) { return source.sourceId == sourceId; });
    if (record == sources.end() || !record->source || replacementPath.isEmpty()) return {false, QStringLiteral("unknown project source")};
    const QString path = absolutePath(replacementPath); const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) return {false, QStringLiteral("replacement source is missing")};
    const AudioSource& expected = *record->source;
    if (record->fileSize >= 0 && file.size() != record->fileSize) {
        return {false, QStringLiteral("replacement source identity does not match")};
    }
    const AudioSourceProbeResult probe = AudioSourceProbe::probe(
        toPath(path), std::chrono::steady_clock::now()
            + std::chrono::seconds(2), cancelled);
    if (cancelledNow()) return {false, QStringLiteral("project relink cancelled")};
    if (!probe.matchesFormat(expected)) {
        return {false, QStringLiteral("replacement source format does not match")};
    }
    auto replacement = std::make_shared<const AudioSource>(AudioSource{toPath(path), expected.sample_rate, expected.channels, expected.total_frames});
    std::vector<AudioEvent> events = document.timelineSnapshot().events; bool changed = false;
    for (AudioEvent& event : events) if (event.source == record->source) { event.source = replacement; changed = true; }
    if (!changed) return {false, QStringLiteral("project source is not in document")};
    AudioDocument rebuilt = AudioDocument::fromEvents(std::move(events));
    if (rebuilt.timelineSnapshot().events.empty()) return {false, QStringLiteral("replacement creates an invalid document")};
    for (const Marker& marker : document.markers()) if (!rebuilt.addMarker(marker)) return {false, QStringLiteral("could not restore markers")};
    if (document.selection() && !rebuilt.setSelection(*document.selection())) return {false, QStringLiteral("could not restore selection")};
    document = std::move(rebuilt); record->source = replacement; record->fileSize = file.size(); record->lastModifiedUtcMs = file.lastModified().toUTC().toMSecsSinceEpoch();
    return {true, {}};
}

} // namespace agplayer::editor
