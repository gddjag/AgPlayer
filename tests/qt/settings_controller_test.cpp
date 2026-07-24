#include "settings_controller.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QTest>

class SettingsControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultCacheDirectoryUsesStandardPaths();
    void defaultExportDirectoryUsesStandardPaths();
    void loadCreatesDefaultDirectories();
};

void SettingsControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void SettingsControllerTest::defaultCacheDirectoryUsesStandardPaths()
{
    SettingsController settings;
    const QString cacheDir = settings.cacheDirectory();
    QVERIFY(!cacheDir.contains(QStringLiteral("D:\\Music")));
    QVERIFY(cacheDir.endsWith(QStringLiteral("/waveform"))
            || cacheDir.endsWith(QStringLiteral("\\waveform")));
}

void SettingsControllerTest::defaultExportDirectoryUsesStandardPaths()
{
    SettingsController settings;
    const QString exportDir = settings.defaultExportDirectory();
    QVERIFY(!exportDir.contains(QStringLiteral("D:\\Music")));
    QVERIFY(exportDir.contains(QStringLiteral("AgPlayer_Export")));
}

void SettingsControllerTest::loadCreatesDefaultDirectories()
{
    SettingsController settings;
    const QString cacheDir = settings.cacheDirectory();
    const QString exportDir = settings.defaultExportDirectory();
    QVERIFY(!cacheDir.isEmpty());
    QVERIFY(!exportDir.isEmpty());
    QVERIFY(QDir(cacheDir).exists());
    QVERIFY(QDir(exportDir).exists());
}

QTEST_MAIN(SettingsControllerTest)
#include "settings_controller_test.moc"
