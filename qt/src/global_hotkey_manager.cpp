#include "global_hotkey_manager.hpp"

#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>

namespace {

QString trimmedToken(const QString& token)
{
    QString result = token.trimmed();
    result.replace(" ", "");
    return result.toLower();
}

bool parseKeyName(const QString& name, uint& outKey)
{
    const QString n = name.toLower();

    // Function keys
    if (n.startsWith('f') && n.length() >= 2 && n.length() <= 3) {
        bool ok = false;
        const int num = n.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 24) {
#ifdef Q_OS_WIN
            outKey = VK_F1 + static_cast<uint>(num - 1);
#else
            outKey = 0;
#endif
            return true;
        }
    }

    // Arrow keys
    if (n == "left") {
#ifdef Q_OS_WIN
        outKey = VK_LEFT;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "right") {
#ifdef Q_OS_WIN
        outKey = VK_RIGHT;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "up") {
#ifdef Q_OS_WIN
        outKey = VK_UP;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "down") {
#ifdef Q_OS_WIN
        outKey = VK_DOWN;
#else
        outKey = 0;
#endif
        return true;
    }

    // Common named keys
    if (n == "space") {
#ifdef Q_OS_WIN
        outKey = VK_SPACE;
#else
        outKey = ' ';
#endif
        return true;
    }
    if (n == "tab") {
#ifdef Q_OS_WIN
        outKey = VK_TAB;
#else
        outKey = '\t';
#endif
        return true;
    }
    if (n == "enter" || n == "return") {
#ifdef Q_OS_WIN
        outKey = VK_RETURN;
#else
        outKey = '\n';
#endif
        return true;
    }
    if (n == "esc" || n == "escape") {
#ifdef Q_OS_WIN
        outKey = VK_ESCAPE;
#else
        outKey = 27;
#endif
        return true;
    }
    if (n == "backspace") {
#ifdef Q_OS_WIN
        outKey = VK_BACK;
#else
        outKey = 8;
#endif
        return true;
    }
    if (n == "delete" || n == "del") {
#ifdef Q_OS_WIN
        outKey = VK_DELETE;
#else
        outKey = 127;
#endif
        return true;
    }
    if (n == "home") {
#ifdef Q_OS_WIN
        outKey = VK_HOME;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "end") {
#ifdef Q_OS_WIN
        outKey = VK_END;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "pageup") {
#ifdef Q_OS_WIN
        outKey = VK_PRIOR;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "pagedown") {
#ifdef Q_OS_WIN
        outKey = VK_NEXT;
#else
        outKey = 0;
#endif
        return true;
    }

    // Media keys
    if (n == "play" || n == "mediaplaypause") {
#ifdef Q_OS_WIN
        outKey = VK_MEDIA_PLAY_PAUSE;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "stop") {
#ifdef Q_OS_WIN
        outKey = VK_MEDIA_STOP;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "prevtrack" || n == "mediaprevtrack") {
#ifdef Q_OS_WIN
        outKey = VK_MEDIA_PREV_TRACK;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "nexttrack" || n == "medianexttrack") {
#ifdef Q_OS_WIN
        outKey = VK_MEDIA_NEXT_TRACK;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "volumeup") {
#ifdef Q_OS_WIN
        outKey = VK_VOLUME_UP;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "volumedown") {
#ifdef Q_OS_WIN
        outKey = VK_VOLUME_DOWN;
#else
        outKey = 0;
#endif
        return true;
    }
    if (n == "volumemute") {
#ifdef Q_OS_WIN
        outKey = VK_VOLUME_MUTE;
#else
        outKey = 0;
#endif
        return true;
    }

    // Single alphanumeric character
    if (n.length() == 1) {
        const QChar c = n.at(0);
        if (c.isLetterOrNumber()) {
#ifdef Q_OS_WIN
            outKey = static_cast<uint>(c.toUpper().unicode());
#else
            outKey = static_cast<uint>(c.toLower().unicode());
#endif
            return true;
        }
    }

    return false;
}

bool parseCombo(const QString& combo, uint& outModifiers, uint& outKey)
{
    if (combo.trimmed().isEmpty()) {
        return false;
    }

    const QStringList parts = combo.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }

    uint modifiers = 0;
    QString keyName;

    for (const QString& raw : parts) {
        const QString token = trimmedToken(raw);
        if (token.isEmpty()) {
            continue;
        }

        if (token == "global" || token == "none") {
            // Explicit no-modifier marker; no-op.
            continue;
        }
        if (token == "ctrl" || token == "control") {
#ifdef Q_OS_WIN
            modifiers |= MOD_CONTROL;
#endif
            continue;
        }
        if (token == "alt") {
#ifdef Q_OS_WIN
            modifiers |= MOD_ALT;
#endif
            continue;
        }
        if (token == "shift") {
#ifdef Q_OS_WIN
            modifiers |= MOD_SHIFT;
#endif
            continue;
        }
        if (token == "win" || token == "meta" || token == "windows") {
#ifdef Q_OS_WIN
            modifiers |= MOD_WIN;
#endif
            continue;
        }

        if (!keyName.isEmpty()) {
            // More than one non-modifier token: invalid format.
            return false;
        }
        keyName = token;
    }

    if (keyName.isEmpty()) {
        return false;
    }

    uint key = 0;
    if (!parseKeyName(keyName, key)) {
        return false;
    }

    outModifiers = modifiers;
    outKey = key;
    return true;
}

} // namespace

GlobalHotkeyManager::GlobalHotkeyManager(QObject* parent)
    : QObject(parent)
{
}

GlobalHotkeyManager::~GlobalHotkeyManager()
{
    unregisterAll();
}

bool GlobalHotkeyManager::registerShortcut(const QString& combo, Action action)
{
    uint modifiers = 0;
    uint key = 0;
    if (!parseCombo(combo, modifiers, key)) {
        return false;
    }
    return registerHotkey(modifiers, key, action);
}

bool GlobalHotkeyManager::registerHotkey(uint modifiers, uint key, Action action)
{
    if (key == 0) {
        return false;
    }

    // Avoid duplicate registrations for the same key combination.
    for (const auto& existing : hotkeys_) {
        if (existing.modifiers == modifiers && existing.key == key) {
            return false;
        }
    }

    Hotkey hotkey{nextId_++, modifiers, key, action};
    if (!registerNativeHotkey(hotkey)) {
        return false;
    }
    hotkeys_.append(hotkey);
    return true;
}

void GlobalHotkeyManager::unregisterAll()
{
    for (const auto& hotkey : hotkeys_) {
        unregisterNativeHotkey(hotkey.id);
    }
    hotkeys_.clear();
}

void GlobalHotkeyManager::setEnabled(bool enabled)
{
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;

    for (const auto& hotkey : hotkeys_) {
        if (enabled) {
            registerNativeHotkey(hotkey);
        } else {
            unregisterNativeHotkey(hotkey.id);
        }
    }
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message,
                                            qintptr* /*result*/)
{
    if (!enabled_ || eventType != "windows_generic_MSG") {
        return false;
    }

#ifdef Q_OS_WIN
    const MSG* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY) {
        return false;
    }

    const int id = static_cast<int>(msg->wParam);
    for (const auto& hotkey : hotkeys_) {
        if (hotkey.id == id) {
            emit triggered(hotkey.action);
            return true;
        }
    }
#else
    Q_UNUSED(message)
#endif

    return false;
}

bool GlobalHotkeyManager::registerNativeHotkey(const Hotkey& hotkey)
{
#ifdef Q_OS_WIN
    HWND hwnd = nullptr; // NULL hwnd registers application-global hotkeys.
    if (!RegisterHotKey(hwnd, hotkey.id, hotkey.modifiers, hotkey.key)) {
        qWarning() << "Failed to register global hotkey id" << hotkey.id
                   << "modifiers" << hotkey.modifiers << "key" << hotkey.key
                   << "error" << GetLastError();
        return false;
    }
    return true;
#else
    Q_UNUSED(hotkey)
    return false;
#endif
}

void GlobalHotkeyManager::unregisterNativeHotkey(int id)
{
#ifdef Q_OS_WIN
    UnregisterHotKey(nullptr, id);
#else
    Q_UNUSED(id)
#endif
}
