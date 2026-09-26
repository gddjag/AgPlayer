#include "global_hotkey_manager.hpp"
#include "macos_system_integration.hpp"

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
    void macMediaKeysMatchAllModifiers();
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

void GlobalHotkeyManagerTest::macMediaKeysMatchAllModifiers()
{
    using agplayer::qt::macos::mediaHotkeyMatches;
    // Carbon: command 8, shift 9, option 11, control 12.
    // NSEvent: shift 17, control 18, option 19, command 20.
    QVERIFY(mediaHotkeyMatches(16, 0, 16, 0));
    QVERIFY(!mediaHotkeyMatches(16, 0, 17, 0));
    QVERIFY(!mediaHotkeyMatches(16, 1U << 9, 16, 0));
    QVERIFY(!mediaHotkeyMatches(16, 0, 16, 1ULL << 17));
    QVERIFY(mediaHotkeyMatches(16, 1U << 9, 16, 1ULL << 17));
    QVERIFY(mediaHotkeyMatches(16, 1U << 8, 16, 1ULL << 20));
    QVERIFY(mediaHotkeyMatches(16, 1U << 12, 16, 1ULL << 18));
    QVERIFY(mediaHotkeyMatches(16, 1U << 11, 16, 1ULL << 19));
    QVERIFY(!mediaHotkeyMatches(16, 1U << 8, 16, 1ULL << 18));
    QVERIFY(!mediaHotkeyMatches(16, 1U << 9, 16, (1ULL << 17) | (1ULL << 19)));
    QVERIFY(mediaHotkeyMatches(16, (1U << 8) | (1U << 11), 16,
                               (1ULL << 20) | (1ULL << 19)));
    // Caps lock, function and numeric-pad flags are not shortcut modifiers.
    QVERIFY(mediaHotkeyMatches(16, 0, 16, (1ULL << 16) | (1ULL << 21) | (1ULL << 23)));
}

QTEST_GUILESS_MAIN(GlobalHotkeyManagerTest)
#include "global_hotkey_manager_test.moc"
