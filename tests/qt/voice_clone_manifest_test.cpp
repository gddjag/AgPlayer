#include "voice_clone_registry.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <array>

using agplayer::voice_clone::VoiceCloneRegistry;

namespace {

constexpr auto kEmptySha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

QString forbiddenProductName()
{
    return QStringLiteral("Vibe") + QStringLiteral("Voice");
}

QJsonObject builtInModel(const QString& stableId,
                         const QString& description,
                         const QString& adapterId,
                         const QString& runtimeId,
                         const bool requiresLicenseAcceptance,
                         const QString& licenseName,
                         const QString& projectUrl,
                         const QString& huggingFaceUrl,
                         const QString& modelScopeUrl)
{
    QString revision;
    QString licenseUrl;
    QString licenseRevision;
    if (stableId == QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")) {
        revision = QStringLiteral("5d83992436eae1d760afd27aff78a71d676296fc");
        licenseRevision = QStringLiteral("022e286b98fbec7e1e916cb940cdf532cd9f488e");
        licenseUrl = QStringLiteral("https://github.com/QwenLM/Qwen3-TTS/blob/%1/LICENSE")
                         .arg(licenseRevision);
    } else if (stableId == QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base")) {
        revision = QStringLiteral("fd4b254389122332181a7c3db7f27e918eec64e3");
        licenseRevision = QStringLiteral("022e286b98fbec7e1e916cb940cdf532cd9f488e");
        licenseUrl = QStringLiteral("https://github.com/QwenLM/Qwen3-TTS/blob/%1/LICENSE")
                         .arg(licenseRevision);
    } else if (stableId == QStringLiteral("IndexTeam/IndexTTS-2.5")) {
        revision = QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63");
        licenseRevision = revision;
        licenseUrl = QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/%1/LICENSE")
                         .arg(revision);
    } else {
        revision = QStringLiteral("29e01c4e8d000f4bcd70751be16fa94bf3d85a18");
        licenseRevision = QStringLiteral("074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc");
        licenseUrl = QStringLiteral("https://github.com/FunAudioLLM/CosyVoice/blob/%1/LICENSE")
                         .arg(licenseRevision);
    }
    return {
        {QStringLiteral("stableId"), stableId},
        {QStringLiteral("displayName"), stableId},
        {QStringLiteral("description"), description},
        {QStringLiteral("provider"), QStringLiteral("Official provider")},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("runtimeId"), runtimeId},
        {QStringLiteral("stable"), true},
        {QStringLiteral("requiresLicenseAcceptance"), requiresLicenseAcceptance},
        {QStringLiteral("license"),
         QJsonObject{{QStringLiteral("name"), licenseName},
                     {QStringLiteral("url"), licenseUrl},
                     {QStringLiteral("revision"), licenseRevision}}},
        {QStringLiteral("officialUrls"),
         QJsonObject{{QStringLiteral("project"), projectUrl},
                     {QStringLiteral("huggingFace"), huggingFaceUrl},
                     {QStringLiteral("modelScope"), modelScopeUrl}}},
        {QStringLiteral("capabilityPreview"),
         QJsonArray{QStringLiteral("voice-clone")}},
    };
}

QJsonObject approvedBuiltInRegistry()
{
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("models"),
         QJsonArray{
             builtInModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
                          QStringLiteral("Relative lightweight multilingual voice cloning model."),
                          QStringLiteral("qwen"),
                          QStringLiteral("qwen"),
                          false,
                          QStringLiteral("Apache-2.0"),
                          QStringLiteral("https://github.com/QwenLM/Qwen3-TTS"),
                          QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
                          QStringLiteral("https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-0.6B-Base")),
             builtInModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
                          QStringLiteral("Higher-quality multilingual voice cloning model."),
                          QStringLiteral("qwen"),
                          QStringLiteral("qwen"),
                          false,
                          QStringLiteral("Apache-2.0"),
                          QStringLiteral("https://github.com/QwenLM/Qwen3-TTS"),
                          QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
                          QStringLiteral("https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-1.7B-Base")),
             builtInModel(QStringLiteral("IndexTeam/IndexTTS-2.5"),
                          QStringLiteral("Experimental expressive multilingual voice cloning model."),
                          QStringLiteral("indextts25"),
                          QStringLiteral("indextts25"),
                          true,
                          QStringLiteral("bilibili Model Use License Agreement"),
                          QStringLiteral("https://github.com/index-tts/index-tts"),
                          QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5"),
                          QStringLiteral("https://modelscope.cn/models/IndexTeam/IndexTTS-2.5")),
             builtInModel(QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
                          QStringLiteral("Multilingual and Chinese dialect voice cloning model."),
                          QStringLiteral("cosyvoice3"),
                          QStringLiteral("cosyvoice3"),
                          false,
                          QStringLiteral("Apache-2.0"),
                          QStringLiteral("https://github.com/FunAudioLLM/CosyVoice"),
                          QStringLiteral("https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
                          QStringLiteral("https://www.modelscope.cn/models/FunAudioLLM/Fun-CosyVoice3-0.5B-2512")),
         }},
    };
}

QString writeJson(const QString& path, const QJsonObject& object)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return path;
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString localManifestPath(const QString& root, const QString& directory = QStringLiteral("local-qwen"))
{
    return QDir(root).filePath(
        QStringLiteral("models/voice-clone/%1/2026.08.13/agplayer-model.json").arg(directory));
}

QString copyLocalManifest(const QString& root, const QString& directory = QStringLiteral("local-qwen"))
{
    const QString destination = localManifestPath(root, directory);
    QDir().mkpath(QFileInfo(destination).absolutePath());
    if (!QFile::copy(QStringLiteral(AGPLAYER_VOICE_CLONE_FIXTURES_DIR "/models/local-qwen/agplayer-model.json"),
                     destination)) {
        return {};
    }
    const QDir modelDirectory = QFileInfo(destination).dir();
    QFile config(modelDirectory.filePath(QStringLiteral("config.json")));
    if (!config.open(QIODevice::WriteOnly)) return {};
    config.close();
    QFile weights(modelDirectory.filePath(QStringLiteral("model.safetensors")));
    if (!weights.open(QIODevice::WriteOnly)) return {};
    weights.close();
    return destination;
}

void writeManifest(const QString& path, const QJsonObject& manifest)
{
    QVERIFY2(!writeJson(path, manifest).isEmpty(), qPrintable(path));
}

} // namespace

class VoiceCloneManifestTest final : public QObject {
    Q_OBJECT

private slots:
    void builtInRegistryContainsOnlyApprovedStableModels();
    void discoversAValidLocalQwenManifest();
    void verifiesHashesBeforeReportingReady();
    void rejectsDuplicateUnknownAndIncompleteLocalManifests();
    void rejectsUnknownAndExecutableManifestFields_data();
    void rejectsUnknownAndExecutableManifestFields();
    void rejectsExecutableFileEntries_data();
    void rejectsExecutableFileEntries();
    void rejectsAbsoluteTraversalAndLinkedPaths();
    void marksUnhashedLocalModelsAsLocalUnverified();
    void appliesIndexLicenseGateToLocalModels();
    void documentationExampleUsesProductionLayoutAndParser();
    void mergesBuiltInAndUserLayersWithoutReplacement();
    void rejectsUnofficialLocalUrls_data();
    void rejectsUnofficialLocalUrls();
    void rejectsInvalidReferenceAudioRules_data();
    void rejectsInvalidReferenceAudioRules();
    void requiresBuiltInUiMetadata_data();
    void requiresBuiltInUiMetadata();
};

void VoiceCloneManifestTest::documentationExampleUsesProductionLayoutAndParser()
{
    QFile documentation(QStringLiteral(AGPLAYER_VOICE_CLONE_ADDING_MODELS_DOC));
    QVERIFY(documentation.open(QIODevice::ReadOnly));
    const QByteArray markdown = documentation.readAll();
    const qsizetype opening = markdown.indexOf("```json");
    const qsizetype jsonStart = opening < 0 ? -1 : markdown.indexOf('\n', opening);
    const qsizetype closing = jsonStart < 0 ? -1 : markdown.indexOf("```", jsonStart + 1);
    QVERIFY(opening >= 0 && jsonStart >= 0 && closing > jsonStart);
    QJsonParseError parseError;
    const QJsonDocument example = QJsonDocument::fromJson(
        markdown.mid(jsonStart + 1, closing - jsonStart - 1), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(example.isObject());

    QTemporaryDir portable;
    QVERIFY(portable.isValid());
    const QString modelDirectory = portable.filePath(
        QStringLiteral("models/voice-clone/local-qwen/2026.08.14"));
    QVERIFY(QDir().mkpath(modelDirectory));
    for (const QJsonValue& value : example.object().value(QStringLiteral("files")).toArray()) {
        const QString relative = value.toObject().value(QStringLiteral("path")).toString();
        QFile file(QDir(modelDirectory).filePath(relative));
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QVERIFY(!writeJson(QDir(modelDirectory).filePath(QStringLiteral("agplayer-model.json")),
                       example.object()).isEmpty());
    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        portable.path(), {QStringLiteral("qwen"), QStringLiteral("indextts25"),
                          QStringLiteral("cosyvoice3")});
    QVERIFY2(discovery.isValid(), qPrintable(discovery.errorString()));
    QCOMPARE(discovery.models.size(), 1);
    QCOMPARE(discovery.models.front().stableId, QStringLiteral("local/qwen-demo"));
}

void VoiceCloneManifestTest::builtInRegistryContainsOnlyApprovedStableModels()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QJsonObject approvedRegistry = approvedBuiltInRegistry();
    const QString registryPath = writeJson(temporary.filePath(QStringLiteral("built-in.json")),
                                           approvedRegistry);
    QVERIFY(!registryPath.isEmpty());
    QFile rawRegistryFile(registryPath);
    QVERIFY(rawRegistryFile.open(QIODevice::ReadOnly));
    const QByteArray rawRegistry = rawRegistryFile.readAll();
    rawRegistryFile.close();
    QVERIFY(!QString::fromUtf8(rawRegistry).contains(forbiddenProductName(),
                                                     Qt::CaseInsensitive));

    const VoiceCloneRegistry registry = VoiceCloneRegistry::loadBuiltIn(registryPath);
    QVERIFY2(registry.isValid(), qPrintable(registry.errorString()));
    QCOMPARE(registry.modelIds(),
             QStringList({QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
                          QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
                          QStringLiteral("IndexTeam/IndexTTS-2.5"),
                          QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512")}));

    struct ExpectedModel {
        const char* stableId;
        const char* adapterId;
        const char* runtimeId;
        bool licenseGate;
    };
    constexpr std::array<ExpectedModel, 4> expectedModels{{
        {"Qwen/Qwen3-TTS-12Hz-0.6B-Base", "qwen", "qwen", false},
        {"Qwen/Qwen3-TTS-12Hz-1.7B-Base", "qwen", "qwen", false},
        {"IndexTeam/IndexTTS-2.5", "indextts25", "indextts25", true},
        {"FunAudioLLM/Fun-CosyVoice3-0.5B-2512", "cosyvoice3", "cosyvoice3", false},
    }};
    for (const ExpectedModel& expected : expectedModels) {
        const auto model = registry.model(QString::fromLatin1(expected.stableId));
        QVERIFY(model.stable);
        QCOMPARE(model.adapterId, QString::fromLatin1(expected.adapterId));
        QCOMPARE(model.runtimeId, QString::fromLatin1(expected.runtimeId));
        QCOMPARE(model.requiresLicenseAcceptance, expected.licenseGate);
        QVERIFY(!model.description.trimmed().isEmpty());
        QVERIFY(model.officialProjectUrl.startsWith(QStringLiteral("https://")));
        QVERIFY(model.huggingFaceUrl.startsWith(QStringLiteral("https://")));
        QVERIFY(model.modelScopeUrl.startsWith(QStringLiteral("https://")));
    }

    QJsonObject unapproved = approvedBuiltInRegistry();
    QJsonArray models = unapproved.value(QStringLiteral("models")).toArray();
    QJsonObject firstModel = models[0].toObject();
    firstModel.insert(QStringLiteral("description"),
                      QStringLiteral("Migrated from ") + forbiddenProductName());
    models[0] = firstModel;
    unapproved.insert(QStringLiteral("models"), models);
    const VoiceCloneRegistry rejected = VoiceCloneRegistry::loadBuiltIn(
        writeJson(temporary.filePath(QStringLiteral("unapproved.json")), unapproved));
    QVERIFY(!rejected.isValid());

    QJsonObject invalidUrl = approvedBuiltInRegistry();
    models = invalidUrl.value(QStringLiteral("models")).toArray();
    QJsonObject first = models[0].toObject();
    QJsonObject urls = first.value(QStringLiteral("officialUrls")).toObject();
    urls.insert(QStringLiteral("project"), QStringLiteral("https://example.invalid/project"));
    first.insert(QStringLiteral("officialUrls"), urls);
    models[0] = first;
    invalidUrl.insert(QStringLiteral("models"), models);
    QVERIFY(!VoiceCloneRegistry::loadBuiltIn(
                 writeJson(temporary.filePath(QStringLiteral("invalid-url.json")), invalidUrl))
                 .isValid());

    QJsonObject wrongLicenseGate = approvedBuiltInRegistry();
    models = wrongLicenseGate.value(QStringLiteral("models")).toArray();
    QJsonObject index = models[2].toObject();
    index.insert(QStringLiteral("requiresLicenseAcceptance"), false);
    models[2] = index;
    wrongLicenseGate.insert(QStringLiteral("models"), models);
    QVERIFY(!VoiceCloneRegistry::loadBuiltIn(
                 writeJson(temporary.filePath(QStringLiteral("invalid-license-gate.json")), wrongLicenseGate))
                 .isValid());

    for (const int modelIndex : {0, 1, 3}) {
        QJsonObject unexpectedGate = approvedBuiltInRegistry();
        models = unexpectedGate.value(QStringLiteral("models")).toArray();
        QJsonObject nonIndexModel = models[modelIndex].toObject();
        nonIndexModel.insert(QStringLiteral("requiresLicenseAcceptance"), true);
        models[modelIndex] = nonIndexModel;
        unexpectedGate.insert(QStringLiteral("models"), models);
        const QString path = temporary.filePath(
            QStringLiteral("unexpected-license-gate-%1.json").arg(modelIndex));
        QVERIFY(!VoiceCloneRegistry::loadBuiltIn(writeJson(path, unexpectedGate)).isValid());
    }

    QJsonObject wrongRuntime = approvedBuiltInRegistry();
    models = wrongRuntime.value(QStringLiteral("models")).toArray();
    first = models[0].toObject();
    first.insert(QStringLiteral("runtimeId"), QStringLiteral("wrong-runtime"));
    models[0] = first;
    wrongRuntime.insert(QStringLiteral("models"), models);
    QVERIFY(!VoiceCloneRegistry::loadBuiltIn(
                 writeJson(temporary.filePath(QStringLiteral("invalid-runtime.json")), wrongRuntime))
                 .isValid());
}

void VoiceCloneManifestTest::discoversAValidLocalQwenManifest()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(!copyLocalManifest(temporary.path()).isEmpty());

    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen"), QStringLiteral("indextts25"), QStringLiteral("cosyvoice3")});
    QVERIFY2(discovery.isValid(), qPrintable(discovery.errorString()));
    QCOMPARE(discovery.models.size(), 1);
    QCOMPARE(discovery.models.front().stableId, QStringLiteral("local/qwen-demo"));
    QCOMPARE(discovery.models.front().adapterId, QStringLiteral("qwen"));
    QCOMPARE(discovery.models.front().runtimeId, QStringLiteral("qwen"));
    QCOMPARE(discovery.models.front().installState, QStringLiteral("ready"));
}

void VoiceCloneManifestTest::verifiesHashesBeforeReportingReady()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());

    auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY2(discovery.isValid(), qPrintable(discovery.errorString()));
    QCOMPARE(discovery.models.size(), 1);
    QCOMPARE(discovery.models.front().installState, QStringLiteral("ready"));

    QFile tamperedFile(QFileInfo(manifestPath).dir().filePath(
        QStringLiteral("model.safetensors")));
    QVERIFY(tamperedFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(tamperedFile.write("tampered-model-data"), qint64{19});
    tamperedFile.close();

    discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid()
            || (discovery.models.size() == 1
                && discovery.models.front().installState != QStringLiteral("ready")));
}

void VoiceCloneManifestTest::rejectsDuplicateUnknownAndIncompleteLocalManifests()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString first = copyLocalManifest(temporary.path(), QStringLiteral("first"));
    const QString duplicate = copyLocalManifest(temporary.path(), QStringLiteral("duplicate"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!duplicate.isEmpty());

    auto discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));

    QFile::remove(duplicate);
    QJsonObject unknownAdapter = readJson(first);
    unknownAdapter.insert(QStringLiteral("adapterId"), QStringLiteral("untrusted-adapter"));
    writeManifest(first, unknownAdapter);
    discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("adapter"), Qt::CaseInsensitive));

    QJsonObject codeInjection = readJson(first);
    codeInjection.insert(QStringLiteral("adapterId"), QStringLiteral("qwen"));
    codeInjection.insert(QStringLiteral("command"), QStringLiteral("untrusted-worker.exe"));
    writeManifest(first, codeInjection);
    discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("command"), Qt::CaseInsensitive));

    QJsonObject missingFile = readJson(first);
    missingFile.remove(QStringLiteral("command"));
    QJsonArray files = missingFile.value(QStringLiteral("files")).toArray();
    files.append(QJsonObject{{QStringLiteral("path"), QStringLiteral("tokenizer.json")},
                             {QStringLiteral("sha256"), QLatin1String(kEmptySha256)}});
    missingFile.insert(QStringLiteral("files"), files);
    writeManifest(first, missingFile);
    discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::rejectsUnknownAndExecutableManifestFields_data()
{
    QTest::addColumn<QString>("fieldName");
    QTest::newRow("unknown") << QStringLiteral("futureField");
    QTest::newRow("command") << QStringLiteral("command");
    QTest::newRow("script") << QStringLiteral("script");
    QTest::newRow("executable") << QStringLiteral("executable");
    QTest::newRow("launcher") << QStringLiteral("launcher");
}

void VoiceCloneManifestTest::rejectsUnknownAndExecutableManifestFields()
{
    QFETCH(QString, fieldName);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());
    QJsonObject manifest = readJson(manifestPath);
    manifest.insert(fieldName, QStringLiteral("untrusted-content"));
    writeManifest(manifestPath, manifest);

    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(fieldName, Qt::CaseInsensitive)
            || discovery.errorString().contains(QStringLiteral("unknown"), Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::rejectsExecutableFileEntries_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::newRow("executable") << QStringLiteral("worker.exe");
    QTest::newRow("launcher") << QStringLiteral("launcher.bat");
    QTest::newRow("script") << QStringLiteral("install.ps1");
}

void VoiceCloneManifestTest::rejectsExecutableFileEntries()
{
    QFETCH(QString, fileName);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());
    QJsonObject manifest = readJson(manifestPath);
    QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    files[0] = QJsonObject{{QStringLiteral("path"), fileName},
                           {QStringLiteral("sha256"), QLatin1String(kEmptySha256)}};
    manifest.insert(QStringLiteral("files"), files);
    writeManifest(manifestPath, manifest);
    QFile rejectedFile(QFileInfo(manifestPath).dir().filePath(fileName));
    QVERIFY(rejectedFile.open(QIODevice::WriteOnly));
    rejectedFile.close();

    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(fileName, Qt::CaseInsensitive)
            || discovery.errorString().contains(QStringLiteral("executable"), Qt::CaseInsensitive)
            || discovery.errorString().contains(QStringLiteral("data"), Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::rejectsAbsoluteTraversalAndLinkedPaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());

    QJsonObject manifest = readJson(manifestPath);
    QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    files[0] = QJsonObject{{QStringLiteral("path"), QDir::tempPath() + QStringLiteral("/config.json")},
                           {QStringLiteral("sha256"), QLatin1String(kEmptySha256)}};
    manifest.insert(QStringLiteral("files"), files);
    writeManifest(manifestPath, manifest);
    auto discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("absolute"), Qt::CaseInsensitive));

    files[0] = QJsonObject{{QStringLiteral("path"), QStringLiteral("../config.json")},
                           {QStringLiteral("sha256"), QLatin1String(kEmptySha256)}};
    manifest.insert(QStringLiteral("files"), files);
    writeManifest(manifestPath, manifest);
    discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("..")));

#ifdef Q_OS_WIN
    const QString modelDirectory = QFileInfo(manifestPath).dir().absolutePath();
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const QString outsideDirectory = QDir::fromNativeSeparators(
        QFileInfo(outside.path()).canonicalFilePath());
    const QString scanRoot = QDir::fromNativeSeparators(
        QFileInfo(temporary.path()).canonicalFilePath());
    QVERIFY(!outsideDirectory.isEmpty());
    QVERIFY(!scanRoot.isEmpty());
    QVERIFY(!outsideDirectory.startsWith(scanRoot + QLatin1Char('/'),
                                         Qt::CaseInsensitive));
    QFile outsideFile(QDir(outsideDirectory).filePath(QStringLiteral("escaped.json")));
    QVERIFY(outsideFile.open(QIODevice::WriteOnly));
    outsideFile.close();
    const QString junction = QDir(modelDirectory).filePath(QStringLiteral("escape"));
    QCOMPARE(QProcess::execute(QStringLiteral("cmd.exe"),
                               {QStringLiteral("/d"),
                                QStringLiteral("/c"),
                                QStringLiteral("mklink"),
                                QStringLiteral("/J"),
                                QDir::toNativeSeparators(junction),
                                QDir::toNativeSeparators(outsideDirectory)}),
             0);
    files[0] = QJsonObject{{QStringLiteral("path"), QStringLiteral("escape/escaped.json")},
                           {QStringLiteral("sha256"), QLatin1String(kEmptySha256)}};
    manifest.insert(QStringLiteral("files"), files);
    writeManifest(manifestPath, manifest);
    discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("link"), Qt::CaseInsensitive)
            || discovery.errorString().contains(QStringLiteral("junction"), Qt::CaseInsensitive));
#else
    QSKIP("Junction escape is a Windows-only contract.");
#endif
}

void VoiceCloneManifestTest::marksUnhashedLocalModelsAsLocalUnverified()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());
    QJsonObject manifest = readJson(manifestPath);
    QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    for (QJsonValueRef file : files) {
        QJsonObject object = file.toObject();
        object.remove(QStringLiteral("sha256"));
        file = object;
    }
    manifest.insert(QStringLiteral("files"), files);
    writeManifest(manifestPath, manifest);

    const auto discovery = VoiceCloneRegistry::discoverUserModels(temporary.path(), {QStringLiteral("qwen")});
    QVERIFY2(discovery.isValid(), qPrintable(discovery.errorString()));
    QCOMPARE(discovery.models.size(), 1);
    QCOMPARE(discovery.models.front().installState, QStringLiteral("local-unverified"));
}

void VoiceCloneManifestTest::appliesIndexLicenseGateToLocalModels()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());
    QJsonObject manifest = readJson(manifestPath);
    manifest.insert(QStringLiteral("adapterId"), QStringLiteral("indextts25"));
    manifest.insert(QStringLiteral("runtimeId"), QStringLiteral("indextts25"));
    manifest.insert(QStringLiteral("source"),
                    QJsonObject{{QStringLiteral("provider"), QStringLiteral("Hugging Face")},
                                {QStringLiteral("url"),
                                 QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5")}});
    manifest.insert(QStringLiteral("license"),
                    QJsonObject{{QStringLiteral("name"),
                                 QStringLiteral("bilibili Model Use License Agreement")},
                                {QStringLiteral("url"),
                                 QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE")},
                                {QStringLiteral("revision"),
                                 QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63")}});
    writeManifest(manifestPath, manifest);

    const auto missingIdentities = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("indextts25")});
    QVERIFY(!missingIdentities.isValid());
    QVERIFY(missingIdentities.errorString().contains(QStringLiteral("license"),
                                                      Qt::CaseInsensitive));

    const QJsonObject bilibili{
        {QStringLiteral("id"), QStringLiteral("bilibili-model-use-license")},
        {QStringLiteral("name"), QStringLiteral("bilibili Model Use License Agreement")},
        {QStringLiteral("url"), QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE")},
        {QStringLiteral("revision"), QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63")},
        {QStringLiteral("spdx"), QStringLiteral("LicenseRef-Bilibili-Model-Use")},
        {QStringLiteral("requiredAcceptance"), true},
        {QStringLiteral("useRestriction"), QStringLiteral("custom-terms")}};
    const QJsonObject maskGct{
        {QStringLiteral("id"), QStringLiteral("maskgct-cc-by-nc-4.0")},
        {QStringLiteral("name"), QStringLiteral("CC-BY-NC-4.0")},
        {QStringLiteral("url"), QStringLiteral("https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md")},
        {QStringLiteral("revision"), QStringLiteral("265c6cef07625665d0c28d2faafb1415562379dc")},
        {QStringLiteral("spdx"), QStringLiteral("CC-BY-NC-4.0")},
        {QStringLiteral("requiredAcceptance"), true},
        {QStringLiteral("useRestriction"), QStringLiteral("non-commercial-only")}};
    manifest.insert(QStringLiteral("licenses"), QJsonArray{bilibili, maskGct});
    writeManifest(manifestPath, manifest);
    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("indextts25")});
    QVERIFY2(discovery.isValid(), qPrintable(discovery.errorString()));
    QCOMPARE(discovery.models.size(), 1);
    QVERIFY(discovery.models.front().requiresLicenseAcceptance);
    QCOMPARE(discovery.models.front().licenseRevision,
             QStringLiteral("c39ce5ba981572cb187443877ff559dfb246ce63"));

    QJsonObject extraLicense = bilibili;
    extraLicense.insert(QStringLiteral("id"), QStringLiteral("extra-notice"));
    extraLicense.insert(QStringLiteral("requiredAcceptance"), false);
    QJsonObject extraLicenseManifest = manifest;
    extraLicenseManifest.insert(QStringLiteral("licenses"),
                                QJsonArray{bilibili, maskGct, extraLicense});
    writeManifest(manifestPath, extraLicenseManifest);
    const auto extraRejected = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("indextts25")});
    QVERIFY(!extraRejected.isValid());
    QVERIFY(extraRejected.errorString().contains(QStringLiteral("license"),
                                                  Qt::CaseInsensitive));

    QJsonObject wrongLicense = manifest;
    QJsonArray wrongLicenses = wrongLicense.value(QStringLiteral("licenses")).toArray();
    QJsonObject wrongMask = wrongLicenses[1].toObject();
    wrongMask.insert(QStringLiteral("revision"), QStringLiteral("wrong"));
    wrongLicenses[1] = wrongMask;
    wrongLicense.insert(QStringLiteral("licenses"), wrongLicenses);
    writeManifest(manifestPath, wrongLicense);
    const auto rejected = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("indextts25")});
    QVERIFY(!rejected.isValid());
    QVERIFY(rejected.errorString().contains(QStringLiteral("license"), Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::mergesBuiltInAndUserLayersWithoutReplacement()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString registryPath = writeJson(temporary.filePath(QStringLiteral("built-in.json")),
                                           approvedBuiltInRegistry());
    const VoiceCloneRegistry builtIn = VoiceCloneRegistry::loadBuiltIn(registryPath);
    QVERIFY2(builtIn.isValid(), qPrintable(builtIn.errorString()));
    QVERIFY(!copyLocalManifest(temporary.path()).isEmpty());
    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY2(discovery.isValid(), qPrintable(discovery.errorString()));

    const VoiceCloneRegistry merged = builtIn.mergeUserModels(discovery);
    QVERIFY2(merged.isValid(), qPrintable(merged.errorString()));
    QCOMPARE(merged.modelIds().size(), 5);
    QVERIFY(merged.modelIds().contains(QStringLiteral("local/qwen-demo")));

    auto replacement = discovery;
    replacement.models.front().stableId = QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base");
    const VoiceCloneRegistry rejected = builtIn.mergeUserModels(replacement);
    QVERIFY(!rejected.isValid());
    QVERIFY(rejected.errorString().contains(QStringLiteral("replace"), Qt::CaseInsensitive)
            || rejected.errorString().contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::rejectsUnofficialLocalUrls_data()
{
    QTest::addColumn<QString>("objectName");
    QTest::addColumn<QString>("url");
    QTest::newRow("community source")
        << QStringLiteral("source")
        << QStringLiteral("https://huggingface.co/community/untrusted-model");
    QTest::newRow("community license")
        << QStringLiteral("license")
        << QStringLiteral("https://github.com/community/untrusted-license");
    QTest::newRow("lookalike host")
        << QStringLiteral("source")
        << QStringLiteral("https://huggingface.co.evil.example/Qwen/model");
}

void VoiceCloneManifestTest::rejectsUnofficialLocalUrls()
{
    QFETCH(QString, objectName);
    QFETCH(QString, url);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());
    QJsonObject manifest = readJson(manifestPath);
    QJsonObject link = manifest.value(objectName).toObject();
    link.insert(QStringLiteral("url"), url);
    manifest.insert(objectName, link);
    writeManifest(manifestPath, manifest);

    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(objectName, Qt::CaseInsensitive)
            || discovery.errorString().contains(QStringLiteral("official"), Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::rejectsInvalidReferenceAudioRules_data()
{
    QTest::addColumn<QJsonObject>("rules");
    QTest::newRow("minimum is string")
        << QJsonObject{{QStringLiteral("minimumSeconds"), QStringLiteral("3")},
                       {QStringLiteral("maximumSeconds"), 30},
                       {QStringLiteral("extensions"), QJsonArray{QStringLiteral("wav")}}};
    QTest::newRow("negative minimum")
        << QJsonObject{{QStringLiteral("minimumSeconds"), -1},
                       {QStringLiteral("maximumSeconds"), 30},
                       {QStringLiteral("extensions"), QJsonArray{QStringLiteral("wav")}}};
    QTest::newRow("reversed duration")
        << QJsonObject{{QStringLiteral("minimumSeconds"), 30},
                       {QStringLiteral("maximumSeconds"), 3},
                       {QStringLiteral("extensions"), QJsonArray{QStringLiteral("wav")}}};
    QTest::newRow("unreasonable maximum")
        << QJsonObject{{QStringLiteral("minimumSeconds"), 3},
                       {QStringLiteral("maximumSeconds"), 3600},
                       {QStringLiteral("extensions"), QJsonArray{QStringLiteral("wav")}}};
    QTest::newRow("empty extensions")
        << QJsonObject{{QStringLiteral("minimumSeconds"), 3},
                       {QStringLiteral("maximumSeconds"), 30},
                       {QStringLiteral("extensions"), QJsonArray{}}};
    QTest::newRow("non-string extension")
        << QJsonObject{{QStringLiteral("minimumSeconds"), 3},
                       {QStringLiteral("maximumSeconds"), 30},
                       {QStringLiteral("extensions"), QJsonArray{QStringLiteral("wav"), 7}}};
}

void VoiceCloneManifestTest::rejectsInvalidReferenceAudioRules()
{
    QFETCH(QJsonObject, rules);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString manifestPath = copyLocalManifest(temporary.path());
    QVERIFY(!manifestPath.isEmpty());
    QJsonObject manifest = readJson(manifestPath);
    manifest.insert(QStringLiteral("referenceAudio"), rules);
    writeManifest(manifestPath, manifest);

    const auto discovery = VoiceCloneRegistry::discoverUserModels(
        temporary.path(), {QStringLiteral("qwen")});
    QVERIFY(!discovery.isValid());
    QVERIFY(discovery.errorString().contains(QStringLiteral("referenceAudio"),
                                             Qt::CaseInsensitive));
}

void VoiceCloneManifestTest::requiresBuiltInUiMetadata_data()
{
    QTest::addColumn<QString>("fieldName");
    QTest::newRow("display name") << QStringLiteral("displayName");
    QTest::newRow("provider") << QStringLiteral("provider");
    QTest::newRow("revision") << QStringLiteral("revision");
    QTest::newRow("capability preview") << QStringLiteral("capabilityPreview");
}

void VoiceCloneManifestTest::requiresBuiltInUiMetadata()
{
    QFETCH(QString, fieldName);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QJsonObject registryObject = approvedBuiltInRegistry();
    QJsonArray models = registryObject.value(QStringLiteral("models")).toArray();
    QJsonObject first = models[0].toObject();
    first.remove(fieldName);
    models[0] = first;
    registryObject.insert(QStringLiteral("models"), models);
    const QString path = writeJson(temporary.filePath(QStringLiteral("missing-metadata.json")),
                                   registryObject);

    const VoiceCloneRegistry registry = VoiceCloneRegistry::loadBuiltIn(path);
    QVERIFY(!registry.isValid());
    QVERIFY(registry.errorString().contains(fieldName, Qt::CaseInsensitive)
            || registry.errorString().contains(QStringLiteral("metadata"), Qt::CaseInsensitive));
}

QTEST_APPLESS_MAIN(VoiceCloneManifestTest)

#include "voice_clone_manifest_test.moc"
