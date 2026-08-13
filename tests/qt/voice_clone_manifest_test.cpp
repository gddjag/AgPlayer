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

using agplayer::voice_clone::VoiceCloneRegistry;

namespace {

constexpr auto kEmptySha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

QJsonObject builtInModel(const QString& stableId,
                         const QString& adapterId,
                         const QString& runtimeId,
                         const bool requiresLicenseAcceptance,
                         const QString& licenseName,
                         const QString& projectUrl,
                         const QString& huggingFaceUrl,
                         const QString& modelScopeUrl)
{
    return {
        {QStringLiteral("stableId"), stableId},
        {QStringLiteral("adapterId"), adapterId},
        {QStringLiteral("runtimeId"), runtimeId},
        {QStringLiteral("stable"), true},
        {QStringLiteral("requiresLicenseAcceptance"), requiresLicenseAcceptance},
        {QStringLiteral("license"),
         QJsonObject{{QStringLiteral("name"), licenseName},
                     {QStringLiteral("url"), huggingFaceUrl}}},
        {QStringLiteral("officialUrls"),
         QJsonObject{{QStringLiteral("project"), projectUrl},
                     {QStringLiteral("huggingFace"), huggingFaceUrl},
                     {QStringLiteral("modelScope"), modelScopeUrl}}},
    };
}

QJsonObject approvedBuiltInRegistry()
{
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("models"),
         QJsonArray{
             builtInModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
                          QStringLiteral("qwen"),
                          QStringLiteral("qwen"),
                          false,
                          QStringLiteral("Apache-2.0"),
                          QStringLiteral("https://github.com/QwenLM/Qwen3-TTS"),
                          QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
                          QStringLiteral("https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-0.6B-Base")),
             builtInModel(QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
                          QStringLiteral("qwen"),
                          QStringLiteral("qwen"),
                          false,
                          QStringLiteral("Apache-2.0"),
                          QStringLiteral("https://github.com/QwenLM/Qwen3-TTS"),
                          QStringLiteral("https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
                          QStringLiteral("https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-1.7B-Base")),
             builtInModel(QStringLiteral("IndexTeam/IndexTTS-2.5"),
                          QStringLiteral("indextts25"),
                          QStringLiteral("indextts25"),
                          true,
                          QStringLiteral("bilibili Model Use License Agreement"),
                          QStringLiteral("https://github.com/index-tts/index-tts"),
                          QStringLiteral("https://huggingface.co/IndexTeam/IndexTTS-2.5"),
                          QStringLiteral("https://modelscope.cn/models/IndexTeam/IndexTTS-2.5")),
             builtInModel(QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
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
    QFile(modelDirectory.filePath(QStringLiteral("config.json"))).open(QIODevice::WriteOnly);
    QFile(modelDirectory.filePath(QStringLiteral("model.safetensors"))).open(QIODevice::WriteOnly);
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
    void rejectsDuplicateUnknownAndIncompleteLocalManifests();
    void rejectsAbsoluteTraversalAndLinkedPaths();
    void marksUnhashedLocalModelsAsLocalUnverified();
};

void VoiceCloneManifestTest::builtInRegistryContainsOnlyApprovedStableModels()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString registryPath = writeJson(temporary.filePath(QStringLiteral("built-in.json")),
                                           approvedBuiltInRegistry());
    QVERIFY(!registryPath.isEmpty());

    const VoiceCloneRegistry registry = VoiceCloneRegistry::loadBuiltIn(registryPath);
    QVERIFY2(registry.isValid(), qPrintable(registry.errorString()));
    QCOMPARE(registry.modelIds(),
             QStringList({QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
                          QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
                          QStringLiteral("IndexTeam/IndexTTS-2.5"),
                          QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512")}));

    const QString forbiddenName = QStringLiteral("Vibe") + QStringLiteral("Voice");
    QVERIFY(!registry.modelIds().join(QLatin1Char('\n')).contains(forbiddenName));
    for (const auto& model : registry.models()) {
        QVERIFY(model.stable);
        QVERIFY(model.officialProjectUrl.startsWith(QStringLiteral("https://")));
        QVERIFY(model.huggingFaceUrl.startsWith(QStringLiteral("https://")));
        QVERIFY(model.modelScopeUrl.startsWith(QStringLiteral("https://")));
        QVERIFY(!model.adapterId.isEmpty());
        QVERIFY(!model.runtimeId.isEmpty());
    }
    QVERIFY(registry.model(QStringLiteral("IndexTeam/IndexTTS-2.5")).requiresLicenseAcceptance);
    QVERIFY(!registry.model(QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base")).requiresLicenseAcceptance);
    QVERIFY(!registry.model(QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base")).requiresLicenseAcceptance);
    QVERIFY(!registry.model(QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512")).requiresLicenseAcceptance);

    QJsonObject unapproved = approvedBuiltInRegistry();
    QJsonArray models = unapproved.value(QStringLiteral("models")).toArray();
    models.append(builtInModel(forbiddenName + QStringLiteral("-1.5B"),
                               QStringLiteral("unapproved"),
                               QStringLiteral("unapproved"),
                               false,
                               QStringLiteral("Apache-2.0"),
                               QStringLiteral("https://example.invalid/project"),
                               QStringLiteral("https://example.invalid/model"),
                               QStringLiteral("https://example.invalid/modelscope")));
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
    const QString outsideDirectory = QDir(temporary.path()).filePath(QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(outsideDirectory));
    QFile outsideFile(QDir(outsideDirectory).filePath(QStringLiteral("escaped.json")));
    QVERIFY(outsideFile.open(QIODevice::WriteOnly));
    outsideFile.close();
    const QString junction = QDir(modelDirectory).filePath(QStringLiteral("escape"));
    const QString command = QStringLiteral("mklink /J \"%1\" \"%2\"")
                                .arg(QDir::toNativeSeparators(junction),
                                     QDir::toNativeSeparators(outsideDirectory));
    QCOMPARE(QProcess::execute(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command}), 0);
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

QTEST_APPLESS_MAIN(VoiceCloneManifestTest)

#include "voice_clone_manifest_test.moc"
