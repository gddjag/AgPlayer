#include "equalizer_controller.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class EqualizerControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void exposesTenFixedBandsAndAppliesAtomicSnapshots();
    void resetAndBuiltInPresetUseTheRealBandParameters();
    void professionalBuiltInPresetsUseDocumentedSafeCurves();
    void customPresetsPersistRenameAndDelete();
    void responseCurveReflectsTheActualDspProgram();
};

void EqualizerControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("AgPlayer"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgPlayer-equalizer-controller-test"));
    QSettings().clear();
}

void EqualizerControllerTest::exposesTenFixedBandsAndAppliesAtomicSnapshots()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);

    EqualizerController controller(player);
    QVERIFY(!controller.enabled());
    QCOMPARE(controller.rowCount(), 10);
    QCOMPARE(controller.data(controller.index(0, 0),
                             EqualizerController::FrequencyRole).toDouble(),
             31.25);
    QCOMPARE(controller.data(controller.index(9, 0),
                             EqualizerController::LabelRole).toString(),
             QStringLiteral("16 kHz"));

    QSignalSpy gainChanged(&controller,
                           &EqualizerController::bandGainChanged);
    QVERIFY(controller.setBandGain(5, 6.04));
    QCOMPARE(controller.bandGain(5), 6.0);
    QCOMPARE(gainChanged.count(), 1);
    controller.setPreampDb(-1.5);
    controller.setEnabled(true);
    controller.setBypassed(false);
    controller.setAutoClipProtection(true);

    ag_equalizer_status status{};
    QCOMPARE(ag_player_equalizer_status(player, &status), AG_OK);
    QVERIFY(status.revision > 0U);
    QCOMPARE(status.enabled, 1);
    QCOMPARE(status.bypassed, 0);
    QCOMPARE(status.auto_clip_protection, 1);

    QVERIFY(!controller.setBandGain(-1, 0.0));
    QVERIFY(!controller.setBandGain(10, 0.0));
    QVERIFY(!controller.setBandGain(0, 12.1));
    ag_player_destroy(player);
}

void EqualizerControllerTest::resetAndBuiltInPresetUseTheRealBandParameters()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);

    QVERIFY(controller.applyPreset(QStringLiteral("bass-boost")));
    QVERIFY(controller.bandGain(0) > 0.0);
    QVERIFY(controller.bandGain(1) > 0.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("bass-boost"));

    controller.resetAll();
    for (int index = 0; index < controller.rowCount(); ++index) {
        QCOMPARE(controller.bandGain(index), 0.0);
    }
    QCOMPARE(controller.preampDb(), 0.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("flat"));
    ag_player_destroy(player);
}

void EqualizerControllerTest::professionalBuiltInPresetsUseDocumentedSafeCurves()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);

    const QStringList expectedIds{
        QStringLiteral("flat"), QStringLiteral("bass-boost"),
        QStringLiteral("bass-cut"), QStringLiteral("vocal"),
        QStringLiteral("treble-boost"), QStringLiteral("treble-cut"),
        QStringLiteral("rock")};
    const QStringList presetIds = controller.presetIds();
    QCOMPARE(presetIds.mid(0, expectedIds.size()), expectedIds);

    struct Curve {
        const char* id;
        double preamp;
        std::array<double, 10> gains;
    };
    const std::array<Curve, 7> curves{{
        {"flat", 0.0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {"bass-boost", -4.5, {4.5, 4, 3, 1.5, 0, 0, 0, 0, 0, 0}},
        {"bass-cut", 0.0, {-4.5, -4, -3, -1.5, 0, 0, 0, 0, 0, 0}},
        {"vocal", -2.5, {-2, -1.5, -0.5, 0.5, 1.5, 2.5, 2, 1, 0, -1}},
        {"treble-boost", -4.5, {0, 0, 0, 0, 0, 0, 1, 2, 3.5, 4.5}},
        {"treble-cut", 0.0, {0, 0, 0, 0, 0, 0, -1, -2, -3.5, -4.5}},
        {"rock", -3.5, {3.5, 3, 1, -1.5, -2, 1, 2.5, 3, 3.5, 2}},
    }};

    for (const Curve& curve : curves) {
        QVERIFY(controller.applyPreset(QString::fromLatin1(curve.id)));
        QCOMPARE(controller.preampDb(), curve.preamp);
        for (int band = 0; band < controller.rowCount(); ++band)
            QCOMPARE(controller.bandGain(band), curve.gains[band]);
    }
    ag_player_destroy(player);
}

void EqualizerControllerTest::customPresetsPersistRenameAndDelete()
{
    QSettings().clear();
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);

    QString customId;
    {
        EqualizerController controller(player);
        QVERIFY(controller.setBandGain(3, 2.5));
        controller.setPreampDb(-0.7);
        customId = controller.saveCustomPreset(QStringLiteral("我的预设"));
        QVERIFY(!customId.isEmpty());
        QVERIFY(controller.renameCustomPreset(customId,
                                               QStringLiteral("新名称")));
    }

    EqualizerController restored(player);
    QVERIFY(restored.presetIds().contains(customId));
    QVERIFY(restored.applyPreset(customId));
    QCOMPARE(restored.bandGain(3), 2.5);
    QCOMPARE(restored.preampDb(), -0.7);
    QVERIFY(restored.deleteCustomPreset(customId));
    QVERIFY(!restored.presetIds().contains(customId));
    QVERIFY(!restored.deleteCustomPreset(QStringLiteral("flat")));
    ag_player_destroy(player);
}

void EqualizerControllerTest::responseCurveReflectsTheActualDspProgram()
{
    QSettings().clear();
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    controller.setEnabled(true);

    const QVariantList flat = controller.responseCurve(96);
    QCOMPARE(flat.size(), 96);
    for (const QVariant& value : flat) {
        QVERIFY(std::abs(value.toDouble()) < 1.0e-9);
    }

    controller.setAutoClipProtection(false);
    QVERIFY(controller.setBandGain(5, 6.0));
    const QVariantList boosted = controller.responseCurve(96);
    QCOMPARE(boosted.size(), 96);
    const auto peak = std::max_element(
        boosted.cbegin(), boosted.cend(),
        [](const QVariant& left, const QVariant& right) {
            return left.toDouble() < right.toDouble();
        });
    QVERIFY(peak != boosted.cend());
    QVERIFY(peak->toDouble() > 4.5);

    ag_player_destroy(player);
}

QTEST_MAIN(EqualizerControllerTest)
#include "equalizer_controller_test.moc"
