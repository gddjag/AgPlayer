#include "audio_editor/project_document.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <filesystem>
#include <limits>
#include <memory>

using namespace agplayer::editor;

namespace {

constexpr SampleFrame kFixtureFrames = 88'200;

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QJsonObject readObject(const QString& path)
{
    return QJsonDocument::fromJson(readBytes(path)).object();
}

bool writeObject(const QString& path, const QJsonObject& object)
{
    return writeBytes(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

void compareEvent(const AudioEvent& actual, const AudioEvent& expected)
{
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.sourceStart, expected.sourceStart);
    QCOMPARE(actual.sourceEnd, expected.sourceEnd);
    QCOMPARE(actual.timelineStart, expected.timelineStart);
    QCOMPARE(actual.gain, expected.gain);
    QCOMPARE(actual.fadeIn, expected.fadeIn);
    QCOMPARE(actual.fadeOut, expected.fadeOut);
    QCOMPARE(actual.fadeInCurve, expected.fadeInCurve);
    QCOMPARE(actual.fadeOutCurve, expected.fadeOutCurve);
    QCOMPARE(actual.speedRatio, expected.speedRatio);
    QCOMPARE(actual.pitchSemitone, expected.pitchSemitone);
    QCOMPARE(actual.mute, expected.mute);
    QCOMPARE(actual.envelope.size(), expected.envelope.size());
    for (std::size_t index = 0; index < expected.envelope.size(); ++index) {
        QCOMPARE(actual.envelope[index].offset, expected.envelope[index].offset);
        QCOMPARE(actual.envelope[index].gain, expected.envelope[index].gain);
    }
}

} // namespace

class ProjectDocumentTest final : public QObject {
    Q_OBJECT

    struct FixtureProject final {
        QString projectPath;
        QString sourcePath;
        AudioDocument document;
        ProjectExportSettings exportSettings;
        ProjectEditorSettings editorSettings;
    };

    static QString fixturePath()
    {
        return QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_FIXTURE"));
    }

    static QString mismatchFixturePath()
    {
        return QString::fromUtf8(qgetenv("AGPLAYER_EDITOR_MISMATCH_FIXTURE"));
    }

    static FixtureProject makeProject(QTemporaryDir& temporary)
    {
        const QString mediaDirectory = temporary.filePath(
            QStringLiteral("媒体/共享源"));
        QDir().mkpath(mediaDirectory);
        const QString sourcePath = QDir(mediaDirectory).filePath(
            QStringLiteral("鼓点 音频.wav"));
        if (!QFile::copy(fixturePath(), sourcePath)) return {};

        auto shared = std::make_shared<const AudioSource>(AudioSource{
            nativePath(sourcePath), 44'100, 2, kFixtureFrames});
        AudioEvent first{7, shared, 0, 20'000, 0};
        first.gain = 0.75F;
        first.fadeIn = 320;
        first.fadeOut = 640;
        first.fadeInCurve = FadeCurve::Exponential;
        first.fadeOutCurve = FadeCurve::Smooth;
        first.mute = true;
        first.envelope = {{100, 0.5F}, {10'000, 0.9F}};
        AudioEvent second{9, shared, 20'000, 30'000, 30'000};
        second.gain = 1.1F;
        second.fadeIn = 120;
        second.fadeOut = 240;
        second.fadeInCurve = FadeCurve::Linear;
        second.fadeOutCurve = FadeCurve::Exponential;
        second.envelope = {{500, 0.8F}};
        AudioDocument document = AudioDocument::fromEvents({first, second});
        document.addMarker({u8"前奏", 1'000});
        document.addMarker({u8"尾声", 39'000});
        document.setSelection({500, 4'500});

        ProjectExportSettings settings;
        settings.codecName = QStringLiteral("flac");
        settings.sampleRate = 96'000;
        settings.bitDepth = 24;
        settings.channels = 2;
        settings.bitRate = 512'000;
        settings.keepMetadata = false;
        settings.variableBitRate = true;
        settings.quality = 73;
        settings.outputDirectory = temporary.filePath(QStringLiteral("导出目录"));
        ProjectEditorSettings editor;
        editor.originalBpm = 120.0;
        editor.targetBpm = 150.0;
        editor.speedPercent = 125.0;
        editor.keepPitch = false;
        editor.formantPreservation = true;
        editor.pitchCents = 250;
        editor.trackSolo = true;
        editor.trackGainDb = -3.5;
        return {temporary.filePath(QStringLiteral("工程 测试.agproj")),
                sourcePath, std::move(document), settings, editor};
    }

    static ProjectSaveRequest request(const FixtureProject& project)
    {
        ProjectSaveRequest value;
        value.document = &project.document;
        value.playheadFrame = 1'234;
        value.visibleStartFrame = 100;
        value.visibleEndFrame = 8'000;
        value.exportSettings = project.exportSettings;
        value.editorSettings = project.editorSettings;
        return value;
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(!fixturePath().isEmpty(), "decoder fixture is required");
        QVERIFY2(QFileInfo::exists(fixturePath()), "decoder fixture is missing");
        QVERIFY2(!mismatchFixturePath().isEmpty(),
                 "mismatch decoder fixture is required");
        QVERIFY2(QFileInfo::exists(mismatchFixturePath()),
                 "mismatch decoder fixture is missing");
    }

    void roundTripsUnicodeRelativePathsSharedSourcesAndAllEditorState()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(!project.projectPath.isEmpty());

        const ProjectSaveResult saved = ProjectDocument::save(
            project.projectPath, request(project));
        QVERIFY2(saved.ok(), qPrintable(saved.message));
        const QByteArray json = readBytes(project.projectPath);
        QVERIFY(!json.isEmpty());
        QVERIFY(json.contains(QStringLiteral("媒体/共享源/鼓点 音频.wav").toUtf8()));
        QVERIFY(!json.contains('\\'));
        const QByteArray lower = json.toLower();
        for (const QByteArray& forbidden : {
                 QByteArray("pcm"), QByteArray("peak"), QByteArray("decoded"),
                 QByteArray("render"), QByteArray("preview"), QByteArray("cache"),
                 QByteArray("handoff"), QByteArray("clipboard")}) {
            QVERIFY2(!lower.contains(forbidden), forbidden.constData());
        }

        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 2);
        const QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QCOMPARE(events[0].toObject().value(QStringLiteral("fadeInCurve")).toString(),
                 QStringLiteral("exponential"));
        QCOMPARE(events[0].toObject().value(QStringLiteral("fadeOutCurve")).toString(),
                 QStringLiteral("smooth"));
        QCOMPARE(root.value(QStringLiteral("exportSettings")).toObject()
                     .value(QStringLiteral("bitDepth")).toInt(), 24);
        const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources[0].toObject().value(QStringLiteral("pathKind")).toString(),
                 QStringLiteral("relative"));

        ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QVERIFY(loaded.document != nullptr);
        QVERIFY(loaded.issues.empty());
        QCOMPARE(loaded.sources.size(), std::size_t{1});
        QCOMPARE(loaded.playheadFrame, SampleFrame{1'234});
        QCOMPARE(loaded.visibleStartFrame, SampleFrame{100});
        QCOMPARE(loaded.visibleEndFrame, SampleFrame{8'000});
        QCOMPARE(loaded.exportSettings.codecName, project.exportSettings.codecName);
        QCOMPARE(loaded.exportSettings.sampleRate, project.exportSettings.sampleRate);
        QCOMPARE(loaded.exportSettings.bitDepth, project.exportSettings.bitDepth);
        QCOMPARE(loaded.exportSettings.channels, project.exportSettings.channels);
        QCOMPARE(loaded.exportSettings.bitRate, project.exportSettings.bitRate);
        QCOMPARE(loaded.exportSettings.keepMetadata,
                 project.exportSettings.keepMetadata);
        QCOMPARE(loaded.exportSettings.variableBitRate,
                 project.exportSettings.variableBitRate);
        QCOMPARE(loaded.exportSettings.quality, project.exportSettings.quality);
        QCOMPARE(loaded.exportSettings.outputDirectory,
                 project.exportSettings.outputDirectory);
        QCOMPARE(loaded.editorSettings.originalBpm,
                 project.editorSettings.originalBpm);
        QCOMPARE(loaded.editorSettings.targetBpm,
                 project.editorSettings.targetBpm);
        QCOMPARE(loaded.editorSettings.speedPercent,
                 project.editorSettings.speedPercent);
        QCOMPARE(loaded.editorSettings.keepPitch,
                 project.editorSettings.keepPitch);
        QCOMPARE(loaded.editorSettings.formantPreservation,
                 project.editorSettings.formantPreservation);
        QCOMPARE(loaded.editorSettings.pitchCents,
                 project.editorSettings.pitchCents);
        QCOMPARE(loaded.editorSettings.trackMuted,
                 project.editorSettings.trackMuted);
        QCOMPARE(loaded.editorSettings.trackSolo,
                 project.editorSettings.trackSolo);
        QCOMPARE(loaded.editorSettings.trackGainDb,
                 project.editorSettings.trackGainDb);

        const TimelineSnapshot expected = project.document.timelineSnapshot();
        const TimelineSnapshot actual = loaded.document->timelineSnapshot();
        QCOMPARE(actual.events.size(), expected.events.size());
        for (std::size_t index = 0; index < expected.events.size(); ++index) {
            compareEvent(actual.events[index], expected.events[index]);
        }
        QCOMPARE(actual.events[0].source.get(), actual.events[1].source.get());
        QCOMPARE(QString::fromStdWString(actual.events[0].source->path.wstring()),
                 QFileInfo(project.sourcePath).absoluteFilePath());
        QCOMPARE(loaded.document->markers(), project.document.markers());
        QCOMPARE(loaded.document->selection(), project.document.selection());
    }

    void olderSchemaOneProjectWithoutBitDepthUsesCompatibleDefault()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        QJsonObject root = readObject(project.projectPath);
        root.insert(QStringLiteral("schemaVersion"), 1);
        QJsonObject settings = root.value(QStringLiteral("exportSettings")).toObject();
        settings.remove(QStringLiteral("bitDepth"));
        root.insert(QStringLiteral("exportSettings"), settings);
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        for (qsizetype index = 0; index < events.size(); ++index) {
            QJsonObject event = events[index].toObject();
            event.remove(QStringLiteral("fadeInCurve"));
            event.remove(QStringLiteral("fadeOutCurve"));
            events[index] = event;
        }
        root.insert(QStringLiteral("events"), events);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QCOMPARE(loaded.exportSettings.bitDepth, 24);
        for (const AudioEvent& event : loaded.document->timelineSnapshot().events) {
            QCOMPARE(event.fadeInCurve, FadeCurve::Linear);
            QCOMPARE(event.fadeOutCurve, FadeCurve::Linear);
        }
    }

    void freshExportSettingsPersistDefault320Kbps()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);

        ProjectExportSettings freshSettings;
        QCOMPARE(freshSettings.bitRate, qint64{320'000});
        project.exportSettings = freshSettings;

        const ProjectSaveResult saved = ProjectDocument::save(
            project.projectPath, request(project));
        QVERIFY2(saved.ok(), qPrintable(saved.message));
        QCOMPARE(readObject(project.projectPath)
                     .value(QStringLiteral("exportSettings")).toObject()
                     .value(QStringLiteral("bitRate")).toString(),
                 QStringLiteral("320000"));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QCOMPARE(loaded.exportSettings.bitRate, qint64{320'000});
    }

    void rejectsUnknownSchemaTwoFadeCurve()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        QJsonObject root = readObject(project.projectPath);
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QJsonObject event = events[0].toObject();
        event.insert(QStringLiteral("fadeInCurve"), QStringLiteral("bezier"));
        events[0] = event;
        root.insert(QStringLiteral("events"), events);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);

        QVERIFY(!loaded.ok());
        QCOMPARE(loaded.message, QStringLiteral("invalid event metadata"));
    }

    void rejectsUnsupportedPerEventTimePitchBeforePersistingOrLoading()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(!project.projectPath.isEmpty());

        const AudioEvent original = project.document.timelineSnapshot().events[0];
        AudioEvent unsupported = original;
        unsupported.speedRatio = 1.25;
        AudioDocument unsupportedDocument = AudioDocument::fromEvents(
            {unsupported});
        ProjectSaveRequest unsupportedRequest = request(project);
        unsupportedRequest.document = &unsupportedDocument;
        const ProjectSaveResult rejectedSave = ProjectDocument::save(
            project.projectPath, unsupportedRequest);
        QVERIFY(!rejectedSave.ok());
        QVERIFY(rejectedSave.message.contains(QStringLiteral("not supported")));

        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        QJsonObject root = readObject(project.projectPath);
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QJsonObject event = events[0].toObject();
        event.insert(QStringLiteral("pitchSemitone"), 3);
        events[0] = event;
        root.insert(QStringLiteral("events"), events);
        QVERIFY(writeObject(project.projectPath, root));
        const ProjectLoadResult rejectedLoad = ProjectDocument::load(
            project.projectPath);
        QVERIFY(!rejectedLoad.ok());
        QVERIFY(rejectedLoad.message.contains(QStringLiteral("not supported")));
    }

    void rejectsSchemaMalformedDuplicateInvalidOverlapAndTraversal()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        const QByteArray validBytes = readBytes(project.projectPath);
        const QJsonObject valid = QJsonDocument::fromJson(validBytes).object();

        auto rejects = [&project](const QJsonObject& object) {
            QVERIFY(writeObject(project.projectPath, object));
            ProjectLoadResult result = ProjectDocument::load(project.projectPath);
            QVERIFY(!result.ok());
            QVERIFY(result.document == nullptr);
        };

        QJsonObject newer = valid;
        newer.insert(QStringLiteral("schemaVersion"), 3);
        rejects(newer);

        QVERIFY(writeBytes(project.projectPath, QByteArrayLiteral("{broken")));
        ProjectLoadResult malformed = ProjectDocument::load(project.projectPath);
        QVERIFY(!malformed.ok());
        QVERIFY(malformed.document == nullptr);

        QJsonObject duplicateSource = valid;
        QJsonArray duplicateSources = duplicateSource.value(
            QStringLiteral("sources")).toArray();
        duplicateSources.append(duplicateSources[0]);
        duplicateSource.insert(QStringLiteral("sources"), duplicateSources);
        rejects(duplicateSource);

        QJsonObject duplicateEvent = valid;
        QJsonArray duplicateEvents = duplicateEvent.value(
            QStringLiteral("events")).toArray();
        QJsonObject duplicate = duplicateEvents[0].toObject();
        duplicate.insert(QStringLiteral("timelineStart"), 50'000);
        duplicateEvents.append(duplicate);
        duplicateEvent.insert(QStringLiteral("events"), duplicateEvents);
        rejects(duplicateEvent);

        QJsonObject invalidRange = valid;
        QJsonArray invalidEvents = invalidRange.value(
            QStringLiteral("events")).toArray();
        QJsonObject invalid = invalidEvents[0].toObject();
        invalid.insert(QStringLiteral("sourceEnd"),
                       invalid.value(QStringLiteral("sourceStart")));
        invalidEvents[0] = invalid;
        invalidRange.insert(QStringLiteral("events"), invalidEvents);
        rejects(invalidRange);

        QJsonObject overlap = valid;
        QJsonArray overlapEvents = overlap.value(QStringLiteral("events")).toArray();
        QJsonObject overlapping = overlapEvents[1].toObject();
        overlapping.insert(QStringLiteral("timelineStart"), 10'000);
        overlapEvents[1] = overlapping;
        overlap.insert(QStringLiteral("events"), overlapEvents);
        rejects(overlap);

        QJsonObject traversal = valid;
        QJsonArray traversalSources = traversal.value(
            QStringLiteral("sources")).toArray();
        QJsonObject escaped = traversalSources[0].toObject();
        escaped.insert(QStringLiteral("path"), QStringLiteral("../outside.wav"));
        traversalSources[0] = escaped;
        traversal.insert(QStringLiteral("sources"), traversalSources);
        rejects(traversal);

        QVERIFY(writeBytes(project.projectPath, validBytes));
        QVERIFY(ProjectDocument::load(project.projectPath).ok());
    }

    void rejectsRelativeSourceResolvedThroughExternalDirectoryLink()
    {
        QTemporaryDir projectDirectory;
        QTemporaryDir externalDirectory;
        QVERIFY(projectDirectory.isValid());
        QVERIFY(externalDirectory.isValid());
        auto project = makeProject(projectDirectory);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());

        const QString externalSource = externalDirectory.filePath(
            QStringLiteral("outside.wav"));
        QVERIFY(QFile::copy(fixturePath(), externalSource));
        const QString linkPath = projectDirectory.filePath(QStringLiteral("linked"));
        std::error_code linkError;
        std::filesystem::create_directory_symlink(
            nativePath(externalDirectory.path()), nativePath(linkPath), linkError);
#ifdef Q_OS_WIN
        if (linkError) {
            constexpr DWORD allowUnprivilegedCreate = 0x2;
            const std::wstring link = linkPath.toStdWString();
            const std::wstring target = externalDirectory.path().toStdWString();
            if (CreateSymbolicLinkW(link.c_str(), target.c_str(),
                                    SYMBOLIC_LINK_FLAG_DIRECTORY
                                        | allowUnprivilegedCreate)) {
                linkError.clear();
            }
        }
#endif
        if (linkError) QSKIP("directory symlink creation is unavailable", "");

        QJsonObject root = readObject(project.projectPath);
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QJsonObject source = sources[0].toObject();
        source.insert(QStringLiteral("pathKind"), QStringLiteral("relative"));
        source.insert(QStringLiteral("path"), QStringLiteral("linked/outside.wav"));
        sources[0] = source;
        root.insert(QStringLiteral("sources"), sources);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY(!loaded.ok());
        QVERIFY(loaded.message.contains(QStringLiteral("escapes")));
    }

    void reportsMissingAndIdentityMismatchedSourcesWithoutRejectingProject()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());

        QVERIFY(QFile::remove(project.sourcePath));
        ProjectLoadResult missing = ProjectDocument::load(project.projectPath);
        QVERIFY(missing.ok());
        QCOMPARE(missing.issues.size(), std::size_t{1});
        QCOMPARE(missing.issues[0].kind, ProjectSourceIssueKind::Missing);
        QCOMPARE(missing.issues[0].sourceId, quint64{1});
        QVERIFY(missing.document != nullptr);

        QVERIFY(QFile::copy(fixturePath(), project.sourcePath));
        QFile changed(project.sourcePath);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write("x", 1), qint64{1});
        changed.close();
        ProjectLoadResult mismatch = ProjectDocument::load(project.projectPath);
        QVERIFY(mismatch.ok());
        QCOMPARE(mismatch.issues.size(), std::size_t{1});
        QCOMPARE(mismatch.issues[0].kind,
                 ProjectSourceIssueKind::IdentityMismatch);
        QVERIFY(mismatch.document != nullptr);
    }

    void relinkValidatesFormatIdentityAndReplacesAllSharedReferences()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY(loaded.ok());
        const auto before = loaded.document->timelineSnapshot();
        const auto beforeSource = before.events[0].source;

        const ProjectRelinkResult rejected = ProjectDocument::relink(
            *loaded.document, loaded.sources, 1, mismatchFixturePath());
        QVERIFY(!rejected.ok());
        const auto afterRejected = loaded.document->timelineSnapshot();
        QCOMPARE(afterRejected.events[0].source.get(), beforeSource.get());
        QCOMPARE(afterRejected.events[1].source.get(), beforeSource.get());

        const QString replacement = temporary.filePath(
            QStringLiteral("重新定位/替代 音频.wav"));
        QDir().mkpath(QFileInfo(replacement).dir().absolutePath());
        QVERIFY(QFile::copy(fixturePath(), replacement));
        const ProjectRelinkResult relinked = ProjectDocument::relink(
            *loaded.document, loaded.sources, 1, replacement);
        QVERIFY2(relinked.ok(), qPrintable(relinked.message));
        const auto after = loaded.document->timelineSnapshot();
        QCOMPARE(after.events[0].source.get(), after.events[1].source.get());
        QVERIFY(after.events[0].source.get() != beforeSource.get());
        QCOMPARE(QString::fromStdWString(after.events[0].source->path.wstring()),
                 QFileInfo(replacement).absoluteFilePath());
    }

    void qSaveFileReplacementAndFailedSaveLeaveACompletePreviousDocument()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        ProjectSaveRequest first = request(project);
        QVERIFY(ProjectDocument::save(project.projectPath, first).ok());
        const QByteArray original = readBytes(project.projectPath);
        QVERIFY(!original.isEmpty());

        ProjectSaveRequest replacement = first;
        replacement.playheadFrame = 2'468;
        QVERIFY(ProjectDocument::save(project.projectPath, replacement).ok());
        const QByteArray replaced = readBytes(project.projectPath);
        QVERIFY(replaced != original);
        QCOMPARE(ProjectDocument::load(project.projectPath).playheadFrame,
                 SampleFrame{2'468});

        ProjectSaveRequest invalid = replacement;
        invalid.visibleEndFrame = project.document.totalFrames() + 1;
        const ProjectSaveResult failed = ProjectDocument::save(
            project.projectPath, invalid);
        QVERIFY(!failed.ok());
        QCOMPARE(readBytes(project.projectPath), replaced);
        QVERIFY(ProjectDocument::load(project.projectPath).ok());
    }

    void rejectsUntitledSourcesAndRoundTripsExact64BitPersistenceValues()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());

        auto untitledSource = std::make_shared<const AudioSource>(AudioSource{
            {}, 44'100, 2, 100});
        AudioDocument untitled = AudioDocument::fromEvents(
            {{1, untitledSource, 0, 100, 0}});
        ProjectSaveRequest untitledRequest;
        untitledRequest.document = &untitled;
        untitledRequest.visibleEndFrame = 100;
        const QString untitledPath = temporary.filePath(QStringLiteral("untitled.agproj"));
        QVERIFY(!ProjectDocument::save(untitledPath, untitledRequest).ok());
        QVERIFY(!QFileInfo::exists(untitledPath));

        constexpr SampleFrame exact = 9'007'199'254'740'993LL;
        const QString sourcePath = temporary.filePath(QStringLiteral("missing.wav"));
        auto source = std::make_shared<const AudioSource>(AudioSource{
            nativePath(sourcePath), 44'100, 2, exact});
        AudioEvent event{static_cast<EventId>(exact), source, exact - 100,
                         exact, exact - 100};
        AudioDocument document = AudioDocument::fromEvents({event});
        ProjectSaveRequest request;
        request.document = &document;
        request.playheadFrame = exact;
        request.visibleStartFrame = exact - 100;
        request.visibleEndFrame = exact;
        request.exportSettings.bitRate = 128'000;
        const std::vector<ProjectSourceRecord> records{{1, source, exact, exact}};
        request.sourceRecords = &records;
        const QString projectPath = temporary.filePath(QStringLiteral("exact.agproj"));
        const ProjectSaveResult saved = ProjectDocument::save(projectPath, request);
        QVERIFY2(saved.ok(), qPrintable(saved.message));
        const QByteArray bytes = readBytes(projectPath);
        QVERIFY(bytes.contains("9007199254740993"));

        const ProjectLoadResult loaded = ProjectDocument::load(projectPath);
        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QCOMPARE(loaded.document->timelineSnapshot().events[0].id,
                 static_cast<EventId>(exact));
        QCOMPARE(loaded.document->timelineSnapshot().events[0].timelineStart,
                 exact - 100);
        QCOMPARE(loaded.document->timelineSnapshot().events[0].source->total_frames,
                 exact);
        QCOMPARE(loaded.playheadFrame, exact);
        QCOMPARE(loaded.visibleStartFrame, exact - 100);
        QCOMPARE(loaded.visibleEndFrame, exact);
        QCOMPARE(loaded.exportSettings.bitRate, qint64{128'000});
        QCOMPARE(loaded.sources[0].fileSize, exact);
        QCOMPARE(loaded.sources[0].lastModifiedUtcMs, exact);
    }

    void mismatchIdentitySurvivesSaveAndReloadUntilExplicitRelink()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());

        QFile changed(project.sourcePath);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write("x", 1), qint64{1});
        changed.close();
        ProjectLoadResult mismatch = ProjectDocument::load(project.projectPath);
        QVERIFY(mismatch.ok());
        QCOMPARE(mismatch.issues.size(), std::size_t{1});
        QCOMPARE(mismatch.issues[0].kind,
                 ProjectSourceIssueKind::IdentityMismatch);

        ProjectSaveRequest saveAgain;
        saveAgain.document = mismatch.document.get();
        saveAgain.playheadFrame = mismatch.playheadFrame;
        saveAgain.visibleStartFrame = mismatch.visibleStartFrame;
        saveAgain.visibleEndFrame = mismatch.visibleEndFrame;
        saveAgain.exportSettings = mismatch.exportSettings;
        saveAgain.sourceRecords = &mismatch.sources;
        const ProjectSaveResult firstResult = ProjectDocument::save(
            project.projectPath, saveAgain);
        QVERIFY(firstResult.ok());

        const ProjectLoadResult reloaded = ProjectDocument::load(project.projectPath);
        QVERIFY(reloaded.ok());
        QCOMPARE(reloaded.issues.size(), std::size_t{1});
        QCOMPARE(reloaded.issues[0].kind,
                 ProjectSourceIssueKind::IdentityMismatch);
    }

    void sameStatButUndecodableSourceIsStillAnIdentityMismatch()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());

        const qint64 sourceSize = QFileInfo(project.sourcePath).size();
        QVERIFY(sourceSize > 0);
        QFile corrupt(project.sourcePath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(corrupt.write(QByteArray(sourceSize, '\0')), sourceSize);
        corrupt.close();

        QJsonObject root = readObject(project.projectPath);
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QJsonObject source = sources[0].toObject();
        const QFileInfo actual(project.sourcePath);
        source.insert(QStringLiteral("fileSize"),
                      QString::number(actual.size()));
        source.insert(QStringLiteral("lastModifiedUtcMs"),
                      QString::number(actual.lastModified().toUTC().toMSecsSinceEpoch()));
        sources[0] = source;
        root.insert(QStringLiteral("sources"), sources);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.issues.size(), std::size_t{1});
        QCOMPARE(loaded.issues[0].kind,
                 ProjectSourceIssueKind::IdentityMismatch);
    }

    void saveRejectsExportSettingsOutsideControllerContract()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);

        const auto rejects = [&project](const ProjectExportSettings& settings) {
            ProjectSaveRequest invalid = request(project);
            invalid.exportSettings = settings;
            QVERIFY(!ProjectDocument::save(project.projectPath, invalid).ok());
        };

        ProjectExportSettings invalid = project.exportSettings;
        invalid.sampleRate = 7'999;
        rejects(invalid);
        invalid.sampleRate = 384'001;
        rejects(invalid);
        invalid = project.exportSettings;
        invalid.bitDepth = 7;
        rejects(invalid);
        invalid.bitDepth = 33;
        rejects(invalid);
        invalid = project.exportSettings;
        invalid.channels = -1;
        rejects(invalid);
        invalid.channels = 9;
        rejects(invalid);
        invalid = project.exportSettings;
        invalid.bitRate = -1;
        rejects(invalid);
        invalid.bitRate = 1'536'001;
        rejects(invalid);
        invalid = project.exportSettings;
        invalid.quality = -1;
        rejects(invalid);
        invalid.quality = 101;
        rejects(invalid);
    }

    void loadRejectsExportSettingsOutsideControllerContract()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        const QJsonObject valid = readObject(project.projectPath);

        const auto rejects = [&project, &valid](const char* field,
                                                 const QJsonValue& value) {
            QJsonObject root = valid;
            QJsonObject settings = root.value(QStringLiteral("exportSettings")).toObject();
            settings.insert(QLatin1String(field), value);
            root.insert(QStringLiteral("exportSettings"), settings);
            QVERIFY(writeObject(project.projectPath, root));
            QVERIFY(!ProjectDocument::load(project.projectPath).ok());
        };

        rejects("sampleRate", 7'999);
        rejects("sampleRate", 384'001);
        rejects("bitDepth", 7);
        rejects("bitDepth", 33);
        rejects("channels", -1);
        rejects("channels", 9);
        rejects("bitRate", QStringLiteral("-1"));
        rejects("bitRate", QStringLiteral("1536001"));
        rejects("quality", -1);
        rejects("quality", 101);
    }

    void exportSettingsPreserveSupportedMultichannelRoundTrip()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        project.exportSettings.channels = 8;

        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);

        QVERIFY(loaded.ok());
        QCOMPARE(loaded.exportSettings.channels, 8);
    }

    void sourceIdsRemainStableWhenNewSourcesAreInsertedBetweenSaves()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY(loaded.ok());

        const QString sourceB = temporary.filePath(QStringLiteral("new-b.wav"));
        const QString sourceC = temporary.filePath(QStringLiteral("new-c.wav"));
        QVERIFY(QFile::copy(fixturePath(), sourceB));
        QVERIFY(QFile::copy(fixturePath(), sourceC));
        QVERIFY(loaded.document->insertSource(
            AudioSource{nativePath(sourceB), 44'100, 2, 100}, 50'000));

        ProjectSaveRequest saveAgain;
        saveAgain.document = loaded.document.get();
        saveAgain.playheadFrame = 1'234;
        saveAgain.visibleStartFrame = 100;
        saveAgain.visibleEndFrame = 8'000;
        saveAgain.exportSettings = loaded.exportSettings;
        saveAgain.sourceRecords = &loaded.sources;
        const ProjectSaveResult firstResult = ProjectDocument::save(
            project.projectPath, saveAgain);
        QVERIFY(firstResult.ok());
        loaded.sources = firstResult.sources;
        const QJsonObject firstSave = readObject(project.projectPath);
        QHash<QString, QString> firstIds;
        for (const QJsonValue& value : firstSave.value(QStringLiteral("sources")).toArray()) {
            const QJsonObject source = value.toObject();
            firstIds.insert(source.value(QStringLiteral("path")).toString(),
                            source.value(QStringLiteral("sourceId")).toVariant().toString());
        }
        const QString bPath = QStringLiteral("new-b.wav");
        QVERIFY(firstIds.contains(bPath));

        QVERIFY(loaded.document->insertSource(
            AudioSource{nativePath(sourceC), 44'100, 2, 100}, 45'000));
        const ProjectSaveResult secondResult = ProjectDocument::save(
            project.projectPath, saveAgain);
        QVERIFY(secondResult.ok());
        const QJsonObject secondSave = readObject(project.projectPath);
        QHash<QString, QString> secondIds;
        for (const QJsonValue& value : secondSave.value(QStringLiteral("sources")).toArray()) {
            const QJsonObject source = value.toObject();
            secondIds.insert(source.value(QStringLiteral("path")).toString(),
                             source.value(QStringLiteral("sourceId")).toVariant().toString());
        }
        QCOMPARE(secondIds.value(bPath), firstIds.value(bPath));
    }

    void newSourceSkipsReservedMaximumAfterValidMaximumMinusOne()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        const auto existing = project.document.timelineSnapshot().events.front().source;
        const QString newPath = temporary.filePath(QStringLiteral("new.wav"));
        const AudioSource added{nativePath(newPath), 44'100, 2, 100};
        QVERIFY(project.document.insertSource(added, 20'000));

        ProjectSourceRecord record;
        record.sourceId = std::numeric_limits<quint64>::max() - 1;
        record.source = existing;
        ProjectSaveRequest save = request(project);
        const std::vector<ProjectSourceRecord> records{record};
        save.sourceRecords = &records;
        const ProjectSaveResult result = ProjectDocument::save(project.projectPath, save);

        QVERIFY2(result.ok(), qPrintable(result.message));
        QCOMPARE(result.sources.size(), std::size_t{2});
        const auto addedRecord = std::find_if(result.sources.begin(), result.sources.end(),
            [&newPath](const ProjectSourceRecord& value) {
                return value.source
                    && QString::fromStdWString(value.source->path.wstring()) == newPath;
            });
        QVERIFY(addedRecord != result.sources.end());
        QCOMPARE(addedRecord->sourceId, quint64{1});
    }

#ifdef Q_OS_WIN
    void windowsCaseVariantInsideProjectStillUsesRelativePath()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString sourcePath = temporary.filePath(QStringLiteral("Media/source.wav"));
        QVERIFY(QDir{}.mkpath(QFileInfo(sourcePath).absolutePath()));
        QVERIFY(QFile::copy(fixturePath(), sourcePath));
        QString caseVariant = sourcePath;
        caseVariant[0] = caseVariant[0].isUpper()
            ? caseVariant[0].toLower() : caseVariant[0].toUpper();
        auto source = std::make_shared<const AudioSource>(AudioSource{
            nativePath(caseVariant), 44'100, 2, 100});
        AudioDocument document = AudioDocument::fromEvents(
            {{1, source, 0, 100, 0}});
        ProjectSaveRequest save;
        save.document = &document;
        save.visibleEndFrame = 100;
        const QString projectPath = temporary.filePath(QStringLiteral("case.agproj"));
        QVERIFY(ProjectDocument::save(projectPath, save).ok());
        const QJsonArray sources = readObject(projectPath)
            .value(QStringLiteral("sources")).toArray();
        QCOMPARE(sources[0].toObject().value(QStringLiteral("pathKind")).toString(),
                 QStringLiteral("relative"));
    }
#endif

    void timelineShrinkMarkersRoundTrip()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(project.document.setSelection({30'000, 40'000}));
        QVERIFY(project.document.deleteSelection());
        QCOMPARE(project.document.totalFrames(), SampleFrame{20'000});
        QCOMPARE(project.document.markers().back().frame, SampleFrame{20'000});
        ProjectSaveRequest shrunk = request(project);
        shrunk.visibleEndFrame = 20'000;
        QVERIFY(ProjectDocument::save(project.projectPath, shrunk).ok());
        ProjectLoadResult reloaded = ProjectDocument::load(project.projectPath);
        QVERIFY2(reloaded.ok(), qPrintable(reloaded.message));
        QCOMPARE(reloaded.document->markers(), project.document.markers());
    }

    void emptyTimelineMarkersRoundTrip()
    {
        QTemporaryDir emptyTemporary;
        QVERIFY(emptyTemporary.isValid());
        auto empty = makeProject(emptyTemporary);
        QVERIFY(empty.document.setSelection({0, 40'000}));
        QVERIFY(empty.document.deleteSelection());
        QCOMPARE(empty.document.totalFrames(), SampleFrame{0});
        QCOMPARE(empty.document.markers().front().frame, SampleFrame{0});
        QCOMPARE(empty.document.markers().back().frame, SampleFrame{0});
        ProjectSaveRequest emptyRequest;
        emptyRequest.document = &empty.document;
        emptyRequest.visibleStartFrame = 0;
        emptyRequest.visibleEndFrame = 0;
        emptyRequest.exportSettings = empty.exportSettings;
        QVERIFY(ProjectDocument::save(empty.projectPath, emptyRequest).ok());
        ProjectLoadResult emptyReloaded = ProjectDocument::load(empty.projectPath);
        QVERIFY2(emptyReloaded.ok(), qPrintable(emptyReloaded.message));
        QCOMPARE(emptyReloaded.document->totalFrames(), SampleFrame{0});
        QCOMPARE(emptyReloaded.document->markers(), empty.document.markers());
    }

    void rejectsOversizedProjectBeforeJsonParsing()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("oversized.agproj"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(16LL * 1024LL * 1024LL + 1LL));
        file.close();

        const ProjectLoadResult loaded = ProjectDocument::load(path);

        QVERIFY(!loaded.ok());
        QCOMPARE(loaded.message, QStringLiteral("project resource limit exceeded"));
    }

    void rejectsOversizedProjectCollectionsBeforeDomainAllocation_data()
    {
        QTest::addColumn<QString>("collection");
        QTest::newRow("sources") << QStringLiteral("sources");
        QTest::newRow("events") << QStringLiteral("events");
        QTest::newRow("markers") << QStringLiteral("markers");
    }

    void rejectsOversizedProjectCollectionsBeforeDomainAllocation()
    {
        QFETCH(QString, collection);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        QJsonObject root = readObject(project.projectPath);
        QJsonArray oversized;
        for (int index = 0; index < 4'097; ++index) {
            oversized.append(QJsonObject{});
        }
        root.insert(collection, oversized);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);

        QVERIFY(!loaded.ok());
        QCOMPARE(loaded.message, QStringLiteral("project resource limit exceeded"));
    }

    void rejectsExcessiveEnvelopePointsBeforeEventAllocation()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());
        QJsonObject root = readObject(project.projectPath);
        QJsonArray envelope;
        for (int index = 0; index < 64; ++index) envelope.append(0);
        QJsonArray events;
        for (int index = 0; index < 1'025; ++index) {
            events.append(QJsonObject{{QStringLiteral("envelope"), envelope}});
        }
        root.insert(QStringLiteral("events"), events);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);

        QVERIFY(!loaded.ok());
        QCOMPARE(loaded.message, QStringLiteral("project resource limit exceeded"));
    }

    void saveRejectsResourceLimitsWithoutReplacingThePreviousProject()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        const ProjectSaveRequest baseline = request(project);
        QVERIFY(ProjectDocument::save(project.projectPath, baseline).ok());
        const QByteArray previous = readBytes(project.projectPath);

        const auto source = project.document.timelineSnapshot().events.front().source;
        const auto saveRejected = [&](std::vector<AudioEvent> events,
                                      std::vector<Marker> markers,
                                      const QString& row) {
            AudioDocument document = AudioDocument::fromEvents(std::move(events));
            for (const Marker& marker : markers) QVERIFY(document.addMarker(marker));
            ProjectSaveRequest oversized = baseline;
            oversized.document = &document;
            oversized.playheadFrame = 0;
            oversized.visibleStartFrame = 0;
            oversized.visibleEndFrame = document.totalFrames();
            const ProjectSaveResult saved = ProjectDocument::save(project.projectPath, oversized);
            QVERIFY2(!saved.ok(), qPrintable(row + QStringLiteral(": ") + saved.message));
            QCOMPARE(saved.message, QStringLiteral("project resource limit exceeded"));
            QCOMPARE(readBytes(project.projectPath), previous);
        };

        std::vector<AudioEvent> tooManySources;
        tooManySources.reserve(4'097);
        for (quint64 index = 0; index < 4'097; ++index) {
            auto distinctSource = std::make_shared<const AudioSource>(*source);
            tooManySources.push_back({index + 1, std::move(distinctSource), 0, 1,
                                      static_cast<SampleFrame>(index)});
        }
        saveRejected(std::move(tooManySources), {}, QStringLiteral("sources"));

        std::vector<AudioEvent> tooManyEvents;
        tooManyEvents.reserve(4'097);
        for (quint64 index = 0; index < 4'097; ++index) {
            tooManyEvents.push_back({index + 1, source, 0, 1,
                                     static_cast<SampleFrame>(index)});
        }
        saveRejected(std::move(tooManyEvents), {}, QStringLiteral("events"));

        std::vector<Marker> tooManyMarkers;
        tooManyMarkers.reserve(4'097);
        for (int index = 0; index < 4'097; ++index) {
            tooManyMarkers.push_back({"marker", 0});
        }
        saveRejected({AudioEvent{1, source, 0, 1, 0}}, std::move(tooManyMarkers),
                     QStringLiteral("markers"));

        std::vector<AudioEvent> tooManyEnvelopePoints;
        tooManyEnvelopePoints.reserve(1'025);
        for (quint64 index = 0; index < 1'025; ++index) {
            AudioEvent event{index + 1, source, 0, 64,
                             static_cast<SampleFrame>(index * 64)};
            for (SampleFrame offset = 0; offset < 64; ++offset) {
                event.envelope.push_back({offset, 1.0F});
            }
            tooManyEnvelopePoints.push_back(std::move(event));
        }
        saveRejected(std::move(tooManyEnvelopePoints), {}, QStringLiteral("envelope"));

        std::vector<Marker> oversizedJson;
        oversizedJson.push_back({std::string(16 * 1024 * 1024, 'x'), 0});
        saveRejected({AudioEvent{1, source, 0, 1, 0}}, std::move(oversizedJson),
                     QStringLiteral("json"));
    }

    void loadSaveAndRelinkRejectTheReservedMaximumSourceId()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        const ProjectSaveRequest baseline = request(project);
        QVERIFY(ProjectDocument::save(project.projectPath, baseline).ok());

        QJsonObject root = readObject(project.projectPath);
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QJsonArray events = root.value(QStringLiteral("events")).toArray();
        QCOMPARE(sources.size(), 1);
        QJsonObject source = sources.first().toObject();
        source.insert(QStringLiteral("sourceId"),
                      QString::number(std::numeric_limits<quint64>::max()));
        sources.replace(0, source);
        for (qsizetype index = 0; index < events.size(); ++index) {
            QJsonObject event = events.at(index).toObject();
            event.insert(QStringLiteral("sourceId"),
                         QString::number(std::numeric_limits<quint64>::max()));
            events.replace(index, event);
        }
        root.insert(QStringLiteral("sources"), sources);
        root.insert(QStringLiteral("events"), events);
        QVERIFY(writeObject(project.projectPath, root));

        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);
        QVERIFY(!loaded.ok());

        ProjectSourceRecord reserved;
        reserved.sourceId = std::numeric_limits<quint64>::max();
        reserved.source = project.document.timelineSnapshot().events.front().source;
        const std::vector<ProjectSourceRecord> reservedRecords{reserved};
        ProjectSaveRequest reservedSave = baseline;
        reservedSave.sourceRecords = &reservedRecords;
        const ProjectSaveResult saved = ProjectDocument::save(
            temporary.filePath(QStringLiteral("reserved-save.agproj")), reservedSave);
        QVERIFY(!saved.ok());
        QCOMPARE(saved.message, QStringLiteral("invalid source id"));

        std::vector<ProjectSourceRecord> reservedSources{reserved};
        const ProjectRelinkResult relinked = ProjectDocument::relink(
            project.document, reservedSources,
            std::numeric_limits<quint64>::max(), project.sourcePath);
        QVERIFY(!relinked.ok());
        QCOMPARE(relinked.message, QStringLiteral("invalid source id"));
    }

    void duplicateProjectSourcesReuseOneBoundedProbe()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());

        QJsonObject root = readObject(project.projectPath);
        const QJsonObject templateSource = root.value(QStringLiteral("sources"))
            .toArray().first().toObject();
        QJsonObject referencedEvent = root.value(QStringLiteral("events"))
            .toArray().first().toObject();
        QJsonArray sources;
        for (int index = 0; index < 4'096; ++index) {
            QJsonObject source = templateSource;
            source.insert(QStringLiteral("sourceId"), QString::number(index + 1));
            sources.append(source);
        }
        referencedEvent.insert(QStringLiteral("sourceId"), QStringLiteral("4096"));
        root.insert(QStringLiteral("sources"), sources);
        root.insert(QStringLiteral("events"), QJsonArray{referencedEvent});
        root.insert(QStringLiteral("markers"), QJsonArray{});
        QVERIFY(writeObject(project.projectPath, root));

        QElapsedTimer elapsed;
        elapsed.start();
        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);

        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QVERIFY2(elapsed.elapsed() < 3'000,
                 qPrintable(QStringLiteral("duplicate project source load took %1 ms")
                                .arg(elapsed.elapsed())));
        QVERIFY(loaded.issues.empty());
        QVERIFY(loaded.document != nullptr);
    }

    void uniqueProjectSourceProbeLimitOpensRemainderOfflineForRelink()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto project = makeProject(temporary);
        QVERIFY(ProjectDocument::save(project.projectPath, request(project)).ok());

        QJsonObject root = readObject(project.projectPath);
        const QJsonObject templateSource = root.value(QStringLiteral("sources"))
            .toArray().first().toObject();
        QJsonObject referencedEvent = root.value(QStringLiteral("events"))
            .toArray().first().toObject();
        const QDir projectDirectory = QFileInfo(project.projectPath).dir();
        QJsonArray sources;
        for (int index = 0; index < 129; ++index) {
            const QString linkPath = temporary.filePath(
                QStringLiteral("source-link-%1.wav").arg(index + 1));
            std::error_code linkError;
            std::filesystem::create_hard_link(
                nativePath(project.sourcePath), nativePath(linkPath), linkError);
            QVERIFY2(!linkError, linkError.message().c_str());

            QJsonObject source = templateSource;
            source.insert(QStringLiteral("sourceId"), QString::number(index + 1));
            source.insert(QStringLiteral("pathKind"), QStringLiteral("relative"));
            source.insert(QStringLiteral("path"), QDir::fromNativeSeparators(
                projectDirectory.relativeFilePath(linkPath)));
            sources.append(source);
        }
        referencedEvent.insert(QStringLiteral("sourceId"), QStringLiteral("129"));
        root.insert(QStringLiteral("sources"), sources);
        root.insert(QStringLiteral("events"), QJsonArray{referencedEvent});
        root.insert(QStringLiteral("markers"), QJsonArray{});
        QVERIFY(writeObject(project.projectPath, root));

        QElapsedTimer elapsed;
        elapsed.start();
        const ProjectLoadResult loaded = ProjectDocument::load(project.projectPath);

        QVERIFY2(loaded.ok(), qPrintable(loaded.message));
        QVERIFY2(elapsed.elapsed() < 7'000,
                 qPrintable(QStringLiteral("unique project source load took %1 ms")
                                .arg(elapsed.elapsed())));
        QVERIFY(!loaded.issues.empty());
        QCOMPARE(loaded.issues.back().kind, ProjectSourceIssueKind::Unavailable);
        QCOMPARE(loaded.issues.back().sourceId, quint64{129});
        QCOMPARE(loaded.issues.back().message,
                 QStringLiteral("project source probe budget exceeded"));
        QVERIFY(loaded.document != nullptr);
    }

};

// QTEST_APPLESS_MAIN in Qt 6.7/6.8 expands an omitted variadic argument,
// rejected by recent Clang in strict C++17. Keep the same app-less test runner.
int main(int argc, char* argv[])
{
    ProjectDocumentTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "project_document_test.moc"
