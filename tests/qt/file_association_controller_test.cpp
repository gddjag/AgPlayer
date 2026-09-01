#include <QCoreApplication>
#include <QTest>

#include <memory>

#include "file_association_controller.hpp"
#include "audio_file_discovery.hpp"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

class FileAssociationControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        controller_ = std::make_unique<FileAssociationController>();
        // Clean up any leftover state from a previous run.
        controller_->unregisterAll();
    }

    void cleanupTestCase()
    {
        if (controller_) {
            controller_->unregisterAll();
            controller_.reset();
        }
    }

    void registerAndQuery()
    {
#ifdef Q_OS_WIN
        const QStringList extensions = {QStringLiteral("agptest")};
        QVERIFY(controller_->registerForExtensions(extensions));
        QVERIFY(controller_->isAssociated(QStringLiteral("agptest")));

        QVERIFY(controller_->unregisterForExtensions(extensions));
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));
#else
        QVERIFY(!controller_->registerForExtensions({QStringLiteral("agptest")}));
        QCOMPARE(controller_->lastError(), QStringLiteral("not supported on this platform"));
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));
#endif
    }

    void supportedAudioExtensionsCoverDropFormats_data()
    {
        QTest::addColumn<QString>("extension");
        for (const QString& extension : {
                 QStringLiteral("mp3"), QStringLiteral("wav"),
                 QStringLiteral("flac"), QStringLiteral("aac"),
                 QStringLiteral("m4a"), QStringLiteral("ogg"),
                 QStringLiteral("wma"), QStringLiteral("ape"),
                 QStringLiteral("opus"), QStringLiteral("aif"),
                 QStringLiteral("aiff")}) {
            QTest::newRow(qPrintable(extension)) << extension;
        }
    }

    void supportedAudioExtensionsCoverDropFormats()
    {
        QFETCH(QString, extension);
        QVERIFY(FileAssociationController::supportedAudioExtensions().contains(extension));
        QVERIFY(agplayer::qt::isSupportedAudioExtension(extension));
        QVERIFY(agplayer::qt::isSupportedAudioExtension(
            QStringLiteral(".") + extension.toUpper()));
    }

    void supportedVideoExtensionsAreDistinctAndCaseInsensitive()
    {
        // Catches hidden video formats leaking into the audio choice list, or
        // a Windows association failing for an upper-case direct-path suffix.
        const QStringList expected{
            QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
            QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v")};
        QCOMPARE(agplayer::qt::supportedVideoExtensions(), expected);
        for (const QString& extension : expected) {
            QVERIFY(!FileAssociationController::supportedAudioExtensions().contains(extension));
            QVERIFY(agplayer::qt::isSupportedVideoExtension(extension.toUpper()));
        }
    }

    void unregisterAllRemovesRegisteredVideoCapabilities()
    {
#ifdef Q_OS_WIN
        // Catches default-player cleanup leaving a video extension or its
        // capability entry behind after the user disables the existing switch.
        const QStringList extensions{
            QStringLiteral("agptest"), QStringLiteral("MP4")};
        QVERIFY(controller_->registerForExtensions(extensions));
        QVERIFY(controller_->isAssociated(QStringLiteral(".mp4")));
        QVERIFY(controller_->unregisterAll());
        QVERIFY(!controller_->isAssociated(QStringLiteral("mp4")));

        const QString associationPath = QStringLiteral(
            "Software\\AgPlayer\\Capabilities\\FileAssociations");
        const std::wstring associationPathW = associationPath.toStdWString();
        HKEY key = nullptr;
        QVERIFY(RegOpenKeyExW(HKEY_CURRENT_USER, associationPathW.c_str(), 0,
                              KEY_READ, &key) != ERROR_SUCCESS);
#else
        QSKIP("Windows Default Apps capabilities are Windows-only");
#endif
    }

    void unregisterAllClearsStaleProgIdWithoutDeletingAnotherDefault()
    {
#ifdef Q_OS_WIN
        // Catches cleanup deleting another player's current default, or leaving
        // AgPlayer's stale OpenWithProgids entry after the default changes.
        const QString extension = QStringLiteral("agpteststale");
        QVERIFY(controller_->registerForExtensions({extension}));

        const QString extensionPath =
            QStringLiteral("Software\\Classes\\.%1").arg(extension);
        const std::wstring extensionPathW = extensionPath.toStdWString();
        const QString otherProgId = QStringLiteral("OtherPlayer.File");
        const std::wstring otherProgIdW = otherProgId.toStdWString();
        QCOMPARE(RegSetKeyValueW(HKEY_CURRENT_USER, extensionPathW.c_str(), nullptr,
                                 REG_SZ, otherProgIdW.c_str(),
                                 static_cast<DWORD>((otherProgIdW.size() + 1)
                                                    * sizeof(wchar_t))),
                 static_cast<LSTATUS>(ERROR_SUCCESS));

        QVERIFY(controller_->unregisterAll());

        HKEY extensionKey = nullptr;
        QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER, extensionPathW.c_str(), 0,
                               KEY_READ, &extensionKey),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
        wchar_t defaultValue[256] = {};
        DWORD defaultValueSize = sizeof(defaultValue);
        DWORD valueType = 0;
        QCOMPARE(RegQueryValueExW(extensionKey, nullptr, nullptr, &valueType,
                                  reinterpret_cast<LPBYTE>(defaultValue),
                                  &defaultValueSize),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
        RegCloseKey(extensionKey);
        QCOMPARE(QString::fromWCharArray(defaultValue), otherProgId);

        const QString openWithPath = extensionPath + QStringLiteral("\\OpenWithProgids");
        const std::wstring openWithPathW = openWithPath.toStdWString();
        HKEY openWithKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, openWithPathW.c_str(), 0, KEY_READ,
                          &openWithKey) == ERROR_SUCCESS) {
            DWORD staleValueSize = 0;
            QCOMPARE(RegQueryValueExW(openWithKey, L"AgPlayerAudioFile", nullptr,
                                      &valueType, nullptr, &staleValueSize),
                     static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND));
            RegCloseKey(openWithKey);
        }
        QCOMPARE(RegDeleteTreeW(HKEY_CURRENT_USER, extensionPathW.c_str()),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
#else
        QSKIP("Windows Default Apps capabilities are Windows-only");
#endif
    }

    void registersWindowsDefaultAppsCapabilities()
    {
#ifdef Q_OS_WIN
        QVERIFY(controller_->registerForExtensions({QStringLiteral("agptest")}));

        const QString registeredPath =
            QStringLiteral("Software\\RegisteredApplications");
        const std::wstring registeredPathW = registeredPath.toStdWString();
        HKEY registeredKey = nullptr;
        QVERIFY(RegOpenKeyExW(HKEY_CURRENT_USER, registeredPathW.c_str(), 0,
                             KEY_READ, &registeredKey) == ERROR_SUCCESS);
        wchar_t capabilityPath[512] = {};
        DWORD capabilityPathSize = sizeof(capabilityPath);
        DWORD valueType = 0;
        QCOMPARE(RegQueryValueExW(
                     registeredKey, L"AgPlayer", nullptr, &valueType,
                     reinterpret_cast<LPBYTE>(capabilityPath),
                     &capabilityPathSize),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
        RegCloseKey(registeredKey);
        QCOMPARE(valueType, static_cast<DWORD>(REG_SZ));
        QCOMPARE(QString::fromWCharArray(capabilityPath),
                 QStringLiteral("Software\\AgPlayer\\Capabilities"));

        const QString associationPath = QStringLiteral(
            "Software\\AgPlayer\\Capabilities\\FileAssociations");
        const std::wstring associationPathW = associationPath.toStdWString();
        HKEY associationKey = nullptr;
        QVERIFY(RegOpenKeyExW(HKEY_CURRENT_USER, associationPathW.c_str(), 0,
                             KEY_READ, &associationKey) == ERROR_SUCCESS);
        wchar_t progId[256] = {};
        DWORD progIdSize = sizeof(progId);
        valueType = 0;
        QCOMPARE(RegQueryValueExW(
                     associationKey, L".agptest", nullptr, &valueType,
                     reinterpret_cast<LPBYTE>(progId), &progIdSize),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
        RegCloseKey(associationKey);
        QCOMPARE(QString::fromWCharArray(progId),
                 QStringLiteral("AgPlayerAudioFile"));

        const QString iconPath = QStringLiteral(
            "Software\\Classes\\AgPlayerAudioFile\\DefaultIcon");
        const std::wstring iconPathW = iconPath.toStdWString();
        HKEY iconKey = nullptr;
        QVERIFY(RegOpenKeyExW(HKEY_CURRENT_USER, iconPathW.c_str(), 0,
                             KEY_READ, &iconKey) == ERROR_SUCCESS);
        wchar_t iconValue[1024] = {};
        DWORD iconValueSize = sizeof(iconValue);
        valueType = 0;
        QCOMPARE(RegQueryValueExW(
                     iconKey, nullptr, nullptr, &valueType,
                     reinterpret_cast<LPBYTE>(iconValue), &iconValueSize),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
        RegCloseKey(iconKey);
        QCOMPARE(valueType, static_cast<DWORD>(REG_SZ));
        QCOMPARE(QString::fromWCharArray(iconValue),
                 QCoreApplication::applicationFilePath() + QStringLiteral(",0"));

        const QString commandPath = QStringLiteral(
            "Software\\Classes\\AgPlayerAudioFile\\shell\\open\\command");
        const std::wstring commandPathW = commandPath.toStdWString();
        HKEY commandKey = nullptr;
        QVERIFY(RegOpenKeyExW(HKEY_CURRENT_USER, commandPathW.c_str(), 0,
                             KEY_READ, &commandKey) == ERROR_SUCCESS);
        wchar_t commandValue[2048] = {};
        DWORD commandValueSize = sizeof(commandValue);
        valueType = 0;
        QCOMPARE(RegQueryValueExW(
                     commandKey, nullptr, nullptr, &valueType,
                     reinterpret_cast<LPBYTE>(commandValue), &commandValueSize),
                 static_cast<LSTATUS>(ERROR_SUCCESS));
        RegCloseKey(commandKey);
        QCOMPARE(QString::fromWCharArray(commandValue),
                 QStringLiteral("\"%1\" \"%2\"")
                     .arg(QCoreApplication::applicationFilePath(),
                          QStringLiteral("%1")));
#else
        QSKIP("Windows Default Apps capabilities are Windows-only");
#endif
    }

    void unregisterAllRemovesProgIdAndExtensions()
    {
#ifdef Q_OS_WIN
        const QStringList extensions = {QStringLiteral("agptest")};
        QVERIFY(controller_->registerForExtensions(extensions));
        QVERIFY(controller_->unregisterAll());
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));

        const QString progIdPath = QStringLiteral("Software\\Classes\\AgPlayerAudioFile");
        const std::wstring progIdPathW = progIdPath.toStdWString();
        HKEY key = nullptr;
        const bool exists =
            RegOpenKeyExW(HKEY_CURRENT_USER, progIdPathW.c_str(), 0, KEY_READ, &key)
            == ERROR_SUCCESS;
        if (exists) {
            RegCloseKey(key);
        }
        QVERIFY(!exists);
#else
        QVERIFY(!controller_->unregisterAll());
#endif
    }

    void unregisterAllIsIdempotent()
    {
#ifdef Q_OS_WIN
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->lastError().isEmpty());
#else
        QVERIFY(!controller_->unregisterAll());
#endif
    }

private:
    std::unique_ptr<FileAssociationController> controller_;
};

QTEST_MAIN(FileAssociationControllerTest)

#include "file_association_controller_test.moc"
