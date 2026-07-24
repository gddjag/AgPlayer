#include <QCoreApplication>
#include <QTest>

#include <memory>

#include "file_association_controller.hpp"

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

private:
    std::unique_ptr<FileAssociationController> controller_;
};

QTEST_MAIN(FileAssociationControllerTest)

#include "file_association_controller_test.moc"
