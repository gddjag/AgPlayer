#include "global_hotkey_manager.hpp"

#include <QDebug>
#include <QHash>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_MACOS
#include "macos_system_integration.hpp"
#include <QMetaObject>
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

#ifdef Q_OS_MACOS
    static const QHash<QString, uint> macKeys{
        {"space", 49}, {"tab", 48}, {"enter", 36}, {"return", 36},
        {"esc", 53}, {"escape", 53}, {"backspace", 51}, {"delete", 117},
        {"del", 117}, {"home", 115}, {"end", 119}, {"pageup", 116},
        {"pagedown", 121}, {"left", 123}, {"right", 124}, {"down", 125},
        {"up", 126}, {"play", 16}, {"mediaplaypause", 16}, {"stop", 7},
        {"prevtrack", 18}, {"mediaprevtrack", 18}, {"nexttrack", 17},
        {"medianexttrack", 17}, {"volumeup", 0}, {"volumedown", 1},
        {"volumemute", 7}, {"a", 0}, {"s", 1}, {"d", 2}, {"f", 3},
        {"h", 4}, {"g", 5}, {"z", 6}, {"x", 7}, {"c", 8}, {"v", 9},
        {"b", 11}, {"q", 12}, {"w", 13}, {"e", 14}, {"r", 15}, {"y", 16},
        {"t", 17}, {"1", 18}, {"2", 19}, {"3", 20}, {"4", 21}, {"6", 22},
        {"5", 23}, {"=", 24}, {"9", 25}, {"7", 26}, {"-", 27}, {"8", 28},
        {"0", 29}, {"]", 30}, {"o", 31}, {"u", 32}, {"[", 33}, {"i", 34},
        {"p", 35}, {"l", 37}, {"j", 38}, {"'", 39}, {"k", 40}, {";", 41},
        {"\\", 42}, {",", 43}, {"/", 44}, {"n", 45}, {"m", 46}, {".", 47},
        {"`", 50}};
    if (n.startsWith('f') && n.length() >= 2 && n.length() <= 3) {
        bool ok = false;
        const int number = n.mid(1).toInt(&ok);
        static const uint functionKeys[] = {
            122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103, 111,
            105, 107, 113, 106, 64, 79, 80, 90};
        if (ok && number >= 1 && number <= 20) {
            outKey = functionKeys[number - 1];
            return true;
        }
    }
    const auto macKey = macKeys.constFind(n);
    if (macKey != macKeys.cend()) {
        outKey = *macKey;
        return true;
    }
#endif

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

bool isMediaKeyName(const QString& name)
{
    const QString n = name.trimmed().toLower();
    return n == "play" || n == "mediaplaypause" || n == "stop"
        || n == "prevtrack" || n == "mediaprevtrack" || n == "nexttrack"
        || n == "medianexttrack" || n == "volumeup" || n == "volumedown"
        || n == "volumemute";
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
#elif defined(Q_OS_MACOS)
            // Qt maps Command to ControlModifier on Apple platforms.
            modifiers |= 1U << 8U;
#endif
            continue;
        }
        if (token == "alt") {
#ifdef Q_OS_WIN
            modifiers |= MOD_ALT;
#elif defined(Q_OS_MACOS)
            modifiers |= 1U << 11U;
#endif
            continue;
        }
        if (token == "shift") {
#ifdef Q_OS_WIN
            modifiers |= MOD_SHIFT;
#elif defined(Q_OS_MACOS)
            modifiers |= 1U << 9U;
#endif
            continue;
        }
        if (token == "meta") {
#ifdef Q_OS_WIN
            modifiers |= MOD_WIN;
#elif defined(Q_OS_MACOS)
            // Qt maps the physical Control key to MetaModifier on macOS.
            modifiers |= 1U << 12U;
#endif
            continue;
        }
        if (token == "win" || token == "windows") {
#ifdef Q_OS_WIN
            modifiers |= MOD_WIN;
#elif defined(Q_OS_MACOS)
            // Legacy Windows-key settings have no physical equivalent on macOS;
            // retain their platform-primary modifier behavior as Command.
            modifiers |= 1U << 8U;
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
    const bool mediaKey = keyName == "play" || keyName == "mediaplaypause"
                          || keyName == "prevtrack"
                          || keyName == "mediaprevtrack"
                          || keyName == "nexttrack"
                          || keyName == "medianexttrack"
                          || keyName == "volumeup"
                          || keyName == "volumedown"
                          || keyName == "volumemute";
    if (modifiers == 0 && !mediaKey) {
        return false;
    }

    outModifiers = modifiers;
    outKey = key;
    return true;
}

} // namespace

GlobalHotkeyManager::GlobalHotkeyManager(Backend backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
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
    const bool mediaKey = isMediaKeyName(combo.section(QLatin1Char('+'), -1));
    return registerHotkey(modifiers, key, action, mediaKey);
}

bool GlobalHotkeyManager::registerHotkey(uint modifiers, uint key, Action action)
{
    return registerHotkey(modifiers, key, action, false);
}

bool GlobalHotkeyManager::registerHotkey(uint modifiers, uint key, Action action,
                                         bool mediaKey)
{
#ifdef Q_OS_MACOS
    // Carbon virtual key code 0 is A, while NX_KEYTYPE_SOUND_UP is also 0.
    // The public raw API cannot distinguish a bare A from an invalid zero, but
    // neither is a supported global shortcut; modified A and media keys remain valid.
    if (key == 0 && modifiers == 0 && !mediaKey) {
        return false;
    }
#else
    if (key == 0) {
        return false;
    }
#endif

    // Avoid duplicate registrations for the same key combination.
    for (const auto& existing : hotkeys_) {
        if (existing.modifiers == modifiers && existing.key == key) {
            return false;
        }
    }

    bool passiveSystemKey = false;
#ifdef Q_OS_WIN
    passiveSystemKey = modifiers == 0
        && (key == VK_VOLUME_UP || key == VK_VOLUME_DOWN);
#endif
    Hotkey hotkey{nextId_++, modifiers, key, action, passiveSystemKey, mediaKey};
    if (!registerNativeHotkey(hotkey)) {
        return false;
    }
    hotkeys_.append(hotkey);
    return true;
}

void GlobalHotkeyManager::unregisterAll()
{
    for (const auto& hotkey : hotkeys_) {
        if (!hotkey.passiveSystemKey) unregisterNativeHotkey(hotkey.id);
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
        if (hotkey.passiveSystemKey) continue;
        if (enabled) {
            registerNativeHotkey(hotkey);
        } else {
            unregisterNativeHotkey(hotkey.id);
        }
    }
}

int GlobalHotkeyManager::passiveSystemShortcutCount() const noexcept
{
    return static_cast<int>(std::count_if(hotkeys_.cbegin(), hotkeys_.cend(),
        [](const Hotkey& hotkey) { return hotkey.passiveSystemKey; }));
}

QString GlobalHotkeyManager::lastError() const
{
    return lastError_;
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message,
                                            qintptr* /*result*/)
{
    if (!enabled_ || eventType != "windows_generic_MSG") {
        return false;
    }

#ifdef Q_OS_WIN
    const MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_APPCOMMAND) {
        const int command = GET_APPCOMMAND_LPARAM(msg->lParam);
        const Action action = command == APPCOMMAND_VOLUME_UP
            ? Action::VolumeUp : Action::VolumeDown;
        if (command == APPCOMMAND_VOLUME_UP || command == APPCOMMAND_VOLUME_DOWN) {
            for (const auto& hotkey : hotkeys_) {
                if (hotkey.passiveSystemKey && hotkey.action == action) {
                    emit triggered(action);
                    break;
                }
            }
        }
        return false;
    }
    if (msg->message != WM_HOTKEY) return false;

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
    if (hotkey.passiveSystemKey) return true;
    if (backend_ == Backend::InMemory) {
        return true;
    }
#ifdef Q_OS_WIN
    HWND hwnd = nullptr; // NULL hwnd registers application-global hotkeys.
    if (!RegisterHotKey(hwnd, hotkey.id, hotkey.modifiers, hotkey.key)) {
        qWarning() << "Failed to register global hotkey id" << hotkey.id
                   << "modifiers" << hotkey.modifiers << "key" << hotkey.key
                   << "error" << GetLastError();
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    QString error;
    const bool registered = hotkey.mediaKey
        ? agplayer::qt::macos::registerMediaHotkey(
              hotkey.id, hotkey.key, this, &GlobalHotkeyManager::dispatchMacHotkey,
              &error)
        : agplayer::qt::macos::registerHotkey(
              hotkey.id, hotkey.modifiers, hotkey.key, this,
              &GlobalHotkeyManager::dispatchMacHotkey, &error);
    if (!registered) {
        lastError_ = error;
        qWarning().noquote() << "AgPlayer macOS global hotkey registration failed:"
                             << error;
    } else {
        lastError_.clear();
    }
    return registered;
#else
    Q_UNUSED(hotkey)
    return false;
#endif
}

void GlobalHotkeyManager::unregisterNativeHotkey(int id)
{
    if (backend_ == Backend::InMemory) {
        return;
    }
#ifdef Q_OS_WIN
    UnregisterHotKey(nullptr, id);
#elif defined(Q_OS_MACOS)
    agplayer::qt::macos::unregisterHotkey(id);
#else
    Q_UNUSED(id)
#endif
}

#ifdef Q_OS_MACOS
void GlobalHotkeyManager::dispatchMacHotkey(void* context, int id)
{
    auto* const manager = static_cast<GlobalHotkeyManager*>(context);
    if (manager == nullptr) return;
    QMetaObject::invokeMethod(manager, [manager, id] {
        if (!manager->enabled_) return;
        for (const auto& hotkey : manager->hotkeys_) {
            if (hotkey.id == id) {
                emit manager->triggered(hotkey.action);
                return;
            }
        }
    }, Qt::QueuedConnection);
}
#endif
