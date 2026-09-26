#pragma once

#include <QString>
#include <QStringList>

namespace agplayer::qt::macos {

using HotkeyCallback = void (*)(void* context, int id);

// Match the AppKit media event at the native registration boundary. Modifier
// arguments use Carbon registration bits and NSEvent flags respectively.
inline bool mediaHotkeyMatches(unsigned int registeredKey, unsigned int modifiers,
                               unsigned int eventKey, quint64 eventFlags) noexcept
{
    // NSEvent's device-independent flags occupy different bits from Carbon.
    // Caps Lock, numeric-pad and Fn flags do not form part of a shortcut.
    const unsigned int normalized = ((eventFlags & (1ULL << 17)) ? (1U << 9) : 0U)
        | ((eventFlags & (1ULL << 18)) ? (1U << 12) : 0U)
        | ((eventFlags & (1ULL << 19)) ? (1U << 11) : 0U)
        | ((eventFlags & (1ULL << 20)) ? (1U << 8) : 0U);
    return registeredKey == eventKey && modifiers == normalized;
}

bool setLoginItemEnabled(bool enabled, QString* error);
bool loginItemEnabled(QString* error);

bool setFileAssociation(const QString& extension, QString* error);
bool restoreFileAssociation(const QString& extension, QString* error);
bool isFileAssociationActive(const QString& extension);
QStringList managedFileAssociations();

bool registerHotkey(int id, unsigned int modifiers, unsigned int key,
                    void* context, HotkeyCallback callback, QString* error);
bool registerMediaHotkey(int id, unsigned int modifiers, unsigned int mediaKey,
                         void* context, HotkeyCallback callback, QString* error);
void unregisterHotkey(int id);

} // namespace agplayer::qt::macos
