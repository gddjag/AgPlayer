#include "translation_manager.hpp"

#include <QTest>

class TranslationManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesOnlyRequestedLanguages();
    void normalizesUnsupportedLanguageToChinese();
    void switchesInstalledQtTranslation();
};

void TranslationManagerTest::exposesOnlyRequestedLanguages()
{
    QCOMPARE(TranslationManager::supportedLanguages(),
             QStringList({QStringLiteral("zh"), QStringLiteral("en"),
                          QStringLiteral("th"), QStringLiteral("vi")}));
}

void TranslationManagerTest::normalizesUnsupportedLanguageToChinese()
{
    QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("EN")),
             QStringLiteral("en"));
    QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("ko")),
             QStringLiteral("zh"));
    QCOMPARE(TranslationManager::normalizedLanguage(QString()),
             QStringLiteral("zh"));
}

void TranslationManagerTest::switchesInstalledQtTranslation()
{
    TranslationManager translations;
    const auto startupTitle = []() {
        return QCoreApplication::translate("EmptyStartup", "开始播放你的音乐");
    };

    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    QCOMPARE(startupTitle(), QStringLiteral("Start playing your music"));

    QVERIFY(translations.setLanguage(QStringLiteral("th")));
    QCOMPARE(startupTitle(), QStringLiteral("เริ่มเล่นเพลงของคุณ"));

    QVERIFY(translations.setLanguage(QStringLiteral("vi")));
    QCOMPARE(startupTitle(), QStringLiteral("Bắt đầu phát nhạc của bạn"));

    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(startupTitle(), QStringLiteral("开始播放你的音乐"));
}

QTEST_MAIN(TranslationManagerTest)
#include "translation_manager_test.moc"
