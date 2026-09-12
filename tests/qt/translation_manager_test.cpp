#include "translation_manager.hpp"

#include <QTest>

class TranslationManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesOnlyRequestedLanguages();
    void normalizesUnsupportedLanguageToChinese();
    void switchesInstalledQtTranslation();
    void switchesDialogButtonsAndFilenamePlaceholders();
    void switchesMetadataNotices();
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

void TranslationManagerTest::switchesDialogButtonsAndFilenamePlaceholders()
{
    TranslationManager translations;
    const char* labels[] = {"确定", "取消", "是", "否", "关闭"};
    const char* english[] = {"OK", "Cancel", "Yes", "No", "Close"};
    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    for (int i = 0; i < 5; ++i)
        QCOMPARE(QCoreApplication::translate("ThemedDialog", labels[i]),
                 QString::fromUtf8(english[i]));
    QCOMPARE(QCoreApplication::translate("FilenameProcessPage", "输入要添加的前缀"),
             QStringLiteral("Enter a prefix to add"));
    QCOMPARE(QCoreApplication::translate("FilenameProcessPage", "输入要添加的后缀"),
             QStringLiteral("Enter a suffix to add"));
    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    for (const auto* label : labels)
        QCOMPARE(QCoreApplication::translate("ThemedDialog", label),
                 QString::fromUtf8(label));
}

void TranslationManagerTest::switchesMetadataNotices()
{
    TranslationManager translations;
    const char* noFiles = "No new supported audio files were found.";
    const char* coverError = "Could not open the cover image: %1";
    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(QCoreApplication::translate("MetadataEditor", noFiles),
             QStringLiteral("未找到可新增的受支持音频文件。"));
    QCOMPARE(QCoreApplication::translate("MetadataEditor", coverError).arg("cover.png"),
             QStringLiteral("无法打开封面图片：cover.png"));
    QCOMPARE(QCoreApplication::translate("MetadataEditPage", "元数据处理提示"),
             QStringLiteral("元数据处理提示"));
    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    QCOMPARE(QCoreApplication::translate("MetadataEditor", noFiles),
             QString::fromUtf8(noFiles));
    QCOMPARE(QCoreApplication::translate("MetadataEditor", coverError).arg("cover.png"),
             QStringLiteral("Could not open the cover image: cover.png"));
    QCOMPARE(QCoreApplication::translate("MetadataEditPage", "元数据处理提示"),
             QStringLiteral("Metadata notice"));
}

QTEST_MAIN(TranslationManagerTest)
#include "translation_manager_test.moc"
