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
    const auto emptyLibraryTitle = []() {
        return QCoreApplication::translate("EmptyLibrary", "Your library is empty");
    };
    const auto musicListTitle = []() {
        return QCoreApplication::translate("ListWindow", "音乐列表");
    };
    const auto lightEditLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "轻度剪辑");
    };
    const auto undoLabel = []() {
        return QCoreApplication::translate("LightEditPage", "撤销");
    };
    const auto channelLabel = []() {
        return QCoreApplication::translate("LightEditPage", "声道");
    };

    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));

    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    QCOMPARE(startupTitle(), QStringLiteral("Start playing your music"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("Your library is empty"));
    QCOMPARE(musicListTitle(), QStringLiteral("Music List"));
    QCOMPARE(lightEditLabel(), QStringLiteral("Light Edit"));
    QCOMPARE(undoLabel(), QStringLiteral("Undo"));
    QCOMPARE(channelLabel(), QStringLiteral("Channels"));

    QVERIFY(translations.setLanguage(QStringLiteral("th")));
    QCOMPARE(startupTitle(), QStringLiteral("เริ่มเล่นเพลงของคุณ"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("ห้องสมุดของคุณว่างเปล่า"));
    QCOMPARE(musicListTitle(), QStringLiteral("รายการเพลง"));
    QCOMPARE(lightEditLabel(), QStringLiteral("ตัดต่อเบื้องต้น"));
    QCOMPARE(undoLabel(), QStringLiteral("เลิกทำ"));
    QCOMPARE(channelLabel(), QStringLiteral("ช่องสัญญาณ"));

    QVERIFY(translations.setLanguage(QStringLiteral("vi")));
    QCOMPARE(startupTitle(), QStringLiteral("Bắt đầu phát nhạc của bạn"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("Thư viện của bạn trống"));
    QCOMPARE(musicListTitle(), QStringLiteral("Danh sách nhạc"));
    QCOMPARE(lightEditLabel(), QStringLiteral("Chỉnh sửa cơ bản"));
    QCOMPARE(undoLabel(), QStringLiteral("Hoàn tác"));
    QCOMPARE(channelLabel(), QStringLiteral("Kênh"));

    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(startupTitle(), QStringLiteral("开始播放你的音乐"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));
}

QTEST_MAIN(TranslationManagerTest)
#include "translation_manager_test.moc"
