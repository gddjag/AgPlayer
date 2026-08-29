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
             QStringList({QStringLiteral("zh"), QStringLiteral("en")}));
}

void TranslationManagerTest::normalizesUnsupportedLanguageToChinese()
{
    QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("EN")),
             QStringLiteral("en"));
    QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("ko")),
             QStringLiteral("zh"));
    QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("th")),
             QStringLiteral("zh"));
    QCOMPARE(TranslationManager::normalizedLanguage(QStringLiteral("vi")),
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
        return QCoreApplication::translate("ListWindow", "AgPlayer 音乐列表");
    };
    const auto audioEditLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "音频编辑");
    };
    const auto metadataLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "元数据修改");
    };
    const auto filenameLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "文件名处理");
    };
    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));

    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    QCOMPARE(startupTitle(), QStringLiteral("Start playing your music"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("Your library is empty"));
    QCOMPARE(musicListTitle(), QStringLiteral("AgPlayer Music Library"));
    QCOMPARE(audioEditLabel(), QStringLiteral("Audio Editor"));
    QCOMPARE(metadataLabel(), QStringLiteral("Metadata Editor"));
    QCOMPARE(filenameLabel(), QStringLiteral("Filename Processing"));

    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(startupTitle(), QStringLiteral("开始播放你的音乐"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));
}

QTEST_MAIN(TranslationManagerTest)
#include "translation_manager_test.moc"
