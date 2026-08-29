#include "audio_preview_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "vocal_separation_controller.hpp"
#include "waveform_provider.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaEnum>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH
#error AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH must name the test worker
#endif

class VocalSeparationControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void doesNotLaunchWorkerDuringConstruction();
    void downloadsMultipleArtifactsSequentiallyThroughTheController();
    void installedMappingUsesCheapDiscoveryThenExplicitAsyncHashing();
    void cancellingVerificationImmediatelyRestoresCheapModelStates();
    void deletingDuringRefreshVerificationCannotResurrectTheModel();
    void destructionWaitsForOwnedVerificationWork();
    void exposesTypedCatalogAndStemAvailabilityFromTheInstalledCatalog();
    void successfulWorkerResultPublishesExistingOutputsWaveformsAndFallbackReason();
    void startingANewResultGenerationClearsPreviouslyPublishedStems();
    void waveformFailuresAdvanceAcrossEveryResultStem();
    void missingWorkerOutputFailsWithoutHistory();
    void partialWorkerOutputIsNeverPublished();
    void historySaveFailureDoesNotPublishAResult();
    void workerOutputOutsideTheSelectedDirectoryIsRejected();
    void providerProbeRetryRemainsInTheProbingState();
    void immediateCancellationBeforeHelloNeverCompletesTheJob();
    void runningRequestRejectsMutationsThatWouldChangeItsMeaning();
    void publishedStemReplacementWithJunctionIsRejectedByEveryAction();
    void modelDeletionRefusesAReparseDirectory();
    void failedDownloadCanBeDeletedAndReset();
    void crashCanRetryTheSameRequest();
    void localFailureForNewRequestCannotRetryThePreviousWorkerRequest();
    void exportNeverOverwritesAndPlaylistUsesTheRealImportPath();
    void batchExportPublishesOneCompleteDirectoryOrNothing();
    void selectedPlaylistActionRejectsAnUnsafeSubset();
    void unresolvedImportFailureEmitsCompletionAndRollsBackThisOperation();
    void unicodeLongPathsWorkThroughHistoryAndExport();
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

QList<VocalModelCard> multiArtifactCatalog(
    const QString& firstSource, const QByteArray& firstBytes,
    const QString& secondSource, const QByteArray& secondBytes)
{
    return {{QStringLiteral("multi-artifact"), VocalModelFamily::Demucs,
             {{QStringLiteral("first.onnx"), QUrl::fromLocalFile(firstSource),
               firstBytes.size(), sha256(firstBytes)},
              {QStringLiteral("second.onnx"), QUrl::fromLocalFile(secondSource),
               secondBytes.size(), sha256(secondBytes)}},
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

#ifdef Q_OS_WIN
bool createJunction(const QString& junctionPath, const QString& targetPath)
{
    QProcess process;
    process.start(QStringLiteral("cmd.exe"),
                  {QStringLiteral("/d"), QStringLiteral("/c"),
                   QStringLiteral("mklink"), QStringLiteral("/J"),
                   QDir::toNativeSeparators(junctionPath),
                   QDir::toNativeSeparators(targetPath)});
    return process.waitForFinished(5000) && process.exitCode() == 0
        && QFileInfo(junctionPath).isDir();
}

class JunctionGuard final {
public:
    explicit JunctionGuard(QString path) : path_(std::move(path)) {}
    ~JunctionGuard()
    {
        const QString native = QDir::toNativeSeparators(path_);
        RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()));
    }
private:
    QString path_;
};
#endif

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
cancellingVerificationImmediatelyRestoresCheapModelStates()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(16 * 1024 * 1024, 'm');
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::Verifying));
    controller.cancel();
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Cancelled);
    QVERIFY(modelStateFor(controller.models(), QStringLiteral("two-stem"))
            != int(VocalSeparationController::ModelState::Verifying));
}

void VocalSeparationControllerTest::
deletingDuringRefreshVerificationCannotResurrectTheModel()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(16 * 1024 * 1024, 'm');
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.verifyInstalledModels());
    QVERIFY(!controller.deleteModel(QStringLiteral("two-stem")));
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    QVERIFY(controller.deleteModel(QStringLiteral("two-stem")));
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::NotInstalled));
}

void VocalSeparationControllerTest::destructionWaitsForOwnedVerificationWork()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(128 * 1024 * 1024, 'v');
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    auto controller = std::make_unique<VocalSeparationController>(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller->verifyInstalledModels());
    QTest::qWait(10);

    QElapsedTimer elapsed;
    elapsed.start();
    controller.reset();
    QVERIFY2(elapsed.elapsed() < 5'000,
             "Controller destruction did not finish owned verification promptly");
    const QString modelPath = QDir(options.dataRoot).filePath(
        QStringLiteral("models/two-stem/test.onnx"));
    QVERIFY2(QFile::remove(modelPath),
             "Owned verification continued using the model after destruction");
}

void VocalSeparationControllerTest::
downloadsMultipleArtifactsSequentiallyThroughTheController()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray firstBytes("first-real-local-artifact");
    const QByteArray secondBytes("second-real-local-artifact");
    const QString firstSource = temporary.filePath(QStringLiteral("源/first.onnx"));
    const QString secondSource = temporary.filePath(QStringLiteral("源/second.onnx"));
    QVERIFY(QDir().mkpath(QFileInfo(firstSource).absolutePath()));
    QVERIFY(writeBytes(firstSource, firstBytes));
    QVERIFY(writeBytes(secondSource, secondBytes));
    auto options = optionsFor(temporary, QStringLiteral("stale"), firstBytes);
    options.catalog = multiArtifactCatalog(
        firstSource, firstBytes, secondSource, secondBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QSignalSpy modelsChanged(&controller,
                             &VocalSeparationController::modelsChanged);
    QVector<double> visibleProgress;
    connect(&controller, &VocalSeparationController::progressChanged,
            this, [&] { visibleProgress.push_back(controller.progress()); });
    QVERIFY(controller.downloadModel(QStringLiteral("multi-artifact")));
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("multi-artifact")),
        int(VocalSeparationController::ModelState::Installed), 5000);
    const QString installedRoot = QDir(options.dataRoot).filePath(
        QStringLiteral("models/multi-artifact"));
    QCOMPARE(QFileInfo(QDir(installedRoot).filePath(QStringLiteral("first.onnx"))).size(),
             qint64(firstBytes.size()));
    QCOMPARE(QFileInfo(QDir(installedRoot).filePath(QStringLiteral("second.onnx"))).size(),
             qint64(secondBytes.size()));
    QVERIFY(modelsChanged.count() >= 4);
    bool resetBetweenArtifacts = false;
    for (qsizetype index = 1; index < visibleProgress.size(); ++index) {
        if (visibleProgress.at(index - 1) > 0.99
            && visibleProgress.at(index) == 0.0) {
            resetBetweenArtifacts = true;
            break;
        }
    }
    QVERIFY(resetBetweenArtifacts);
}

void VocalSeparationControllerTest::
installedMappingUsesCheapDiscoveryThenExplicitAsyncHashing()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray expected("trusted-test-model");
    const QByteArray sameSizeWrong("untrusted-model---");
    QCOMPARE(sameSizeWrong.size(), expected.size());
    auto options = optionsFor(temporary, QStringLiteral("stale"), expected);
    installTestModel(options, QStringLiteral("two-stem"), sameSizeWrong);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::Verifying));
    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::NotInstalled), 5000);
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
             int(VocalSeparationController::ModelState::Verifying));
    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.models().first().toMap().value(QStringLiteral("state")).toInt(),
        int(VocalSeparationController::ModelState::Installed), 5000);
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

void VocalSeparationControllerTest::
startingANewResultGenerationClearsPreviouslyPublishedStems()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(
        temporary, QStringLiteral("delayed-result"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    QVERIFY(stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                .value(QStringLiteral("available")).toBool());

    const QString invalidNextInput = temporary.filePath(
        QStringLiteral("下一次输入.wav"));
    QVERIFY(QFile::copy(audioFixture(), invalidNextInput));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(invalidNextInput)));
    QVERIFY(QFile::remove(invalidNextInput));
    QVERIFY(!controller.start());
    QVERIFY(!stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                 .value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.previewStem(VocalSeparationController::StemKind::Vocals));

    QVERIFY(QFile::copy(audioFixture(), invalidNextInput));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(invalidNextInput)));
    QTest::qWait(300);
    QVERIFY(controller.start());
    QVERIFY(!stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                 .value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.previewStem(VocalSeparationController::StemKind::Vocals));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
}

void VocalSeparationControllerTest::waveformFailuresAdvanceAcrossEveryResultStem()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("invalid-audio"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    QSignalSpy waveformFailed(&waveforms, &WaveformProvider::waveformFailed);
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(waveformFailed.count(), 2, 5'000);
    for (const QVariant& value : controller.stems())
        QVERIFY(value.toMap().value(QStringLiteral("waveform")).toList().isEmpty());
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

void VocalSeparationControllerTest::historySaveFailureDoesNotPublishAResult()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    QVERIFY(QDir().mkpath(QDir(options.dataRoot).filePath(QStringLiteral("history.json"))));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Failed, 5000);
    QCOMPARE(controller.history().size(), 0);
    for (const QVariant& value : controller.stems()) {
        QVERIFY(!value.toMap().value(QStringLiteral("available")).toBool());
    }
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

void VocalSeparationControllerTest::
immediateCancellationBeforeHelloNeverCompletesTheJob()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const QString marker = temporary.filePath(QStringLiteral("start.marker"));
    auto options = optionsFor(
        temporary, QStringLiteral("delayed-hello"), modelBytes, marker);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    controller.cancel();
    controller.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Cancelled, 5000);
    QCOMPARE(controller.history().size(), 0);
    QVERIFY(!QFileInfo::exists(marker));
}

void VocalSeparationControllerTest::
runningRequestRejectsMutationsThatWouldChangeItsMeaning()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(
        temporary, QStringLiteral("delayed-result"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.stage(), QStringLiteral("inference"), 2000);
    QVERIFY(!controller.selectModel(QStringLiteral("five-stem")));
    QVERIFY(!controller.setStemSelected(
        VocalSeparationController::StemKind::Vocals, false));
    QVERIFY(!controller.selectOutputFormat(QStringLiteral("flac")));
    QVERIFY(!controller.selectOutputDirectory(QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("next-output")))));
    QVERIFY(!controller.selectDevice(
        VocalSeparationController::DeviceMode::CPU));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);
}

void VocalSeparationControllerTest::
publishedStemReplacementWithJunctionIsRejectedByEveryAction()
{
#ifndef Q_OS_WIN
    QSKIP("NTFS junction coverage is Windows-only");
#else
    QTemporaryDir temporary;
    QTemporaryDir external;
    QVERIFY(temporary.isValid());
    QVERIFY(external.isValid());
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
    QTRY_VERIFY_WITH_TIMEOUT(
        !stemFor(controller.stems(), VocalSeparationController::StemKind::Accompaniment)
             .value(QStringLiteral("waveform")).toList().isEmpty(), 5000);
    const QString vocalsPath = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals)
                                    .value(QStringLiteral("path")).toString();
    const QString jobDirectory = QFileInfo(vocalsPath).absolutePath();
    const QVariantList stems = controller.stems();
    QVERIFY(QDir(jobDirectory).removeRecursively());
    for (const QVariant& value : stems) {
        const QString name = QFileInfo(
            value.toMap().value(QStringLiteral("path")).toString()).fileName();
        QVERIFY(QFile::copy(audioFixture(), external.filePath(name)));
    }
    if (!createJunction(jobDirectory, external.path())) {
        QSKIP("This environment cannot create an NTFS directory junction");
    }
    JunctionGuard guard(jobDirectory);
    const QString playlistId = playlists.createPlaylist(QStringLiteral("安全检查"));
    const bool previewAccepted = controller.previewStem(
        VocalSeparationController::StemKind::Vocals);
    const bool exportAccepted = controller.exportStem(
        VocalSeparationController::StemKind::Vocals,
        QUrl::fromLocalFile(temporary.filePath(QStringLiteral("export.wav"))));
    const bool playlistAccepted = controller.addStemToPlaylist(
        VocalSeparationController::StemKind::Vocals, playlistId);
    if (importer.busy()) QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 5000);
    QVERIFY(!previewAccepted);
    QVERIFY(!exportAccepted);
    QVERIFY(!playlistAccepted);
    QVERIFY(QFileInfo::exists(external.filePath(QFileInfo(vocalsPath).fileName())));
#endif
}

void VocalSeparationControllerTest::modelDeletionRefusesAReparseDirectory()
{
#ifndef Q_OS_WIN
    QSKIP("NTFS junction coverage is Windows-only");
#else
    QTemporaryDir temporary;
    QTemporaryDir external;
    QVERIFY(temporary.isValid());
    QVERIFY(external.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    const QString externalModel = external.filePath(QStringLiteral("test.onnx"));
    const QString sentinel = external.filePath(QStringLiteral("sentinel.txt"));
    QVERIFY(writeBytes(externalModel, modelBytes));
    QVERIFY(writeBytes(sentinel, QByteArrayLiteral("keep")));
    const QString modelsRoot = QDir(options.dataRoot).filePath(QStringLiteral("models"));
    QVERIFY(QDir().mkpath(modelsRoot));
    const QString junction = QDir(modelsRoot).filePath(QStringLiteral("two-stem"));
    if (!createJunction(junction, external.path())) {
        QSKIP("This environment cannot create an NTFS directory junction");
    }
    JunctionGuard guard(junction);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(!controller.deleteModel(QStringLiteral("two-stem")));
    QVERIFY(QFileInfo::exists(externalModel));
    QVERIFY(QFileInfo::exists(sentinel));
#endif
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
    const QString originalInput = controller.inputInfo()
                                      .value(QStringLiteral("path")).toString();
    const QString originalOutput = options.outputDirectory;
    QVERIFY(controller.retry());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);
    const QVariantMap record = controller.history().last().toMap();
    QCOMPARE(record.value(QStringLiteral("inputPath")).toString(), originalInput);
    QCOMPARE(record.value(QStringLiteral("modelId")).toString(),
             QStringLiteral("two-stem"));
    for (const QVariant& value : record.value(QStringLiteral("stems")).toList()) {
        QVERIFY(QFileInfo(value.toMap().value(QStringLiteral("path")).toString())
                    .absoluteFilePath()
                    .startsWith(QFileInfo(originalOutput).absoluteFilePath()));
    }
}

void VocalSeparationControllerTest::
localFailureForNewRequestCannotRetryThePreviousWorkerRequest()
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
                              VocalSeparationController::JobState::Failed, 5'000);

    const QString nextInput = temporary.filePath(QStringLiteral("输入-B.wav"));
    QVERIFY(QFile::copy(audioFixture(), nextInput));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(nextInput)));
    QVERIFY(QFile::remove(nextInput));
    QVERIFY(!controller.start());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Failed);
    QVERIFY(!controller.retry());
    QTest::qWait(300);
    QCOMPARE(controller.history().size(), 0);
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
    QVERIFY(!controller.addStemToPlaylist(
        VocalSeparationController::StemKind::Vocals,
        QStringLiteral("missing-playlist")));
}

void VocalSeparationControllerTest::
batchExportPublishesOneCompleteDirectoryOrNothing()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);

    const QString completeRoot = temporary.filePath(QStringLiteral("完整导出"));
    QVERIFY(QDir().mkpath(completeRoot));
    QVERIFY(controller.exportSelected(QUrl::fromLocalFile(completeRoot)));
    const QFileInfoList published = QDir(completeRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(published.size(), 1);
    QCOMPARE(QDir(published.first().absoluteFilePath()).entryList(
                 QDir::Files | QDir::NoDotAndDotDot).size(), 2);
    QCOMPARE(QDir(completeRoot).entryList(QDir::Files | QDir::NoDotAndDotDot).size(),
             0);

    const QString accompaniment = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                                      .value(QStringLiteral("path")).toString();
    QVERIFY(QFile::remove(accompaniment));
    const QString failedRoot = temporary.filePath(QStringLiteral("失败导出"));
    QVERIFY(QDir().mkpath(failedRoot));
    QVERIFY(!controller.exportSelected(QUrl::fromLocalFile(failedRoot)));
    QCOMPARE(QDir(failedRoot).entryList(
                 QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
}

void VocalSeparationControllerTest::selectedPlaylistActionRejectsAnUnsafeSubset()
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
                              VocalSeparationController::JobState::Completed, 5'000);
    const QString accompaniment = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                                      .value(QStringLiteral("path")).toString();
    QVERIFY(QFile::remove(accompaniment));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("all or none"));
    QVERIFY(!controller.addSelectedToPlaylist(playlistId));
    QVERIFY(!importer.busy());
}

void VocalSeparationControllerTest::
unresolvedImportFailureEmitsCompletionAndRollsBackThisOperation()
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
    QSignalSpy operation(&controller,
                         &VocalSeparationController::playlistOperationFinished);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    const QString vocals = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals)
                                 .value(QStringLiteral("path")).toString();
    const QString accompaniment = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                                      .value(QStringLiteral("path")).toString();
    importer.importPaths({vocals});
    QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 5'000);
    const int vocalsRow = library.indexForLocalFile(vocals);
    QVERIFY(vocalsRow >= 0);
    const QString vocalsId = library.data(
        library.index(vocalsRow), LibraryModel::TrackIdRole).toString();
    QVERIFY(writeBytes(accompaniment, QByteArrayLiteral("not audio")));
    const QString playlistId = playlists.createPlaylist(QStringLiteral("rollback"));

    QVERIFY(controller.addSelectedToPlaylist(playlistId));
    QTRY_COMPARE_WITH_TIMEOUT(operation.count(), 1, 5'000);
    QVERIFY(!operation.first().at(0).toBool());
    QVERIFY(!playlists.containsTrack(playlistId, vocalsId));
}

void VocalSeparationControllerTest::unicodeLongPathsWorkThroughHistoryAndExport()
{
#ifndef Q_OS_WIN
    QSKIP("Windows long-path coverage");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString longPart;
    while (longPart.size() < 285)
        longPart += QStringLiteral("超长目录段0123456789/");
    const QString longRoot = QDir(temporary.path()).filePath(longPart);
    if (!QDir().mkpath(longRoot))
        QSKIP("The active Windows filesystem/runtime cannot create a >260 Unicode path");
    QVERIFY(QFileInfo(longRoot).absoluteFilePath().size() > 260);
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    options.dataRoot = QDir(longRoot).filePath(QStringLiteral("应用历史"));
    options.outputDirectory = QDir(longRoot).filePath(QStringLiteral("分离输出"));
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    QCOMPARE(controller.history().size(), 1);
    const QString exportRoot = QDir(longRoot).filePath(QStringLiteral("导出目标"));
    QVERIFY(QDir().mkpath(exportRoot));
    const QString target = QDir(exportRoot).filePath(QStringLiteral("最终人声.wav"));
    QVERIFY(controller.exportStem(VocalSeparationController::StemKind::Vocals,
                                  QUrl::fromLocalFile(target)));
    QVERIFY(QFileInfo(target).isFile());
#endif
}

QTEST_MAIN(VocalSeparationControllerTest)
#include "vocal_separation_controller_test.moc"
