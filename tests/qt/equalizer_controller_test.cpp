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
    void cleanup();
    void exposesSeventeenFixedBandsAndAppliesAtomicSnapshots();
    void resetAndBuiltInPresetUseTheRealBandParameters();
    void builtInPresetsExposeEightSafeReferenceCurves();
    void migratesLegacyTenBandSettingsByLogFrequency();
    void legacyFlatSettingsRemainFlatAfterMigration();
    void retainedLegacyPresetIdBecomesCustomAfterMigration();
    void migratesLegacyCustomPresetsAndRemovedPresetId();
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

void EqualizerControllerTest::cleanup()
{
    QSettings().clear();
}

void EqualizerControllerTest::exposesSeventeenFixedBandsAndAppliesAtomicSnapshots()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);

    EqualizerController controller(player);
    QVERIFY(!controller.enabled());
    QCOMPARE(controller.currentPresetId(), QStringLiteral("flat"));
    QCOMPARE(controller.rowCount(), 17);
    QCOMPARE(controller.data(controller.index(0, 0),
                             EqualizerController::FrequencyRole).toDouble(),
             20.0);
    QCOMPARE(controller.data(controller.index(16, 0),
                             EqualizerController::LabelRole).toString(),
             QStringLiteral("20 kHz"));

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
    QVERIFY(!controller.setBandGain(17, 0.0));
    QVERIFY(!controller.setBandGain(0, 12.1));
    ag_player_destroy(player);
}

void EqualizerControllerTest::resetAndBuiltInPresetUseTheRealBandParameters()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);

    QVERIFY(controller.applyPreset(QStringLiteral("bass")));
    QVERIFY(controller.bandGain(0) > 0.0);
    QVERIFY(controller.bandGain(1) > 0.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("bass"));

    controller.resetAll();
    for (int index = 0; index < controller.rowCount(); ++index) {
        QCOMPARE(controller.bandGain(index), 0.0);
    }
    QCOMPARE(controller.preampDb(), 0.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("flat"));
    ag_player_destroy(player);
}

void EqualizerControllerTest::builtInPresetsExposeEightSafeReferenceCurves()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);

    const QStringList expectedIds{
        QStringLiteral("flat"), QStringLiteral("bass"),
        QStringLiteral("classical"), QStringLiteral("pop"),
        QStringLiteral("rock"), QStringLiteral("vocal"),
        QStringLiteral("edm"), QStringLiteral("jazz")};
    const QStringList presetIds = controller.presetIds();
    QCOMPARE(presetIds, expectedIds);
    QCOMPARE(controller.presetNames().size(), 8);

    struct Curve {
        const char* id;
        double preamp;
        std::array<double, 17> gains;
    };
    const std::array<Curve, 8> curves{{
        {"flat", 0.0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {"bass", -6.9, {5, 4.5, 3.5, 2.5, 1.5, 0.5, 0, 0, -0.5, -0.5, 0, 0, 0, 0, 0, 0, 0}},
        {"classical", -4.5, {2.5, 2, 1.5, 0.5, -0.5, -1, -1, -0.5, 0.5, 1.5, 2, 2.5, 3, 2.5, 1.5, 0.5, 0}},
        {"pop", -3.8, {-0.5, 0, 1, 2, 2.5, 1.5, 0, -1, -1, 0, 1, 2, 2.5, 2, 1, 0, -0.5}},
        {"rock", -5.4, {4, 3.5, 2, 0, -1.5, -2, -1, 0.5, 2, 3, 3.5, 3, 2.5, 2, 1.5, 1, 0}},
        {"vocal", -4.9, {-3, -2.5, -2, -1, -0.5, 0.5, 1.5, 2.5, 3, 3, 2.5, 2, 1, -0.5, -1.5, -2, -2}},
        {"edm", -6.6, {4.5, 4, 3.5, 2, 0.5, -1, -1.5, -1, 0, 1.5, 3, 4, 4.5, 4, 3, 2, 1}},
        {"jazz", -4.7, {2.5, 2, 1.5, 0.5, -0.5, -1, -0.5, 0.5, 1.5, 2.5, 3, 2.5, 2, 1.5, 1, 0.5, 0}},
    }};

    for (const Curve& curve : curves) {
        QVERIFY(controller.applyPreset(QString::fromLatin1(curve.id)));
        QCOMPARE(controller.preampDb(), curve.preamp);
        for (int band = 0; band < controller.rowCount(); ++band)
            QCOMPARE(controller.bandGain(band), curve.gains[band]);

        if (QString::fromLatin1(curve.id) != QStringLiteral("flat")) {
            controller.setEnabled(true);
            controller.setBypassed(false);
            controller.setAutoClipProtection(false);
            const QVariantList response = controller.responseCurve(4'096);
            QVERIFY(!response.isEmpty());
            double peakDb = -100.0;
            for (const QVariant& point : response)
                peakDb = std::max(peakDb, point.toDouble());
            QVERIFY2(peakDb <= -0.5,
                     qPrintable(QStringLiteral("%1 preset peak is %2 dB")
                                    .arg(QString::fromLatin1(curve.id))
                                    .arg(peakDb, 0, 'f', 3)));
        }
    }
    ag_player_destroy(player);
}

void EqualizerControllerTest::retainedLegacyPresetIdBecomesCustomAfterMigration()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{3.5, 3, 1, -1.5, -2, 1, 2.5, 3, 3.5, 2});
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("rock"));
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom"));
    QCOMPARE(controller.bandGain(0), 3.5);
    QCOMPARE(controller.bandGain(16), 2.0);
    ag_player_destroy(player);
}

void EqualizerControllerTest::legacyFlatSettingsRemainFlatAfterMigration()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("flat"));
        settings.setValue(QStringLiteral("preampDb"), 0.0);
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("flat"));
    for (int band = 0; band < controller.rowCount(); ++band)
        QCOMPARE(controller.bandGain(band), 0.0);
    ag_player_destroy(player);
}

void EqualizerControllerTest::migratesLegacyTenBandSettingsByLogFrequency()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{-10, -8, -6, -4, -2, 0, 2, 4, 6, 8});
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("bass-boost"));
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QCOMPARE(controller.rowCount(), 17);
    QCOMPARE(controller.bandGain(0), -10.0);
    QCOMPARE(controller.bandGain(2), -8.6);
    QCOMPARE(controller.bandGain(3), -7.3);
    QCOMPARE(controller.bandGain(4), -6.0);
    QCOMPARE(controller.bandGain(16), 8.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("schemaVersion")).toInt(), 2);
    QCOMPARE(settings.value(QStringLiteral("bandCount")).toInt(), 17);
    QCOMPARE(settings.value(QStringLiteral("bandGains")).toList().size(), 17);
    settings.endGroup();
    ag_player_destroy(player);
}

void EqualizerControllerTest::migratesLegacyCustomPresetsAndRemovedPresetId()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("treble-cut"));
        settings.beginWriteArray(QStringLiteral("customPresets"), 1);
        settings.setArrayIndex(0);
        settings.setValue(QStringLiteral("id"), QStringLiteral("custom-old"));
        settings.setValue(QStringLiteral("name"), QStringLiteral("旧十段"));
        settings.setValue(QStringLiteral("preampDb"), -2.0);
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        settings.endArray();
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom"));
    QVERIFY(controller.presetIds().contains(QStringLiteral("custom-old")));
    QVERIFY(controller.applyPreset(QStringLiteral("custom-old")));
    QCOMPARE(controller.bandGain(0), 0.0);
    QCOMPARE(controller.bandGain(4), 2.0);
    QCOMPARE(controller.bandGain(16), 9.0);
    QCOMPARE(controller.preampDb(), -2.0);
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
