#include "audio_preview_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "vocal_separation_controller.hpp"
#include "waveform_provider.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMetaEnum>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifndef AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH
#error AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH must name the test worker
#endif

class VocalSeparationControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void doesNotLaunchWorkerDuringConstruction();
    void exposesTypedCatalogAndStemAvailabilityFromTheInstalledCatalog();
    void successfulWorkerResultPublishesExistingOutputsWaveformsAndFallbackReason();
    void missingWorkerOutputFailsWithoutHistory();
    void partialWorkerOutputIsNeverPublished();
    void workerOutputOutsideTheSelectedDirectoryIsRejected();
    void providerProbeRetryRemainsInTheProbingState();
    void failedDownloadCanBeDeletedAndReset();
    void crashCanRetryTheSameRequest();
    void exportNeverOverwritesAndPlaylistUsesTheRealImportPath();
};

namespace {

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QList<VocalModelCard> testCatalog(const QByteArray& modelBytes)
{
    const VocalDownloadFile file{QStringLiteral("test.onnx"), {},
                                 modelBytes.size(), sha256(modelBytes)};
    return {{QStringLiteral("two-stem"), VocalModelFamily::Mdx, {file},
             {QStringLiteral("vocals"), QStringLiteral("instrumental")},
             QStringLiteral("test"), QStringLiteral("test")},
            {QStringLiteral("five-stem"), VocalModelFamily::Demucs, {file},
             {QStringLiteral("vocals"), QStringLiteral("instrumental"),
              QStringLiteral("drums"), QStringLiteral("bass"),
              QStringLiteral("other")},
             QStringLiteral("test"), QStringLiteral("test")}};
}

VocalSeparationControllerOptions optionsFor(
    const QTemporaryDir& temporary, const QString& scenario,
    const QByteArray& modelBytes, const QString& markerPath = {})
{
    VocalSeparationControllerOptions options;
    options.workerProgram = QString::fromUtf8(
        AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH);
    options.workerArguments = markerPath.isEmpty()
        ? QStringList{scenario} : QStringList{scenario, markerPath};
    options.dataRoot = QDir(temporary.path()).filePath(QStringLiteral("应用数据"));
    options.outputDirectory = QDir(temporary.path()).filePath(QStringLiteral("输出结果"));
    options.runtimeLibraryPath = temporary.filePath(QStringLiteral("onnxruntime.dll"));
    options.catalog = testCatalog(modelBytes);
    options.deadlines = {500, 2000, 250};
    options.verifyRuntimeIntegrity = false;
    return options;
}

void installTestModel(const VocalSeparationControllerOptions& options,
                      const QString& modelId, const QByteArray& modelBytes)
{
    const QString path = QDir(options.dataRoot).filePath(
        QStringLiteral("models/%1/test.onnx").arg(modelId));
    QVERIFY(writeBytes(path, modelBytes));
}

QString audioFixture()
{
    return qEnvironmentVariable("AGPLAYER_TEST_AUDIO");
}

QVariantMap stemFor(const QVariantList& stems,
                    VocalSeparationController::StemKind kind)
{
    for (const QVariant& value : stems) {
        const QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() == int(kind)) return stem;
    }
    return {};
}

int modelStateFor(const QVariantList& models, const QString& modelId)
{
    for (const QVariant& value : models) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("id")).toString() == modelId)
            return model.value(QStringLiteral("state")).toInt();
    }
    return -1;
}

} // namespace

void VocalSeparationControllerTest::doesNotLaunchWorkerDuringConstruction()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString marker = temporary.filePath(QStringLiteral("worker-started.marker"));
    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(
        temporary, QStringLiteral("retry"), modelBytes, marker);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QTest::qWait(200);
    QVERIFY(!QFileInfo::exists(marker));
}

void VocalSeparationControllerTest::
exposesTypedCatalogAndStemAvailabilityFromTheInstalledCatalog()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::ModelState>().keyCount() > 0,
             true);
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::JobState>().keyCount() > 0,
             true);
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::StemKind>().keyCount() > 0,
             true);
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::DeviceMode>().keyCount() > 0,
             true);
    QCOMPARE(controller.models().size(), 2);
    QCOMPARE(controller.models().first().toMap().value(QStringLiteral("state")).toInt(),
             int(VocalSeparationController::ModelState::Installed));
    QCOMPARE(controller.stems().size(), 2);

    QVERIFY(controller.selectModel(QStringLiteral("five-stem")));
    QCOMPARE(controller.stems().size(), 5);
    QVERIFY(stemFor(controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                .value(QStringLiteral("derived")).toBool());
}

void VocalSeparationControllerTest::
successfulWorkerResultPublishesExistingOutputsWaveformsAndFallbackReason()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    QVERIFY(QFileInfo::exists(audioFixture()));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);
    QCOMPARE(controller.history().size(), 1);
    const QVariantMap vocals = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals);
    QVERIFY(vocals.value(QStringLiteral("available")).toBool());
    QVERIFY(QFileInfo::exists(vocals.value(QStringLiteral("path")).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(
        !stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
             .value(QStringLiteral("waveform")).toList().isEmpty(),
        5000);
    const QVariantList devices = controller.availableDevices();
    QCOMPARE(devices.at(1).toMap().value(QStringLiteral("reason")).toString(),
             QStringLiteral("No tested GPU"));
}

void VocalSeparationControllerTest::missingWorkerOutputFailsWithoutHistory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("missing-result"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Failed, 5000);
    QVERIFY(!controller.error().isEmpty());
    QCOMPARE(controller.history().size(), 0);
}

void VocalSeparationControllerTest::partialWorkerOutputIsNeverPublished()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("partial-result"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Failed, 5000);
    for (const QVariant& value : controller.stems()) {
        QVERIFY(!value.toMap().value(QStringLiteral("available")).toBool());
        QVERIFY(value.toMap().value(QStringLiteral("path")).toString().isEmpty());
    }
    QCOMPARE(controller.history().size(), 0);
}

void VocalSeparationControllerTest::
workerOutputOutsideTheSelectedDirectoryIsRejected()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("escaped-result"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Failed, 5000);
    QVERIFY(!controller.error().isEmpty());
    QCOMPARE(controller.history().size(), 0);
}

void VocalSeparationControllerTest::providerProbeRetryRemainsInTheProbingState()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const QString marker = temporary.filePath(QStringLiteral("probe-attempt.marker"));
    auto options = optionsFor(temporary, QStringLiteral("retry"), modelBytes, marker);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.probeDevices());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Failed, 5000);
    QVERIFY(controller.retry());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Probing);
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Idle, 5000);
}

void VocalSeparationControllerTest::failedDownloadCanBeDeletedAndReset()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.downloadModel(QStringLiteral("two-stem")));
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Failed), 5000);
    QVERIFY(controller.deleteModel(QStringLiteral("two-stem")));
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::NotInstalled));
}

void VocalSeparationControllerTest::crashCanRetryTheSameRequest()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const QString marker = temporary.filePath(QStringLiteral("attempt.marker"));
    auto options = optionsFor(temporary, QStringLiteral("retry"), modelBytes, marker);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Failed, 5000);
    QVERIFY(controller.retry());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);
}

void VocalSeparationControllerTest::
exportNeverOverwritesAndPlaylistUsesTheRealImportPath()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    LibraryModel library;
    ImportController importer(&library);
    PlaylistModel playlists(temporary.filePath(QStringLiteral("playlists.json")));
    VocalSeparationController controller(
        &preview, &waveforms, &library, &importer, &playlists, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);

    const QString exportRoot = temporary.filePath(QStringLiteral("导出"));
    QVERIFY(QDir().mkpath(exportRoot));
    QVERIFY(controller.exportStem(VocalSeparationController::StemKind::Vocals,
                                  QUrl::fromLocalFile(exportRoot)));
    QVERIFY(!controller.exportStem(VocalSeparationController::StemKind::Vocals,
                                   QUrl::fromLocalFile(exportRoot)));

    const QString playlistId = playlists.createPlaylist(QStringLiteral("分离结果"));
    QVERIFY(!playlistId.isEmpty());
    QVERIFY(controller.addStemToPlaylist(VocalSeparationController::StemKind::Vocals,
                                         playlistId));
    QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 5000);
    const QString path = stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                             .value(QStringLiteral("path")).toString();
    const int row = library.indexForLocalFile(path);
    QVERIFY(row >= 0);
    const QString trackId = library.data(library.index(row),
                                         LibraryModel::TrackIdRole).toString();
    QVERIFY(playlists.containsTrack(playlistId, trackId));
}

QTEST_MAIN(VocalSeparationControllerTest)
#include "vocal_separation_controller_test.moc"
