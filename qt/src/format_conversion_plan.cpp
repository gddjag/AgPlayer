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

struct ResolvedDepth {
    QString key;
    QString codec;
    QString sampleFormat;
};

ResolvedDepth resolve_depth(const QString& format, const QString& requested,
                            const agplayer::AudioStreamProbe& source)
{
    QString depth = requested.trimmed().toLower();
    if (depth.isEmpty() || depth == QStringLiteral("auto")) {
        if (format == QStringLiteral("wav")
            && source.sample_format.find("flt") != std::string::npos) {
            depth = QStringLiteral("flt");
        } else if (source.bits_per_sample > 24
                   && (format == QStringLiteral("wav")
                       || format == QStringLiteral("aiff"))) {
            depth = QStringLiteral("s32");
        } else if (source.bits_per_sample > 16) {
            depth = QStringLiteral("s24");
        } else {
            depth = QStringLiteral("s16");
        }
    }

    if (format == QStringLiteral("wav")) {
        if (depth == QStringLiteral("s24")) {
            return {depth, QStringLiteral("pcm_s24le"), QStringLiteral("s32")};
        }
        if (depth == QStringLiteral("s32")) {
            return {depth, QStringLiteral("pcm_s32le"), QStringLiteral("s32")};
        }
        if (depth == QStringLiteral("flt")) {
            return {depth, QStringLiteral("pcm_f32le"), QStringLiteral("flt")};
        }
        return {QStringLiteral("s16"), QStringLiteral("pcm_s16le"),
                QStringLiteral("s16")};
    }
    if (format == QStringLiteral("aiff")) {
        if (depth == QStringLiteral("s24")) {
            return {depth, QStringLiteral("pcm_s24be"), QStringLiteral("s32")};
        }
        if (depth == QStringLiteral("s32")) {
            return {depth, QStringLiteral("pcm_s32be"), QStringLiteral("s32")};
        }
        return {QStringLiteral("s16"), QStringLiteral("pcm_s16be"),
                QStringLiteral("s16")};
    }
    if (format == QStringLiteral("flac")) {
        return depth == QStringLiteral("s24")
            ? ResolvedDepth{depth, {}, QStringLiteral("s32")}
            : ResolvedDepth{QStringLiteral("s16"), {}, QStringLiteral("s16")};
    }
    if (format == QStringLiteral("alac")) {
        return depth == QStringLiteral("s24")
            ? ResolvedDepth{depth, {}, QStringLiteral("s32p")}
            : ResolvedDepth{QStringLiteral("s16"), {}, QStringLiteral("s16p")};
    }
    return {};
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
            {QStringLiteral("bitDepth"), request.bitDepth},
            {QStringLiteral("audioStreamIndex"), task.audioStreamIndex},
            {QStringLiteral("keepMetadata"), request.keepMetadata},
            {QStringLiteral("keepCover"), request.keepCover},
            {QStringLiteral("extractAudio"), request.extractAudio},
            {QStringLiteral("preserveDirectories"), request.preserveDirectories},
        };
        const auto selectedStream = std::find_if(
            input.probe.audio_streams.cbegin(), input.probe.audio_streams.cend(),
            [&task](const agplayer::AudioStreamProbe& stream) {
                return stream.stream_index == task.audioStreamIndex;
            });
        if (selectedStream != input.probe.audio_streams.cend()) {
            const QString format = request.formatKey.toLower();
            if (task.resolvedProfile.value(QStringLiteral("sampleRate")).toInt()
                == 0) {
                int resolvedRate = selectedStream->sample_rate;
                if (!capability->sample_rates.empty()) {
                    resolvedRate = capability->sample_rates.front();
                    for (const int candidate : capability->sample_rates) {
                        if (std::abs(candidate - selectedStream->sample_rate)
                            < std::abs(resolvedRate
                                       - selectedStream->sample_rate)) {
                            resolvedRate = candidate;
                        }
                    }
                }
                task.resolvedProfile.insert(QStringLiteral("sampleRate"),
                                            resolvedRate);
            }
            if (task.resolvedProfile.value(
                    QStringLiteral("channelLayout")).toString().isEmpty()) {
                task.resolvedProfile.insert(
                    QStringLiteral("channelLayout"),
                    QString::fromStdString(selectedStream->channel_layout));
            }
            if (task.resolvedProfile.value(
                    QStringLiteral("sampleFormat")).toString().isEmpty()
                && !capability->sample_formats.empty()) {
                const auto preferred = std::find(
                    capability->sample_formats.cbegin(),
                    capability->sample_formats.cend(), "fltp");
                task.resolvedProfile.insert(
                    QStringLiteral("sampleFormat"),
                    QString::fromStdString(
                        preferred != capability->sample_formats.cend()
                            ? *preferred : capability->sample_formats.front()));
            }
            const ResolvedDepth depth = resolve_depth(format, request.bitDepth,
                                                       *selectedStream);
            if (!depth.key.isEmpty()) {
                task.resolvedProfile.insert(QStringLiteral("bitDepth"), depth.key);
                task.resolvedProfile.insert(QStringLiteral("sampleFormat"),
                                            depth.sampleFormat);
                if (!depth.codec.isEmpty()) {
                    task.resolvedProfile.insert(QStringLiteral("codec"), depth.codec);
                }
                const bool automatic = request.bitDepth.trimmed().isEmpty()
                    || request.bitDepth.compare(QStringLiteral("auto"),
                                                Qt::CaseInsensitive) == 0;
                if (automatic && selectedStream->bits_per_sample > 24
                    && (format == QStringLiteral("flac")
                        || format == QStringLiteral("alac"))) {
                    task.differences.push_back({
                        QStringLiteral("bitDepth"),
                        selectedStream->bits_per_sample,
                        QStringLiteral("s24"),
                        QStringLiteral("Output format supports at most 24-bit audio"),
                        true});
                } else if (automatic && format == QStringLiteral("aiff")
                           && selectedStream->sample_format.find("flt")
                               != std::string::npos) {
                    task.differences.push_back({
                        QStringLiteral("bitDepth"),
                        QString::fromStdString(selectedStream->sample_format),
                        depth.key,
                        QStringLiteral("AIFF uses integer PCM"), true});
                }
            }
        }
        for (const FormatPlanDifference& difference : task.differences) {
            batch.requiresConfirmation = batch.requiresConfirmation
                || difference.requiresConfirmation;
        }
        batch.tasks.push_back(std::move(task));
    }
    batch.ready = true;
    return batch;
}
