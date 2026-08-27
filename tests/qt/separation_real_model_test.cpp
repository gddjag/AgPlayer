#include "native_worker_backend.hpp"

#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace agplayer::separation;

class SeparationRealModelTest final : public QObject {
    Q_OBJECT

private slots:
    void approvedCatalogModelRunsOnCpuWhenExplicitlyEnabled();
};

void SeparationRealModelTest::approvedCatalogModelRunsOnCpuWhenExplicitlyEnabled()
{
    const QString runtime = qEnvironmentVariable("AGPLAYER_SEPARATION_ORT_DLL");
    const QString input = qEnvironmentVariable("AGPLAYER_SEPARATION_REAL_AUDIO");
    const QString catalogRoot = qEnvironmentVariable("AGPLAYER_SEPARATION_CATALOG_ROOT");
    if (runtime.isEmpty() || input.isEmpty() || catalogRoot.isEmpty()) {
        QSKIP("Opt-in real-model test requires AGPLAYER_SEPARATION_ORT_DLL, "
              "AGPLAYER_SEPARATION_REAL_AUDIO, and AGPLAYER_SEPARATION_CATALOG_ROOT");
    }

    const QDir catalog(catalogRoot);
    const QList<QPair<QString, QStringList>> approved{
        {QStringLiteral("kara"),
         {catalog.filePath(QStringLiteral("UVR_MDXNET_KARA.onnx"))}},
        {QStringLiteral("hq3"),
         {catalog.filePath(QStringLiteral("UVR-MDX-NET-Inst_HQ_3.onnx"))}},
        {QStringLiteral("demucs"),
         {catalog.filePath(QStringLiteral("htdemucs_ft_bass_fp16weights.onnx")),
          catalog.filePath(QStringLiteral("htdemucs_ft_drums_fp16weights.onnx")),
          catalog.filePath(QStringLiteral("htdemucs_ft_other_fp16weights.onnx")),
          catalog.filePath(QStringLiteral("htdemucs_ft_vocals_fp16weights.onnx"))}},
    };
    for (const auto& entry : approved) {
        QJsonArray modelFiles;
        for (const QString& model : entry.second) {
            QVERIFY2(QFileInfo::exists(model), qPrintable(model));
            modelFiles.push_back(model);
        }
        QTemporaryDir output;
        QVERIFY(output.isValid());
        NativeWorkerBackend backend;
        std::atomic_bool cancelled{false};
        const BackendResult result = backend.separate(
            {{QStringLiteral("runtimePath"), runtime},
             {QStringLiteral("inputPath"), input},
             {QStringLiteral("modelFiles"), modelFiles},
             {QStringLiteral("outputDirectory"), output.path()},
             {QStringLiteral("baseName"), entry.first},
             {QStringLiteral("extension"), QStringLiteral("wav")},
             {QStringLiteral("stems"), QJsonArray{QStringLiteral("vocals")}},
             {QStringLiteral("device"), QStringLiteral("cpu")}},
            cancelled, [](double, const QString&) {});
        QVERIFY2(result.ok,
                 qPrintable(entry.first + QStringLiteral(": ") + result.code
                            + QStringLiteral(": ") + result.message));
        const QJsonArray outputs = result.payload.value(QStringLiteral("outputs")).toArray();
        QCOMPARE(outputs.size(), 1);
        QVERIFY(QFileInfo::exists(outputs.first().toString()));
    }
}

QTEST_GUILESS_MAIN(SeparationRealModelTest)
#include "separation_real_model_test.moc"
