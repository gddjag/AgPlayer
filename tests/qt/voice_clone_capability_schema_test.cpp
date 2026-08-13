#include "voice_clone_capability_schema.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using agplayer::voice_clone::validateCapabilitySchema;
using agplayer::voice_clone::validateParameters;

namespace {

QJsonObject control(const QString& key,
                    const QString& type,
                    const QString& group,
                    const QJsonValue& defaultValue,
                    const bool required = false)
{
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("label"), key},
        {QStringLiteral("description"), key + QStringLiteral(" control")},
        {QStringLiteral("type"), type},
        {QStringLiteral("group"), group},
        {QStringLiteral("default"), defaultValue},
        {QStringLiteral("required"), required},
    };
}

QJsonObject completeSchema()
{
    QJsonObject language = control(QStringLiteral("language"),
                                   QStringLiteral("enum"),
                                   QStringLiteral("basic"),
                                   QStringLiteral("zh"));
    language.insert(QStringLiteral("options"), QJsonArray{QStringLiteral("zh"), QStringLiteral("en")});

    QJsonObject seed = control(QStringLiteral("seed"),
                               QStringLiteral("int"),
                               QStringLiteral("advanced"),
                               42);
    seed.insert(QStringLiteral("minimum"), 0);
    seed.insert(QStringLiteral("maximum"), 100);
    seed.insert(QStringLiteral("step"), 1);

    QJsonObject speed = control(QStringLiteral("speed"),
                                QStringLiteral("double"),
                                QStringLiteral("advanced"),
                                1.0);
    speed.insert(QStringLiteral("minimum"), 0.5);
    speed.insert(QStringLiteral("maximum"), 2.0);
    speed.insert(QStringLiteral("step"), 0.1);

    QJsonObject prompt = control(QStringLiteral("prompt"),
                                 QStringLiteral("string"),
                                 QStringLiteral("basic"),
                                 QStringLiteral(""));
    prompt.insert(QStringLiteral("maximumLength"), 160);

    QJsonObject referenceAudio = control(QStringLiteral("referenceAudio"),
                                         QStringLiteral("file"),
                                         QStringLiteral("basic"),
                                         QStringLiteral(""),
                                         true);
    referenceAudio.insert(QStringLiteral("extensions"), QJsonArray{QStringLiteral("wav"), QStringLiteral("flac")});

    QJsonObject advancedKnob = control(QStringLiteral("advancedKnob"),
                                       QStringLiteral("int"),
                                       QStringLiteral("advanced"),
                                       3);
    advancedKnob.insert(QStringLiteral("minimum"), 1);
    advancedKnob.insert(QStringLiteral("maximum"), 5);
    advancedKnob.insert(QStringLiteral("visibleWhen"),
                        QJsonObject{{QStringLiteral("key"), QStringLiteral("enableAdvanced")},
                                    {QStringLiteral("equals"), true}});

    return {
        {QStringLiteral("protocolVersion"), 1},
        {QStringLiteral("groups"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("basic")},
                                {QStringLiteral("label"), QStringLiteral("Basic")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("advanced")},
                                {QStringLiteral("label"), QStringLiteral("Advanced")}}}},
        {QStringLiteral("parameters"),
         QJsonArray{control(QStringLiteral("enableAdvanced"),
                            QStringLiteral("bool"),
                            QStringLiteral("basic"),
                            false),
                    language,
                    seed,
                    speed,
                    prompt,
                    referenceAudio,
                    advancedKnob}},
    };
}

QJsonObject validParameters(const QString& referenceAudio)
{
    return {
        {QStringLiteral("enableAdvanced"), true},
        {QStringLiteral("language"), QStringLiteral("zh")},
        {QStringLiteral("seed"), 42},
        {QStringLiteral("speed"), 1.2},
        {QStringLiteral("prompt"), QStringLiteral("请保持自然语速")},
        {QStringLiteral("referenceAudio"), referenceAudio},
        {QStringLiteral("advancedKnob"), 3},
    };
}

} // namespace

class VoiceCloneCapabilitySchemaTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsBasicAndAdvancedControlsWithValidDefaults();
    void rejectsOutOfRangeDefaultsAndProtocolMismatches();
    void validatesConditionalVisibilityAndRejectsUnknownParameters();
    void rejectsInvalidIntEnumStringAndFileParameters();
};

void VoiceCloneCapabilitySchemaTest::acceptsBasicAndAdvancedControlsWithValidDefaults()
{
    const QJsonObject schema = completeSchema();
    const auto validation = validateCapabilitySchema(schema);
    QVERIFY2(validation.isValid(), qPrintable(validation.errorString()));
    QCOMPARE(validation.groups(), QStringList({QStringLiteral("basic"), QStringLiteral("advanced")}));
    QCOMPARE(validation.controlTypes(), QStringList({QStringLiteral("bool"),
                                                     QStringLiteral("enum"),
                                                     QStringLiteral("int"),
                                                     QStringLiteral("double"),
                                                     QStringLiteral("string"),
                                                     QStringLiteral("file")}));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referenceAudio = temporary.filePath(QStringLiteral("reference.wav"));
    QFile referenceFile(referenceAudio);
    QVERIFY(referenceFile.open(QIODevice::WriteOnly));
    referenceFile.close();
    const auto parameters = validateParameters(schema, validParameters(referenceAudio));
    QVERIFY2(parameters.isValid(), qPrintable(parameters.errorString()));
}

void VoiceCloneCapabilitySchemaTest::rejectsOutOfRangeDefaultsAndProtocolMismatches()
{
    QJsonObject schema = completeSchema();
    QJsonArray controls = schema.value(QStringLiteral("parameters")).toArray();
    QJsonObject speed = controls[3].toObject();
    speed.insert(QStringLiteral("default"), 2.1);
    controls[3] = speed;
    schema.insert(QStringLiteral("parameters"), controls);
    auto validation = validateCapabilitySchema(schema);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("default"), Qt::CaseInsensitive));

    schema = completeSchema();
    schema.insert(QStringLiteral("protocolVersion"), 2);
    validation = validateCapabilitySchema(schema);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("protocol"), Qt::CaseInsensitive));
}

void VoiceCloneCapabilitySchemaTest::validatesConditionalVisibilityAndRejectsUnknownParameters()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referenceAudio = temporary.filePath(QStringLiteral("reference.flac"));
    QFile referenceFile(referenceAudio);
    QVERIFY(referenceFile.open(QIODevice::WriteOnly));
    referenceFile.close();

    QJsonObject hiddenAdvanced = validParameters(referenceAudio);
    hiddenAdvanced.insert(QStringLiteral("enableAdvanced"), false);
    auto parameters = validateParameters(completeSchema(), hiddenAdvanced);
    QVERIFY(!parameters.isValid());
    QVERIFY(parameters.errorString().contains(QStringLiteral("advancedKnob")));

    QJsonObject outOfRange = validParameters(referenceAudio);
    outOfRange.insert(QStringLiteral("speed"), 2.1);
    parameters = validateParameters(completeSchema(), outOfRange);
    QVERIFY(!parameters.isValid());
    QVERIFY(parameters.errorString().contains(QStringLiteral("speed")));

    QJsonObject unknown = validParameters(referenceAudio);
    unknown.insert(QStringLiteral("workerOnlySecret"), true);
    parameters = validateParameters(completeSchema(), unknown);
    QVERIFY(!parameters.isValid());
    QVERIFY(parameters.errorString().contains(QStringLiteral("workerOnlySecret")));
}

void VoiceCloneCapabilitySchemaTest::rejectsInvalidIntEnumStringAndFileParameters()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referenceAudio = temporary.filePath(QStringLiteral("reference.wav"));
    QFile referenceFile(referenceAudio);
    QVERIFY(referenceFile.open(QIODevice::WriteOnly));
    referenceFile.close();

    QJsonObject values = validParameters(referenceAudio);
    values.insert(QStringLiteral("seed"), 101);
    auto validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("seed")));

    values = validParameters(referenceAudio);
    values.insert(QStringLiteral("seed"), 42.5);
    validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("seed")));

    values = validParameters(referenceAudio);
    values.insert(QStringLiteral("language"), QStringLiteral("de"));
    validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("language")));

    values = validParameters(referenceAudio);
    values.insert(QStringLiteral("prompt"), QString(161, QLatin1Char('x')));
    validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("prompt")));

    values = validParameters(referenceAudio);
    values.remove(QStringLiteral("referenceAudio"));
    validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("referenceAudio"))
            || validation.errorString().contains(QStringLiteral("required"), Qt::CaseInsensitive));

    values = validParameters(temporary.filePath(QStringLiteral("missing.wav")));
    validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("referenceAudio"))
            || validation.errorString().contains(QStringLiteral("exist"), Qt::CaseInsensitive));

    const QString unsupportedAudio = temporary.filePath(QStringLiteral("reference.mp3"));
    QFile unsupportedFile(unsupportedAudio);
    QVERIFY(unsupportedFile.open(QIODevice::WriteOnly));
    unsupportedFile.close();
    values = validParameters(unsupportedAudio);
    validation = validateParameters(completeSchema(), values);
    QVERIFY(!validation.isValid());
    QVERIFY(validation.errorString().contains(QStringLiteral("referenceAudio"))
            || validation.errorString().contains(QStringLiteral("extension"), Qt::CaseInsensitive));
}

QTEST_APPLESS_MAIN(VoiceCloneCapabilitySchemaTest)

#include "voice_clone_capability_schema_test.moc"
