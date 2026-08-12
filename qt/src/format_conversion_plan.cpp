#include "format_conversion_plan.hpp"

#include "transcode_capability.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {

QString extension_for(const QString& format_key)
{
    return format_key.compare(QStringLiteral("alac"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("m4a") : format_key.toLower();
}

QString folded_path(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

bool escapes_root(const QString& relative_path)
{
    const QString clean = QDir::cleanPath(relative_path);
    return QDir::isAbsolutePath(clean) || clean == QStringLiteral("..")
        || clean.startsWith(QStringLiteral("../"))
        || clean.startsWith(QStringLiteral("..\\"));
}

int selected_audio_stream(const FormatPlanInput& input,
                          QList<FormatPlanDifference>& differences)
{
    if (input.selectedAudioStreamIndex >= 0) {
        const auto found = std::find_if(
            input.probe.audio_streams.cbegin(), input.probe.audio_streams.cend(),
            [&input](const agplayer::AudioStreamProbe& stream) {
                return stream.stream_index == input.selectedAudioStreamIndex;
            });
        return found == input.probe.audio_streams.cend()
            ? -1 : input.selectedAudioStreamIndex;
    }

    const auto default_stream = std::find_if(
        input.probe.audio_streams.cbegin(), input.probe.audio_streams.cend(),
        [](const agplayer::AudioStreamProbe& stream) {
            return stream.is_default;
        });
    const int resolved = default_stream != input.probe.audio_streams.cend()
        ? default_stream->stream_index
        : input.probe.audio_streams.front().stream_index;
    if (input.probe.audio_streams.size() > 1) {
        differences.push_back({QStringLiteral("audioStream"),
                               QVariant(), resolved,
                               QStringLiteral("Multiple audio streams require confirmation"),
                               true});
    }
    return resolved;
}

QString base_output_path(const FormatPlanInput& input,
                         const FormatConversionRequest& request,
                         QString& error)
{
    QString relative_directory;
    if (request.preserveDirectories && !input.importRoot.isEmpty()) {
        const QString relative = QDir(input.importRoot).relativeFilePath(
            QFileInfo(input.inputPath).absoluteFilePath());
        if (escapes_root(relative)) {
            error = QStringLiteral("Input is outside its import root: %1")
                        .arg(input.inputPath);
            return {};
        }
        relative_directory = QFileInfo(relative).path();
        if (relative_directory == QStringLiteral(".")) {
            relative_directory.clear();
        }
    }

    QDir output_root(request.outputDirectory.isEmpty()
                         ? QFileInfo(input.inputPath).absolutePath()
                         : request.outputDirectory);
    QString directory = output_root.absolutePath();
    if (!relative_directory.isEmpty()) {
        directory = output_root.filePath(relative_directory);
    }
    return QDir(directory).filePath(
        QFileInfo(input.inputPath).completeBaseName()
        + QStringLiteral(".") + extension_for(request.formatKey));
}

QString numbered_output_path(const QString& base_path,
                             const QSet<QString>& reserved)
{
    const QFileInfo info(base_path);
    QString candidate = base_path;
    int number = 1;
    while (QFileInfo::exists(candidate)
           || reserved.contains(folded_path(candidate))) {
        candidate = info.dir().filePath(
            QStringLiteral("%1_%2.%3")
                .arg(info.completeBaseName(), QString::number(number),
                     info.suffix()));
        ++number;
    }
    return candidate;
}

} // namespace

FormatBatchPlan build_format_conversion_plan(
    const QList<FormatPlanInput>& inputs,
    const FormatConversionRequest& request)
{
    FormatBatchPlan batch;
    if (inputs.isEmpty()) {
        batch.fatalError = QStringLiteral("No tasks selected");
        return batch;
    }

    const std::vector<agplayer::TranscodeFormatCapability> capabilities =
        agplayer::transcode_capabilities();
    const QByteArray format_key = request.formatKey.toLower().toUtf8();
    const auto* capability = agplayer::find_transcode_capability(
        capabilities, format_key.toStdString());
    if (capability == nullptr || !capability->available) {
        batch.fatalError = capability == nullptr
            ? QStringLiteral("Unknown output format: %1").arg(request.formatKey)
            : QString::fromStdString(capability->unavailable_reason);
        return batch;
    }

    QSet<QString> reserved;
    for (const FormatPlanInput& input : inputs) {
        if (input.inputPath.isEmpty() || input.probe.audio_streams.empty()) {
            batch.fatalError = QStringLiteral("Input contains no audio stream: %1")
                                   .arg(input.inputPath);
            batch.tasks.clear();
            return batch;
        }
        if (input.probe.is_video && !request.extractAudio) {
            batch.fatalError = QStringLiteral(
                "Video input requires audio extraction: %1")
                                   .arg(input.inputPath);
            batch.tasks.clear();
            return batch;
        }

        FormatTaskPlan task;
        task.taskId = input.taskId;
        task.inputPath = input.inputPath;
        task.audioStreamIndex = selected_audio_stream(input, task.differences);
        if (task.audioStreamIndex < 0) {
            batch.fatalError = QStringLiteral("Selected audio stream is unavailable: %1")
                                   .arg(input.inputPath);
            batch.tasks.clear();
            return batch;
        }

        QString path_error;
        const QString base_path = base_output_path(input, request, path_error);
        if (!path_error.isEmpty()) {
            batch.fatalError = path_error;
            batch.tasks.clear();
            return batch;
        }
        const bool conflict = QFileInfo::exists(base_path)
            || reserved.contains(folded_path(base_path));
        switch (request.conflictPolicy) {
        case FormatConflictPolicy::AutoNumber:
            task.outputPath = numbered_output_path(base_path, reserved);
            break;
        case FormatConflictPolicy::Skip:
            task.outputPath = base_path;
            task.skipped = conflict;
            break;
        case FormatConflictPolicy::Overwrite:
            task.outputPath = base_path;
            break;
        case FormatConflictPolicy::Ask:
            task.outputPath = base_path;
            if (conflict) {
                task.differences.push_back({
                    QStringLiteral("conflict"), base_path, base_path,
                    QStringLiteral("Output already exists"), true});
            }
            break;
        }

        if (!task.skipped
            && folded_path(task.outputPath) == folded_path(task.inputPath)) {
            batch.fatalError = QStringLiteral(
                "Input and output path must be different: %1")
                                   .arg(task.inputPath);
            batch.tasks.clear();
            return batch;
        }
        reserved.insert(folded_path(task.outputPath));

        task.resolvedProfile = {
            {QStringLiteral("format"), request.formatKey.toLower()},
            {QStringLiteral("codec"), QString::fromStdString(capability->codec_name)},
            {QStringLiteral("muxer"), QString::fromStdString(capability->muxer_name)},
            {QStringLiteral("bitrate"), request.bitrate},
            {QStringLiteral("bitrateMode"), request.bitrateMode},
            {QStringLiteral("quality"), request.quality},
            {QStringLiteral("sampleRate"), request.sampleRate},
            {QStringLiteral("channelLayout"), request.channelLayout},
            {QStringLiteral("sampleFormat"), request.sampleFormat},
            {QStringLiteral("audioStreamIndex"), task.audioStreamIndex},
            {QStringLiteral("keepMetadata"), request.keepMetadata},
            {QStringLiteral("keepCover"), request.keepCover},
        };
        for (const FormatPlanDifference& difference : task.differences) {
            batch.requiresConfirmation = batch.requiresConfirmation
                || difference.requiresConfirmation;
        }
        batch.tasks.push_back(std::move(task));
    }
    batch.ready = true;
    return batch;
}
