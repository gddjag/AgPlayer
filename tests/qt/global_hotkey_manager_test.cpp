#include "global_hotkey_manager.hpp"

#include <QObject>
#include <QTest>

class GlobalHotkeyManagerTest : public QObject {
    Q_OBJECT

private slots:
    void emptyOrInvalidShortcutsAreRejected();
    void zeroKeyRegistrationIsRejected();
    void duplicateRegistrationsAreRejected();
    void enabledStateCanBeToggled();
    void parseOnlyCombo();
    void modifierPlusKeyCombo();
};

void GlobalHotkeyManagerTest::emptyOrInvalidShortcutsAreRejected()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(!manager.registerShortcut(QString(), GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(!manager.registerShortcut("   ", GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(!manager.registerShortcut("Global + UnknownKey123",
                                       GlobalHotkeyManager::Action::PlayPause));
}

void GlobalHotkeyManagerTest::zeroKeyRegistrationIsRejected()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(!manager.registerHotkey(0, 0, GlobalHotkeyManager::Action::PlayPause));
}

void GlobalHotkeyManagerTest::duplicateRegistrationsAreRejected()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    // Both map to the same raw key on Windows; the second registration should
    // fail because the combination is already registered.
    QVERIFY(manager.registerShortcut("Global + Space",
                                      GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(!manager.registerShortcut("Space",
                                       GlobalHotkeyManager::Action::PlayPause));
}

void GlobalHotkeyManagerTest::enabledStateCanBeToggled()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(manager.registerShortcut("Global + F10",
                                      GlobalHotkeyManager::Action::PlayPause));
    manager.setEnabled(false);
    manager.setEnabled(true);
    manager.unregisterAll();
}

void GlobalHotkeyManagerTest::parseOnlyCombo()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(manager.registerShortcut("Space", GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(manager.registerShortcut("Left", GlobalHotkeyManager::Action::Previous));
    QVERIFY(manager.registerShortcut("Tab", GlobalHotkeyManager::Action::Next));
}

void GlobalHotkeyManagerTest::modifierPlusKeyCombo()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(manager.registerShortcut("Alt + P", GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(manager.registerShortcut("Ctrl + Shift + F",
                                      GlobalHotkeyManager::Action::PlayPause));
}

QTEST_GUILESS_MAIN(GlobalHotkeyManagerTest)
#include "global_hotkey_manager_test.moc"
