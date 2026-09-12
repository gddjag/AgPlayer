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
    void unmodifiedTypingKeysAreRejected();
    void mediaKeysAreAcceptedWithoutModifiers();
    void volumeMediaKeysRemainAvailableToWindows();
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
    QVERIFY(manager.registerShortcut("Ctrl + Alt + Space",
                                      GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(!manager.registerShortcut("Ctrl + Alt + Space",
                                       GlobalHotkeyManager::Action::PlayPause));
}

void GlobalHotkeyManagerTest::enabledStateCanBeToggled()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(manager.registerShortcut("Ctrl + F10",
                                      GlobalHotkeyManager::Action::PlayPause));
    manager.setEnabled(false);
    manager.setEnabled(true);
    manager.unregisterAll();
}

void GlobalHotkeyManagerTest::unmodifiedTypingKeysAreRejected()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(!manager.registerShortcut("Space", GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(!manager.registerShortcut("Global + Left",
                                      GlobalHotkeyManager::Action::Previous));
    QVERIFY(!manager.registerShortcut("Tab", GlobalHotkeyManager::Action::Next));
}

void GlobalHotkeyManagerTest::mediaKeysAreAcceptedWithoutModifiers()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(manager.registerShortcut("MediaPlayPause",
                                     GlobalHotkeyManager::Action::PlayPause));
    QVERIFY(manager.registerShortcut("MediaPrevTrack",
                                     GlobalHotkeyManager::Action::Previous));
    QVERIFY(manager.registerShortcut("MediaNextTrack",
                                     GlobalHotkeyManager::Action::Next));
}

void GlobalHotkeyManagerTest::volumeMediaKeysRemainAvailableToWindows()
{
    GlobalHotkeyManager manager(GlobalHotkeyManager::Backend::InMemory);
    QVERIFY(manager.registerShortcut("VolumeUp",
                                     GlobalHotkeyManager::Action::VolumeUp));
    QVERIFY(manager.registerShortcut("VolumeDown",
                                     GlobalHotkeyManager::Action::VolumeDown));
#ifdef Q_OS_WIN
    QCOMPARE(manager.passiveSystemShortcutCount(), 2);
#else
    QCOMPARE(manager.passiveSystemShortcutCount(), 0);
#endif
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
