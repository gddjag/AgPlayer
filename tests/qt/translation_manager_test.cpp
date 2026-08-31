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
        return QCoreApplication::translate("ToolSidebar", "元数据编辑");
    };
    const auto filenameLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "文件名处理");
    };
    const auto thumbnailWaveformColorLabel = []() {
        return QCoreApplication::translate("SettingsPage", "缩略波形颜色");
    };
    const auto libraryResourceFoldersLabel = []() {
        return QCoreApplication::translate("LibraryNavigationModel", "资源文件夹");
    };
    const auto sideResourceFoldersLabel = []() {
        return QCoreApplication::translate("SideNavigation", "资源文件夹");
    };
    const auto searchPlaceholder = []() {
        return QCoreApplication::translate(
            "SearchFilter", "歌曲 · 艺术家 · 专辑 · 标签");
    };
    const auto vocalStemLabel = []() {
        return QCoreApplication::translate("VocalSeparationController", "Vocals");
    };
    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));
    QCOMPARE(vocalStemLabel(), QStringLiteral("人声"));

    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    QCOMPARE(startupTitle(), QStringLiteral("Start playing your music"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("Your library is empty"));
    QCOMPARE(musicListTitle(), QStringLiteral("AgPlayer Music Library"));
    QCOMPARE(audioEditLabel(), QStringLiteral("Audio Editor"));
    QCOMPARE(metadataLabel(), QStringLiteral("Metadata Editor"));
    QCOMPARE(filenameLabel(), QStringLiteral("Filename Processing"));
    QCOMPARE(thumbnailWaveformColorLabel(),
             QStringLiteral("Thumbnail waveform color"));
    QCOMPARE(libraryResourceFoldersLabel(), QStringLiteral("Resource Folders"));
    QCOMPARE(sideResourceFoldersLabel(), QStringLiteral("Resource Folders"));
    QCOMPARE(searchPlaceholder(), QStringLiteral("Song · Artist · Album · Tag"));
    QCOMPARE(vocalStemLabel(), QStringLiteral("Vocals"));

    QVERIFY(translations.setLanguage(QStringLiteral("th")));
    QCOMPARE(translations.language(), QStringLiteral("zh"));
    QCOMPARE(startupTitle(), QStringLiteral("开始播放你的音乐"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));
    QCOMPARE(thumbnailWaveformColorLabel(), QStringLiteral("缩略波形颜色"));
    QCOMPARE(libraryResourceFoldersLabel(), QStringLiteral("资源文件夹"));
    QCOMPARE(sideResourceFoldersLabel(), QStringLiteral("资源文件夹"));
    QCOMPARE(searchPlaceholder(), QStringLiteral("歌曲 · 艺术家 · 专辑 · 标签"));
}

QTEST_MAIN(TranslationManagerTest)
#include "translation_manager_test.moc"
