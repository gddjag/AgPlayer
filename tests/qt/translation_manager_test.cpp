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
        return QCoreApplication::translate("ListWindow", "AgPlayer 音乐列表");
    };
    const auto lightEditLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "轻度剪辑");
    };
    const auto metadataLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "元数据修改");
    };
    const auto filenameLabel = []() {
        return QCoreApplication::translate("ToolSidebar", "文件名处理");
    };
    const auto undoLabel = []() {
        return QCoreApplication::translate("LightEditPage", "撤销");
    };
    const auto channelLabel = []() {
        return QCoreApplication::translate("LightEditPage", "声道");
    };
    const auto libraryManagerLabel = []() {
        return QCoreApplication::translate("LibraryManagerPage", "曲库管理");
    };

    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));

    QVERIFY(translations.setLanguage(QStringLiteral("en")));
    QCOMPARE(startupTitle(), QStringLiteral("Start playing your music"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("Your library is empty"));
    QCOMPARE(musicListTitle(), QStringLiteral("AgPlayer Music Library"));
    QCOMPARE(lightEditLabel(), QStringLiteral("Light Edit"));
    QCOMPARE(metadataLabel(), QStringLiteral("Metadata Editor"));
    QCOMPARE(filenameLabel(), QStringLiteral("Filename Processing"));
    QCOMPARE(undoLabel(), QStringLiteral("Undo"));
    QCOMPARE(channelLabel(), QStringLiteral("Channels"));
    QCOMPARE(libraryManagerLabel(), QStringLiteral("Library Management"));

    QVERIFY(translations.setLanguage(QStringLiteral("th")));
    QCOMPARE(startupTitle(), QStringLiteral("เริ่มเล่นเพลงของคุณ"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("ห้องสมุดของคุณว่างเปล่า"));
    QCOMPARE(musicListTitle(), QStringLiteral("คลังเพลง AgPlayer"));
    QCOMPARE(lightEditLabel(), QStringLiteral("ตัดต่อเบื้องต้น"));
    QCOMPARE(metadataLabel(), QStringLiteral("แก้ไขเมตาดาตา"));
    QCOMPARE(filenameLabel(), QStringLiteral("จัดการชื่อไฟล์"));
    QCOMPARE(undoLabel(), QStringLiteral("เลิกทำ"));
    QCOMPARE(channelLabel(), QStringLiteral("ช่องสัญญาณ"));
    QCOMPARE(libraryManagerLabel(), QStringLiteral("การจัดการคลังเพลง"));

    QVERIFY(translations.setLanguage(QStringLiteral("vi")));
    QCOMPARE(startupTitle(), QStringLiteral("Bắt đầu phát nhạc của bạn"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("Thư viện của bạn trống"));
    QCOMPARE(musicListTitle(), QStringLiteral("Thư viện nhạc AgPlayer"));
    QCOMPARE(lightEditLabel(), QStringLiteral("Chỉnh sửa cơ bản"));
    QCOMPARE(metadataLabel(), QStringLiteral("Sửa siêu dữ liệu"));
    QCOMPARE(filenameLabel(), QStringLiteral("Xử lý tên tệp"));
    QCOMPARE(undoLabel(), QStringLiteral("Hoàn tác"));
    QCOMPARE(channelLabel(), QStringLiteral("Kênh"));
    QCOMPARE(libraryManagerLabel(), QStringLiteral("Quản lý thư viện"));

    QVERIFY(translations.setLanguage(QStringLiteral("zh")));
    QCOMPARE(startupTitle(), QStringLiteral("开始播放你的音乐"));
    QCOMPARE(emptyLibraryTitle(), QStringLiteral("音乐库为空"));
}

QTEST_MAIN(TranslationManagerTest)
#include "translation_manager_test.moc"
