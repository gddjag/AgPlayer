#include "separation_protocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace agplayer::separation;

class SeparationProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesEveryVersionTwoMessage();
    void rejectsMalformedUnknownAndIncompatibleMessages();
    void rejectsOversizedNdjsonLines();
    void serializesEveryMessageWithVersionAndRequestId();
};

void SeparationProtocolTest::parsesEveryVersionTwoMessage()
{
    const QStringList types{QStringLiteral("hello"), QStringLiteral("probe"),
                            QStringLiteral("start"), QStringLiteral("progress"),
                            QStringLiteral("cancel"), QStringLiteral("result"),
                            QStringLiteral("error"), QStringLiteral("shutdown")};

    for (const QString& type : types) {
        const QByteArray line = QJsonDocument(QJsonObject{
            {QStringLiteral("protocol"), 2},
            {QStringLiteral("requestId"), QStringLiteral("request-7")},
            {QStringLiteral("type"), type},
            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("value"), 7}}},
        }).toJson(QJsonDocument::Compact);
        const ProtocolParseResult parsed = parseProtocolMessage(line);
        QVERIFY2(parsed.ok, qPrintable(parsed.error.message));
        QCOMPARE(protocolTypeName(parsed.message.type), type);
        QCOMPARE(parsed.message.protocol, 2);
        QCOMPARE(parsed.message.requestId, QStringLiteral("request-7"));
        QCOMPARE(parsed.message.payload.value(QStringLiteral("value")).toInt(), 7);
    }
}

void SeparationProtocolTest::rejectsMalformedUnknownAndIncompatibleMessages()
{
    struct Case {
        QByteArray line;
        QString code;
    };
    const QList<Case> cases{
        {QByteArrayLiteral("{bad"), QStringLiteral("malformed_json")},
        {QByteArrayLiteral("[]"), QStringLiteral("malformed_message")},
        {QByteArrayLiteral(R"({"protocol":1,"requestId":"r","type":"hello"})"),
         QStringLiteral("incompatible_protocol")},
        {QByteArrayLiteral(R"({"protocol":3,"requestId":"r","type":"hello"})"),
         QStringLiteral("incompatible_protocol")},
        {QByteArrayLiteral(R"({"protocol":2,"requestId":"r","type":"surprise"})"),
         QStringLiteral("unknown_message")},
        {QByteArrayLiteral(R"({"protocol":2,"requestId":"","type":"hello"})"),
         QStringLiteral("invalid_request_id")},
        {QByteArrayLiteral(R"({"protocol":2,"requestId":7,"type":"hello"})"),
         QStringLiteral("invalid_request_id")},
        {QByteArrayLiteral(R"({"protocol":2,"requestId":"r","type":"hello","payload":[]})"),
         QStringLiteral("malformed_message")},
        {QByteArrayLiteral(R"({"protocol":2,"requestId":"r","type":"hello","extra":true})"),
         QStringLiteral("malformed_message")},
    };

    for (const Case& testCase : cases) {
        const ProtocolParseResult parsed = parseProtocolMessage(testCase.line);
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.error.code, testCase.code);
        if (testCase.code == QStringLiteral("incompatible_protocol")) {
            QCOMPARE(parsed.error.message,
                     QStringLiteral("Only separation protocol version 2 is supported"));
        }
    }
}

void SeparationProtocolTest::rejectsOversizedNdjsonLines()
{
    const ProtocolParseResult oversized = parseProtocolMessage(
        QByteArray(1024 * 1024 + 1, 'x'));
    QVERIFY(!oversized.ok);
    QCOMPARE(oversized.error.code, QStringLiteral("message_too_large"));
}

void SeparationProtocolTest::serializesEveryMessageWithVersionAndRequestId()
{
    const QByteArray encoded = encodeProtocolMessage(
        ProtocolType::Progress, QStringLiteral("job-12"),
        QJsonObject{{QStringLiteral("fraction"), 0.25}});
    QVERIFY(encoded.endsWith('\n'));

    const QJsonObject object = QJsonDocument::fromJson(encoded.trimmed()).object();
    QCOMPARE(object.value(QStringLiteral("protocol")).toInt(), 2);
    QCOMPARE(object.value(QStringLiteral("requestId")).toString(), QStringLiteral("job-12"));
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("progress"));
    QCOMPARE(object.value(QStringLiteral("payload")).toObject()
                 .value(QStringLiteral("fraction")).toDouble(), 0.25);
}

QTEST_GUILESS_MAIN(SeparationProtocolTest)
#include "separation_protocol_test.moc"
