#include "rolling_keyboard_handler.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTest>

class RollingKeyboardHandlerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        window_.reset(new QQuickWindow);
        window_->resize(320, 240);
        window_->show();
        window_->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window_.data()));
    }
    void cleanup() {
        window_->contentItem()->forceActiveFocus();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
    void cleanupTestCase() {
        window_->hide();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        window_.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
    void destroyingAttachedHandlerDoesNotCorruptItsWindowCallback() {
        for (int i = 0; i < 64; ++i) {
            auto* handler = new RollingKeyboardHandler(window_->contentItem());
            handler->setShortcuts({{"cue", "C"}});
            delete handler;
        }
        QCOMPARE(window_->contentItem()->window(), window_.data());
    }
    void shortcutOverrideAndRebindingCancelHeld() {
        RollingKeyboardHandler handler(window_->contentItem());
        handler.setShortcuts({{"cue", "C"}});
        QSignalSpy pressed(&handler, &RollingKeyboardHandler::actionPressed);
        QSignalSpy cancelled(&handler, &RollingKeyboardHandler::cancelled);
        QKeyEvent overrideEvent(QEvent::ShortcutOverride, Qt::Key_C, Qt::NoModifier);
        overrideEvent.setAccepted(false);
        QCoreApplication::sendEvent(window_.data(), &overrideEvent);
        QVERIFY(overrideEvent.isAccepted());
        QCOMPARE(pressed.count(), 0);
        QTest::keyPress(window_.data(), Qt::Key_C);
        handler.setShortcuts({{"cue", "Shift+Q"}});
        QCOMPARE(cancelled.count(), 1);
        QTest::keyRelease(window_.data(), Qt::Key_C);
        QTest::keyClick(window_.data(), Qt::Key_C);
        QCOMPARE(pressed.count(), 1);
        QTest::keyClick(window_.data(), Qt::Key_Q, Qt::ShiftModifier);
        QCOMPARE(pressed.count(), 2);
    }
    void routesPressReleaseAndIgnoresAutoRepeat() {
        RollingKeyboardHandler handler(window_->contentItem());
        handler.setShortcuts({{"cue", "C"}, {"hotCueDelete1", "Alt+1"}});
        QSignalSpy pressed(&handler, &RollingKeyboardHandler::actionPressed);
        QSignalSpy released(&handler, &RollingKeyboardHandler::actionReleased);
        QTest::keyPress(window_.data(), Qt::Key_C);
        QCOMPARE(pressed.count(), 1);
        QCOMPARE(pressed.at(0).at(0).toString(), QString("cue"));
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier, "c", true);
        QCoreApplication::sendEvent(window_.data(), &repeat);
        QCOMPARE(pressed.count(), 1);
        QTest::keyRelease(window_.data(), Qt::Key_C);
        QCOMPARE(released.count(), 1);
        QTest::keyClick(window_.data(), Qt::Key_1, Qt::AltModifier);
        QCOMPARE(pressed.at(1).at(0).toString(), QString("hotCueDelete1"));
    }
    void textEditingAndDisabledModeDoNotTrigger() {
        RollingKeyboardHandler handler(window_->contentItem());
        handler.setShortcuts({{"cue", "C"}});
        QSignalSpy pressed(&handler, &RollingKeyboardHandler::actionPressed);
        QQuickItem editor(window_->contentItem());
        editor.setFlag(QQuickItem::ItemAcceptsInputMethod, true);
        editor.forceActiveFocus();
        QTest::keyClick(window_.data(), Qt::Key_C);
        QCOMPARE(pressed.count(), 0);
        window_->contentItem()->forceActiveFocus();
        handler.setEnabled(false);
        QTest::keyClick(window_.data(), Qt::Key_C);
        QCOMPARE(pressed.count(), 0);
    }
    void focusLossCancelsAndStaleReleaseIsIgnored() {
        RollingKeyboardHandler handler(window_->contentItem());
        handler.setShortcuts({{"cue", "C"}});
        QSignalSpy cancelled(&handler, &RollingKeyboardHandler::cancelled);
        QSignalSpy released(&handler, &RollingKeyboardHandler::actionReleased);
        QTest::keyPress(window_.data(), Qt::Key_C);
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(window_.data(), &deactivate);
        QCOMPARE(cancelled.count(), 1);
        QTest::keyRelease(window_.data(), Qt::Key_C);
        QCOMPARE(released.count(), 0);
    }
    void nonFocusPopupOwnsArrowsAndRoutingResumesAfterClose() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Item {
                id: root
                width: 320
                height: 240
                property alias popupObject: popup
                property alias openerObject: opener
                Item { id: opener; focus: true; width: 1; height: 1 }
                Popup {
                    id: popup
                    parent: root
                    width: 120
                    height: 80
                    focus: false
                    contentItem: ListView { model: ["One", "Two"] }
                }
            }
        )", QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* rootItem = qobject_cast<QQuickItem*>(root.data());
        QVERIFY(rootItem);
        rootItem->setParentItem(window_->contentItem());
        auto* popup = root->property("popupObject").value<QObject*>();
        auto* opener = qobject_cast<QQuickItem*>(
            root->property("openerObject").value<QObject*>());
        QVERIFY(popup);
        QVERIFY(opener);

        RollingKeyboardHandler handler(window_->contentItem());
        handler.setShortcuts({{"gridLeft", "Left"}});
        QSignalSpy pressed(&handler, &RollingKeyboardHandler::actionPressed);
        opener->forceActiveFocus();
        QVERIFY(opener->hasActiveFocus());

        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("visible").toBool());
        QVERIFY(opener->hasActiveFocus());
        QTest::keyClick(window_.data(), Qt::Key_Left);
        QCOMPARE(pressed.count(), 0);

        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(opener->hasActiveFocus());
        QTest::keyClick(window_.data(), Qt::Key_Left);
        QCOMPARE(pressed.count(), 1);
        QCOMPARE(pressed.at(0).at(0).toString(), QString("gridLeft"));
    }

private:
    QScopedPointer<QQuickWindow> window_;
};
QTEST_MAIN(RollingKeyboardHandlerTest)
#include "rolling_keyboard_handler_test.moc"
