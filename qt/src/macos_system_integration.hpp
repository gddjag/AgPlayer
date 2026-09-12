#pragma once

#include <QString>
#include <QStringList>

namespace agplayer::qt::macos {

using HotkeyCallback = void (*)(void* context, int id);

bool setLoginItemEnabled(bool enabled, QString* error);
bool loginItemEnabled(QString* error);

bool setFileAssociation(const QString& extension, QString* error);
bool restoreFileAssociation(const QString& extension, QString* error);
bool isFileAssociationActive(const QString& extension);
QStringList managedFileAssociations();

bool registerHotkey(int id, unsigned int modifiers, unsigned int key,
                    void* context, HotkeyCallback callback, QString* error);
bool registerMediaHotkey(int id, unsigned int mediaKey,
                         void* context, HotkeyCallback callback, QString* error);
void unregisterHotkey(int id);

} // namespace agplayer::qt::macos
