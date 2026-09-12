#pragma once

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QString>

class GlobalHotkeyManager final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    enum class Backend {
        Native,
        InMemory,
    };

    enum class Action {
        PlayPause,
        Previous,
        Next,
        VolumeUp,
        VolumeDown,
        ToggleMiniPlayer,
    };
    Q_ENUM(Action)

    explicit GlobalHotkeyManager(Backend backend = Backend::Native,
                                 QObject* parent = nullptr);
    ~GlobalHotkeyManager() override;

    // Parse a user-facing string such as "Global + Space" or "Alt + P" and
    // register the resulting key combination. Returns false if the string is
    // empty or cannot be parsed.
    bool registerShortcut(const QString& combo, Action action);

    // Register a raw hotkey. Returns false if registration failed.
    bool registerHotkey(uint modifiers, uint key, Action action);

    void unregisterAll();

    // Enables or disables all registered hotkeys without losing registrations.
    void setEnabled(bool enabled);

    int passiveSystemShortcutCount() const noexcept;
    QString lastError() const;

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

signals:
    void triggered(Action action);

private:
    struct Hotkey {
        int id;
        uint modifiers;
        uint key;
        Action action;
        bool passiveSystemKey = false;
        bool mediaKey = false;
    };

    bool registerHotkey(uint modifiers, uint key, Action action, bool mediaKey);
    bool registerNativeHotkey(const Hotkey& hotkey);
    void unregisterNativeHotkey(int id);
#ifdef Q_OS_MACOS
    static void dispatchMacHotkey(void* context, int id);
#endif

    QList<Hotkey> hotkeys_;
    int nextId_ = 1;
    bool enabled_ = true;
    Backend backend_ = Backend::Native;
    QString lastError_;
};
