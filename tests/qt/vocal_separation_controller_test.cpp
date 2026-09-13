#include "audio_preview_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "vocal_separation_controller.hpp"
#include "waveform_provider.hpp"
#include "waveform_provider_test_access.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QProcess>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTranslator>
#include <QTimer>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH
#error AG_SEPARATION_CONTROLLER_TEST_WORKER_PATH must name the test worker
#endif

namespace {
QString externalFixtureRoot(const QString& root)
{
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_ARM_64
    return QDir(root).filePath(QStringLiteral("macos-arm64"));
#else
    return QDir(root).filePath(QStringLiteral("macos-x86_64"));
#endif
#else
    return root;
#endif
}
QString externalFixturePython()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("env/bin/python");
#else
    return QStringLiteral("env/Scripts/python.exe");
#endif
}
QByteArray externalFixtureMarker()
{
#ifdef Q_OS_MACOS
#ifdef Q_PROCESSOR_ARM_64
    return QByteArrayLiteral("audio-separator=0.30.2\npython=3.10.18\ntorch=2.5.1\nplatform=macos-arm64\nverification=external-separation-worker-v2\n");
#else
    return QByteArrayLiteral("audio-separator=0.24.1\npython=3.10.18\ntorch=2.2.2\nplatform=macos-x86_64\nverification=external-separation-worker-v2\n");
#endif
#else
    return QByteArrayLiteral("audio-separator=0.30.2\nverification=external-separation-worker-v1\n");
#endif
}
QString externalConfiguratorSource()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("/usr/bin/false");
#else
    return qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/cmd.exe");
#endif
}
QString externalConfiguratorName()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("uv");
#else
    return QStringLiteral("uv.exe");
#endif
}
}

class ExternalSeparationRuntimeTestDriver {
public:
    static void launchStage(ExternalSeparationRuntime& runtime, int stage,
                            const QString& program, const QStringList& arguments)
    {
        runtime.busy_ = true;
        runtime.step_ = stage;
        runtime.launch(program, arguments);
    }
    static void preparePython(ExternalSeparationRuntime& runtime)
    {
#ifdef Q_OS_MACOS
        QFile::copy(externalConfiguratorSource(), QDir(runtime.root_).filePath(externalConfiguratorName()));
#endif
        runtime.busy_ = true;
        runtime.step_ = 2;
        runtime.advance();
    }
    static int stage(const ExternalSeparationRuntime& runtime)
    {
        return runtime.step_;
    }
    static bool failedExistingVerificationStartsTrustedRepair(ExternalSeparationRuntime& runtime)
    {
        runtime.busy_ = true;
        runtime.step_ = 4;
        runtime.verifyingExistingEnvironment_ = true;
        runtime.stageFailed(QStringLiteral("controlled import failure"));
        return runtime.cacheVerification_.isRunning();
    }
    static QStringList repairExistingEnvironmentArguments(ExternalSeparationRuntime& runtime)
    {
#ifdef Q_OS_MACOS
        QFile::copy(externalConfiguratorSource(), QDir(runtime.root_).filePath(externalConfiguratorName()));
#endif
        runtime.busy_ = true;
        runtime.step_ = 2;
        runtime.repairAttempted_ = true;
        runtime.advance();
        return runtime.process_.arguments();
    }
};

class VocalSeparationControllerTestDriver {
public:
    static QString workerProgram(const VocalSeparationController& controller) {
        return controller.options_.workerProgram;
    }
    static void publishPythonConfigurationProgress(VocalSeparationController& controller) {
        emit controller.externalRuntime_->progress(0.42, QStringLiteral("python-only-phase"));
        emit controller.cudaRuntime_->progress(0.81, QStringLiteral("cuda-only-phase"));
    }
    static bool lateRuntimeVerificationPreservesInstalledState(VocalSeparationController& controller, bool checked) {
        controller.runtimeVerified_ = true;
        controller.runtimeVerificationKnown_ = true;
        const QString fingerprint = controller.runtimeVerificationFingerprint_;
        VocalSeparationController::VerificationResult stale;
        stale.runtimeChecked = checked;
        stale.runtimeVerified = false;
        stale.runtimeFingerprint = QStringLiteral("old-runtime-before-install");
        stale.verifiedModels = controller.verifiedModelIds_;
        stale.checkedModels = controller.verifiedOrRejectedModelIds_;
        stale.modelFingerprints = controller.modelVerificationFingerprints_;
        controller.verificationPurpose_ = VocalSeparationController::VerificationPurpose::Refresh;
        controller.finishVerification(controller.verificationGeneration_, stale);
        return controller.runtimeVerified_ && controller.runtimeVerificationKnown_
            && controller.runtimeVerificationFingerprint_ == fingerprint;
    }
    static bool lateModelVerificationPreservesInstalledState(VocalSeparationController& controller) {
        const auto verified = controller.verifiedModelIds_;
        VocalSeparationController::VerificationResult stale;
        stale.runtimeChecked = true;
        stale.runtimeVerified = controller.runtimeVerified_;
        stale.runtimeFingerprint = controller.runtimeVerificationFingerprint_;
        controller.verificationPurpose_ = VocalSeparationController::VerificationPurpose::Refresh;
        controller.finishVerification(controller.verificationGeneration_, stale);
        return !verified.isEmpty() && controller.verifiedModelIds_ == verified;
    }
    static void useLocalProxy(VocalSeparationController& controller, quint16 port) {
        controller.network_.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", port));
    }
    static int sharedRuntimeDownloads(const VocalSeparationController& controller) {
        return int(std::count_if(controller.downloadQueue_.cbegin(), controller.downloadQueue_.cend(),
            [](const auto& item) { return item.runtimeArchive; }));
    }
    static void publishGpuCandidate(VocalSeparationController& controller)
    {
        controller.handleProbe({{"cpu", true}, {"gpu", true}});
    }
    static void prepareResultWaveform(VocalSeparationController& controller)
    {
        controller.stems_ = {QVariantMap{{"kind", int(VocalSeparationController::StemKind::Vocals)},
                                        {"path", "result.wav"}, {"waveform", QVariantList{}}}};
        controller.waveformQueue_ = {{"result.wav", "result-track",
            VocalSeparationController::StemKind::Vocals, controller.resultGeneration_}};
    }

    static void deliverResultWaveform(VocalSeparationController& controller,
                                      const QVariantList& peaks, bool complete)
    {
        controller.handleWaveform("result.wav", {{"_trackId", "result-track"},
                                                  {"mix", peaks}, {"_complete", complete}});
    }

    static QByteArray lifecycleDiagnostic(
        const VocalSeparationController& controller)
    {
        return QStringLiteral(
                   "job=%1 stage=%2 error=%3 process=%4 running=%5 "
                   "canAccept=%6 request=%7 verification=%8 purpose=%9")
            .arg(int(controller.jobState_))
            .arg(controller.stage_)
            .arg(controller.error_)
            .arg(int(controller.process_.state()))
            .arg(controller.process_.isProcessRunning())
            .arg(controller.process_.canAcceptRequest())
            .arg(controller.process_.activeRequestId())
            .arg(controller.verificationWatcher_ != nullptr)
            .arg(int(controller.verificationPurpose_))
            .toUtf8();
    }

    static void seedDownloadFailure(VocalSeparationController& controller,
                                    const bool mirrorAttempted)
    {
        VocalSeparationController::DownloadItem item;
        item.file = {QStringLiteral("test.onnx"),
                     QUrl(QStringLiteral("https://huggingface.co/test.onnx")),
                     16, QString(64, QLatin1Char('a'))};
        item.destination = QStringLiteral("unused.onnx");
        item.mirrorUrl = QUrl(QStringLiteral("https://hf-mirror.com/test.onnx"));
        item.mirrorAttempted = mirrorAttempted;
        controller.downloadQueue_ = {item};
        controller.downloadingModelId_ = QStringLiteral("two-stem");
        controller.downloadSource_ = mirrorAttempted
            ? QStringLiteral("国内镜像") : QStringLiteral("官方线路");
    }

    static void failDownload(VocalSeparationController& controller,
                             const QString& diagnostic)
    {
        controller.handleDownloadFailure({false, diagnostic}, false);
    }

    static void cancelDownloader(VocalSeparationController& controller)
    {
        controller.downloader_->cancel();
    }

    static void seedQueuedRoute(VocalSeparationController& controller,
                                const VocalDownloadFile& file,
                                const QUrl& mirror,
                                const bool mirrorAttempted)
    {
        VocalSeparationController::DownloadItem item;
        item.file = file;
        item.destination = QDir(controller.options_.dataRoot)
                               .filePath(QStringLiteral("queued.onnx"));
        item.mirrorUrl = mirror;
        item.mirrorAttempted = mirrorAttempted;
        controller.downloadQueue_ = {item};
        controller.downloadingModelId_ = QStringLiteral("two-stem");
    }

    static void startQueuedDownload(VocalSeparationController& controller)
    {
        controller.startNextDownload();
    }

    static bool downloadPipelineIdle(
        const VocalSeparationController& controller)
    {
        for (const auto& task : controller.modelConfigurations_)
            if (task->verification != nullptr) return false;
        return controller.verificationWatcher_ == nullptr
            && controller.runtimeInstallerWatcher_ == nullptr;
    }

    static bool runtimeOnlyDownloadActive(
        const VocalSeparationController& controller)
    {
        return controller.runtimeOnlyDownload_
            && controller.downloadingModelId_ == QStringLiteral("runtime")
            && !controller.downloadQueue_.isEmpty()
            && controller.downloadQueue_.constFirst().runtimeArchive;
    }

    static quint64 verificationGeneration(
        const VocalSeparationController& controller)
    {
        return controller.verificationGeneration_;
    }

    static bool verificationInFlight(
        const VocalSeparationController& controller)
    {
        return controller.verificationWatcher_ != nullptr;
    }
    static bool workerAvailable(const VocalSeparationController& controller)
    {
        return controller.process_.canAcceptRequest();
    }

    static bool hardwareDiscoveryInFlight(const VocalSeparationController& controller)
    {
        return controller.cudaRuntime_->checking();
    }

    static bool directoryAndVerificationIdle(
        const VocalSeparationController& controller)
    {
        return controller.modelDirectoryIndexWatcher_ == nullptr
            && controller.verificationWatcher_ == nullptr;
    }

    static bool beginRefreshVerification(
        VocalSeparationController& controller)
    {
        return controller.beginVerification(
            VocalSeparationController::VerificationPurpose::Refresh);
    }

    static bool startVerificationPending(const VocalSeparationController& controller)
    {
        return controller.pendingStartVerification_;
    }

    static QString activeSeparationModel(const VocalSeparationController& controller)
    {
        return controller.activeRequest_
                && controller.activeRequest_->kind == VocalSeparationController::RequestKind::Separation
            ? controller.activeRequest_->modelId : QString{};
    }
};

class VocalSeparationControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultWorkerProgramMatchesPlatform()
    {
        QTemporaryDir temporary;
        VocalSeparationControllerOptions options;
        options.dataRoot = temporary.path();
        AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
        WaveformProvider waveforms;
        VocalSeparationController controller(
            &preview, &waveforms, nullptr, nullptr, nullptr, options);
#ifdef Q_OS_WIN
        const QString name = QStringLiteral("AgSeparationWorker.exe");
#else
        const QString name = QStringLiteral("AgSeparationWorker");
#endif
        QCOMPARE(VocalSeparationControllerTestDriver::workerProgram(controller),
                 QDir(QCoreApplication::applicationDirPath()).filePath(name));
    }
    void macosPythonInterpreterLinksMustStayInThePrivateRuntime();
    void lateRuntimeVerificationCannotOverwriteInstalledState();
    void sharedRuntimeIsDeduplicatedAndIndependentOfModelCancellation();
    void configurationDownloadsRunIndependently();
    void switchingModelPreservesPublishedResult();
    void doesNotLaunchWorkerDuringConstruction();
    void exposesOutputChoicesAndPublishesTheSelectedInputWaveform();
    void clearsTheSelectedInputWithoutLeavingStaleWaveformData();
    void historyActionsPreserveReservedUnicodePaths();
    void downloadsMultipleArtifactsSequentiallyThroughTheController();
    void downloadProgressNeverMutatesAnActiveSeparationJob();
    void downloadingAnotherModelDoesNotBlockSeparation();
    void modelSelectionRespondsDuringDeviceProbe();
    void unchangedModelReusesDeviceProbe();
    void startWaitsForBackgroundVerificationWithoutFailing();
    void cancellingQueuedStartPreservesBackgroundDownloadVerification();
    void configurationDuringRefreshPreservesRunningSeparationContext();
    void downloadFailureRetriesMirrorBeforeReportingExhaustion();
    void downloadSafetyDiagnosticsAreLocalized();
    void queuedDownloadRouteAndCancellationUseProductionControllerState();
    void runtimeCanBeConfiguredWithoutCatalogModelLookup();
    void configuredRuntimeDoesNotPretendUnknownModelsAreConfigured();
    void installedMappingUsesCheapDiscoveryThenExplicitAsyncHashing();
    void unchangedVerifiedModelIsNotHashedAgainByDirectoryRefresh();
    void changedVerifiedModelInvalidatesTheCachedResult();
    void changedRuntimeInvalidatesTheCachedResultBeforeProbe();
    void unexpectedRuntimeEntryInvalidatesTheCachedResultBeforeProbe();
    void cancellingRevalidationDoesNotCacheARejectedModel();
    void configurationCanTakeOverBackgroundRefreshVerification();
    void customModelDirectoryPersistsAndRecognizesTrustedNestedFiles();
    void customModelDirectoryListsNestedRawModelsWithBackendDiagnostics();
    void knownLocalMdxProfileIsDiscoveredButStillRequiresItsFingerprint();
    void customSidecarManifestUsesTrustedFingerprintAndReportsRejection();
    void cancellingVerificationImmediatelyRestoresCheapModelStates();
    void deletingDuringRefreshVerificationCannotResurrectTheModel();
    void verificationHashHonorsCancellationBeforeReadingFile();
    void destructionWaitsForOwnedVerificationWork();
    void exposesTypedCatalogAndStemAvailabilityFromTheInstalledCatalog();
    void exposesStartEligibilityAndAnAlwaysSelectableAutoDevice();
    void workerPayloadUsesCurrentLanguageStemLabelsAndCatalogDisplayName();
    void successfulWorkerResultPublishesExistingOutputsWaveformsAndFallbackReason();
    void progressiveResultWaveformKeepsTheRequestUntilComplete();
    void stemPreviewVolumesRemainIndependentAndDriveTheSharedPreview();
    void mdxResultPreviewSwitchesBetweenMixAndSoloAtTheSharedPosition();
    void demucsResultMixExcludesTheDerivedAccompanimentWhenComponentsAreComplete();
    void demucsResultMixFallsBackToTheDerivedAccompanimentWhenComponentsAreIncomplete_data();
    void demucsResultMixFallsBackToTheDerivedAccompanimentWhenComponentsAreIncomplete();
    void startingANewResultGenerationClearsPreviouslyPublishedStems();
    void selectingANewInputImmediatelyClearsCompletedResultAndItsPreview();
    void waveformFailuresAdvanceAcrossEveryResultStem();
    void missingWorkerOutputFailsWithoutHistory();
    void partialWorkerOutputIsNeverPublished();
    void historySaveFailureDoesNotPublishAResult();
    void workerOutputOutsideTheSelectedDirectoryIsRejected();
    void providerProbeRetryRemainsInTheProbingState();
    void pageProbeWaitsForVerificationAndSelectsGpu();
    void automaticDemucsShowsCpuCompatibilityAndPreservesGpuForOtherModels();
    void knownVrModelOffersExternalConfigurationWithPause();
    void externalRuntimeRealInstallAndCachedRepair();
    void cudaRuntimeRejectsUnverifiedComponents();
    void cudaRuntimeRealCachedInstall();
    void externalRuntimeRetriesThePinnedArchiveThroughTheBackupRoute();
    void externalRuntimePersistsStageFailure();
    void externalRuntimeStallSwitchesSourceAndStopsAfterBackup();
    void externalRuntimeQuarantinesIncompletePythonWithoutDeletingIt();
    void externalRuntimePauseThenImmediateResumeIsNonBlocking();
    void externalRuntimeAlreadyVerifiedDoesNotInvalidateOrReinstall();
    void externalRuntimeRejectsLegacyReadyMarker();
    void externalRuntimeReadyStartQueuesAsynchronousVerification();
    void externalRuntimeFailedExistingVerificationKeepsEnvironmentAndUsesTrustedRepair();
    void externalRuntimeRepairReinstallsDependencies();
    void gpuCardDistinguishesDetectedDriverFromMissingCuda();
    void explicitDemucsGpuRequiresCudaBeforeStartingWorker();
    void existingPythonEnvironmentUpgradesTheBundledWorkerWithoutDownloading();
    void pythonWorkerUpgradeRejectsARedirectedRuntimeRoot();
    void immediateCancellationBeforeHelloNeverCompletesTheJob();
    void runningRequestRejectsMutationsThatWouldChangeItsMeaning();
    void publishedStemReplacementWithJunctionIsRejectedByEveryAction();
    void modelDeletionRefusesAReparseDirectory();
    void failedDownloadCanBeDeletedAndReset();
    void crashCanRetryTheSameRequest();
    void localFailureForNewRequestCannotRetryThePreviousWorkerRequest();
    void exportNeverOverwritesAndPlaylistUsesTheRealImportPath();
    void stemDownloadPublishesDirectlyToTheConfiguredOutputDirectory();
    void batchExportPublishesOneCompleteDirectoryOrNothing();
    void exportAllPublishesEveryAvailableStemRegardlessOfSelection();
    void selectedPlaylistActionRejectsAnUnsafeSubset();
    void unresolvedImportFailureEmitsCompletionAndRollsBackThisOperation();
    void destructionBeforeImportCompletionLeavesPlaylistUnchanged();
    void unicodeLongPathsWorkThroughHistoryAndExport();
};

namespace {

class UnavailableDownloadReply final : public QNetworkReply {
public:
    UnavailableDownloadReply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setError(QNetworkReply::TimeoutError, "route timed out");
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {}
    qint64 readData(char*, qint64) override { return -1; }
};

class UnavailableDownloadNetwork final : public QNetworkAccessManager {
public:
    QStringList hosts;
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        hosts.append(request.url().host());
        return new UnavailableDownloadReply(request, this);
    }
};

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
             QStringLiteral("test"), QStringLiteral("test"),
             QStringLiteral("Two stem test"), QStringLiteral("test two stems")},
            {QStringLiteral("five-stem"), VocalModelFamily::Demucs, {file},
             {QStringLiteral("vocals"), QStringLiteral("instrumental"),
              QStringLiteral("drums"), QStringLiteral("bass"),
              QStringLiteral("other")},
             QStringLiteral("test"), QStringLiteral("test"),
             QStringLiteral("Five stem test"), QStringLiteral("test five stems")}};
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
    options.deadlines = {5000, 5000, 1000};
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

class ChineseStemLabelTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* sourceText,
                      const char*, int) const override
    {
        if (qstrcmp(context, "VocalSeparationController") != 0) return {};
        if (qstrcmp(sourceText, "Vocals") == 0) return QStringLiteral("人声");
        if (qstrcmp(sourceText, "Instrumental") == 0) return QStringLiteral("伴奏");
        if (qstrcmp(sourceText, "Drums") == 0) return QStringLiteral("鼓组");
        if (qstrcmp(sourceText, "Bass") == 0) return QStringLiteral("贝斯");
        if (qstrcmp(sourceText, "Other") == 0) return QStringLiteral("其他");
        return {};
    }
};

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

void VocalSeparationControllerTest::
exposesOutputChoicesAndPublishesTheSelectedInputWaveform()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QCOMPARE(controller.outputDirectory(), options.outputDirectory);
    QCOMPARE(controller.outputFormat(), QStringLiteral("wav"));
    QSignalSpy outputDirectoryChanged(&controller,
                                      &VocalSeparationController::outputDirectoryChanged);
    QSignalSpy outputFormatChanged(&controller,
                                   &VocalSeparationController::outputFormatChanged);
    QVERIFY(controller.selectOutputDirectory(QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("export")))));
    QVERIFY(controller.selectOutputFormat(QStringLiteral("flac")));
    QVERIFY(controller.selectOutputFormat(QStringLiteral("mp3")));
    QCOMPARE(controller.outputFormat(), QStringLiteral("mp3"));
    QCOMPARE(outputDirectoryChanged.count(), 1);
    QCOMPARE(outputFormatChanged.count(), 2);

    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QCOMPARE(controller.inputInfo().value(QStringLiteral("coverUrl")).toUrl(),
             QUrl{});
    QCOMPARE(controller.inputInfo()
                 .value(QStringLiteral("coverFallbackUrl")).toUrl(),
             QUrl(QStringLiteral(
                 "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png")));
    QTRY_VERIFY_WITH_TIMEOUT(
        !controller.inputInfo().value(QStringLiteral("waveform")).toList().isEmpty(),
        5'000);
    QVERIFY(controller.inputInfo().value(QStringLiteral("durationMs")).toLongLong() > 0);
}

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
clearsTheSelectedInputWithoutLeavingStaleWaveformData()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.clearInput());
    QVERIFY(controller.inputInfo().isEmpty());
}

void VocalSeparationControllerTest::historyActionsPreserveReservedUnicodePaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString specialInput = temporary.filePath(
        QStringLiteral("历史输入 #100% 中文.wav"));
    QVERIFY(QFile::copy(audioFixture(), specialInput));

    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(
        temporary, QStringLiteral("success"), modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.selectHistoryInput(specialInput));
    QCOMPARE(controller.inputInfo().value(QStringLiteral("path")).toString(),
             QFileInfo(specialInput).absoluteFilePath());
    QVERIFY(!controller.openHistoryOutputDirectory(
        temporary.filePath(QStringLiteral("missing-output"))));
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
verificationHashHonorsCancellationBeforeReadingFile()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(8 * 1024 * 1024, 'c');
    const QString modelPath = temporary.filePath(QStringLiteral("test.onnx"));
    QVERIFY(writeBytes(modelPath, modelBytes));
    const VocalDownloadFile file{QStringLiteral("test.onnx"), {},
                                 modelBytes.size(), sha256(modelBytes)};
    const auto cancellation = std::make_shared<std::atomic_bool>(true);

    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!VocalSeparationInstaller::isVerifiedFile(
        file, modelPath, cancellation));
    QVERIFY2(elapsed.elapsed() < 1'000,
             "Pre-cancelled verification still read the model file");
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
    connect(&controller, &VocalSeparationController::downloadProgressChanged,
            this, [&] { visibleProgress.push_back(controller.downloadProgress()); });
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
    bool monotonic = true;
    for (qsizetype index = 1; index < visibleProgress.size(); ++index) {
        if (visibleProgress.at(index) < visibleProgress.at(index - 1))
            monotonic = false;
    }
    QVERIFY(monotonic);
    QCOMPARE(controller.downloadProgress(), 1.0);
}

void VocalSeparationControllerTest::
downloadProgressNeverMutatesAnActiveSeparationJob()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray installedBytes("installed-test-model");
    const QByteArray downloadBytes(2 * 1024 * 1024, 'd');
    const QString downloadSource = temporary.filePath(
        QStringLiteral("download-source.onnx"));
    QVERIFY(writeBytes(downloadSource, downloadBytes));

    auto options = optionsFor(
        temporary, QStringLiteral("long-delayed-result"), installedBytes);
    options.catalog = {
        {QStringLiteral("two-stem"), VocalModelFamily::Mdx,
         {{QStringLiteral("test.onnx"), {}, installedBytes.size(),
           sha256(installedBytes)}},
         {QStringLiteral("vocals"), QStringLiteral("instrumental")},
         QStringLiteral("test"), QStringLiteral("test")},
        {QStringLiteral("download-model"), VocalModelFamily::Mdx,
         {{QStringLiteral("download.onnx"), QUrl::fromLocalFile(downloadSource),
           downloadBytes.size(), sha256(downloadBytes)}},
         {QStringLiteral("vocals"), QStringLiteral("instrumental")},
         QStringLiteral("test"), QStringLiteral("test")},
    };
    installTestModel(options, QStringLiteral("two-stem"), installedBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    // This test covers progress isolation, not a two-second Worker startup SLA.
    // Match the controller's real hello/heartbeat contract before asserting it.
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.stage(), QStringLiteral("inference"), 5'000);
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Running);
    QCOMPARE(controller.progress(), 0.5);

    QSignalSpy jobProgressChanged(
        &controller, &VocalSeparationController::progressChanged);
    QVERIFY(controller.downloadModel(QStringLiteral("download-model")));
    QTRY_VERIFY_WITH_TIMEOUT(controller.downloadBusy(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("download-model")),
        int(VocalSeparationController::ModelState::Installed), 5000);

    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Running);
    QCOMPARE(controller.progress(), 0.5);
    QCOMPARE(jobProgressChanged.count(), 0);
    QCOMPARE(controller.downloadProgress(), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed,
                              5000);
}

void VocalSeparationControllerTest::lateRuntimeVerificationCannotOverwriteInstalledState()
{
    QTemporaryDir temporary;
    auto options = optionsFor(temporary, "success", "model");
    installTestModel(options, QStringLiteral("two-stem"), "model");
    QVERIFY(writeBytes(options.runtimeLibraryPath, "installed runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QTRY_COMPARE_WITH_TIMEOUT(modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(VocalSeparationControllerTestDriver::directoryAndVerificationIdle(controller), 5000);
    QVERIFY(VocalSeparationControllerTestDriver::lateRuntimeVerificationPreservesInstalledState(controller, false));
    QVERIFY(VocalSeparationControllerTestDriver::lateRuntimeVerificationPreservesInstalledState(controller, true));
    QVERIFY(VocalSeparationControllerTestDriver::lateModelVerificationPreservesInstalledState(controller));
}

void VocalSeparationControllerTest::sharedRuntimeIsDeduplicatedAndIndependentOfModelCancellation()
{
    QTemporaryDir temporary;
    const QByteArray bytes("verified-model");
    auto options = optionsFor(temporary, "success", bytes);
    installTestModel(options, "two-stem", bytes);
    installTestModel(options, "five-stem", bytes);
    QTcpServer proxy;
    QVERIFY(proxy.listen(QHostAddress::LocalHost));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    VocalSeparationControllerTestDriver::useLocalProxy(controller, proxy.serverPort());
    QVERIFY(controller.configureRuntime("two-stem"));
    QVERIFY(controller.configureRuntime("five-stem"));
    const auto state = [&controller](const QString& id) {
        for (const auto& value : controller.models()) {
            const auto card = value.toMap();
            if (card.value("id").toString() == id) return card.value("configurationState").toString();
        }
        return QString();
    };
    QTRY_COMPARE_WITH_TIMEOUT(state("two-stem"), QStringLiteral("waiting-runtime"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(state("five-stem"), QStringLiteral("waiting-runtime"), 5000);
    QCOMPARE(VocalSeparationControllerTestDriver::sharedRuntimeDownloads(controller), 1);
    QTRY_VERIFY_WITH_TIMEOUT(proxy.hasPendingConnections(), 3000);
    QScopedPointer<QTcpSocket> request(proxy.nextPendingConnection());
    QVERIFY(QMetaObject::invokeMethod(&controller, "cancelConfiguration", Q_ARG(QString, "model:two-stem")));
    QCOMPARE(state("five-stem"), QStringLiteral("waiting-runtime"));
    QCOMPARE(VocalSeparationControllerTestDriver::sharedRuntimeDownloads(controller), 1);
    QVERIFY(!proxy.hasPendingConnections());
    QVERIFY(QMetaObject::invokeMethod(&controller, "pauseConfiguration", Q_ARG(QString, "runtime:directml")));
    QCOMPARE(controller.runtimeConfigurations().first().toMap().value("configurationState").toString(), QStringLiteral("paused"));
    QVERIFY(QMetaObject::invokeMethod(&controller, "cancelConfiguration", Q_ARG(QString, "runtime:directml")));
    QCOMPARE(VocalSeparationControllerTestDriver::sharedRuntimeDownloads(controller), 0);
    QVERIFY(!controller.runtimeReady());
    QVERIFY(!controller.downloadBusy());
}

void VocalSeparationControllerTest::configurationDownloadsRunIndependently()
{
    QTemporaryDir temporary;
    const QByteArray bytes(65536, 'p');
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    options.catalog = {options.catalog.first(), options.catalog.first()};
    for (int i = 0; i < 2; ++i) {
        options.catalog[i].id = QStringLiteral("parallel-%1").arg(i);
        options.catalog[i].files = {{QStringLiteral("test.onnx"),
            QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(i)),
            bytes.size(), sha256(bytes)}};
    }
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.configureRuntime("parallel-0"));
    QVERIFY2(controller.configureRuntime("parallel-1"), "Another model must start without waiting for the first download");
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
    QScopedPointer<QTcpSocket> first(server.nextPendingConnection());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
    QScopedPointer<QTcpSocket> second(server.nextPendingConnection());
    QTRY_VERIFY_WITH_TIMEOUT(first->bytesAvailable() > 0 && second->bytesAvailable() > 0, 5000);
    const bool firstIsA = first->readAll().contains("GET /0 ");
    second->readAll();
    auto* a = firstIsA ? first.data() : second.data();
    auto* b = firstIsA ? second.data() : first.data();
    const QByteArray header = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n";
    a->write(header + bytes.left(4096)); a->flush();
    b->write(header + bytes.left(4096)); b->flush();
    QVERIFY(QMetaObject::invokeMethod(&controller, "pauseConfiguration", Q_ARG(QString, "model:parallel-0")));
    b->write(bytes.mid(4096)); b->disconnectFromHost();
    QTRY_COMPARE_WITH_TIMEOUT(modelStateFor(controller.models(), "parallel-1"), int(VocalSeparationController::ModelState::Installed), 5000);
    QCOMPARE(modelStateFor(controller.models(), "parallel-0"), int(VocalSeparationController::ModelState::Paused));
    QVERIFY(controller.configureRuntime("parallel-1"));
    QVERIFY(!server.hasPendingConnections());
    QVERIFY(QMetaObject::invokeMethod(&controller, "cancelConfiguration", Q_ARG(QString, "model:parallel-0")));
    QVERIFY(!controller.downloadBusy());
}

void VocalSeparationControllerTest::switchingModelPreservesPublishedResult()
{
    QTemporaryDir temporary;
    const QByteArray bytes("trusted-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    auto other = options.catalog.first(); other.id = "other-model";
    other.stems = {QStringLiteral("vocals")}; options.catalog.push_back(other);
    installTestModel(options, "two-stem", bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(&preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(), VocalSeparationController::JobState::Completed, 5000);
    const auto before = stemFor(controller.stems(), VocalSeparationController::StemKind::Accompaniment);
    QVERIFY(before.value("available").toBool());
    QVERIFY(controller.selectModel("other-model"));
    const auto after = stemFor(controller.stems(), VocalSeparationController::StemKind::Accompaniment);
    QCOMPARE(after.value("path"), before.value("path"));
    QVERIFY(after.value("available").toBool());
    QVERIFY(controller.previewStem(VocalSeparationController::StemKind::Accompaniment));
    QVERIFY(QDir().mkpath(temporary.filePath("preserved-export")));
    QVERIFY(controller.exportAll(QUrl::fromLocalFile(temporary.filePath("preserved-export"))));
}

void VocalSeparationControllerTest::downloadingAnotherModelDoesNotBlockSeparation()
{
    QTemporaryDir temporary;
    const QByteArray installedBytes("installed-model");
    const QByteArray downloadBytes(1024 * 1024, 'd');
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto options = optionsFor(temporary, QStringLiteral("success"), installedBytes);
    auto downloading = options.catalog.first();
    downloading.id = QStringLiteral("background-model");
    downloading.files = {{QStringLiteral("test.onnx"),
        QUrl(QStringLiteral("http://127.0.0.1:%1/model").arg(server.serverPort())),
        downloadBytes.size(), sha256(downloadBytes)}};
    options.catalog.push_back(downloading);
    installTestModel(options, QStringLiteral("two-stem"), installedBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.downloadModel(downloading.id));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
    QScopedPointer<QTcpSocket> socket(server.nextPendingConnection());
    QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0, 5000);
    socket->readAll();
    socket->write("HTTP/1.1 200 OK\r\nContent-Length: "
        + QByteArray::number(downloadBytes.size()) + "\r\nConnection: close\r\n\r\n");
    socket->write(downloadBytes.left(4096));
    socket->flush();
    QVERIFY2(controller.canStart(), qPrintable(controller.startDisabledReason()));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
        VocalSeparationController::JobState::Completed, 5000);
    QVERIFY(controller.downloadBusy());
    QCOMPARE(controller.downloadingModelId(), downloading.id);
    socket->write(downloadBytes.mid(4096));
    socket->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.downloadBusy(), 5000);
    QCOMPARE(modelStateFor(controller.models(), downloading.id),
        int(VocalSeparationController::ModelState::Installed));
    QCOMPARE(controller.history().size(), 1);
}

void VocalSeparationControllerTest::modelSelectionRespondsDuringDeviceProbe()
{
    QTemporaryDir temporary;
    const QByteArray bytes("installed-model");
    auto options = optionsFor(temporary, QStringLiteral("delayed-hello"), bytes);
    installTestModel(options, QStringLiteral("two-stem"), bytes);
    installTestModel(options, QStringLiteral("five-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.probeDevices());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Probing);
    QVERIFY(controller.selectModel(QStringLiteral("five-stem")));
    QCOMPARE(controller.selectedModelId(), QStringLiteral("five-stem"));
    QVERIFY(controller.selectModel(QStringLiteral("two-stem")));
    QCOMPARE(controller.selectedModelId(), QStringLiteral("two-stem"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
        VocalSeparationController::JobState::Idle, 10000);
    QCOMPARE(controller.selectedModelId(), QStringLiteral("two-stem"));
    QTRY_VERIFY_WITH_TIMEOUT(VocalSeparationControllerTestDriver::workerAvailable(controller), 5000);
}

void VocalSeparationControllerTest::startWaitsForBackgroundVerificationWithoutFailing()
{
    QTemporaryDir temporary;
    const QByteArray bytes("installed-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    installTestModel(options, QStringLiteral("two-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(VocalSeparationControllerTestDriver::beginRefreshVerification(controller));
    QVERIFY(VocalSeparationControllerTestDriver::verificationInFlight(controller));
    QVERIFY(controller.start());
    QVERIFY(VocalSeparationControllerTestDriver::startVerificationPending(controller));
    QVERIFY(!controller.probeDevices());
    QVERIFY(!controller.configureRuntime(QStringLiteral("python-vr-5hp")));
    QVERIFY(!controller.configureRuntime(QStringLiteral("five-stem")));
    QVERIFY(VocalSeparationControllerTestDriver::startVerificationPending(controller));
    QCOMPARE(VocalSeparationControllerTestDriver::activeSeparationModel(controller),
             QStringLiteral("two-stem"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
        VocalSeparationController::JobState::Completed, 5000);
    QCOMPARE(controller.history().size(), 1);
}

void VocalSeparationControllerTest::cancellingQueuedStartPreservesBackgroundDownloadVerification()
{
    QTemporaryDir temporary;
    const QByteArray bytes(4 * 1024 * 1024, 'q');
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    installTestModel(options, QStringLiteral("two-stem"), bytes);
    installTestModel(options, QStringLiteral("five-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.downloadModel(QStringLiteral("five-stem")));
    // Model preflight now has its own task; a separate Refresh occupies the
    // shared worker-verification slot so Start still exercises its queued path.
    QVERIFY(VocalSeparationControllerTestDriver::beginRefreshVerification(controller));
    QVERIFY(VocalSeparationControllerTestDriver::verificationInFlight(controller));
    const auto generation = VocalSeparationControllerTestDriver::verificationGeneration(controller);
    QVERIFY(controller.start());
    QVERIFY(VocalSeparationControllerTestDriver::startVerificationPending(controller));
    controller.cancel();
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Cancelled);
    QVERIFY(!VocalSeparationControllerTestDriver::startVerificationPending(controller));
    QVERIFY(VocalSeparationControllerTestDriver::activeSeparationModel(controller).isEmpty());
    QVERIFY(controller.downloadBusy());
    QCOMPARE(controller.downloadingModelId(), QStringLiteral("five-stem"));
    QCOMPARE(VocalSeparationControllerTestDriver::verificationGeneration(controller), generation);
    QVERIFY(VocalSeparationControllerTestDriver::verificationInFlight(controller));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.downloadBusy(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(modelStateFor(controller.models(), QStringLiteral("five-stem")),
             int(VocalSeparationController::ModelState::Installed), 5000);
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Cancelled);
    QVERIFY(controller.history().isEmpty());
}

void VocalSeparationControllerTest::configurationDuringRefreshPreservesRunningSeparationContext()
{
    QTemporaryDir temporary;
    const QByteArray bytes(4 * 1024 * 1024, 'r');
    auto options = optionsFor(temporary, QStringLiteral("long-delayed-result"), bytes);
    installTestModel(options, QStringLiteral("two-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.stage(), QStringLiteral("inference"), 5000);
    installTestModel(options, QStringLiteral("five-stem"), bytes);
    QVERIFY(VocalSeparationControllerTestDriver::beginRefreshVerification(controller));
    QVERIFY(VocalSeparationControllerTestDriver::verificationInFlight(controller));
    QVERIFY(controller.configureRuntime(QStringLiteral("five-stem")));
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Running);
    QCOMPARE(VocalSeparationControllerTestDriver::activeSeparationModel(controller),
             QStringLiteral("two-stem"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(), VocalSeparationController::JobState::Completed, 5000);
    QCOMPARE(controller.history().size(), 1);
}

void VocalSeparationControllerTest::unchangedModelReusesDeviceProbe()
{
    QTemporaryDir temporary;
    const QByteArray bytes("installed-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    installTestModel(options, QStringLiteral("two-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    // Hardware discovery changes the probe fingerprint. Establish the stable
    // environment before testing reuse, including its queued completion signal.
    QTRY_VERIFY_WITH_TIMEOUT(!VocalSeparationControllerTestDriver::hardwareDiscoveryInFlight(controller), 10000);
    QCoreApplication::processEvents();
    QVERIFY(controller.probeDevices());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
        VocalSeparationController::JobState::Idle, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(VocalSeparationControllerTestDriver::workerAvailable(controller), 5000);
    const auto generation = VocalSeparationControllerTestDriver::verificationGeneration(controller);
    QVERIFY(controller.probeDevices());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Idle);
    QCOMPARE(VocalSeparationControllerTestDriver::verificationGeneration(controller), generation);
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
             int(VocalSeparationController::ModelState::PendingVerification));
    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::NotInstalled), 5000);
}

void VocalSeparationControllerTest::
unchangedVerifiedModelIsNotHashedAgainByDirectoryRefresh()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(8 * 1024 * 1024, 'v');
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !VocalSeparationControllerTestDriver::verificationInFlight(controller),
        5'000);
    const quint64 verifiedGeneration =
        VocalSeparationControllerTestDriver::verificationGeneration(controller);

    QVERIFY(controller.verifyInstalledModels());
    QTRY_VERIFY_WITH_TIMEOUT(
        VocalSeparationControllerTestDriver::directoryAndVerificationIdle(
            controller),
        5'000);
    QCOMPARE(
        VocalSeparationControllerTestDriver::verificationGeneration(controller),
        verifiedGeneration);
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::Installed));
}

void VocalSeparationControllerTest::
changedVerifiedModelInvalidatesTheCachedResult()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray trustedBytes("trusted-test-model");
    const QByteArray changedBytes("untrusted-model---");
    QCOMPARE(changedBytes.size(), trustedBytes.size());
    auto options = optionsFor(temporary, QStringLiteral("stale"), trustedBytes);
    installTestModel(options, QStringLiteral("two-stem"), trustedBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    const quint64 verifiedGeneration =
        VocalSeparationControllerTestDriver::verificationGeneration(controller);
    QTest::qWait(5);
    installTestModel(options, QStringLiteral("two-stem"), changedBytes);

    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::NotInstalled), 5'000);
    QVERIFY(VocalSeparationControllerTestDriver::verificationGeneration(controller)
            > verifiedGeneration);
}

void VocalSeparationControllerTest::
changedRuntimeInvalidatesTheCachedResultBeforeProbe()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime-a")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    const quint64 verifiedGeneration =
        VocalSeparationControllerTestDriver::verificationGeneration(controller);
    QTest::qWait(5);
    QVERIFY(writeBytes(options.runtimeLibraryPath,
                       QByteArrayLiteral("runtime-b")));

    QVERIFY(controller.probeDevices());
    QVERIFY(VocalSeparationControllerTestDriver::verificationGeneration(controller)
            > verifiedGeneration);
    controller.cancel();
}

void VocalSeparationControllerTest::
unexpectedRuntimeEntryInvalidatesTheCachedResultBeforeProbe()
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

    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    const quint64 verifiedGeneration =
        VocalSeparationControllerTestDriver::verificationGeneration(controller);
    QVERIFY(QDir().mkpath(temporary.filePath(
        QStringLiteral("unexpected-runtime-entry"))));

    QVERIFY(controller.probeDevices());
    QVERIFY(VocalSeparationControllerTestDriver::verificationGeneration(controller)
            > verifiedGeneration);
    controller.cancel();
}

void VocalSeparationControllerTest::
cancellingRevalidationDoesNotCacheARejectedModel()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(32 * 1024 * 1024, 'r');
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    QTest::qWait(5);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);

    QVERIFY(controller.probeDevices());
    QVERIFY(VocalSeparationControllerTestDriver::verificationInFlight(
        controller));
    controller.cancel();
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::PendingVerification));
}

void VocalSeparationControllerTest::
configurationCanTakeOverBackgroundRefreshVerification()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes(16 * 1024 * 1024, 'c');
    auto options = optionsFor(temporary, QStringLiteral("stale"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(VocalSeparationControllerTestDriver::beginRefreshVerification(
        controller));
    QVERIFY(VocalSeparationControllerTestDriver::verificationInFlight(
        controller));
    QVERIFY(controller.configureRuntime(QStringLiteral("two-stem")));
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    QVERIFY(VocalSeparationControllerTestDriver::directoryAndVerificationIdle(
        controller));
}

void VocalSeparationControllerTest::
customModelDirectoryPersistsAndRecognizesTrustedNestedFiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(
        temporary, QStringLiteral("stale"), modelBytes);
    const QString customRoot = temporary.filePath(
        QStringLiteral("用户模型目录"));
    QVERIFY(QDir().mkpath(customRoot));

    {
        AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
        WaveformProvider waveforms;
        VocalSeparationController controller(
            &preview, &waveforms, nullptr, nullptr, nullptr, options);
        QVERIFY(controller.selectModelDirectory(
            QUrl::fromLocalFile(customRoot)));
        QCOMPARE(controller.modelStorageDirectory(),
                 QFileInfo(customRoot).absoluteFilePath());
        const QString nestedRoot = QDir(customRoot).filePath(
            QStringLiteral("人声模型/MDX/正式版"));
        QVERIFY(QDir().mkpath(nestedRoot));
        QVERIFY(writeBytes(QDir(nestedRoot).filePath(
                               QStringLiteral("test.onnx")),
                           modelBytes));
        QTRY_COMPARE_WITH_TIMEOUT(
            modelStateFor(controller.models(), QStringLiteral("two-stem")),
            int(VocalSeparationController::ModelState::Installed), 5000);
    }

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController restored(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QCOMPARE(restored.modelStorageDirectory(),
             QFileInfo(customRoot).absoluteFilePath());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(restored.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5000);
}

void VocalSeparationControllerTest::
customModelDirectoryListsNestedRawModelsWithBackendDiagnostics()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(
        temporary, QStringLiteral("stale"), modelBytes);
    const QString customRoot = temporary.filePath(QStringLiteral("模型"));
    QVERIFY(QDir().mkpath(customRoot));
    const QString nested = QDir(customRoot).filePath(
        QStringLiteral("UVR5/MDX_Net_Models"));
    QVERIFY(QDir().mkpath(nested));
    const QString rawModel = QDir(nested).filePath(
        QStringLiteral("Kim_Vocal_2.onnx"));
    QVERIFY(writeBytes(rawModel, modelBytes));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModelDirectory(QUrl::fromLocalFile(customRoot)));
    QTest::qWait(900);
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::NotInstalled));
    QVariantMap discovered;
    for (const QVariant& value : controller.models()) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("modelPath")).toString()
            == QFileInfo(rawModel).absoluteFilePath()) {
            discovered = model;
            break;
        }
    }
    QVERIFY(!discovered.isEmpty());
    QCOMPARE(discovered.value(QStringLiteral("origin")).toString(),
             QStringLiteral("custom"));
    QCOMPARE(discovered.value(QStringLiteral("backend")).toString(),
             QStringLiteral("onnxruntime-native"));
    QCOMPARE(discovered.value(QStringLiteral("available")).toBool(), false);
    QVERIFY(!discovered.value(QStringLiteral("failureReason")).toString().isEmpty());
}

void VocalSeparationControllerTest::knownLocalMdxProfileIsDiscoveredButStillRequiresItsFingerprint()
{
    QTemporaryDir temporary;
    const auto options = optionsFor(temporary, QStringLiteral("stale"), QByteArray("model"));
    const QString root = temporary.filePath(QStringLiteral("models/nested/mdx"));
    QVERIFY(QDir().mkpath(root));
    QFile spoof(QDir(root).filePath(QStringLiteral("Kim_Vocal_2.onnx")));
    QVERIFY(spoof.open(QIODevice::WriteOnly));
    QVERIFY(spoof.resize(66'759'214));
    spoof.close();
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(&preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModelDirectory(QUrl::fromLocalFile(temporary.filePath(QStringLiteral("models")))));
    QTRY_VERIFY_WITH_TIMEOUT(controller.models().size() > options.catalog.size(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(VocalSeparationControllerTestDriver::downloadPipelineIdle(controller), 5000);
    const auto models = controller.models();
    const auto found = std::find_if(models.cbegin(), models.cend(), [](const QVariant& value) {
        return value.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("kim-vocal-2");
    });
    QVERIFY(found != models.cend());
    QCOMPARE(found->toMap().value(QStringLiteral("origin")).toString(), QStringLiteral("custom"));
    QVERIFY(!found->toMap().value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.downloadBusy());
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
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::ResultPreviewMode>()
                 .keyCount(),
             3);
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::ResultPreviewMode>()
                 .keyToValue("None"),
             int(VocalSeparationController::ResultPreviewMode::None));
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::ResultPreviewMode>()
                 .keyToValue("Mix"),
             int(VocalSeparationController::ResultPreviewMode::Mix));
    QCOMPARE(QMetaEnum::fromType<VocalSeparationController::ResultPreviewMode>()
                 .keyToValue("Solo"),
             int(VocalSeparationController::ResultPreviewMode::Solo));
    const QMetaEnum modelStates = QMetaEnum::fromType<VocalSeparationController::ModelState>();
    const QMetaEnum jobStates = QMetaEnum::fromType<VocalSeparationController::JobState>();
    QVERIFY(modelStates.keyToValue("ModelFailed") >= 0);
    QVERIFY(jobStates.keyToValue("JobFailed") >= 0);
    QCOMPARE(modelStates.keyToValue("Failed"), -1);
    QCOMPARE(jobStates.keyToValue("Failed"), -1);
    QCOMPARE(controller.models().size(), 2);
    const QVariantMap twoStemCard = controller.models().first().toMap();
    QVERIFY(!twoStemCard.value(QStringLiteral("name")).toString().isEmpty());
    QVERIFY(!twoStemCard.value(QStringLiteral("useCase")).toString().isEmpty());
    QVERIFY(twoStemCard.value(QStringLiteral("bytes")).toLongLong() > 0);
    QVERIFY(!twoStemCard.value(QStringLiteral("stems")).toList().isEmpty());
    QCOMPARE(controller.models().first().toMap().value(QStringLiteral("state")).toInt(),
             int(VocalSeparationController::ModelState::PendingVerification));
    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        controller.models().first().toMap().value(QStringLiteral("state")).toInt(),
        int(VocalSeparationController::ModelState::Installed), 5000);
    QCOMPARE(controller.stems().size(), 5);
    for (const VocalSeparationController::StemKind kind : {
             VocalSeparationController::StemKind::Drums,
             VocalSeparationController::StemKind::Bass,
             VocalSeparationController::StemKind::Other}) {
        const QVariantMap stem = stemFor(controller.stems(), kind);
        QVERIFY(!stem.value(QStringLiteral("supported")).toBool());
        QVERIFY(!stem.value(QStringLiteral("selected")).toBool());
        QVERIFY(!stem.value(QStringLiteral("available")).toBool());
        QVERIFY(stem.value(QStringLiteral("path")).toString().isEmpty());
        QVERIFY(stem.value(QStringLiteral("waveform")).toList().isEmpty());
    }

    QVERIFY(controller.selectModel(QStringLiteral("five-stem")));
    QCOMPARE(controller.stems().size(), 5);
    QVERIFY(stemFor(controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                .value(QStringLiteral("derived")).toBool());
}

void VocalSeparationControllerTest::
exposesStartEligibilityAndAnAlwaysSelectableAutoDevice()
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

    QVERIFY(controller.metaObject()->indexOfProperty("canStart") >= 0);
    QVERIFY(controller.metaObject()->indexOfProperty("startDisabledReason") >= 0);
    QCOMPARE(controller.availableDevices().first().toMap().value(QStringLiteral("mode")).toInt(),
             int(VocalSeparationController::DeviceMode::Auto));
    QVERIFY(controller.availableDevices().first().toMap().value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.property("canStart").toBool());
    QVERIFY(!controller.property("startDisabledReason").toString().isEmpty());

    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.verifyInstalledModels());
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.property("canStart").toBool(), 5'000);
    QVERIFY(controller.property("startDisabledReason").toString().isEmpty());
    QSignalSpy eligibilityChanged(&controller,
        &VocalSeparationController::startEligibilityChanged);
    QVERIFY(controller.setStemSelected(VocalSeparationController::StemKind::Vocals, false));
    QVERIFY(controller.setStemSelected(VocalSeparationController::StemKind::Accompaniment, false));
    QVERIFY(!controller.property("canStart").toBool());
    QVERIFY(eligibilityChanged.count() >= 2);
    QVERIFY(controller.setStemSelected(VocalSeparationController::StemKind::Vocals, true));
    QVERIFY(controller.property("canStart").toBool());
    QVERIFY(controller.selectDevice(VocalSeparationController::DeviceMode::CPU));
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::CPU);
    QVERIFY(controller.selectDevice(VocalSeparationController::DeviceMode::Auto));
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::Auto);
    QVERIFY(!controller.selectDevice(VocalSeparationController::DeviceMode::GPU));
}

void VocalSeparationControllerTest::
workerPayloadUsesCurrentLanguageStemLabelsAndCatalogDisplayName()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const QString marker = temporary.filePath(QStringLiteral("request.json"));
    auto options = optionsFor(temporary,
                              QStringLiteral("capture-payload-delayed-shutdown"),
                              modelBytes, marker);
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
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5'000);
    QFile englishMarker(marker);
    QVERIFY(englishMarker.open(QIODevice::ReadOnly));
    const QJsonObject english = QJsonDocument::fromJson(englishMarker.readAll()).object();
    englishMarker.close();
    QCOMPARE(english.value(QStringLiteral("baseName")).toString(),
             QFileInfo(audioFixture()).completeBaseName());
    QCOMPARE(english.value(QStringLiteral("directoryName")).toString(),
             QFileInfo(audioFixture()).completeBaseName()
                 + QStringLiteral("-two-stem"));
    QCOMPARE(english.value(QStringLiteral("modelName")).toString(),
             QStringLiteral("Two stem test"));
    QCOMPARE(english.value(QStringLiteral("stemLabels")).toArray(),
             QJsonArray({QStringLiteral("Vocals"), QStringLiteral("Instrumental")}));

    QVERIFY(QFile::remove(marker));
    QVERIFY(!controller.canStart());
    QVERIFY(!controller.start());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Completed);
    QTRY_VERIFY_WITH_TIMEOUT(controller.canStart(), 5'000);
    ChineseStemLabelTranslator chineseTranslator;
    QCoreApplication::installTranslator(&chineseTranslator);
    QVERIFY(controller.start());
    // The output names must follow the UI language at the moment the user
    // starts the job, even if the language changes while model verification
    // is still running asynchronously.
    QCoreApplication::removeTranslator(&chineseTranslator);
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5'000);
    QFile chineseMarker(marker);
    QVERIFY(chineseMarker.open(QIODevice::ReadOnly));
    const QJsonObject chinese = QJsonDocument::fromJson(chineseMarker.readAll()).object();
    QCOMPARE(chinese.value(QStringLiteral("stemLabels")).toArray(),
             QJsonArray({QStringLiteral("人声"), QStringLiteral("伴奏")}));
}

void VocalSeparationControllerTest::progressiveResultWaveformKeepsTheRequestUntilComplete()
{
    QTemporaryDir temporary;
    auto options = optionsFor(temporary, QStringLiteral("success"), QByteArray("test"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    VocalSeparationControllerTestDriver::prepareResultWaveform(controller);
    VocalSeparationControllerTestDriver::deliverResultWaveform(controller, {0.2, 0.0}, false);
    VocalSeparationControllerTestDriver::deliverResultWaveform(controller, {0.2, 0.8}, true);
    QCOMPARE(controller.stems().first().toMap().value("waveform").toList(), QVariantList({0.2, 0.8}));
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
    const QVariantMap historyRecord = controller.history().first().toMap();
    QCOMPARE(historyRecord.value(QStringLiteral("inputName")).toString(),
             QFileInfo(audioFixture()).fileName());
    QCOMPARE(historyRecord.value(QStringLiteral("status")).toString(),
             QStringLiteral("completed"));
    QVERIFY(!historyRecord.value(QStringLiteral("outputPath")).toString().isEmpty());
    const QVariantMap vocals = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals);
    QVERIFY(vocals.value(QStringLiteral("available")).toBool());
    QVERIFY(QFileInfo::exists(vocals.value(QStringLiteral("path")).toString()));
    QTRY_VERIFY_WITH_TIMEOUT(
        !stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
             .value(QStringLiteral("waveform")).toList().isEmpty(),
        5000);
    const QVariantList devices = controller.availableDevices();
    QCOMPARE(devices.at(2).toMap().value(QStringLiteral("reason")).toString(),
             QStringLiteral("No tested GPU"));
}

void VocalSeparationControllerTest::
stemPreviewVolumesRemainIndependentAndDriveTheSharedPreview()
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
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.jobState() == VocalSeparationController::JobState::Completed
            || controller.jobState()
                == VocalSeparationController::JobState::JobFailed,
        5'000);
    const QByteArray lifecycle =
        VocalSeparationControllerTestDriver::lifecycleDiagnostic(controller);
    QVERIFY2(controller.jobState()
                 == VocalSeparationController::JobState::Completed,
             lifecycle.constData());

    QVERIFY(controller.setStemPreviewVolume(
        VocalSeparationController::StemKind::Vocals, 0.25));
    QVERIFY(controller.setStemPreviewVolume(
        VocalSeparationController::StemKind::Accompaniment, 0.75));
    QVERIFY(!controller.setStemPreviewVolume(
        VocalSeparationController::StemKind::Drums, 0.5));
    QCOMPARE(stemFor(controller.stems(),
                     VocalSeparationController::StemKind::Vocals)
                 .value(QStringLiteral("previewVolume")).toDouble(),
             0.25);
    QCOMPARE(stemFor(controller.stems(),
                     VocalSeparationController::StemKind::Accompaniment)
                 .value(QStringLiteral("previewVolume")).toDouble(),
             0.75);

    QVERIFY(controller.previewStem(
        VocalSeparationController::StemKind::Vocals));
    QCOMPARE(preview.volume(), 0.25);
    QVERIFY(controller.previewStem(
        VocalSeparationController::StemKind::Accompaniment));
    QCOMPARE(preview.volume(), 0.75);
    QVERIFY(controller.previewInput());
    QCOMPARE(preview.volume(), 1.0);
}

void VocalSeparationControllerTest::
mdxResultPreviewSwitchesBetweenMixAndSoloAtTheSharedPosition()
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
                              VocalSeparationController::JobState::Completed,
                              5'000);

    const QString vocalsPath = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals)
                                    .value(QStringLiteral("path")).toString();
    const QString accompanimentPath = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                                           .value(QStringLiteral("path")).toString();
    QVERIFY(!vocalsPath.isEmpty());
    QVERIFY(!accompanimentPath.isEmpty());
    QCOMPARE(controller.resultPreviewMode(),
             VocalSeparationController::ResultPreviewMode::None);

    QVERIFY(controller.toggleResultMix(240));
    QCOMPARE(controller.resultPreviewMode(),
             VocalSeparationController::ResultPreviewMode::Mix);
    QCOMPARE(preview.mixSourceIds(), QStringList({vocalsPath, accompanimentPath}));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 1'000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= 190, 1'000);

    QVERIFY(controller.previewStemAt(
        VocalSeparationController::StemKind::Vocals, 620));
    QCOMPARE(controller.resultPreviewMode(),
             VocalSeparationController::ResultPreviewMode::Solo);
    QCOMPARE(controller.resultPreviewSoloKind(),
             VocalSeparationController::StemKind::Vocals);
    QCOMPARE(preview.mixSourceIds(), QStringList({vocalsPath}));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 1'000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= 570, 1'000);
    const qint64 soloPosition = preview.positionMs();

    QVERIFY(controller.toggleResultMix(soloPosition));
    QCOMPARE(controller.resultPreviewMode(),
             VocalSeparationController::ResultPreviewMode::Mix);
    QCOMPARE(preview.mixSourceIds(), QStringList({vocalsPath, accompanimentPath}));
    QTRY_VERIFY_WITH_TIMEOUT(preview.playing(), 1'000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.positionMs() >= soloPosition - 50, 1'000);
}

void VocalSeparationControllerTest::
demucsResultMixExcludesTheDerivedAccompanimentWhenComponentsAreComplete()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("five-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModel(QStringLiteral("five-stem")));
    QVERIFY(controller.setStemSelected(
        VocalSeparationController::StemKind::Drums, true));
    QVERIFY(controller.setStemSelected(
        VocalSeparationController::StemKind::Bass, true));
    QVERIFY(controller.setStemSelected(
        VocalSeparationController::StemKind::Other, true));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed,
                              5'000);

    const auto path = [&controller](VocalSeparationController::StemKind kind) {
        return stemFor(controller.stems(), kind)
            .value(QStringLiteral("path")).toString();
    };
    const QString accompanimentPath = path(
        VocalSeparationController::StemKind::Accompaniment);
    QVERIFY(!accompanimentPath.isEmpty());

    QVERIFY(controller.toggleResultMix(0));
    QCOMPARE(preview.mixSourceIds(),
             QStringList({path(VocalSeparationController::StemKind::Vocals),
                          path(VocalSeparationController::StemKind::Drums),
                          path(VocalSeparationController::StemKind::Bass),
                          path(VocalSeparationController::StemKind::Other)}));
    QVERIFY(!preview.mixSourceIds().contains(accompanimentPath));
}

void VocalSeparationControllerTest::
demucsResultMixFallsBackToTheDerivedAccompanimentWhenComponentsAreIncomplete_data()
{
    QTest::addColumn<bool>("drums");
    QTest::addColumn<bool>("bass");
    QTest::addColumn<bool>("other");

    QTest::newRow("no-components") << false << false << false;
    QTest::newRow("one-component") << true << false << false;
    QTest::newRow("two-components") << true << true << false;
}

void VocalSeparationControllerTest::
demucsResultMixFallsBackToTheDerivedAccompanimentWhenComponentsAreIncomplete()
{
    QFETCH(bool, drums);
    QFETCH(bool, bass);
    QFETCH(bool, other);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("five-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModel(QStringLiteral("five-stem")));
    if (drums) {
        QVERIFY(controller.setStemSelected(
            VocalSeparationController::StemKind::Drums, true));
    }
    if (bass) {
        QVERIFY(controller.setStemSelected(
            VocalSeparationController::StemKind::Bass, true));
    }
    if (other) {
        QVERIFY(controller.setStemSelected(
            VocalSeparationController::StemKind::Other, true));
    }
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed,
                              5'000);

    const QString vocalsPath = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals)
                                    .value(QStringLiteral("path")).toString();
    const QString accompanimentPath = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Accompaniment)
                                           .value(QStringLiteral("path")).toString();
    QVERIFY(!vocalsPath.isEmpty());
    QVERIFY(!accompanimentPath.isEmpty());
    QCOMPARE(stemFor(controller.stems(), VocalSeparationController::StemKind::Drums)
                 .value(QStringLiteral("available")).toBool(), drums);
    QCOMPARE(stemFor(controller.stems(), VocalSeparationController::StemKind::Bass)
                 .value(QStringLiteral("available")).toBool(), bass);
    QCOMPARE(stemFor(controller.stems(), VocalSeparationController::StemKind::Other)
                 .value(QStringLiteral("available")).toBool(), other);

    QVERIFY(controller.toggleResultMix(0));
    QCOMPARE(preview.mixSourceIds(), QStringList({vocalsPath, accompanimentPath}));
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
    QSignalSpy waveformReady(&waveforms, &WaveformProvider::waveformReady);
    QSignalSpy waveformFailed(&waveforms, &WaveformProvider::waveformFailed);
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    QVERIFY(stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                .value(QStringLiteral("available")).toBool());
    const std::weak_ptr<void> previousWaveformResources =
        WaveformProviderTestAccess::activeResources(waveforms);
    QVERIFY(!previousWaveformResources.expired());
    const int terminalCountBeforeClear = waveformReady.count()
        + waveformFailed.count();

    const QString invalidNextInput = temporary.filePath(
        QStringLiteral("下一次输入.wav"));
    QVERIFY(QFile::copy(audioFixture(), invalidNextInput));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(invalidNextInput)));
    QVERIFY(QFile::remove(invalidNextInput));
    QVERIFY(!controller.start());
    QVERIFY(!stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                 .value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.previewStem(VocalSeparationController::StemKind::Vocals));
    WaveformProviderTestAccess::waitForAnalysis(waveforms);
    QTRY_VERIFY_WITH_TIMEOUT(previousWaveformResources.expired(), 2'000);
    QCOMPARE(waveformReady.count() + waveformFailed.count(),
             terminalCountBeforeClear);

    QVERIFY(QFile::copy(audioFixture(), invalidNextInput));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(invalidNextInput)));
    QTRY_VERIFY_WITH_TIMEOUT(controller.canStart(), 5'000);
    QVERIFY(controller.start());
    QVERIFY(!stemFor(controller.stems(), VocalSeparationController::StemKind::Vocals)
                 .value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.previewStem(VocalSeparationController::StemKind::Vocals));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
}

void VocalSeparationControllerTest::
selectingANewInputImmediatelyClearsCompletedResultAndItsPreview()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    const QString nextInput = temporary.filePath(QStringLiteral("next-input.wav"));
    QVERIFY(QFile::copy(audioFixture(), nextInput));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    QVERIFY(controller.previewStem(VocalSeparationController::StemKind::Vocals));
    QVERIFY(preview.hasSource());

    QVERIFY(controller.selectInput(QUrl::fromLocalFile(nextInput)));
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Idle);
    QCOMPARE(controller.progress(), 0.0);
    QVERIFY(!preview.hasSource());
    for (const QVariant& value : controller.stems()) {
        const QVariantMap stem = value.toMap();
        QVERIFY(!stem.value(QStringLiteral("available")).toBool());
        QVERIFY(stem.value(QStringLiteral("path")).toString().isEmpty());
        QVERIFY(stem.value(QStringLiteral("waveform")).toList().isEmpty());
    }
    QTRY_VERIFY_WITH_TIMEOUT(
        !controller.inputInfo().value(QStringLiteral("waveform")).toList().isEmpty(),
        5'000);
    QCOMPARE(controller.inputInfo().value(QStringLiteral("path")).toString(),
             QFileInfo(nextInput).absoluteFilePath());
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
                              VocalSeparationController::JobState::JobFailed, 5000);
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
                              VocalSeparationController::JobState::JobFailed, 5000);
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
                              VocalSeparationController::JobState::JobFailed, 5000);
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
                              VocalSeparationController::JobState::JobFailed, 5000);
    QVERIFY(!controller.error().isEmpty());
    QCOMPARE(controller.history().size(), 0);
    QVERIFY(controller.actualProvider().isEmpty());
    QVERIFY(controller.actualDevice().isEmpty());
    QCOMPARE(controller.outputGain(), 1.0);
}

void VocalSeparationControllerTest::cudaRuntimeRejectsUnverifiedComponents()
{
    QTemporaryDir root;
    QDir().mkpath(root.filePath("native"));
    QFile dll(root.filePath("native/onnxruntime.dll"));
    QVERIFY(dll.open(QIODevice::WriteOnly)); dll.write("untrusted"); dll.close();
    QNetworkAccessManager network;
    CudaSeparationRuntime runtime(root.path(), &network);
    QSignalSpy changed(&runtime, &CudaSeparationRuntime::changed);
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 10000);
    QVERIFY(!runtime.ready());
}

void VocalSeparationControllerTest::externalRuntimePersistsStageFailure()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QSignalSpy finished(&runtime, &ExternalSeparationRuntime::finished);
#ifdef Q_OS_MACOS
    ExternalSeparationRuntimeTestDriver::launchStage(runtime, 4, "/bin/sh",
        {"-c", "echo controlled-import-failure; exit 7"});
#else
    ExternalSeparationRuntimeTestDriver::launchStage(runtime, 4, "cmd.exe",
        {"/D", "/C", "echo controlled-import-failure & exit /b 7"});
#endif
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.first().first().toBool());
    QFile log(fixture.filePath("install.log"));
    QVERIFY2(log.open(QIODevice::ReadOnly), "Failed installation must retain diagnostics after app exit");
    const auto bytes = log.readAll();
    QVERIFY(bytes.contains("controlled-import-failure"));
    QVERIFY(bytes.contains("stage=4"));
    QVERIFY(!runtime.ready());
    QVERIFY(network.hosts.isEmpty());
}

void VocalSeparationControllerTest::macosPythonInterpreterLinksMustStayInThePrivateRuntime()
{
#ifndef Q_OS_MACOS
    QSKIP("macOS uv venv interpreters use confined symlinks", "");
#else
    QTemporaryDir root;
    QTemporaryDir outside;
    const QDir fixture(externalFixtureRoot(root.path()));
    const QString managed = fixture.filePath("python/managed/bin/python3.10");
    QVERIFY(writeBytes(managed, "managed interpreter"));
    QVERIFY(QDir().mkpath(fixture.filePath("env/bin")));
    const QString interpreter = fixture.filePath(externalFixturePython());
    QVERIFY(QFile::link(managed, interpreter));
    QVERIFY(writeBytes(fixture.filePath("verified-vr-1"), externalFixtureMarker()));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QVERIFY(runtime.ready());
    QVERIFY(QFile::remove(interpreter));
    const QString escaped = outside.filePath("python");
    QVERIFY(writeBytes(escaped, "outside interpreter"));
    QVERIFY(QFile::link(escaped, interpreter));
    QVERIFY(!runtime.ready());
    QVERIFY(network.hosts.isEmpty());
#endif
}

void VocalSeparationControllerTest::externalRuntimeStallSwitchesSourceAndStopsAfterBackup()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    // The real service owns the process/timer/state machine; only its slow
    // network tool is replaced by a local sleeping process and a failing exe.
    QVERIFY(QFile::copy(externalConfiguratorSource(),
                        fixture.filePath(externalConfiguratorName())));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QSignalSpy finished(&runtime, &ExternalSeparationRuntime::finished);
    QSignalSpy progress(&runtime, &ExternalSeparationRuntime::progress);
#ifdef Q_OS_MACOS
    ExternalSeparationRuntimeTestDriver::launchStage(runtime, 2, "/bin/sleep", {"60"});
#else
    ExternalSeparationRuntimeTestDriver::launchStage(runtime, 2, "powershell.exe",
        {"-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 60"});
#endif
    if (auto* watchdog = runtime.findChild<QTimer*>("pythonInstallerInactivity"))
        watchdog->start(100);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.first().first().toBool());
    int switches = 0;
    for (const auto& event : progress)
        if (event.at(1).toString().contains(QStringLiteral("切换"))) ++switches;
    QCOMPARE(switches, 1);
    QFile log(fixture.filePath("install.log"));
    QVERIFY(log.open(QIODevice::ReadOnly));
    const auto bytes = log.readAll();
    QVERIFY(bytes.contains("inactivity"));
    QVERIFY(bytes.contains("source=backup"));
    QVERIFY(!runtime.busy());
}

void VocalSeparationControllerTest::externalRuntimeQuarantinesIncompletePythonWithoutDeletingIt()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    const QString managed = fixture.filePath("python/cpython-3.11.13-windows-x86_64-none");
    QVERIFY(writeBytes(QDir(managed).filePath("BUILD"), "partial-install-evidence"));
    QVERIFY(writeBytes(fixture.filePath("env/pyvenv.cfg"), "incomplete environment"));
    QVERIFY(writeBytes(fixture.filePath("cache/keep.whl"), "download cache"));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QSignalSpy finished(&runtime, &ExternalSeparationRuntime::finished);
    ExternalSeparationRuntimeTestDriver::preparePython(runtime);
#ifdef Q_OS_MACOS
    QVERIFY(QFileInfo::exists(managed)); // uv owns Mac managed-download recovery.
#else
    QVERIFY2(!QFileInfo::exists(managed), "Incomplete managed Python must not poison the next uv attempt");
#endif
    QVERIFY(!QFileInfo::exists(fixture.filePath("env")));
    QVERIFY(QFileInfo::exists(fixture.filePath("cache/keep.whl")));
    const auto recovery = fixture.entryList({".incomplete-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
#ifdef Q_OS_MACOS
    QCOMPARE(recovery.size(), 1);
#else
    QCOMPARE(recovery.size(), 2);
#endif
#ifdef Q_OS_MACOS
    QFile preserved(QDir(managed).filePath("BUILD"));
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray("partial-install-evidence"));
#else
    bool evidencePreserved = false;
    for (const auto& path : recovery) {
        QFile build(fixture.filePath(path + "/BUILD"));
        if (build.open(QIODevice::ReadOnly)) evidencePreserved |= build.readAll() == "partial-install-evidence";
    }
    QVERIFY(evidencePreserved);
#endif
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!runtime.ready());
}

void VocalSeparationControllerTest::explicitDemucsGpuRequiresCudaBeforeStartingWorker()
{
#ifdef Q_OS_MACOS
    QSKIP("CUDA is Windows-specific; Mac CoreML/CPU policy has dedicated coverage", "");
#endif
    QTemporaryDir temporary;
    const QByteArray bytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    installTestModel(options, QStringLiteral("five-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModel("five-stem"));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    VocalSeparationControllerTestDriver::publishGpuCandidate(controller);
    QVERIFY(controller.selectDevice(VocalSeparationController::DeviceMode::GPU));
    QVERIFY(!controller.canStart());
    QVERIFY(controller.startDisabledReason().contains("CUDA"));
    QVERIFY(!controller.start());
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::GPU);
    QCOMPARE(controller.history().size(), 0);
    QVERIFY(controller.selectDevice(VocalSeparationController::DeviceMode::CPU));
    QVERIFY(controller.canStart());
}

void VocalSeparationControllerTest::externalRuntimePauseThenImmediateResumeIsNonBlocking()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    QVERIFY(QFile::copy(externalConfiguratorSource(), fixture.filePath(externalConfiguratorName())));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QSignalSpy finished(&runtime, &ExternalSeparationRuntime::finished);
#ifdef Q_OS_MACOS
    ExternalSeparationRuntimeTestDriver::launchStage(runtime, 2, "/bin/sleep", {"60"});
#else
    ExternalSeparationRuntimeTestDriver::launchStage(runtime, 2, "powershell.exe",
        {"-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 60"});
#endif
    QElapsedTimer uiCall; uiCall.start();
    runtime.pause();
    runtime.resume();
    QVERIFY2(uiCall.elapsed() < 250, "Pause/resume must not synchronously wait for installer processes");
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.first().first().toBool()); // Local fixture deliberately cannot provide Python.
    QVERIFY(!runtime.busy());
    QVERIFY(!runtime.paused());
    QVERIFY(network.hosts.isEmpty());
}

void VocalSeparationControllerTest::externalRuntimeAlreadyVerifiedDoesNotInvalidateOrReinstall()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    QVERIFY(writeBytes(fixture.filePath(externalFixturePython()), "verified interpreter - must not execute"));
    QVERIFY(writeBytes(fixture.filePath("verified-vr-1"),
                       externalFixtureMarker()));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QVERIFY(runtime.ready());
    QSignalSpy finished(&runtime, &ExternalSeparationRuntime::finished);
    QVERIFY(runtime.start());
    QVERIFY2(runtime.ready(), "Existing marker remains valid until asynchronous verification completes");
    QVERIFY2(runtime.busy(), "Repeated configure must verify an existing environment without reinstalling it");
    QCOMPARE(ExternalSeparationRuntimeTestDriver::stage(runtime), 4);
    QCOMPARE(finished.count(), 0);
    runtime.cancel();
    QVERIFY(runtime.ready());
    QVERIFY(network.hosts.isEmpty());
}

void VocalSeparationControllerTest::externalRuntimeRejectsLegacyReadyMarker()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    QVERIFY(writeBytes(fixture.filePath(externalFixturePython()), "interpreter"));
    QVERIFY(writeBytes(fixture.filePath("verified-vr-1"), "audio-separator=0.30.2"));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QVERIFY(!runtime.ready());
    QVERIFY(runtime.start());
    QVERIFY(runtime.busy());
    QCOMPARE(ExternalSeparationRuntimeTestDriver::stage(runtime), 4);
    QVERIFY(QFileInfo::exists(fixture.filePath(externalFixturePython())));
    QVERIFY(network.hosts.isEmpty());
    runtime.cancel();
}

void VocalSeparationControllerTest::externalRuntimeReadyStartQueuesAsynchronousVerification()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    QVERIFY(writeBytes(fixture.filePath(externalFixturePython()), "interpreter"));
    QVERIFY(writeBytes(fixture.filePath("verified-vr-1"),
                       externalFixtureMarker()));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QVERIFY(runtime.ready());
    QSignalSpy finished(&runtime, &ExternalSeparationRuntime::finished);
    QVERIFY(runtime.start());
    QVERIFY(runtime.busy());
    QCOMPARE(ExternalSeparationRuntimeTestDriver::stage(runtime), 4);
    QCOMPARE(finished.count(), 0);
    runtime.cancel();
    QVERIFY(!runtime.busy());
    QVERIFY(network.hosts.isEmpty());
}

void VocalSeparationControllerTest::externalRuntimeFailedExistingVerificationKeepsEnvironmentAndUsesTrustedRepair()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    const QString interpreter = fixture.filePath(externalFixturePython());
    QVERIFY(writeBytes(interpreter, "broken interpreter fixture"));
    QVERIFY(writeBytes(fixture.filePath("verified-vr-1"),
                       externalFixtureMarker()));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    QVERIFY(runtime.ready());

    QVERIFY(ExternalSeparationRuntimeTestDriver::failedExistingVerificationStartsTrustedRepair(runtime));
    QCOMPARE(ExternalSeparationRuntimeTestDriver::stage(runtime), -1);
    QVERIFY(QFileInfo::exists(interpreter));
    QVERIFY(!QFileInfo::exists(fixture.filePath("verified-vr-1")));
    runtime.cancel();
    QVERIFY(QFileInfo::exists(interpreter));
    QVERIFY(network.hosts.isEmpty());
}

void VocalSeparationControllerTest::externalRuntimeRepairReinstallsDependencies()
{
    QTemporaryDir root;
    const QDir fixture(externalFixtureRoot(root.path()));
    QVERIFY(QDir().mkpath(fixture.path()));
    const QString interpreter = fixture.filePath(externalFixturePython());
    QVERIFY(writeBytes(interpreter, "existing interpreter fixture"));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root.path(), &network);
    const auto arguments = ExternalSeparationRuntimeTestDriver::repairExistingEnvironmentArguments(runtime);
    QCOMPARE(arguments.value(0), QStringLiteral("pip"));
    QCOMPARE(arguments.value(1), QStringLiteral("install"));
    QVERIFY(arguments.contains(QStringLiteral("--reinstall")));
    QVERIFY(!arguments.contains(QStringLiteral("venv")));
    QVERIFY(QFileInfo::exists(interpreter));
    runtime.cancel();
}

void VocalSeparationControllerTest::gpuCardDistinguishesDetectedDriverFromMissingCuda()
{
#ifdef Q_OS_MACOS
    QSKIP("CUDA is Windows-specific; Mac CoreML/CPU policy has dedicated coverage", "");
#endif
    // Read-only hardware integration: no model session, download or driver installation.
    QTemporaryDir root;
    auto options = optionsFor(root, "success", "model");
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVariantMap card;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto& value : controller.models()) {
            const auto model = value.toMap();
            if (model.value("id") == "five-stem") card = model;
        }
        return !card.value("gpuReason").toString().contains(QStringLiteral("正在"));
    })(), 10000);
    if (!card.value("gpuRuntimeConfigurable").toBool()) QSKIP("No NVIDIA driver on this test host", "");
    QVERIFY(!card.value("gpuRuntimeReady").toBool());
    QVERIFY(!card.value("gpuHardwareName").toString().isEmpty());
    QVERIFY(!card.value("gpuDriverVersion").toString().isEmpty());
    QVERIFY(card.value("gpuReason").toString().contains(card.value("gpuHardwareName").toString()));
    QVERIFY(card.value("gpuReason").toString().contains(card.value("gpuDriverVersion").toString()));
    qInfo().noquote() << card.value("gpuReason").toString();
}

void VocalSeparationControllerTest::cudaRuntimeRealCachedInstall()
{
    const QString root = qEnvironmentVariable("AGPLAYER_CUDA_RUNTIME_TEST_ROOT");
    if (root.isEmpty()) QSKIP("Opt-in: verifies and installs isolated pinned CUDA runtime", "");
    QNetworkAccessManager network;
    CudaSeparationRuntime runtime(root, &network);
    QSignalSpy changed(&runtime, &CudaSeparationRuntime::changed);
    QSignalSpy finished(&runtime, &CudaSeparationRuntime::finished);
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 90000);
    QVERIFY(runtime.start());
    runtime.pause(); QVERIFY(runtime.paused());
    QTRY_VERIFY_WITH_TIMEOUT(([&] { runtime.resume(); return !runtime.paused(); })(), 90000);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 300000);
    QVERIFY2(finished.first().first().toBool(), qPrintable(finished.first().at(1).toString()));
    QVERIFY(runtime.ready());
    QVERIFY(QFileInfo::exists(runtime.libraryPath()));
}

void VocalSeparationControllerTest::externalRuntimeRealInstallAndCachedRepair()
{
    const QString root = qEnvironmentVariable("AGPLAYER_EXTERNAL_RUNTIME_SMOKE_ROOT");
    if (root.isEmpty()) QSKIP("Opt-in: installs an isolated optional Python environment", "");
    QNetworkAccessManager network;
    ExternalSeparationRuntime runtime(root, &network);
    connect(&runtime, &ExternalSeparationRuntime::progress, &runtime,
            [](double fraction, const QString& detail) { qInfo() << fraction << detail; });
    QSignalSpy completed(&runtime, &ExternalSeparationRuntime::finished);
    QVERIFY(runtime.start());
    runtime.pause(); // Includes the cache-verification / completed-download boundary.
    QTest::qWait(250);
    QVERIFY(runtime.busy());
    QVERIFY(runtime.paused());
    runtime.resume();
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 600000);
    QVERIFY2(completed.first().first().toBool(), qPrintable(completed.first().at(1).toString()));
    QVERIFY(runtime.ready());
    completed.clear();
    QVERIFY(runtime.start()); // Existing verified environment must be reused without reinstalling it.
    QVERIFY(runtime.ready()); // Repeated configuration preserves the previously verified environment.
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 600000);
    QVERIFY2(completed.first().first().toBool(), qPrintable(completed.first().at(1).toString()));
    QVERIFY(runtime.ready());
}

void VocalSeparationControllerTest::existingPythonEnvironmentUpgradesTheBundledWorkerWithoutDownloading()
{
    QTemporaryDir temporary;
    const QString root = temporary.filePath("installed-runtime");
    const QString configuredRoot = externalFixtureRoot(root);
    const QString python = QDir(configuredRoot).filePath(externalFixturePython());
    const QString marker = QDir(configuredRoot).filePath("verified-vr-1");
    const QString worker = QDir(configuredRoot).filePath("external_separation_worker.py");
    QVERIFY(writeBytes(python, "existing interpreter - never executed by this test"));
    QVERIFY(writeBytes(marker,
                       externalFixtureMarker()));
    QVERIFY(writeBytes(worker, "# worker from the previous application version\n"));
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(root, &network);
    QFile bundled(":/separation/external_separation_worker.py");
    QFile installed(worker);
    QVERIFY(bundled.open(QIODevice::ReadOnly));
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), bundled.readAll());
    QVERIFY(runtime.ready());
    QVERIFY(!runtime.busy());
    QVERIFY(network.hosts.isEmpty());
    QFile interpreter(python);
    QVERIFY(interpreter.open(QIODevice::ReadOnly));
    QCOMPARE(interpreter.readAll(), QByteArray("existing interpreter - never executed by this test"));
    // Refuse a worker changed after construction instead of accepting the old
    // environment marker as permission to execute arbitrary stale contents.
    installed.close();
    QVERIFY(writeBytes(worker, "# stale or replaced worker\n"));
    QVERIFY(!runtime.ready());
}

void VocalSeparationControllerTest::pythonWorkerUpgradeRejectsARedirectedRuntimeRoot()
{
#ifndef Q_OS_WIN
    QSKIP("NTFS junction coverage is Windows-only", "");
#else
    QTemporaryDir temporary;
    QTemporaryDir outside;
    QVERIFY(writeBytes(outside.filePath("env/Scripts/python.exe"), "interpreter"));
    QVERIFY(writeBytes(outside.filePath("verified-vr-1"), "audio-separator=0.30.2"));
    const QString worker = outside.filePath("external_separation_worker.py");
    QVERIFY(writeBytes(worker, "outside worker must not be replaced"));
    const QString redirectedRoot = temporary.filePath("redirected-runtime");
    QVERIFY(createJunction(redirectedRoot, outside.path()));
    JunctionGuard guard(redirectedRoot);
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(redirectedRoot, &network);
    QVERIFY(!runtime.ready());
    QFile original(worker);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("outside worker must not be replaced"));
    QVERIFY(network.hosts.isEmpty());
#endif
}

void VocalSeparationControllerTest::externalRuntimeRetriesThePinnedArchiveThroughTheBackupRoute()
{
    QTemporaryDir temporary;
    UnavailableDownloadNetwork network;
    ExternalSeparationRuntime runtime(temporary.filePath("runtime"), &network);
    QSignalSpy completed(&runtime, &ExternalSeparationRuntime::finished);
    QVERIFY(runtime.start());
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 10000);
    QVERIFY(!completed.first().first().toBool());
    QVERIFY(network.hosts.contains(QStringLiteral("github.com")));
    QVERIFY(network.hosts.contains(QStringLiteral("ghfast.top")));
    QVERIFY2(completed.first().at(1).toString().contains("route timed out"),
             "Final failure must retain the transport cause, not a generic retries message");
    QVERIFY(!runtime.ready());
}

void VocalSeparationControllerTest::knownVrModelOffersExternalConfigurationWithPause()
{
    QTemporaryDir temporary;
    auto options = optionsFor(temporary, QStringLiteral("success"), QByteArray("test"));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(&preview, &waveforms, nullptr, nullptr, nullptr, options);
    const QString directory = temporary.filePath(QStringLiteral("local-vr"));
    QVERIFY(QDir().mkpath(directory));
    QFile model(QDir(directory).filePath(QStringLiteral("5_HP-Karaoke-UVR.pth")));
    QVERIFY(model.open(QIODevice::WriteOnly));
    QVERIFY(model.resize(126782699));
    model.close();
    QVERIFY(controller.selectModelDirectory(QUrl::fromLocalFile(directory)));
    QTRY_VERIFY_WITH_TIMEOUT(controller.selectModel(QStringLiteral("python-vr-5hp")), 5000);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(!controller.canStart()); // A matching filename is not an installed Python environment.
    VocalSeparationControllerTestDriver::publishGpuCandidate(controller);
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::GPU);
    QVERIFY(!controller.start()); // Still no environment; device display must already reflect the CPU adapter.
#ifdef Q_OS_MACOS
    // Mac validates CoreML/MPS per model after the optional runtime is ready.
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::GPU);
#else
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::Auto);
#endif
    QVERIFY(controller.configureRuntime(QStringLiteral("python-vr-5hp")));
    QVERIFY(controller.downloadBusy());
    QVERIFY(controller.downloadModel(QStringLiteral("python-vr-5hp")));
    QCOMPARE(VocalSeparationControllerTestDriver::sharedRuntimeDownloads(controller), 0);
    VocalSeparationControllerTestDriver::publishPythonConfigurationProgress(controller);
    QVariantMap pythonCard;
    for (const auto& value : controller.models())
        if (value.toMap().value("id") == "python-vr-5hp") pythonCard = value.toMap();
    QCOMPARE(pythonCard.value("configurationProgress").toDouble(), 0.42);
    QCOMPARE(pythonCard.value("configurationDetail").toString(), QStringLiteral("python-only-phase"));
    controller.pauseDownload();
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("python-vr-5hp")), int(VocalSeparationController::ModelState::Paused));
    controller.cancelDownload();
    QVERIFY(!controller.downloadBusy());
    QVERIFY(!controller.canStart());
}

void VocalSeparationControllerTest::automaticDemucsShowsCpuCompatibilityAndPreservesGpuForOtherModels()
{
    QTemporaryDir temporary;
    const QByteArray bytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), bytes);
    installTestModel(options, QStringLiteral("five-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    VocalSeparationController controller(nullptr, nullptr, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModel(QStringLiteral("five-stem")));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    VocalSeparationControllerTestDriver::publishGpuCandidate(controller);
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::GPU);
    QVERIFY(controller.start());
#ifndef Q_OS_MACOS
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::Auto);
#endif
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(), VocalSeparationController::JobState::Completed, 5000);
    QVERIFY(controller.availableDevices().at(2).toMap().value("available").toBool());
    QCOMPARE(controller.history().first().toMap().value("provider").toString(), QStringLiteral("cpu"));
}

void VocalSeparationControllerTest::pageProbeWaitsForVerificationAndSelectsGpu()
{
    QTemporaryDir temporary;
    const QByteArray bytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("gpu-probe"), bytes);
    installTestModel(options, QStringLiteral("two-stem"), bytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(&preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.verifyInstalledModels());
    QVERIFY(controller.probeDevices()); // Page-open request must survive the ongoing hash check.
    QTRY_COMPARE_WITH_TIMEOUT(controller.deviceMode(), VocalSeparationController::DeviceMode::GPU, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(), VocalSeparationController::JobState::Idle, 5000);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    // The probe result sets Idle before its worker finishes graceful shutdown.
    // Match the UI's start-eligibility gate instead of racing that process exit.
    QTRY_VERIFY_WITH_TIMEOUT(controller.canStart(), 5000);
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(), VocalSeparationController::JobState::Completed, 5000);
    // A real Auto-provider fallback must not leave an unavailable GPU selected,
    // otherwise the next job is disabled after the successful CPU fallback.
    QCOMPARE(controller.deviceMode(), VocalSeparationController::DeviceMode::Auto);
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
                              VocalSeparationController::JobState::JobFailed, 5000);
    QVERIFY(controller.retry());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Probing);
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Idle, 5000);
    const QVariantMap automatic = controller.availableDevices().first().toMap();
    QCOMPARE(automatic.value(QStringLiteral("mode")).toInt(),
             int(VocalSeparationController::DeviceMode::Auto));
    QVERIFY(automatic.value(QStringLiteral("available")).toBool());
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
        temporary, QStringLiteral("long-delayed-result"), modelBytes);
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.stage(), QStringLiteral("inference"), 5000);
    const QString originalInput = controller.inputInfo().value(QStringLiteral("path")).toString();
    const QString replacement = temporary.filePath(QStringLiteral("replacement.wav"));
    QVERIFY(QFile::copy(audioFixture(), replacement));
    QVERIFY(!controller.selectInput(QUrl::fromLocalFile(replacement)));
    QCOMPARE(controller.inputInfo().value(QStringLiteral("path")).toString(), originalInput);
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::Running);
    QVERIFY(!controller.selectModel(QStringLiteral("five-stem")));
    QVERIFY(!controller.setStemSelected(
        VocalSeparationController::StemKind::Vocals, false));
    QVERIFY(!controller.selectOutputFormat(QStringLiteral("flac")));
    QVERIFY(!controller.selectOutputDirectory(QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("next-output")))));
    QVERIFY(!controller.selectModelDirectory(QUrl::fromLocalFile(
        temporary.filePath(QStringLiteral("next-models")))));
    QVERIFY(!controller.selectDevice(
        VocalSeparationController::DeviceMode::CPU));
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);
}

void VocalSeparationControllerTest::
publishedStemReplacementWithJunctionIsRejectedByEveryAction()
{
#ifndef Q_OS_WIN
    QSKIP("NTFS junction coverage is Windows-only", "");
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
        if (!value.toMap().value(QStringLiteral("available")).toBool())
            continue;
        const QString name = QFileInfo(
            value.toMap().value(QStringLiteral("path")).toString()).fileName();
        QVERIFY(QFile::copy(audioFixture(), external.filePath(name)));
    }
    if (!createJunction(jobDirectory, external.path())) {
        QSKIP("This environment cannot create an NTFS directory junction", "");
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
    QSKIP("NTFS junction coverage is Windows-only", "");
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
        QSKIP("This environment cannot create an NTFS directory junction", "");
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
    // Isolate the model failure: runtime configuration is now independent and
    // deliberately continues even if an unrelated model download fails.
    QVERIFY(writeBytes(options.runtimeLibraryPath, "runtime"));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.downloadModel(QStringLiteral("two-stem")));
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("two-stem")),
        int(VocalSeparationController::ModelState::ModelFailed), 5000);
    QVERIFY(!controller.downloadBusy());
    QVERIFY(controller.downloadingModelId().isEmpty());
    QVERIFY(controller.deleteModel(QStringLiteral("two-stem")));
    QCOMPARE(modelStateFor(controller.models(), QStringLiteral("two-stem")),
             int(VocalSeparationController::ModelState::NotInstalled));
    QVERIFY(controller.downloadModel(QStringLiteral("two-stem")));
    QTRY_VERIFY_WITH_TIMEOUT(controller.downloadBusy(), 5000);
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
                              VocalSeparationController::JobState::JobFailed, 5000);
    QVERIFY(controller.canRetry());
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
                              VocalSeparationController::JobState::JobFailed, 5'000);

    const QString nextInput = temporary.filePath(QStringLiteral("输入-B.wav"));
    QVERIFY(QFile::copy(audioFixture(), nextInput));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(nextInput)));
    QVERIFY(!controller.canRetry());
    QVERIFY(QFile::remove(nextInput));
    QVERIFY(!controller.start());
    QCOMPARE(controller.jobState(), VocalSeparationController::JobState::JobFailed);
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
customSidecarManifestUsesTrustedFingerprintAndReportsRejection()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray trustedBytes("trusted-test-model");
    const QString requestMarker = temporary.filePath(
        QStringLiteral("custom-request.json"));
    const auto options = optionsFor(
        temporary, QStringLiteral("capture-payload-delayed-shutdown"),
        trustedBytes, requestMarker);
    const QString customRoot = temporary.filePath(QStringLiteral("自定义模型"));
    QVERIFY(QDir().mkpath(customRoot));
    QVERIFY(writeBytes(QDir(customRoot).filePath(QStringLiteral("renamed.onnx")),
                       trustedBytes));
    const QJsonObject trustedManifest{
        {QStringLiteral("id"), QStringLiteral("custom-two-stem")},
        {QStringLiteral("family"), QStringLiteral("MDX")},
        {QStringLiteral("stems"), QJsonArray{
             QStringLiteral("vocals"), QStringLiteral("instrumental")}},
        {QStringLiteral("files"), QJsonArray{QJsonObject{
             {QStringLiteral("name"), QStringLiteral("renamed.onnx")},
             {QStringLiteral("bytes"), trustedBytes.size()},
             {QStringLiteral("sha256"), sha256(trustedBytes)},
             {QStringLiteral("shape"), QJsonArray{1, 2, 256}},
        }}},
    };
    QVERIFY(writeBytes(
        QDir(customRoot).filePath(QStringLiteral("custom-two-stem.json")),
        QJsonDocument(trustedManifest).toJson(QJsonDocument::Compact)));

    const QByteArray unknownBytes("compatible-new-model-data");
    QVERIFY(writeBytes(QDir(customRoot).filePath(QStringLiteral("unknown.onnx")),
                       unknownBytes));
    QJsonObject rejectedManifest = trustedManifest;
    rejectedManifest.insert(QStringLiteral("id"),
                            QStringLiteral("compatible-new-profile"));
    rejectedManifest.insert(QStringLiteral("profile"),
                            QStringLiteral("uvr-mdxnet-kara"));
    rejectedManifest.insert(QStringLiteral("files"), QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("unknown.onnx")},
        {QStringLiteral("bytes"), unknownBytes.size()},
        {QStringLiteral("sha256"), sha256(unknownBytes)},
        {QStringLiteral("shape"), QJsonArray{1, 2, 256}},
    }});
    QVERIFY(writeBytes(
        QDir(customRoot).filePath(QStringLiteral("unknown-profile.json")),
        QJsonDocument(rejectedManifest).toJson(QJsonDocument::Compact)));
    QJsonObject unsupportedManifest = rejectedManifest;
    unsupportedManifest.insert(QStringLiteral("id"),
                               QStringLiteral("unsupported-profile"));
    unsupportedManifest.insert(QStringLiteral("profile"),
                               QStringLiteral("arbitrary-onnx"));
    QVERIFY(writeBytes(
        QDir(customRoot).filePath(QStringLiteral("unsupported-profile.json")),
        QJsonDocument(unsupportedManifest).toJson(QJsonDocument::Compact)));

    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModelDirectory(QUrl::fromLocalFile(customRoot)));
    QTRY_COMPARE_WITH_TIMEOUT(
        modelStateFor(controller.models(), QStringLiteral("custom-two-stem")),
        int(VocalSeparationController::ModelState::Installed), 5000);
    QVERIFY(controller.selectModel(QStringLiteral("custom-two-stem")));

    QVariantMap accepted;
    for (const QVariant& value : controller.models()) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("id")).toString()
            == QStringLiteral("custom-two-stem")) {
            accepted = model;
            break;
        }
    }
    QCOMPARE(accepted.value(QStringLiteral("origin")).toString(),
             QStringLiteral("custom"));
    QCOMPARE(accepted.value(QStringLiteral("compatibility")).toString(),
             QStringLiteral("trusted-worker-profile"));
    QVERIFY(accepted.value(QStringLiteral("profile")).toString().contains(
        QStringLiteral("mdx"), Qt::CaseInsensitive));
    QCOMPARE(accepted.value(QStringLiteral("paths")).toStringList(),
             QStringList{QDir(customRoot).filePath(QStringLiteral("renamed.onnx"))});
    QCOMPARE(accepted.value(QStringLiteral("hashes")).toStringList(),
             QStringList{sha256(trustedBytes)});
    QVERIFY(accepted.value(QStringLiteral("rejectionReason")).toString().isEmpty());

    QVariantMap compatible;
    for (const QVariant& value : controller.models()) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("id")).toString()
            == QStringLiteral("compatible-new-profile")) {
            compatible = model;
            break;
        }
    }
    QVERIFY(!compatible.isEmpty());
    QCOMPARE(compatible.value(QStringLiteral("state")).toInt(),
             int(VocalSeparationController::ModelState::Installed));
    QCOMPARE(compatible.value(QStringLiteral("profile")).toString(),
             QStringLiteral("uvr-mdxnet-kara"));
    QCOMPARE(compatible.value(QStringLiteral("hashes")).toStringList(),
             QStringList{sha256(unknownBytes)});
    QCOMPARE(modelStateFor(controller.models(),
                           QStringLiteral("unsupported-profile")),
             int(VocalSeparationController::ModelState::ModelFailed));

    QVERIFY(controller.selectModel(QStringLiteral("compatible-new-profile")));
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed,
                              5'000);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(requestMarker), 5'000);
    QFile marker(requestMarker);
    QVERIFY(marker.open(QIODevice::ReadOnly));
    const QJsonObject request =
        QJsonDocument::fromJson(marker.readAll()).object();
    QCOMPARE(request.value(QStringLiteral("modelProfile")).toString(),
             QStringLiteral("uvr-mdxnet-kara"));
    QCOMPARE(request.value(QStringLiteral("modelSha256")).toArray(),
             QJsonArray{sha256(unknownBytes)});
    QCOMPARE(request.value(QStringLiteral("modelBytes")).toArray(),
             QJsonArray{unknownBytes.size()});
}

void VocalSeparationControllerTest::
downloadFailureRetriesMirrorBeforeReportingExhaustion()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    const auto options = optionsFor(temporary, QStringLiteral("stale"),
                                    modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QSignalSpy exhausted(
        &controller, &VocalSeparationController::downloadSourcesExhausted);

    VocalSeparationControllerTestDriver::seedDownloadFailure(controller, false);
    VocalSeparationControllerTestDriver::failDownload(
        controller, QStringLiteral("official failed"));
    QCOMPARE(exhausted.count(), 0);
    QCOMPARE(controller.downloadSource(), QStringLiteral("国内镜像"));
    QVERIFY(controller.downloadBusy());

    VocalSeparationControllerTestDriver::failDownload(
        controller, QStringLiteral("mirror failed"));
    QCOMPARE(exhausted.count(), 1);
    const QVariantMap outcome = exhausted.takeFirst().at(0).toMap();
    QCOMPARE(outcome.value(QStringLiteral("modelId")).toString(),
             QStringLiteral("two-stem"));
    QCOMPARE(outcome.value(QStringLiteral("source")).toString(),
             QStringLiteral("国内镜像"));
    QCOMPARE(outcome.value(QStringLiteral("diagnostic")).toString(),
             QStringLiteral("mirror failed"));
    QVERIFY(!controller.downloadBusy());

    VocalSeparationControllerTestDriver::seedDownloadFailure(controller, true);
    VocalSeparationControllerTestDriver::cancelDownloader(controller);
    QVERIFY2(exhausted.count() == 0,
             "cancelling a download must not open backup-source UI");
}

void VocalSeparationControllerTest::downloadSafetyDiagnosticsAreLocalized()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto options = optionsFor(temporary, QStringLiteral("stale"),
                                    QByteArray("trusted-test-model"));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QSignalSpy exhausted(
        &controller, &VocalSeparationController::downloadSourcesExhausted);

    const auto verify = [&](const QString& diagnostic,
                            const QString& expected) {
        VocalSeparationControllerTestDriver::seedDownloadFailure(controller, true);
        VocalSeparationControllerTestDriver::failDownload(controller, diagnostic);
        QCOMPARE(controller.error(), expected);
        QCOMPARE(exhausted.count(), 1);
        const QVariantMap outcome = exhausted.takeFirst().at(0).toMap();
        QCOMPARE(outcome.value(QStringLiteral("diagnostic")).toString(), expected);
    };

    verify(QStringLiteral("Unsafe partial download path"),
           QStringLiteral("临时下载路径不安全，已停止下载。请更换模型目录后重试"));
    verify(QStringLiteral("Download exceeded expected size"),
           QStringLiteral("下载内容超过清单声明大小，已停止下载"));
}

void VocalSeparationControllerTest::
queuedDownloadRouteAndCancellationUseProductionControllerState()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("queued-route-model");
    const QString sourcePath = temporary.filePath(QStringLiteral("source.onnx"));
    QVERIFY(writeBytes(sourcePath, modelBytes));
    const auto options = optionsFor(temporary, QStringLiteral("stale"),
                                    modelBytes);
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QSignalSpy exhausted(
        &controller, &VocalSeparationController::downloadSourcesExhausted);

    VocalSeparationControllerTestDriver::seedQueuedRoute(
        controller,
        {QStringLiteral("queued.onnx"), QUrl::fromLocalFile(sourcePath),
         modelBytes.size(), sha256(modelBytes)},
        QUrl(QStringLiteral("https://hf-mirror.com/queued.onnx")), false);
    VocalSeparationControllerTestDriver::startQueuedDownload(controller);
    QCOMPARE(controller.downloadSource(), QStringLiteral("官方线路"));
    QVERIFY(controller.downloadBusy());

    controller.cancelDownload();
    QVERIFY(!controller.downloadBusy());
    QCOMPARE(controller.downloadProgress(), 0.0);
    QCOMPARE(exhausted.count(), 0);

    QVERIFY(controller.downloadModel(QStringLiteral("two-stem")));
    controller.cancelDownload();
    QVERIFY(!controller.downloadBusy());
    QCOMPARE(exhausted.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(
        VocalSeparationControllerTestDriver::downloadPipelineIdle(controller),
        5'000);
    QVERIFY(controller.downloadModel(QStringLiteral("two-stem")));
    controller.cancelDownload();
    QVERIFY(!controller.downloadBusy());
    QCOMPARE(exhausted.count(), 0);
}

void VocalSeparationControllerTest::
stemDownloadPublishesDirectlyToTheConfiguredOutputDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray modelBytes("trusted-test-model");
    auto options = optionsFor(temporary, QStringLiteral("success"), modelBytes);
    options.outputDirectory = temporary.filePath(QStringLiteral("direct-output"));
    QVERIFY(QDir().mkpath(options.outputDirectory));
    installTestModel(options, QStringLiteral("two-stem"), modelBytes);
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArrayLiteral("runtime")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller.start());
    QTRY_COMPARE_WITH_TIMEOUT(controller.jobState(),
                              VocalSeparationController::JobState::Completed, 5000);

    const QString source = stemFor(
        controller.stems(), VocalSeparationController::StemKind::Vocals)
                               .value(QStringLiteral("path")).toString();
    QVERIFY(controller.exportStemToOutputDirectory(
        VocalSeparationController::StemKind::Vocals));
    QVERIFY(QFileInfo::exists(QDir(options.outputDirectory)
        .filePath(QFileInfo(source).fileName())));
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
    // Result waveform analysis can still own the decoded file briefly after
    // the worker job reaches Completed. Wait for that real async owner before
    // deleting the stem used to exercise the atomic export failure path.
    WaveformProviderTestAccess::waitForAnalysis(waveforms);
    QVERIFY(QFile::remove(accompaniment));
    const QString failedRoot = temporary.filePath(QStringLiteral("失败导出"));
    QVERIFY(QDir().mkpath(failedRoot));
    QVERIFY(!controller.exportSelected(QUrl::fromLocalFile(failedRoot)));
    QCOMPARE(QDir(failedRoot).entryList(
                 QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
}

void VocalSeparationControllerTest::
exportAllPublishesEveryAvailableStemRegardlessOfSelection()
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
    QVERIFY(controller.setStemSelected(
        VocalSeparationController::StemKind::Accompaniment, false));

    const QString exportRoot = temporary.filePath(QStringLiteral("全部音轨"));
    QVERIFY(QDir().mkpath(exportRoot));
    QVERIFY(controller.exportAll(QUrl::fromLocalFile(exportRoot)));
    const QFileInfoList published = QDir(exportRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(published.size(), 1);
    QCOMPARE(QDir(published.first().absoluteFilePath()).entryList(
                 QDir::Files | QDir::NoDotAndDotDot).size(), 2);
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
    // The result waveform provider may still be decoding this stem after the
    // controller reports Completed. Release that real async reader before
    // deleting the file used to simulate an unsafe playlist subset.
    WaveformProviderTestAccess::waitForAnalysis(waveforms);
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

void VocalSeparationControllerTest::
destructionBeforeImportCompletionLeavesPlaylistUnchanged()
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
    auto controller = std::make_unique<VocalSeparationController>(
        &preview, &waveforms, &library, &importer, &playlists, options);
    QVERIFY(controller->selectInput(QUrl::fromLocalFile(audioFixture())));
    QVERIFY(controller->start());
    QTRY_COMPARE_WITH_TIMEOUT(controller->jobState(),
                              VocalSeparationController::JobState::Completed, 5'000);
    const QString vocals = stemFor(
        controller->stems(), VocalSeparationController::StemKind::Vocals)
                                 .value(QStringLiteral("path")).toString();
    importer.importPaths({vocals});
    QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 5'000);
    const int vocalsRow = library.indexForLocalFile(vocals);
    QVERIFY(vocalsRow >= 0);
    const QString vocalsId = library.data(
        library.index(vocalsRow), LibraryModel::TrackIdRole).toString();
    const QString playlistId = playlists.createPlaylist(
        QStringLiteral("destroy-before-import-finished"));

    QVERIFY(controller->addSelectedToPlaylist(playlistId));
    QVERIFY(importer.busy());
    controller.reset();

    QVERIFY(!playlists.containsTrack(playlistId, vocalsId));
    QCOMPARE(playlists.trackIdsForPlaylist(playlistId), QStringList{});
    QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 5'000);
}

void VocalSeparationControllerTest::unicodeLongPathsWorkThroughHistoryAndExport()
{
#ifndef Q_OS_WIN
    QSKIP("Windows long-path coverage", "");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString longPart;
    while (longPart.size() < 285)
        longPart += QStringLiteral("超长目录段0123456789/");
    const QString longRoot = QDir(temporary.path()).filePath(longPart);
    if (!QDir().mkpath(longRoot))
        QSKIP("The active Windows filesystem/runtime cannot create a >260 Unicode path", "");
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

void VocalSeparationControllerTest::runtimeCanBeConfiguredWithoutCatalogModelLookup()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto options = optionsFor(temporary, QStringLiteral("stale"),
                                    QByteArray("model"));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(
        &preview, &waveforms, nullptr, nullptr, nullptr, options);

    QVERIFY(controller.configureRuntime(QString{}));
    QVERIFY(VocalSeparationControllerTestDriver::runtimeOnlyDownloadActive(
        controller));
    QCOMPARE(controller.downloadSource(), QStringLiteral("官方线路"));

    controller.cancelDownload();
    QVERIFY(!controller.downloadBusy());
}

void VocalSeparationControllerTest::configuredRuntimeDoesNotPretendUnknownModelsAreConfigured()
{
    QTemporaryDir temporary;
    const auto options = optionsFor(temporary, QStringLiteral("stale"), QByteArray("model"));
    QVERIFY(writeBytes(options.runtimeLibraryPath, QByteArray("runtime")));
    const QString root = temporary.filePath(QStringLiteral("models"));
    QVERIFY(writeBytes(QDir(root).filePath(QStringLiteral("unknown.onnx")), QByteArray("unknown")));
    AudioPreviewController preview(AG_AUDIO_BACKEND_NULL);
    WaveformProvider waveforms;
    VocalSeparationController controller(&preview, &waveforms, nullptr, nullptr, nullptr, options);
    QVERIFY(controller.selectModelDirectory(QUrl::fromLocalFile(root)));
    QTRY_VERIFY_WITH_TIMEOUT(controller.models().size() > options.catalog.size(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(VocalSeparationControllerTestDriver::downloadPipelineIdle(controller), 3000);
    const QVariantMap unknown = controller.models().last().toMap();
    QVERIFY(!controller.configureRuntime(unknown.value(QStringLiteral("id")).toString()));
    QVERIFY(controller.error().contains(QStringLiteral("张量与频谱配置")));
    QVERIFY(!controller.downloadBusy());
}

QTEST_MAIN(VocalSeparationControllerTest)
#include "vocal_separation_controller_test.moc"
