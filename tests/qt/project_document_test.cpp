#include "audio_editor/project_document.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
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
        first.speedRatio = 1.25;
        first.pitchSemitone = -3;
        first.mute = true;
        first.envelope = {{100, 0.5F}, {10'000, 0.9F}};
        AudioEvent second{9, shared, 20'000, 30'000, 30'000};
        second.gain = 1.1F;
        second.fadeIn = 120;
        second.fadeOut = 240;
        second.speedRatio = 0.8;
        second.pitchSemitone = 4;
        second.envelope = {{500, 0.8F}};
        AudioDocument document = AudioDocument::fromEvents({first, second});
        document.addMarker({u8"前奏", 1'000});
        document.addMarker({u8"尾声", 39'000});
        document.setSelection({500, 4'500});

        ProjectExportSettings settings;
        settings.codecName = QStringLiteral("flac");
        settings.sampleRate = 96'000;
        settings.channels = 2;
        settings.bitRate = 512'000;
        settings.keepMetadata = false;
        settings.variableBitRate = true;
        settings.quality = 73;
        settings.outputDirectory = temporary.filePath(QStringLiteral("导出目录"));
        return {temporary.filePath(QStringLiteral("工程 测试.agproj")),
                sourcePath, std::move(document), settings};
    }

    static ProjectSaveRequest request(const FixtureProject& project)
    {
        ProjectSaveRequest value;
        value.document = &project.document;
        value.playheadFrame = 1'234;
        value.visibleStartFrame = 100;
        value.visibleEndFrame = 8'000;
        value.exportSettings = project.exportSettings;
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
        for (const QByteArray forbidden : {
                 QByteArray("pcm"), QByteArray("peak"), QByteArray("decoded"),
                 QByteArray("render"), QByteArray("preview"), QByteArray("cache"),
                 QByteArray("handoff"), QByteArray("clipboard")}) {
            QVERIFY2(!lower.contains(forbidden), forbidden.constData());
        }

        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);
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
        QCOMPARE(loaded.exportSettings.channels, project.exportSettings.channels);
        QCOMPARE(loaded.exportSettings.bitRate, project.exportSettings.bitRate);
        QCOMPARE(loaded.exportSettings.keepMetadata,
                 project.exportSettings.keepMetadata);
        QCOMPARE(loaded.exportSettings.variableBitRate,
                 project.exportSettings.variableBitRate);
        QCOMPARE(loaded.exportSettings.quality, project.exportSettings.quality);
        QCOMPARE(loaded.exportSettings.outputDirectory,
                 project.exportSettings.outputDirectory);

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
        newer.insert(QStringLiteral("schemaVersion"), 2);
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
        request.exportSettings.bitRate = exact;
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
        QCOMPARE(loaded.exportSettings.bitRate, exact);
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
};

QTEST_APPLESS_MAIN(ProjectDocumentTest)

#include "project_document_test.moc"
