#include "native_drop_router.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QWindow>

#include <algorithm>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

class NativeDropRouterTest final : public QObject {
    Q_OBJECT

private slots:
    void routesCanonicalLocalPathsToTheRequestedTarget();
    void preservesTheResourceFolderTargetForApplicationDispatch();
    void receivesQtUrlDropEvents();
    void leavesQtDirectoryDropsForQmlHitTesting();
    void routesQtAudioDropsToResourceHitTarget();
#ifdef Q_OS_WIN
    void receivesARealWindowsDropFilesMessage();
#endif
};

void NativeDropRouterTest::preservesTheResourceFolderTargetForApplicationDispatch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NativeDropRouter router;
    QSignalSpy dropped(&router, &NativeDropRouter::pathsDropped);

    router.routeLocalPaths(NativeDropRouter::Target::ResourceFolder,
                           {directory.path(), directory.path()});

    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.front().at(0).value<NativeDropRouter::Target>(),
             NativeDropRouter::Target::ResourceFolder);
    QCOMPARE(dropped.front().at(1).toStringList().size(), 1);
    QVERIFY(QFileInfo(dropped.front().at(1).toStringList().front()).isDir());
}

void NativeDropRouterTest::routesCanonicalLocalPathsToTheRequestedTarget()
{
    NativeDropRouter router;
    QSignalSpy dropped(&router, &NativeDropRouter::pathsDropped);

    router.routeLocalPaths(
        NativeDropRouter::Target::Main,
        {QStringLiteral("C:\\音乐\\第一首.wav"),
         QStringLiteral("C:/音乐/第二首.flac")});

    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.front().at(0).value<NativeDropRouter::Target>(),
             NativeDropRouter::Target::Main);
    const QStringList paths = dropped.front().at(1).toStringList();
    QCOMPARE(paths.size(), 2);
    QVERIFY(paths.front().contains(QStringLiteral("音乐")));
    QVERIFY(!paths.front().contains(QLatin1Char('\\')));
}

void NativeDropRouterTest::receivesQtUrlDropEvents()
{
    NativeDropRouter router;
    QWindow window;
    window.resize(320, 180);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    router.registerWindow(&window, NativeDropRouter::Target::List);

    QSignalSpy dropped(&router, &NativeDropRouter::pathsDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/音乐/Qt 拖放.flac"))});
    QDragEnterEvent enter(QPoint(100, 60), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(100, 60), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &drop);

    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.front().at(0).value<NativeDropRouter::Target>(),
             NativeDropRouter::Target::List);
    QCOMPARE(dropped.front().at(1).toStringList(),
             QStringList({QStringLiteral("C:/音乐/Qt 拖放.flac")}));
}

void NativeDropRouterTest::leavesQtDirectoryDropsForQmlHitTesting()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NativeDropRouter router;
    QWindow window;
    window.resize(320, 180);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    router.registerWindow(&window, NativeDropRouter::Target::List);

    QSignalSpy dropped(&router, &NativeDropRouter::pathsDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(directory.path())});
    QDragEnterEvent enter(QPoint(40, 40), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(40, 40), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &drop);

    QCOMPARE(dropped.count(), 0);
}

void NativeDropRouterTest::routesQtAudioDropsToResourceHitTarget()
{
    NativeDropRouter router;
    QWindow window;
    window.resize(320, 180);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    router.registerWindow(&window, NativeDropRouter::Target::List);
    router.registerHitTarget(
        &window, NativeDropRouter::Target::ResourceFolder,
        [](const QPointF& position) { return position.x() < 100.0; });

    QSignalSpy dropped(&router, &NativeDropRouter::pathsDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/音乐/资源区音频.flac"))});
    QDragEnterEvent enter(QPoint(40, 40), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(40, 40), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &drop);

    QVERIFY(drop.isAccepted());
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.front().at(0).value<NativeDropRouter::Target>(),
             NativeDropRouter::Target::ResourceFolder);
    QCOMPARE(dropped.front().at(1).toStringList(),
             QStringList({QStringLiteral("C:/音乐/资源区音频.flac")}));
}

#ifdef Q_OS_WIN
void NativeDropRouterTest::receivesARealWindowsDropFilesMessage()
{
    NativeDropRouter router;
    QWindow window;
    window.resize(320, 180);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    router.registerWindow(&window, NativeDropRouter::Target::List);
    router.registerHitTarget(
        &window, NativeDropRouter::Target::ResourceFolder,
        [](const QPointF& position) { return position.x() < 100.0; });

    QSignalSpy dropped(&router, &NativeDropRouter::pathsDropped);
    const HWND handle = reinterpret_cast<HWND>(window.winId());
    const auto postDrop = [handle](const std::wstring& path,
                                   const POINT point) {
        const SIZE_T pathBytes = (path.size() + 2U) * sizeof(wchar_t);
        const SIZE_T totalBytes = sizeof(DROPFILES) + pathBytes;
        HGLOBAL memory = GlobalAlloc(GHND, totalBytes);
        if (memory == nullptr) return false;
        auto* payload = static_cast<unsigned char*>(GlobalLock(memory));
        if (payload == nullptr) {
            GlobalFree(memory);
            return false;
        }
        auto* header = reinterpret_cast<DROPFILES*>(payload);
        header->pFiles = sizeof(DROPFILES);
        header->pt = point;
        header->fNC = FALSE;
        header->fWide = TRUE;
        auto* destination = reinterpret_cast<wchar_t*>(
            payload + sizeof(DROPFILES));
        std::copy(path.cbegin(), path.cend(), destination);
        destination[path.size()] = L'\0';
        destination[path.size() + 1U] = L'\0';
        GlobalUnlock(memory);
        if (PostMessageW(handle, WM_DROPFILES,
                         reinterpret_cast<WPARAM>(memory), 0)) {
            return true;
        }
        GlobalFree(memory);
        return false;
    };

    const std::wstring resourcePath = L"C:\\\u97f3\u4e50\\resource-folder";
    QVERIFY(postDrop(resourcePath, POINT{40, 40}));
    QTRY_COMPARE_WITH_TIMEOUT(dropped.count(), 1, 1000);
    QCOMPARE(dropped.front().at(0).value<NativeDropRouter::Target>(),
             NativeDropRouter::Target::ResourceFolder);
    QCOMPARE(dropped.front().at(1).toStringList(),
             QStringList({QStringLiteral("C:/\u97f3\u4e50/resource-folder")}));

    const std::wstring listPath = L"C:\\\u97f3\u4e50\\list.flac";
    QVERIFY(postDrop(listPath, POINT{200, 40}));
    QTRY_COMPARE_WITH_TIMEOUT(dropped.count(), 2, 1000);
    QCOMPARE(dropped.at(1).at(0).value<NativeDropRouter::Target>(),
             NativeDropRouter::Target::List);
    QCOMPARE(dropped.at(1).at(1).toStringList(),
             QStringList({QStringLiteral("C:/\u97f3\u4e50/list.flac")}));
}
#endif

QTEST_MAIN(NativeDropRouterTest)
#include "native_drop_router_test.moc"
