#include <QCoreApplication>
#include <QScopeGuard>
#include <QTest>
#include <QUuid>

#include <memory>

#include "audio_file_discovery.hpp"
#include "file_association_controller.hpp"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

class FileAssociationControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        registryRoot_ = QStringLiteral("Software\\AgPlayer\\Tests\\%1")
                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
#endif
        controller_ = std::make_unique<FileAssociationController>(registryRoot_);
        removeTestRoot();
    }

    void cleanupTestCase()
    {
        controller_.reset();
        removeTestRoot();
    }

    void registryNamespaceIsIsolated()
    {
#ifdef Q_OS_WIN
        QCOMPARE(controller_->registryRootPath(), registryRoot_);
        QVERIFY(controller_->registryRootPath().startsWith(
            QStringLiteral("Software\\AgPlayer\\Tests\\")));
#elif defined(Q_OS_MACOS)
        QVERIFY(!controller_->registerForExtensions({QStringLiteral("agptest")}));
        QCOMPARE(controller_->lastError(),
                 QStringLiteral("Info.plist 未声明 .agptest 文件类型"));
#else
        QVERIFY(!controller_->registerForExtensions({QStringLiteral("agptest")}));
#endif
    }

    void registerAndQuery()
    {
#ifdef Q_OS_WIN
        const auto cleanupSandbox = qScopeGuard([this] { removeTestRoot(); });
        const QStringList extensions = {QStringLiteral("agptest")};
        QVERIFY(controller_->registerForExtensions(extensions));
        QVERIFY(controller_->isAssociated(QStringLiteral("agptest")));

        QVERIFY(controller_->unregisterForExtensions(extensions));
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));
#elif defined(Q_OS_MACOS)
        QVERIFY(!controller_->registerForExtensions({QStringLiteral("agptest")}));
        QCOMPARE(controller_->lastError(),
                 QStringLiteral("Info.plist 未声明 .agptest 文件类型"));
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
        QVERIFY(!agplayer::qt::isSupportedVideoExtension(extension));
    }

    void supportedVideoExtensionsAreDistinctAndCaseInsensitive()
    {
        const QStringList expected{
            QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
            QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v")};
        QCOMPARE(agplayer::qt::supportedVideoExtensions(), expected);
        for (const QString& extension : expected) {
            QVERIFY(!FileAssociationController::supportedAudioExtensions().contains(extension));
            QVERIFY(agplayer::qt::isSupportedVideoExtension(extension.toUpper()));
            QVERIFY(!agplayer::qt::isSupportedAudioExtension(extension));
        }
    }

    void registerAndUnregisterAllPreservesSharedExtensionData()
    {
#ifdef Q_OS_WIN
        const auto cleanupSandbox = qScopeGuard([this] { removeTestRoot(); });
        const QString extensionPath = registryPath(QStringLiteral("Classes\\.mp4"));
        QVERIFY(writeString(extensionPath, nullptr, QStringLiteral("OtherPlayer.File")));
        QVERIFY(writeString(extensionPath, L"PerceivedType", QStringLiteral("other-video")));
        QVERIFY(writeString(extensionPath + QStringLiteral("\\OpenWithProgids"),
                            L"OtherPlayer.File", QString()));
        QVERIFY(writeString(extensionPath + QStringLiteral("\\shell\\custom"), nullptr,
                            QStringLiteral("keep")));
        const QString sentinelPath = registryPath(QStringLiteral("AgPlayer\\Settings"));
        QVERIFY(writeString(sentinelPath, L"sentinel",
                            QStringLiteral("keep-settings")));

        const QStringList extensions = FileAssociationController::supportedAudioExtensions()
            + agplayer::qt::supportedVideoExtensions();
        QVERIFY(controller_->registerForExtensions(extensions));

        for (const QString& extension : extensions) {
            QVERIFY(controller_->isAssociated(extension));
            QCOMPARE(readString(registryPath(
                         QStringLiteral("AgPlayer\\Capabilities\\FileAssociations")),
                         QStringLiteral(".%1").arg(extension)),
                     QStringLiteral("AgPlayerAudioFile"));
            QCOMPARE(readString(registryPath(
                         QStringLiteral("Classes\\.%1\\OpenWithProgids").arg(extension)),
                         QStringLiteral("AgPlayerAudioFile")), QString());
        }
        QCOMPARE(readString(extensionPath, QString()), QStringLiteral("OtherPlayer.File"));
        QCOMPARE(readString(extensionPath, QStringLiteral("PerceivedType")),
                 QStringLiteral("other-video"));
        QCOMPARE(readString(extensionPath + QStringLiteral("\\OpenWithProgids"),
                            QStringLiteral("OtherPlayer.File")), QString());
        QCOMPARE(readString(extensionPath + QStringLiteral("\\shell\\custom"), QString()),
                 QStringLiteral("keep"));

        QVERIFY(controller_->unregisterAll());
        for (const QString& extension : extensions) {
            QVERIFY(!controller_->isAssociated(extension));
            QVERIFY(!valueExists(registryPath(
                         QStringLiteral("Classes\\.%1\\OpenWithProgids").arg(extension)),
                         QStringLiteral("AgPlayerAudioFile")));
        }
        QVERIFY(!keyExists(registryPath(QStringLiteral("AgPlayer\\Capabilities"))));
        QVERIFY(!valueExists(registryPath(QStringLiteral("RegisteredApplications")),
                             QStringLiteral("AgPlayer")));
        QVERIFY(!keyExists(registryPath(QStringLiteral("Classes\\AgPlayerAudioFile"))));
        QCOMPARE(readString(extensionPath, QString()), QStringLiteral("OtherPlayer.File"));
        QCOMPARE(readString(extensionPath, QStringLiteral("PerceivedType")),
                 QStringLiteral("other-video"));
        QCOMPARE(readString(extensionPath + QStringLiteral("\\OpenWithProgids"),
                            QStringLiteral("OtherPlayer.File")), QString());
        QCOMPARE(readString(extensionPath + QStringLiteral("\\shell\\custom"), QString()),
                 QStringLiteral("keep"));
        QVERIFY(keyExists(sentinelPath));
        QCOMPARE(readString(sentinelPath, QStringLiteral("sentinel")),
                 QStringLiteral("keep-settings"));
#elif defined(Q_OS_MACOS)
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->lastError().isEmpty());
#else
        QVERIFY(!controller_->unregisterAll());
#endif
    }

    void unregisterAllIsIdempotent()
    {
#ifdef Q_OS_WIN
        const auto cleanupSandbox = qScopeGuard([this] { removeTestRoot(); });
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->lastError().isEmpty());
#elif defined(Q_OS_MACOS)
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->unregisterAll());
        QVERIFY(controller_->lastError().isEmpty());
#else
        QVERIFY(!controller_->unregisterAll());
#endif
    }

private:
#ifdef Q_OS_WIN
    QString registryPath(const QString& relativePath) const
    {
        return registryRoot_ + QLatin1Char('\\') + relativePath;
    }

    bool writeString(const QString& path, const wchar_t* valueName, const QString& value) const
    {
        const std::wstring pathW = path.toStdWString();
        const std::wstring valueW = value.toStdWString();
        return RegSetKeyValueW(HKEY_CURRENT_USER, pathW.c_str(), valueName, REG_SZ,
                               valueW.c_str(), static_cast<DWORD>(
                                   (valueW.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    QString readString(const QString& path, const QString& valueName) const
    {
        const std::wstring pathW = path.toStdWString();
        const std::wstring valueNameW = valueName.toStdWString();
        wchar_t value[512] = {};
        DWORD valueSize = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, pathW.c_str(),
                         valueName.isEmpty() ? nullptr : valueNameW.c_str(),
                         RRF_RT_REG_SZ, nullptr, value, &valueSize) != ERROR_SUCCESS) {
            return {};
        }
        return QString::fromWCharArray(value);
    }

    bool keyExists(const QString& path) const
    {
        const std::wstring pathW = path.toStdWString();
        HKEY key = nullptr;
        const bool exists = RegOpenKeyExW(HKEY_CURRENT_USER, pathW.c_str(), 0,
                                          KEY_READ, &key) == ERROR_SUCCESS;
        if (exists) {
            RegCloseKey(key);
        }
        return exists;
    }

    bool valueExists(const QString& path, const QString& valueName) const
    {
        const std::wstring pathW = path.toStdWString();
        const std::wstring valueNameW = valueName.toStdWString();
        DWORD valueSize = 0;
        return RegGetValueW(HKEY_CURRENT_USER, pathW.c_str(), valueNameW.c_str(),
                            RRF_RT_REG_SZ, nullptr, nullptr, &valueSize) == ERROR_SUCCESS;
    }

    void removeTestRoot() const
    {
        if (!registryRoot_.isEmpty()) {
            const std::wstring rootW = registryRoot_.toStdWString();
            RegDeleteTreeW(HKEY_CURRENT_USER, rootW.c_str());
        }
    }
#else
    void removeTestRoot() const {}
#endif

    QString registryRoot_;
    std::unique_ptr<FileAssociationController> controller_;
};

QTEST_MAIN(FileAssociationControllerTest)

#include "file_association_controller_test.moc"
