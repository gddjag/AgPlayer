#include "equalizer_controller.hpp"

#include "../../core/src/audio_editor/editor_player_bridge.hpp"

#include <agplayer/c_api.h>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <limits>
#include <memory>

namespace {

class ConstantAudioStream final : public agplayer::IAudioStreamSource {
public:
    ConstantAudioStream()
    {
        metadata_.sample_rate = 48'000;
        metadata_.channels = 2;
        metadata_.duration_ms = 3'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::int64_t totalFrames = 144'000;
        const std::int64_t remaining = totalFrames - positionFrames_;
        const std::size_t frames = static_cast<std::size_t>(
            std::max<std::int64_t>(0, std::min<std::int64_t>(1'024, remaining)));
        block = {};
        block.frames = frames;
        block.timestamp_frame = positionFrames_;
        block.timestamp_ms = positionFrames_ * 1'000 / metadata_.sample_rate;
        block.samples.assign(frames * 2U, 0.5F);
        positionFrames_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = positionFrames_ >= totalFrames;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        positionFrames_ = positionMs * metadata_.sample_rate / 1'000;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t positionFrames_ = 0;
};

int equalizerSubmitCallCount = 0;

ag_result failEqualizerSubmissionsAfterFirst(
    ag_player* const player, const ag_equalizer_settings* const settings)
{
    ++equalizerSubmitCallCount;
    return equalizerSubmitCallCount == 1
               ? ag_player_set_equalizer(player, settings)
               : AG_INTERNAL_ERROR;
}

ag_result failEveryEqualizerSubmission(
    ag_player*, const ag_equalizer_settings*)
{
    return AG_INTERNAL_ERROR;
}

} // namespace

class EqualizerControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void exposesEighteenFixedBandsAndAppliesAtomicSnapshots();
    void resetAndBuiltInPresetUseTheRealBandParameters();
    void nonFlatPresetActivatesTheEqualizer();
    void builtInPresetsExposeEightSafeReferenceCurves();
    void migratesSchemaTwoSeventeenBandSettingsAndCustomPreset();
    void migratesLegacyTenBandSettingsByLogFrequency();
    void legacyFlatSettingsRemainFlatAfterMigration();
    void invalidStoredBandLengthCannotMasqueradeAsBuiltInPreset();
    void initialSubmissionFailureRestoresEngineDefaults();
    void submissionFailureRestoresLastCompleteSnapshot();
    void retainedLegacyPresetIdBecomesCustomAfterMigration();
    void migratesLegacyCustomPresetsAndRemovedPresetId();
    void restoresStoredDspValuesWithoutApplyingEditorPrecision();
    void validatesCustomPresetStoredPreamps();
    void customPresetsPersistRenameAndDelete();
    void persistsSupportedGainRangesAndClampsInOneSnapshot();
    void precisionControlsFutureEditsWithoutRewritingStoredValues();
    void responseCurveReflectsTheActualDspProgram();
    void responseCurveIncludesAutomaticProtectionAndActiveSampleRate();
    void refreshStatusPublishesOutputPeakOnlyWhenItChanges();
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

void EqualizerControllerTest::exposesEighteenFixedBandsAndAppliesAtomicSnapshots()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);

    EqualizerController controller(player);
    QVERIFY(!controller.enabled());
    QCOMPARE(controller.currentPresetId(), QStringLiteral("flat"));
    QCOMPARE(controller.rowCount(), 18);
    QCOMPARE(controller.data(controller.index(0, 0),
                             EqualizerController::FrequencyRole).toDouble(),
             20.0);
    QCOMPARE(controller.data(controller.index(14, 0),
                             EqualizerController::FrequencyRole).toDouble(),
             10'000.0);
    QCOMPARE(controller.data(controller.index(17, 0),
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
    QVERIFY(!controller.setBandGain(18, 0.0));
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

void EqualizerControllerTest::nonFlatPresetActivatesTheEqualizer()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QVERIFY(!controller.enabled());
    controller.setBypassed(true);

    QVERIFY(controller.applyPreset(QStringLiteral("bass")));
    QVERIFY(controller.enabled());
    QVERIFY(!controller.bypassed());
    QCOMPARE(controller.currentPresetId(), QStringLiteral("bass"));

    ag_equalizer_status status{};
    QCOMPARE(ag_player_equalizer_status(player, &status), AG_OK);
    QCOMPARE(status.enabled, 1);
    QCOMPARE(status.bypassed, 0);
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
        std::array<double, 17> oldGains;
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
        QCOMPARE(controller.bandGain(14), 0.0);
        for (int oldBand = 0; oldBand < 17; ++oldBand) {
            const int newBand = oldBand < 14 ? oldBand : oldBand + 1;
            QCOMPARE(controller.bandGain(newBand), curve.oldGains[oldBand]);
        }

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
    QCOMPARE(controller.bandGain(14), 0.0);
    QCOMPARE(controller.bandGain(17), 2.0);
    ag_player_destroy(player);
}

void EqualizerControllerTest::migratesSchemaTwoSeventeenBandSettingsAndCustomPreset()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("schemaVersion"), 2);
        settings.setValue(QStringLiteral("bandCount"), 17);
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{15.0, 1.25, -6, -5, -4, -3, -2, -1, 0,
                                       1, 2, 3, 4, 5, 6, 7, 8});
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("custom-v2"));
        settings.beginWriteArray(QStringLiteral("customPresets"), 1);
        settings.setArrayIndex(0);
        settings.setValue(QStringLiteral("id"), QStringLiteral("custom-v2"));
        settings.setValue(QStringLiteral("name"), QStringLiteral("十七段"));
        settings.setValue(QStringLiteral("preampDb"), -2.0);
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{-8, -7, -6, -5, -4, -3, -2, -1, 0,
                                       1, 2, 3, 4, 5, 6, 7, 8});
        settings.endArray();
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QCOMPARE(controller.rowCount(), 18);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom-v2"));
    const std::array<double, 17> migratedCurrent{
        15.0, 1.25, -6, -5, -4, -3, -2, -1, 0,
        1, 2, 3, 4, 5, 6, 7, 8};
    for (int oldBand = 0; oldBand < 17; ++oldBand) {
        const int newBand = oldBand < 14 ? oldBand : oldBand + 1;
        QCOMPARE(controller.bandGain(newBand),
                 migratedCurrent[static_cast<std::size_t>(oldBand)]);
    }
    QCOMPARE(controller.bandGain(14), 0.0);

    QVERIFY(controller.applyPreset(QStringLiteral("custom-v2")));
    for (int oldBand = 0; oldBand < 17; ++oldBand) {
        const int newBand = oldBand < 14 ? oldBand : oldBand + 1;
        QCOMPARE(controller.bandGain(newBand), static_cast<double>(oldBand - 8));
    }
    QCOMPARE(controller.bandGain(14), 0.0);
    QCOMPARE(controller.preampDb(), -2.0);

    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("schemaVersion")).toInt(), 3);
    QCOMPARE(settings.value(QStringLiteral("bandCount")).toInt(), 18);
    QCOMPARE(settings.value(QStringLiteral("bandGains")).toList().size(), 18);
    settings.endGroup();
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

void EqualizerControllerTest::invalidStoredBandLengthCannotMasqueradeAsBuiltInPreset()
{
    QVariantList schemaThreeSeventeenBands(17, 0.0);
    schemaThreeSeventeenBands[0] = 4.0;
    const QList<QVariantList> malformedGains{
        {}, QVariantList{4.0, 3.0, 2.0}, schemaThreeSeventeenBands};

    for (const QVariantList& gains : malformedGains) {
        QSettings().clear();
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("schemaVersion"), 3);
        settings.setValue(QStringLiteral("bandCount"), 18);
        if (!gains.isEmpty()) {
            settings.setValue(QStringLiteral("bandGains"), gains);
        }
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("rock"));
        settings.endGroup();

        const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
        ag_player* player = nullptr;
        QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
        EqualizerController controller(player);
        QCOMPARE(controller.currentPresetId(), QStringLiteral("custom"));
        ag_player_destroy(player);
    }
}

void EqualizerControllerTest::submissionFailureRestoresLastCompleteSnapshot()
{
    QVariantList gains(18, 0.0);
    gains[0] = 10.0;
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("schemaVersion"), 3);
        settings.setValue(QStringLiteral("bandCount"), 18);
        settings.setValue(QStringLiteral("gainRangeDb"), 12.0);
        settings.setValue(QStringLiteral("preampDb"), 11.0);
        settings.setValue(QStringLiteral("bandGains"), gains);
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("custom"));
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    equalizerSubmitCallCount = 0;
    EqualizerController controller(
        player, nullptr, &failEqualizerSubmissionsAfterFirst);
    QSignalSpy failed(&controller, &EqualizerController::submissionFailed);
    const QString acceptedPresetId =
        controller.saveCustomPreset(QStringLiteral("Accepted snapshot"));
    QVERIFY(acceptedPresetId.startsWith(QStringLiteral("custom-")));
    ag_equalizer_status acceptedStatus{};
    QCOMPARE(ag_player_equalizer_status(player, &acceptedStatus), AG_OK);

    QVERIFY(!controller.setGainRangeDb(6.0));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(controller.gainRangeDb(), 12.0);
    QCOMPARE(controller.bandGain(0), 10.0);
    QCOMPARE(controller.preampDb(), 11.0);
    QCOMPARE(controller.currentPresetId(), acceptedPresetId);

    QVERIFY(!controller.setBandGain(1, 2.0));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(controller.bandGain(1), 0.0);
    QCOMPARE(controller.currentPresetId(), acceptedPresetId);

    controller.setEnabled(true);
    QCOMPARE(failed.count(), 3);
    QVERIFY(!controller.enabled());

    ag_equalizer_status finalStatus{};
    QCOMPARE(ag_player_equalizer_status(player, &finalStatus), AG_OK);
    QCOMPARE(finalStatus.revision, acceptedStatus.revision);
    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("currentPresetId")).toString(),
             acceptedPresetId);
    QCOMPARE(settings.value(QStringLiteral("gainRangeDb")).toDouble(), 12.0);
    settings.endGroup();
    ag_player_destroy(player);
}

void EqualizerControllerTest::initialSubmissionFailureRestoresEngineDefaults()
{
    QVariantList gains(18, 0.0);
    gains[0] = 10.0;
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("schemaVersion"), 3);
        settings.setValue(QStringLiteral("bandCount"), 18);
        settings.setValue(QStringLiteral("enabled"), false);
        settings.setValue(QStringLiteral("bypassed"), true);
        settings.setValue(QStringLiteral("autoClipProtection"), false);
        settings.setValue(QStringLiteral("preampDb"), 11.0);
        settings.setValue(QStringLiteral("bandGains"), gains);
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("custom"));
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(
        player, nullptr, &failEveryEqualizerSubmission);

    QVERIFY(controller.enabled());
    QVERIFY(!controller.bypassed());
    QVERIFY(controller.autoClipProtection());
    QCOMPARE(controller.preampDb(), 0.0);
    QCOMPARE(controller.bandGain(0), 0.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("flat"));
    ag_equalizer_status status{};
    QCOMPARE(ag_player_equalizer_status(player, &status), AG_OK);
    QCOMPARE(status.revision, 0U);
    QCOMPARE(status.enabled, 1);
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
    QCOMPARE(controller.rowCount(), 18);
    QCOMPARE(controller.bandGain(0), -10.0);
    QCOMPARE(controller.bandGain(2), -8.6);
    QCOMPARE(controller.bandGain(3), -7.3);
    QCOMPARE(controller.bandGain(4), -6.0);
    QCOMPARE(controller.bandGain(14), 0.0);
    QCOMPARE(controller.bandGain(17), 8.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("schemaVersion")).toInt(), 3);
    QCOMPARE(settings.value(QStringLiteral("bandCount")).toInt(), 18);
    QCOMPARE(settings.value(QStringLiteral("bandGains")).toList().size(), 18);
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
                          QStringLiteral("custom-old"));
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
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom-old"));
    QVERIFY(controller.presetIds().contains(QStringLiteral("custom-old")));
    QVERIFY(controller.applyPreset(QStringLiteral("custom-old")));
    QCOMPARE(controller.bandGain(0), 0.0);
    QCOMPARE(controller.bandGain(4), 2.0);
    QCOMPARE(controller.bandGain(14), 0.0);
    QCOMPARE(controller.bandGain(17), 9.0);
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

void EqualizerControllerTest::persistsSupportedGainRangesAndClampsInOneSnapshot()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);

    QCOMPARE(controller.gainRangeDb(), 12.0);
    QCOMPARE(controller.data(controller.index(0, 0),
                             EqualizerController::MinimumRole).toDouble(),
             -12.0);
    QVERIFY(controller.setBandGain(0, 10.0));
    QVERIFY(controller.setBandGain(1, -10.0));
    QVERIFY(controller.setBandGain(2, 3.1));
    controller.setPreampDb(11.0);
    const QString customPresetId =
        controller.saveCustomPreset(QStringLiteral("待收窄"));
    QVERIFY(!customPresetId.isEmpty());
    ag_equalizer_status before{};
    QCOMPARE(ag_player_equalizer_status(player, &before), AG_OK);

    QSignalSpy rangeChanged(&controller, &EqualizerController::gainRangeDbChanged);
    QSignalSpy bandChanged(&controller, &EqualizerController::bandGainChanged);
    QSignalSpy preampChanged(&controller, &EqualizerController::preampDbChanged);
    QSignalSpy modelChanged(&controller, &EqualizerController::dataChanged);
    QSignalSpy presetChanged(&controller,
                             &EqualizerController::currentPresetChanged);
    QVERIFY(controller.setGainRangeDb(6.0));
    QCOMPARE(rangeChanged.count(), 1);
    QCOMPARE(bandChanged.count(), 2);
    QCOMPARE(preampChanged.count(), 1);
    QCOMPARE(modelChanged.count(), 1);
    QCOMPARE(presetChanged.count(), 1);
    QCOMPARE(controller.gainRangeDb(), 6.0);
    QCOMPARE(controller.bandGain(0), 6.0);
    QCOMPARE(controller.bandGain(1), -6.0);
    QCOMPARE(controller.bandGain(2), 3.1);
    QCOMPARE(controller.preampDb(), 6.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom"));
    QCOMPARE(controller.data(controller.index(0, 0),
                             EqualizerController::MaximumRole).toDouble(),
             6.0);
    ag_equalizer_status after{};
    QCOMPARE(ag_player_equalizer_status(player, &after), AG_OK);
    QCOMPARE(after.revision, before.revision + 1U);

    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("gainRangeDb")).toDouble(), 6.0);
    settings.endGroup();

    rangeChanged.clear();
    bandChanged.clear();
    preampChanged.clear();
    modelChanged.clear();
    presetChanged.clear();
    ag_equalizer_status beforeNoopRange{};
    QCOMPARE(ag_player_equalizer_status(player, &beforeNoopRange), AG_OK);
    QVERIFY(!controller.setGainRangeDb(7.0));
    QVERIFY(controller.setGainRangeDb(6.0));
    QCOMPARE(controller.gainRangeDb(), 6.0);
    QCOMPARE(rangeChanged.count(), 0);
    QCOMPARE(bandChanged.count(), 0);
    QCOMPARE(preampChanged.count(), 0);
    QCOMPARE(modelChanged.count(), 0);
    QCOMPARE(presetChanged.count(), 0);
    ag_equalizer_status afterNoopRange{};
    QCOMPARE(ag_player_equalizer_status(player, &afterNoopRange), AG_OK);
    QCOMPARE(afterNoopRange.revision, beforeNoopRange.revision);
    QVERIFY(!controller.setBandGain(3, 6.1));
    QVERIFY(controller.setGainRangeDb(18.0));
    QCOMPARE(controller.gainRangeDb(), 18.0);
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("gainRangeDb")).toDouble(), 18.0);
    settings.endGroup();
    QVERIFY(controller.setGainRangeDb(12.0));
    QCOMPARE(controller.gainRangeDb(), 12.0);

    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("gainRangeDb")).toDouble(), 12.0);
    settings.endGroup();
    EqualizerController restored(player);
    QCOMPARE(restored.gainRangeDb(), 12.0);
    ag_player_destroy(player);
}

void EqualizerControllerTest::restoresStoredDspValuesWithoutApplyingEditorPrecision()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("schemaVersion"), 3);
        settings.setValue(QStringLiteral("bandCount"), 18);
        settings.setValue(QStringLiteral("gainRangeDb"), 6.0);
        settings.setValue(QStringLiteral("precisionMode"), QStringLiteral("low"));
        settings.setValue(QStringLiteral("preampDb"), 15.0);
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{15.0, 1.25, -18.0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0});
        settings.setValue(QStringLiteral("currentPresetId"),
                          QStringLiteral("custom-exact"));
        settings.beginWriteArray(QStringLiteral("customPresets"), 1);
        settings.setArrayIndex(0);
        settings.setValue(QStringLiteral("id"), QStringLiteral("custom-exact"));
        settings.setValue(QStringLiteral("name"), QStringLiteral("精确保留"));
        settings.setValue(QStringLiteral("preampDb"), -18.0);
        settings.setValue(QStringLiteral("bandGains"),
                          QVariantList{-15.0, 1.25, 18.0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0});
        settings.endArray();
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QCOMPARE(controller.gainRangeDb(), 6.0);
    QCOMPARE(controller.precisionMode(), QStringLiteral("low"));
    QCOMPARE(controller.preampDb(), 15.0);
    QCOMPARE(controller.bandGain(0), 15.0);
    QCOMPARE(controller.bandGain(1), 1.25);
    QCOMPARE(controller.bandGain(2), -18.0);
    QCOMPARE(controller.currentPresetId(), QStringLiteral("custom-exact"));
    QVERIFY(controller.applyPreset(QStringLiteral("custom-exact")));
    QCOMPARE(controller.preampDb(), -18.0);
    QCOMPARE(controller.bandGain(0), -15.0);
    QCOMPARE(controller.bandGain(1), 1.25);
    QCOMPARE(controller.bandGain(2), 18.0);
    EqualizerController restored(player);
    QCOMPARE(restored.gainRangeDb(), 6.0);
    QCOMPARE(restored.precisionMode(), QStringLiteral("low"));
    QCOMPARE(restored.bandGain(1), 1.25);
    QCOMPARE(restored.preampDb(), -18.0);
    ag_player_destroy(player);
}

void EqualizerControllerTest::validatesCustomPresetStoredPreamps()
{
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("equalizer"));
        settings.setValue(QStringLiteral("schemaVersion"), 3);
        settings.setValue(QStringLiteral("bandCount"), 18);
        settings.beginWriteArray(QStringLiteral("customPresets"), 5);
        const auto writePreset = [&settings](const int index, const QString& id,
                                             const double preamp) {
            settings.setArrayIndex(index);
            settings.setValue(QStringLiteral("id"), id);
            settings.setValue(QStringLiteral("name"), id);
            settings.setValue(QStringLiteral("preampDb"), preamp);
            settings.setValue(QStringLiteral("bandGains"),
                              QVariantList{0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 0, 0, 0, 0, 0, 0, 0, 0});
        };
        writePreset(0, QStringLiteral("custom-negative-limit"), -18.0);
        writePreset(1, QStringLiteral("custom-positive-limit"), 18.0);
        writePreset(2, QStringLiteral("custom-nan"),
                    std::numeric_limits<double>::quiet_NaN());
        writePreset(3, QStringLiteral("custom-infinity"),
                    std::numeric_limits<double>::infinity());
        writePreset(4, QStringLiteral("custom-outside"), 18.1);
        settings.endArray();
        settings.endGroup();
    }

    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);
    QVERIFY(controller.applyPreset(QStringLiteral("custom-negative-limit")));
    QCOMPARE(controller.preampDb(), -18.0);
    QVERIFY(controller.applyPreset(QStringLiteral("custom-positive-limit")));
    QCOMPARE(controller.preampDb(), 18.0);
    QVERIFY(controller.applyPreset(QStringLiteral("custom-nan")));
    QCOMPARE(controller.preampDb(), 0.0);
    QVERIFY(controller.applyPreset(QStringLiteral("custom-infinity")));
    QCOMPARE(controller.preampDb(), 0.0);
    QVERIFY(controller.applyPreset(QStringLiteral("custom-outside")));
    QCOMPARE(controller.preampDb(), 0.0);
    ag_player_destroy(player);
}

void EqualizerControllerTest::precisionControlsFutureEditsWithoutRewritingStoredValues()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    EqualizerController controller(player);

    QCOMPARE(controller.precisionMode(), QStringLiteral("high"));
    QCOMPARE(controller.gainStepDb(), 0.1);
    QVERIFY(controller.setGainRangeDb(18.0));
    QVERIFY(controller.setBandGain(0, 1.15));
    QCOMPARE(controller.bandGain(0), 1.2);
    QVERIFY(controller.setBandGain(1, -1.15));
    QCOMPARE(controller.bandGain(1), -1.2);
    QVERIFY(controller.setBandGain(2, 18.0));
    QCOMPARE(controller.bandGain(2), 18.0);

    QSignalSpy highBandChanged(&controller,
                               &EqualizerController::bandGainChanged);
    ag_equalizer_status beforeIdempotentBand{};
    QCOMPARE(ag_player_equalizer_status(player, &beforeIdempotentBand), AG_OK);
    QVERIFY(controller.setBandGain(0, 1.15));
    QCOMPARE(highBandChanged.count(), 0);
    ag_equalizer_status afterIdempotentBand{};
    QCOMPARE(ag_player_equalizer_status(player, &afterIdempotentBand), AG_OK);
    QCOMPARE(afterIdempotentBand.revision, beforeIdempotentBand.revision);

    QSignalSpy precisionChanged(&controller,
                                &EqualizerController::precisionModeChanged);
    QSignalSpy stepChanged(&controller, &EqualizerController::gainStepDbChanged);
    QVERIFY(controller.setPrecisionMode(QStringLiteral("medium")));
    QCOMPARE(precisionChanged.count(), 1);
    QCOMPARE(stepChanged.count(), 1);
    QCOMPARE(controller.gainStepDb(), 0.5);
    QCOMPARE(controller.bandGain(0), 1.2);
    QVERIFY(controller.setBandGain(0, 1.26));
    controller.setPreampDb(-1.26);
    QCOMPARE(controller.bandGain(0), 1.5);
    QCOMPARE(controller.preampDb(), -1.5);

    ag_equalizer_status beforeNoopSetters{};
    QCOMPARE(ag_player_equalizer_status(player, &beforeNoopSetters), AG_OK);
    precisionChanged.clear();
    stepChanged.clear();
    QVERIFY(controller.setPrecisionMode(QStringLiteral("medium")));
    QVERIFY(!controller.setPrecisionMode(QStringLiteral("ultra")));
    QCOMPARE(precisionChanged.count(), 0);
    QCOMPARE(stepChanged.count(), 0);
    ag_equalizer_status afterNoopSetters{};
    QCOMPARE(ag_player_equalizer_status(player, &afterNoopSetters), AG_OK);
    QCOMPARE(afterNoopSetters.revision, beforeNoopSetters.revision);

    QSignalSpy noopPreampChanged(&controller,
                                 &EqualizerController::preampDbChanged);
    ag_equalizer_status beforeNoopPreamp{};
    QCOMPARE(ag_player_equalizer_status(player, &beforeNoopPreamp), AG_OK);
    controller.setPreampDb(-1.5);
    controller.setPreampDb(18.1);
    QCOMPARE(noopPreampChanged.count(), 0);
    ag_equalizer_status afterNoopPreamp{};
    QCOMPARE(ag_player_equalizer_status(player, &afterNoopPreamp), AG_OK);
    QCOMPARE(afterNoopPreamp.revision, beforeNoopPreamp.revision);

    QVERIFY(controller.setPrecisionMode(QStringLiteral("low")));
    QCOMPARE(controller.gainStepDb(), 1.0);
    QCOMPARE(controller.bandGain(0), 1.5);
    QVERIFY(controller.setBandGain(0, 1.26));
    QCOMPARE(controller.bandGain(0), 1.0);
    QVERIFY(!controller.setPrecisionMode(QStringLiteral("ultra")));
    QCOMPARE(controller.precisionMode(), QStringLiteral("low"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    QCOMPARE(settings.value(QStringLiteral("precisionMode")).toString(),
             QStringLiteral("low"));
    settings.endGroup();
    EqualizerController restored(player);
    QCOMPARE(restored.precisionMode(), QStringLiteral("low"));
    QCOMPARE(restored.gainStepDb(), 1.0);
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

void EqualizerControllerTest::responseCurveIncludesAutomaticProtectionAndActiveSampleRate()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    QCOMPARE(agplayer::editor::load_editor_playback_stream(
                 player, std::make_shared<ConstantAudioStream>()),
             AG_OK);
    EqualizerController controller(player);
    controller.setEnabled(true);
    QVERIFY(controller.setBandGain(5, 6.0));
    controller.refreshStatus();

    QCOMPARE(controller.sampleRate(), 48'000);
    QVERIFY(controller.active());
    const QVariantList protectedCurve = controller.responseCurve(96);
    QCOMPARE(protectedCurve.size(), 96);
    const auto peak = std::max_element(
        protectedCurve.cbegin(), protectedCurve.cend(),
        [](const QVariant& left, const QVariant& right) {
            return left.toDouble() < right.toDouble();
        });
    QVERIFY(peak != protectedCurve.cend());
    QVERIFY(peak->toDouble() <= -0.49);
    ag_player_destroy(player);
}

void EqualizerControllerTest::refreshStatusPublishesOutputPeakOnlyWhenItChanges()
{
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    ag_player* player = nullptr;
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    QCOMPARE(agplayer::editor::load_editor_playback_stream(
                 player, std::make_shared<ConstantAudioStream>()),
             AG_OK);

    EqualizerController controller(player);
    QSignalSpy peakChanged(&controller,
                           &EqualizerController::outputPeakDbChanged);
    QCOMPARE(controller.outputPeakDb(), -120.0);
    QCOMPARE(ag_player_play(player), AG_OK);
    QTRY_VERIFY_WITH_TIMEOUT(
        (controller.refreshStatus(), controller.outputPeakDb() > -100.0),
        1'000);
    QCOMPARE(peakChanged.count(), 1);

    ag_equalizer_status status{};
    QCOMPARE(ag_player_equalizer_status(player, &status), AG_OK);
    controller.refreshStatus();
    QCOMPARE(controller.outputPeakDb(), status.output_peak_db);
    QCOMPARE(peakChanged.count(), 1);

    QCOMPARE(ag_player_set_muted(player, 1), AG_OK);
    QTRY_COMPARE_WITH_TIMEOUT(
        (controller.refreshStatus(), controller.outputPeakDb()),
        -120.0, 250);
    QCOMPARE(peakChanged.count(), 2);
    controller.refreshStatus();
    QCOMPARE(peakChanged.count(), 2);

    ag_player_destroy(player);
}

QTEST_MAIN(EqualizerControllerTest)
#include "equalizer_controller_test.moc"
