#pragma once

#include "transcode_probe.hpp"

#include <QList>
#include <QString>
#include <QUuid>
#include <QVariant>
#include <QVariantMap>

enum class FormatConflictPolicy {
    AutoNumber,
    Skip,
    Overwrite,
    Ask,
};

struct FormatConversionRequest {
    QString formatKey;
    QString codecName;
    QString bitrateMode = QStringLiteral("cbr");
    qint64 bitrate = 0;
    int quality = 75;
    int sampleRate = 0;
    QString channelLayout;
    QString sampleFormat;
    QString bitDepth;
    QString outputDirectory;
    FormatConflictPolicy conflictPolicy = FormatConflictPolicy::AutoNumber;
    bool keepMetadata = false;
    bool keepCover = false;
    bool preserveDirectories = false;
    bool extractAudio = false;
};

struct FormatPlanDifference {
    QString field;
    QVariant requested;
    QVariant resolved;
    QString reason;
    bool requiresConfirmation = false;
};

struct FormatPlanInput {
    QUuid taskId;
    QString inputPath;
    QString importRoot;
    agplayer::MediaProbe probe;
    int selectedAudioStreamIndex = -1;
};

struct FormatTaskPlan {
    QUuid taskId;
    QString inputPath;
    QString outputPath;
    int audioStreamIndex = -1;
    QVariantMap resolvedProfile;
    QList<FormatPlanDifference> differences;
    bool skipped = false;
};

struct FormatBatchPlan {
    QList<FormatTaskPlan> tasks;
    bool ready = false;
    bool requiresConfirmation = false;
    QString fatalError;
};

FormatBatchPlan build_format_conversion_plan(
    const QList<FormatPlanInput>& inputs,
    const FormatConversionRequest& request);
