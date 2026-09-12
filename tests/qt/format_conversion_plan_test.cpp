#include "format_conversion_plan.hpp"
#include "transcode_probe.hpp"

#include "../core/bpm_fixture.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class FormatConversionPlanTest final : public QObject {
    Q_OBJECT

private slots:
    void probesGeneratedAudio();
    void plansConflictPoliciesWithoutCreatingOutput();
    void preservesImportRootAndRejectsTraversal();
    void rejectsVideoUnlessExtractionIsEnabled();
    void probesAndPreservesFriendlySourceBitDepth();
    void resolvesExplicitFriendlyDepthForLosslessFormats();
    void plansRecommendedOutputExtensions();
    void confirmsAutomaticDepthConversionsThatCannotBePreservedExactly();
    void doesNotPromoteUnknownLossySourceDepth();
};

namespace {

agplayer::MediaProbe audioProbe(bool video = false)
{
    agplayer::MediaProbe probe;
    probe.container = video ? "matroska,webm" : "wav";
    probe.is_video = video;
    probe.audio_streams.push_back({0, "pcm_s16le", "", "Main", true,
                                   16'000, "s16", "mono", 256'000, 1'000});
    return probe;
}

FormatPlanInput inputFor(const QString& path,
                         const QString& root = {},
                         bool video = false)
{
    FormatPlanInput input;
    input.taskId = QUuid::createUuid();
    input.inputPath = path;
    input.importRoot = root;
    input.probe = audioProbe(video);
    return input;
}

FormatConversionRequest flacRequest(const QString& outputDirectory)
{
    FormatConversionRequest request;
    request.formatKey = QStringLiteral("flac");
    request.outputDirectory = outputDirectory;
    request.conflictPolicy = FormatConflictPolicy::AutoNumber;
    request.keepMetadata = true;
    return request;
}

} // namespace

void FormatConversionPlanTest::probesGeneratedAudio()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("probe.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(path, 120, 1));

    agplayer::MediaProbe probe;
    std::string error;
    QCOMPARE(agplayer::probe_transcode_input(path.toUtf8().toStdString(),
                                             probe, error),
             AG_OK);
    QVERIFY2(error.empty(), error.c_str());
    QVERIFY(!probe.container.empty());
    QVERIFY(!probe.is_video);
    QCOMPARE(probe.audio_streams.size(), std::size_t{1});
    QCOMPARE(probe.audio_streams.front().sample_rate, 16'000);
    QCOMPARE(probe.audio_streams.front().channel_layout, std::string("mono"));
    QVERIFY(probe.audio_streams.front().duration_ms > 0);
}

void FormatConversionPlanTest::plansConflictPoliciesWithoutCreatingOutput()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("song.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));
    const QString outputDirectory = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString existing =
        QDir(outputDirectory).filePath(QStringLiteral("song.flac"));
    QFile existingFile(existing);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    existingFile.write("existing");
    existingFile.close();

    FormatConversionRequest request = flacRequest(outputDirectory);
    const QList<FormatPlanInput> inputs{inputFor(input)};

    FormatBatchPlan plan = build_format_conversion_plan(inputs, request);
    QVERIFY(plan.ready);
    QVERIFY(!plan.requiresConfirmation);
    QCOMPARE(plan.tasks.size(), 1);
    QCOMPARE(QFileInfo(plan.tasks.front().outputPath).fileName(),
             QStringLiteral("song_1.flac"));
    QVERIFY(!QFileInfo::exists(plan.tasks.front().outputPath));

    request.conflictPolicy = FormatConflictPolicy::Skip;
    plan = build_format_conversion_plan(inputs, request);
    QVERIFY(plan.ready);
    QVERIFY(plan.tasks.front().skipped);
    QCOMPARE(plan.tasks.front().outputPath, existing);

    request.conflictPolicy = FormatConflictPolicy::Overwrite;
    plan = build_format_conversion_plan(inputs, request);
    QVERIFY(plan.ready);
    QVERIFY(!plan.tasks.front().skipped);
    QCOMPARE(plan.tasks.front().outputPath, existing);

    request.conflictPolicy = FormatConflictPolicy::Ask;
    plan = build_format_conversion_plan(inputs, request);
    QVERIFY(plan.ready);
    QVERIFY(plan.requiresConfirmation);
    QCOMPARE(plan.tasks.front().outputPath, existing);
    QVERIFY(std::any_of(plan.tasks.front().differences.cbegin(),
                        plan.tasks.front().differences.cend(),
                        [](const FormatPlanDifference& difference) {
        return difference.field == QStringLiteral("conflict")
               && difference.requiresConfirmation;
    }));
    QVERIFY(QFileInfo(existing).size() == 8);
}

void FormatConversionPlanTest::preservesImportRootAndRejectsTraversal()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString root = temp.filePath(QStringLiteral("album"));
    const QString nested = QDir(root).filePath(QStringLiteral("disc-1"));
    const QString outputDirectory = temp.filePath(QStringLiteral("converted"));
    QVERIFY(QDir().mkpath(nested));
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString input = QDir(nested).filePath(QStringLiteral("track.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatConversionRequest request = flacRequest(outputDirectory);
    request.preserveDirectories = true;
    FormatBatchPlan plan = build_format_conversion_plan(
        {inputFor(input, root)}, request);
    QVERIFY(plan.ready);
    QCOMPARE(QDir::cleanPath(plan.tasks.front().outputPath),
             QDir::cleanPath(QDir(outputDirectory).filePath(
                 QStringLiteral("disc-1/track.flac"))));

    const QString outside = temp.filePath(QStringLiteral("outside.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(outside, 120, 1));
    plan = build_format_conversion_plan({inputFor(outside, root)}, request);
    QVERIFY(!plan.ready);
    QVERIFY(plan.fatalError.contains(QStringLiteral("outside"),
                                     Qt::CaseInsensitive));
}

void FormatConversionPlanTest::rejectsVideoUnlessExtractionIsEnabled()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("clip.mkv"));
    QFile marker(input);
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("fixture-marker");
    marker.close();

    FormatConversionRequest request = flacRequest(temp.path());
    FormatBatchPlan plan = build_format_conversion_plan(
        {inputFor(input, {}, true)}, request);
    QVERIFY(!plan.ready);
    QVERIFY(plan.fatalError.contains(QStringLiteral("video"),
                                     Qt::CaseInsensitive));

    request.extractAudio = true;
    plan = build_format_conversion_plan({inputFor(input, {}, true)}, request);
    QVERIFY(plan.ready);
    QCOMPARE(plan.tasks.front().audioStreamIndex, 0);
    QCOMPARE(plan.tasks.front().resolvedProfile
                 .value(QStringLiteral("extractAudio")).toBool(), true);
}

void FormatConversionPlanTest::probesAndPreservesFriendlySourceBitDepth()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    agplayer::MediaProbe probed;
    std::string error;
    QCOMPARE(agplayer::probe_transcode_input(input.toUtf8().toStdString(),
                                             probed, error), AG_OK);
    QCOMPARE(probed.audio_streams.front().bits_per_sample, 16);

    FormatPlanInput source = inputFor(input);
    source.probe.audio_streams.front().bits_per_sample = 24;
    source.probe.audio_streams.front().sample_format = "s32";
    for (const QString& format : {QStringLiteral("wav"),
                                  QStringLiteral("flac"),
                                  QStringLiteral("alac")}) {
        FormatConversionRequest request;
        request.formatKey = format;
        request.outputDirectory = temp.filePath(format);
        const FormatBatchPlan plan = build_format_conversion_plan({source}, request);
        QVERIFY2(plan.ready, qPrintable(plan.fatalError));
        const QVariantMap profile = plan.tasks.front().resolvedProfile;
        QCOMPARE(profile.value(QStringLiteral("bitDepth")).toString(),
                 QStringLiteral("s24"));
        QCOMPARE(profile.value(QStringLiteral("sampleFormat")).toString(),
                 format == QStringLiteral("alac")
                     ? QStringLiteral("s32p") : QStringLiteral("s32"));
        if (format == QStringLiteral("wav")) {
            QCOMPARE(profile.value(QStringLiteral("codec")).toString(),
                     QStringLiteral("pcm_s24le"));
        }
    }
}

void FormatConversionPlanTest::resolvesExplicitFriendlyDepthForLosslessFormats()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    for (const QString& format : {QStringLiteral("wav"),
                                  QStringLiteral("flac"),
                                  QStringLiteral("alac")}) {
        FormatConversionRequest request;
        request.formatKey = format;
        request.bitDepth = QStringLiteral("s24");
        request.outputDirectory = temp.filePath(format);
        const FormatBatchPlan plan = build_format_conversion_plan(
            {inputFor(input)}, request);
        QVERIFY2(plan.ready, qPrintable(plan.fatalError));
        QCOMPARE(plan.tasks.front().resolvedProfile
                     .value(QStringLiteral("bitDepth")).toString(),
                 QStringLiteral("s24"));
        QCOMPARE(plan.tasks.front().resolvedProfile
                     .value(QStringLiteral("sampleFormat")).toString(),
                 format == QStringLiteral("alac")
                     ? QStringLiteral("s32p") : QStringLiteral("s32"));
    }
}

void FormatConversionPlanTest::plansRecommendedOutputExtensions()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    struct Expected {
        const char* format;
        const char* suffix;
    };
    const Expected expected[] = {
        {"aac", "aac"}, {"alac", "m4a"}, {"aiff", "aiff"},
    };
    for (const Expected& item : expected) {
        FormatConversionRequest request;
        request.formatKey = QString::fromLatin1(item.format);
        request.outputDirectory = temp.path();
        const FormatBatchPlan plan = build_format_conversion_plan(
            {inputFor(input)}, request);
        QVERIFY2(plan.ready, qPrintable(plan.fatalError));
        QCOMPARE(QFileInfo(plan.tasks.front().outputPath).suffix(),
                 QString::fromLatin1(item.suffix));
    }
}

void FormatConversionPlanTest::confirmsAutomaticDepthConversionsThatCannotBePreservedExactly()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.wav"));
    QVERIFY(agplayer::test::writeClickTrackWav(input, 120, 1));

    FormatPlanInput planInput = inputFor(input);
    planInput.probe.audio_streams.front().bits_per_sample = 32;
    planInput.probe.audio_streams.front().sample_format = "flt";

    FormatConversionRequest request;
    request.outputDirectory = temp.path();
    request.formatKey = QStringLiteral("flac");
    FormatBatchPlan plan = build_format_conversion_plan({planInput}, request);
    QVERIFY(plan.ready);
    QVERIFY(plan.requiresConfirmation);
    QCOMPARE(plan.tasks.front().resolvedProfile.value(QStringLiteral("bitDepth")),
             QVariant(QStringLiteral("s24")));

    request.formatKey = QStringLiteral("aiff");
    plan = build_format_conversion_plan({planInput}, request);
    QVERIFY(plan.ready);
    QVERIFY(plan.requiresConfirmation);
    QCOMPARE(plan.tasks.front().resolvedProfile.value(QStringLiteral("bitDepth")),
             QVariant(QStringLiteral("s32")));
}

void FormatConversionPlanTest::doesNotPromoteUnknownLossySourceDepth()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("source.mp3"));
    QFile marker(input);
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("fixture-marker");
    marker.close();

    FormatPlanInput planInput = inputFor(input);
    auto& stream = planInput.probe.audio_streams.front();
    stream.codec = "mp3";
    stream.sample_format = "fltp";
    stream.bits_per_sample = 0;

    FormatConversionRequest request;
    request.outputDirectory = temp.path();
    request.formatKey = QStringLiteral("flac");
    const FormatBatchPlan plan = build_format_conversion_plan({planInput}, request);
    QVERIFY(plan.ready);
    QVERIFY(!plan.requiresConfirmation);
    QCOMPARE(plan.tasks.front().resolvedProfile.value(QStringLiteral("bitDepth")),
             QVariant(QStringLiteral("s16")));
}

QTEST_APPLESS_MAIN(FormatConversionPlanTest)

#include "format_conversion_plan_test.moc"
