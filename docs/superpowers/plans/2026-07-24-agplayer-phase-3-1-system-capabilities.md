# AgPlayer Phase 3-1 系统级能力实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Windows 上完成音频文件注册表关联、双击启动播放，以及将“检查更新”改为离线提示“当前已是最新版本”。

**Architecture:** 新增 `FileAssociationController` 负责 Windows 注册表操作（非 Windows 平台安全降级），`SettingsController` 持有并驱动它；`main.cpp` 解析命令行音频文件并通过 `ImportController`/`PlaybackController` 启动播放；QML 仅绑定信号与 UI。

**Tech Stack:** C++17, Qt 6.7, Windows API (`advapi32`), CMake, ctest.

## Global Constraints

- 必须保持现有三层架构：Core（C++17 / C ABI）不处理任何平台 Shell 行为；Qt Bridge 负责 Windows 注册表；QML 只负责 UI 与信号绑定。
- 不打包 EXE，开发/测试继续使用现有构建产物 `AgPlayer.exe`。
- 所有功能必须可离线工作，不访问网络。
- 错误必须静默处理，不能阻断启动或弹出未请求的错误窗。
- 注册表操作限定在 `HKEY_CURRENT_USER`，避免需要管理员权限。
- 不改变 `core/include/agplayer/c_api.h` 的公开函数签名。
- 所有修改必须在 Windows x64 Debug/Release 下通过 `ctest`。

---

## Task 1: 创建 `FileAssociationController`

**Files:**
- Create: `qt/src/file_association_controller.hpp`
- Create: `qt/src/file_association_controller.cpp`
- Modify: `qt/CMakeLists.txt:2-37`

**Interfaces:**
- Consumes: `QCoreApplication::applicationFilePath()`
- Produces: `FileAssociationController` with `registerForExtensions`, `unregisterForExtensions`, `unregisterAll`, `isAssociated`, `lastError`, `supportedAudioExtensions`

**问题说明：**
需要一个独立的 Qt Bridge 控制器来管理 Windows 文件关联注册表操作，并在非 Windows 平台安全降级。

- [ ] **Step 1: 创建头文件 `qt/src/file_association_controller.hpp`**

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class FileAssociationController final : public QObject {
    Q_OBJECT
public:
    explicit FileAssociationController(QObject* parent = nullptr);

    Q_INVOKABLE bool registerForExtensions(const QStringList& extensions);
    Q_INVOKABLE bool unregisterForExtensions(const QStringList& extensions);
    Q_INVOKABLE bool unregisterAll();
    Q_INVOKABLE bool isAssociated(const QString& extension) const;
    Q_INVOKABLE QString lastError() const;

    static QStringList supportedAudioExtensions();

private:
    QString lastError_;

    bool writeProgId(const QString& appPath);
    bool removeProgId();
    bool writeExtension(const QString& extension, const QString& progId);
    bool removeExtension(const QString& extension);
};
```

- [ ] **Step 2: 创建实现 `qt/src/file_association_controller.cpp`**

```cpp
#include "file_association_controller.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

constexpr char kProgId[] = "AgPlayerAudioFile";
constexpr char kProgIdDisplayName[] = "AgPlayer Audio File";

QString normalizeExtension(const QString& ext)
{
    QString result = ext.trimmed().toLower();
    while (!result.isEmpty() && result.startsWith('.')) {
        result.remove(0, 1);
    }
    return result;
}

#ifdef Q_OS_WIN

bool writeRegistryString(HKEY key, const wchar_t* subKey, const wchar_t* valueName,
                         const QString& value)
{
    const std::wstring valueW = value.toStdWString();
    const LSTATUS status = RegSetKeyValueW(
        key,
        subKey,
        valueName,
        REG_SZ,
        valueW.c_str(),
        static_cast<DWORD>((valueW.size() + 1) * sizeof(wchar_t)));
    return status == ERROR_SUCCESS;
}

bool deleteRegistryKey(HKEY key, const wchar_t* subKey)
{
    return RegDeleteTreeW(key, subKey) == ERROR_SUCCESS
           || RegDeleteKeyW(key, subKey) == ERROR_SUCCESS;
}

bool deleteRegistryValue(HKEY key, const wchar_t* subKey, const wchar_t* valueName)
{
    return RegDeleteKeyValueW(key, subKey, valueName) == ERROR_SUCCESS;
}

#endif

} // namespace

FileAssociationController::FileAssociationController(QObject* parent)
    : QObject(parent)
{
}

QStringList FileAssociationController::supportedAudioExtensions()
{
    return {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("flac"),
            QStringLiteral("aac"), QStringLiteral("m4a"), QStringLiteral("ogg"),
            QStringLiteral("wma"), QStringLiteral("ape"), QStringLiteral("opus")};
}

QString FileAssociationController::lastError() const
{
    return lastError_;
}

bool FileAssociationController::registerForExtensions(const QStringList& extensions)
{
    lastError_.clear();

#ifdef Q_OS_WIN
    const QString appPath = QCoreApplication::applicationFilePath();
    if (appPath.isEmpty()) {
        lastError_ = tr("Cannot determine application path");
        return false;
    }

    if (!writeProgId(appPath)) {
        return false;
    }

    const QString progId = QString::fromLatin1(kProgId);
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (normalized.isEmpty()) {
            continue;
        }
        if (!writeExtension(normalized, progId)) {
            // Rollback: remove the ProgID we just wrote.
            removeProgId();
            return false;
        }
    }
    return true;
#else
    lastError_ = tr("not supported on this platform");
    return false;
#endif
}

bool FileAssociationController::unregisterForExtensions(const QStringList& extensions)
{
    lastError_.clear();

#ifdef Q_OS_WIN
    bool allOk = true;
    for (const QString& ext : extensions) {
        const QString normalized = normalizeExtension(ext);
        if (normalized.isEmpty()) {
            continue;
        }
        if (!removeExtension(normalized)) {
            allOk = false;
        }
    }
    return allOk;
#else
    lastError_ = tr("not supported on this platform");
    return false;
#endif
}

bool FileAssociationController::unregisterAll()
{
    lastError_.clear();

#ifdef Q_OS_WIN
    const QStringList extensions = supportedAudioExtensions();
    for (const QString& ext : extensions) {
        removeExtension(ext);
    }
    return removeProgId();
#else
    lastError_ = tr("not supported on this platform");
    return false;
#endif
}

bool FileAssociationController::isAssociated(const QString& extension) const
{
    const QString normalized = normalizeExtension(extension);
    if (normalized.isEmpty()) {
        return false;
    }

#ifdef Q_OS_WIN
    const QString keyPath = QStringLiteral("Software\\Classes\\.%1").arg(normalized);
    const std::wstring keyPathW = keyPath.toStdWString();

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPathW.c_str(), 0, KEY_READ, &key)
        != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[256] = {};
    DWORD valueSize = sizeof(value);
    DWORD valueType = 0;
    const LSTATUS status = RegQueryValueExW(
        key,
        nullptr,
        nullptr,
        &valueType,
        reinterpret_cast<LPBYTE>(value),
        &valueSize);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || valueType != REG_SZ) {
        return false;
    }
    return QString::fromWCharArray(value) == QString::fromLatin1(kProgId);
#else
    return false;
#endif
}

#ifdef Q_OS_WIN

bool FileAssociationController::writeProgId(const QString& appPath)
{
    const QString rootPath = QStringLiteral("Software\\Classes\\%1").arg(QString::fromLatin1(kProgId));
    const std::wstring rootPathW = rootPath.toStdWString();

    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        rootPathW.c_str(),
        0,
        nullptr,
        0,
        KEY_WRITE,
        nullptr,
        &key,
        nullptr);
    if (status != ERROR_SUCCESS) {
        lastError_ = tr("Failed to create ProgID registry key");
        return false;
    }

    bool ok = true;
    ok &= writeRegistryString(key, nullptr, nullptr, QString::fromLatin1(kProgIdDisplayName));
    ok &= writeRegistryString(
        key,
        L"DefaultIcon",
        nullptr,
        QStringLiteral("%1,0").arg(appPath));
    ok &= writeRegistryString(
        key,
        L"shell\\open\\command",
        nullptr,
        QStringLiteral("\"%1\" \"%2\"").arg(appPath));

    RegCloseKey(key);

    if (!ok) {
        lastError_ = tr("Failed to write ProgID registry values");
        removeProgId();
        return false;
    }
    return true;
}

bool FileAssociationController::removeProgId()
{
    const QString rootPath = QStringLiteral("Software\\Classes\\%1").arg(QString::fromLatin1(kProgId));
    const std::wstring rootPathW = rootPath.toStdWString();
    if (RegDeleteTreeW(HKEY_CURRENT_USER, rootPathW.c_str()) == ERROR_SUCCESS) {
        return true;
    }
    lastError_ = tr("Failed to remove ProgID registry key");
    return false;
}

bool FileAssociationController::writeExtension(const QString& extension,
                                               const QString& progId)
{
    const QString keyPath = QStringLiteral("Software\\Classes\\.%1").arg(extension);
    const std::wstring keyPathW = keyPath.toStdWString();

    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        keyPathW.c_str(),
        0,
        nullptr,
        0,
        KEY_WRITE,
        nullptr,
        &key,
        nullptr);
    if (status != ERROR_SUCCESS) {
        lastError_ = tr("Failed to create extension registry key for .%1").arg(extension);
        return false;
    }

    bool ok = true;
    ok &= writeRegistryString(key, nullptr, nullptr, progId);
    ok &= writeRegistryString(key, nullptr, L"PerceivedType", QStringLiteral("audio"));
    ok &= writeRegistryString(key, L"OpenWithProgids", progId.toStdWString().c_str(),
                              QString());

    RegCloseKey(key);

    if (!ok) {
        lastError_ = tr("Failed to write extension registry values for .%1").arg(extension);
        removeExtension(extension);
        return false;
    }
    return true;
}

bool FileAssociationController::removeExtension(const QString& extension)
{
    const QString keyPath = QStringLiteral("Software\\Classes\\.%1").arg(extension);
    const std::wstring keyPathW = keyPath.toStdWString();

    // Only remove if this extension points to our ProgID; do not destroy other
    // applications' associations.
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPathW.c_str(), 0, KEY_READ, &key)
        == ERROR_SUCCESS) {
        wchar_t value[256] = {};
        DWORD valueSize = sizeof(value);
        DWORD valueType = 0;
        const bool isOurs =
            RegQueryValueExW(key, nullptr, nullptr, &valueType,
                             reinterpret_cast<LPBYTE>(value), &valueSize)
                == ERROR_SUCCESS
            && valueType == REG_SZ
            && QString::fromWCharArray(value) == QString::fromLatin1(kProgId);
        RegCloseKey(key);

        if (isOurs) {
            if (RegDeleteTreeW(HKEY_CURRENT_USER, keyPathW.c_str()) == ERROR_SUCCESS) {
                return true;
            }
            lastError_ = tr("Failed to remove extension registry key for .%1").arg(extension);
            return false;
        }
    }
    return true;
}

#endif
```

- [ ] **Step 3: 将新文件加入 `qt/CMakeLists.txt`**

在 `src/audio_tools_controller.cpp` 之前插入：

```cmake
    src/file_association_controller.cpp
    src/file_association_controller.hpp
```

- [ ] **Step 4: 构建并运行最小可执行测试**

```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -InstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -Arch amd64
cmake --build build/debug --config Debug --target agplayer_qt
```

Expected: `agplayer_qt` 静态库编译成功，无新增警告。

- [ ] **Step 5: 提交**

```bash
git add qt/src/file_association_controller.hpp qt/src/file_association_controller.cpp qt/CMakeLists.txt
git commit -m "feat(qt): add FileAssociationController for Windows registry file associations"
```

---

## Task 2: 在 `SettingsController` 中集成文件关联与离线更新

**Files:**
- Modify: `qt/src/settings_controller.hpp`
- Modify: `qt/src/settings_controller.cpp`

**Interfaces:**
- Consumes: `FileAssociationController`, `RuntimeLog::log`
- Produces: `updateCheckFinished(message, success)` signal; file association rebind on load/setter

**问题说明：**
`SettingsController` 需要持有 `FileAssociationController`，在设置变更时同步注册表，并在加载时自动重绑。`checkForUpdates()` 需要改为离线提示。

- [ ] **Step 1: 修改 `qt/src/settings_controller.hpp`**

在 `#include <QStringList>` 下添加：

```cpp
#include <memory>
```

在类前向声明区域（`class SettingsController` 之前）添加：

```cpp
class FileAssociationController;
```

在 `signals:` 区域 `openOfficialWebsite()` 之后添加：

```cpp
    void updateCheckFinished(const QString& message, bool success);
```

在 `private:` 区域末尾添加：

```cpp
    std::unique_ptr<FileAssociationController> fileAssociationController_;

    void applyFileAssociations();
```

- [ ] **Step 2: 修改 `qt/src/settings_controller.cpp`**

在 `#include "runtime_log.hpp"` 后添加：

```cpp
#include "file_association_controller.hpp"
```

修改构造函数：

```cpp
SettingsController::SettingsController(QObject* parent)
    : QObject(parent),
      settings_(this),
      fileAssociationController_(std::make_unique<FileAssociationController>(this))
{
    load();
    recalculateCacheSize();
}
```

修改 `setSetAsDefaultPlayer`：

```cpp
void SettingsController::setSetAsDefaultPlayer(bool value)
{
    if (setAsDefaultPlayer_ == value) {
        return;
    }
    setAsDefaultPlayer_ = value;
    settings_.setValue(QStringLiteral("general/setAsDefaultPlayer"), value);

    applyFileAssociations();

    emit setAsDefaultPlayerChanged();
}
```

修改 `setFileAssociations`：

```cpp
void SettingsController::setFileAssociations(const QStringList& value)
{
    if (fileAssociations_ == value) {
        return;
    }
    fileAssociations_ = value;
    settings_.setValue(QStringLiteral("general/fileAssociations"), value);

    applyFileAssociations();

    emit fileAssociationsChanged();
}
```

修改 `rebindFileAssociations`：

```cpp
void SettingsController::rebindFileAssociations()
{
    if (fileAssociationController_ == nullptr) {
        return;
    }

    fileAssociationController_->unregisterAll();

    if (setAsDefaultPlayer_) {
        if (!fileAssociationController_->registerForExtensions(fileAssociations_)) {
            RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
                QStringLiteral("Failed to rebind file associations: %1")
                    .arg(fileAssociationController_->lastError()));
        }
    }
}
```

修改 `checkForUpdates`：

```cpp
void SettingsController::checkForUpdates()
{
    emit updateCheckFinished(tr("当前已是最新版本"), true);
}
```

在文件末尾添加 `applyFileAssociations` 实现（在匿名命名空间之后、任何现有 helper 之后）：

```cpp
void SettingsController::applyFileAssociations()
{
    if (fileAssociationController_ == nullptr) {
        return;
    }

    if (!setAsDefaultPlayer_) {
        if (!fileAssociationController_->unregisterForExtensions(fileAssociations_)) {
            RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
                QStringLiteral("Failed to unregister file associations: %1")
                    .arg(fileAssociationController_->lastError()));
        }
        return;
    }

    if (!fileAssociationController_->registerForExtensions(fileAssociations_)) {
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("Settings"),
            QStringLiteral("Failed to register file associations: %1")
                .arg(fileAssociationController_->lastError()));
    }
}
```

修改 `load()` 中读取 `setAsDefaultPlayer_` 和 `fileAssociations_` 的位置，确保在读取后调用一次 `applyFileAssociations()`。在 `load()` 末尾 `recalculateCacheSize()` 之前添加：

```cpp
    applyFileAssociations();
```

- [ ] **Step 3: 构建并运行设置控制器测试**

```powershell
cmake --build build/debug --config Debug --target settings_controller_test
ctest -C Debug -R "^settings_controller_test$" --output-on-failure
```

Expected: 测试通过。

- [ ] **Step 4: 提交**

```bash
git add qt/src/settings_controller.hpp qt/src/settings_controller.cpp
git commit -m "feat(qt/settings): integrate file association controller and offline update check"
```

---

## Task 3: 在 `SettingsPage.qml` 中显示离线更新提示

**Files:**
- Modify: `app/qml/AgPlayer/SettingsPage.qml:1518-1636`

**Interfaces:**
- Consumes: `SettingsController.updateCheckFinished(message, success)`
- Produces: `MessageDialog` 弹窗显示更新检查结果

**问题说明：**
“检查更新”按钮点击后需要弹出对话框显示“当前已是最新版本”。

- [ ] **Step 1: 在 AboutSection 内添加 MessageDialog 与 Connections**

在 `AboutSection` 的 `SettingCard` 之前添加：

```qml
    component AboutSection: ColumnLayout {
        spacing: Theme.spacingLg

        SectionHeader {
            title: qsTr("关于")
            subtitle: "About"
        }

        MessageDialog {
            id: updateDialog
            title: qsTr("检查更新")
            buttons: MessageDialog.Ok
            text: ""
        }

        Connections {
            target: SettingsController
            function onUpdateCheckFinished(message, success) {
                updateDialog.text = message
                updateDialog.open()
            }
        }

        SettingCard {
            title: ""
```

确保文件顶部已导入 `QtQuick.Dialogs`（若未导入，添加 `import QtQuick.Dialogs`）。

- [ ] **Step 2: 构建并运行 QML 主窗口测试**

```powershell
cmake --build build/debug --config Debug --target qml_main_window_test
ctest -C Debug -R "^qml_main_window_test$" --output-on-failure
```

Expected: 测试通过。

- [ ] **Step 3: 提交**

```bash
git add app/qml/AgPlayer/SettingsPage.qml
git commit -m "feat(qml/settings): show offline update check dialog"
```

---

## Task 4: 实现命令行音频文件双击启动播放

**Files:**
- Modify: `app/main.cpp:54-71`, `app/main.cpp:141-145`, `app/main.cpp:264-295`, `app/main.cpp:366`

**Interfaces:**
- Consumes: `QGuiApplication::arguments()`, `ImportController`, `PlaybackController`, `LibraryModel`
- Produces: 启动后自动导入并播放命令行指定的音频文件

**问题说明：**
当用户双击关联的音频文件时，Windows 会以 `AgPlayer.exe <file>` 启动程序，需要解析并播放该文件。

- [ ] **Step 1: 解析命令行非选项参数**

在现有 QA 参数解析块中（`app/main.cpp:57-71`），追加命令行文件路径解析：

```cpp
    QString initialFilePath;
    {
        const QStringList cliArgs = QGuiApplication::arguments();
        for (int i = 1; i < cliArgs.size(); ++i) {
            const QString& arg = cliArgs.at(i);
            if (arg == QStringLiteral("--qa-play") && i + 1 < cliArgs.size()) {
                qaPlayPath = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-screenshot-main")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMain = cliArgs.at(++i);
            } else if (arg == QStringLiteral("--qa-screenshot-mini")
                       && i + 1 < cliArgs.size()) {
                qaScreenshotMini = cliArgs.at(++i);
            } else if (!arg.startsWith('-') && initialFilePath.isEmpty()) {
                initialFilePath = arg;
            }
        }
    }
```

- [ ] **Step 2: 在 QML 加载后触发导入/播放**

在 `engine.loadFromModule("AgPlayer", "Main");` 之后、rootObjects 非空分支内添加：

```cpp
        QQmlApplicationEngine engine;
        engine.addImportPath("qrc:/");
        engine.loadFromModule("AgPlayer", "Main");

        QString pendingPlayFilePath = initialFilePath;
        if (!pendingPlayFilePath.isEmpty() && !QFileInfo::exists(pendingPlayFilePath)) {
            pendingPlayFilePath.clear();
        }

        if (!engine.rootObjects().isEmpty()) {
```

在 `register_agplayer_qml_types(...)` 之后、`windows.setShutdownActions({...})` 之前，添加导入完成后的播放逻辑：

```cpp
        auto playFileIfPending = [&]() {
            if (pendingPlayFilePath.isEmpty()) {
                return;
            }
            const QUrl url = QUrl::fromLocalFile(pendingPlayFilePath);
            importer.importUrls({url});
        };

        QObject::connect(&importer, &ImportController::finished, &app,
                         [&library, &playback, pendingPlayFilePath]() {
            if (pendingPlayFilePath.isEmpty()) {
                return;
            }
            const int row = library.indexForLocalFile(pendingPlayFilePath);
            if (row >= 0) {
                playback.playRow(row);
            }
        });
```

注意：`LibraryModel` 需要提供 `indexForLocalFile(const QString&)` 方法（见 Task 5）。

在 `windows.showListWindow();` 之后、`registerGlobalHotkeys();` 之前调用：

```cpp
            playFileIfPending();
```

- [ ] **Step 3: 修改 `app/main.cpp` 中 `--qa-play` 路径处理**

若 `qaPlayPath` 已设置，让 `pendingPlayFilePath = qaPlayPath`，使 `--qa-play` 复用同一套导入/播放路径。在 `QString pendingPlayFilePath = initialFilePath;` 前添加：

```cpp
        if (!qaPlayPath.isEmpty()) {
            initialFilePath = qaPlayPath;
        }
```

然后删除原有的 `--qa-play` 直接调用 `ag_player_load`/`ag_player_play` 的块（`app/main.cpp:264-295`），因为导入控制器路径已覆盖该功能。

- [ ] **Step 4: 构建并运行导入/播放控制器测试**

```powershell
cmake --build build/debug --config Debug --target import_controller_test playback_controller_test
cmake --build build/debug --config Debug --target qml_main_window_test
ctest -C Debug -R "^(import_controller_test|playback_controller_test|qml_main_window_test)$" --output-on-failure
```

Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add app/main.cpp
git commit -m "feat(app): play audio file passed on command line"
```

---

## Task 5: 为 `LibraryModel` 添加 `indexForLocalFile`

**Files:**
- Modify: `qt/src/library_model.hpp`
- Modify: `qt/src/library_model.cpp`

**Interfaces:**
- Consumes: `TrackRecord` 中的文件路径
- Produces: `int indexForLocalFile(const QString& localFilePath) const`

**问题说明：**
命令行文件启动后，需要在 `LibraryModel` 中查找对应行号以触发播放。

- [ ] **Step 1: 在 `library_model.hpp` 添加方法声明**

在 `public:` 区域添加：

```cpp
    int indexForLocalFile(const QString& localFilePath) const;
```

- [ ] **Step 2: 在 `library_model.cpp` 实现方法**

```cpp
int LibraryModel::indexForLocalFile(const QString& localFilePath) const
{
    const QFileInfo targetInfo(localFilePath);
    const QString canonicalTarget = targetInfo.canonicalFilePath();

    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        if (tracks_[i].filePath == localFilePath) {
            return i;
        }
        if (!canonicalTarget.isEmpty()) {
            const QFileInfo candidateInfo(tracks_[i].filePath);
            if (candidateInfo.canonicalFilePath() == canonicalTarget) {
                return i;
            }
        }
    }
    return -1;
}
```

- [ ] **Step 3: 构建并运行相关测试**

```powershell
cmake --build build/debug --config Debug --target library_model_test
cmake --build build/debug --config Debug --target qml_main_window_test
ctest -C Debug -R "^(library_model_test|qml_main_window_test)$" --output-on-failure
```

Expected: 全部通过。

- [ ] **Step 4: 提交**

```bash
git add qt/src/library_model.hpp qt/src/library_model.cpp
git commit -m "feat(qt/library): add indexForLocalFile helper"
```

---

## Task 6: 添加 `FileAssociationController` 单元测试

**Files:**
- Create: `tests/qt/file_association_controller_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FileAssociationController`
- Produces: 注册/反注册/回读测试通过

**问题说明：**
需要验证 `FileAssociationController` 的注册表操作正确，且不会污染真实音频扩展名。

- [ ] **Step 1: 创建测试文件 `tests/qt/file_association_controller_test.cpp`**

```cpp
#include <QCoreApplication>
#include <QTest>

#include "file_association_controller.hpp"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

class FileAssociationControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        controller_ = std::make_unique<FileAssociationController>();
        // Clean up any leftover state from a previous run.
        controller_->unregisterAll();
    }

    void cleanupTestCase()
    {
        if (controller_) {
            controller_->unregisterAll();
            controller_.reset();
        }
    }

    void registerAndQuery()
    {
#ifdef Q_OS_WIN
        const QStringList extensions = {QStringLiteral("agptest")};
        QVERIFY(controller_->registerForExtensions(extensions));
        QVERIFY(controller_->isAssociated(QStringLiteral("agptest")));

        QVERIFY(controller_->unregisterForExtensions(extensions));
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));
#else
        QVERIFY(!controller_->registerForExtensions({QStringLiteral("agptest")}));
        QCOMPARE(controller_->lastError(), QStringLiteral("not supported on this platform"));
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));
#endif
    }

    void unregisterAllRemovesProgIdAndExtensions()
    {
#ifdef Q_OS_WIN
        const QStringList extensions = {QStringLiteral("agptest")};
        QVERIFY(controller_->registerForExtensions(extensions));
        QVERIFY(controller_->unregisterAll());
        QVERIFY(!controller_->isAssociated(QStringLiteral("agptest")));

        const QString progIdPath = QStringLiteral("Software\\Classes\\AgPlayerAudioFile");
        const std::wstring progIdPathW = progIdPath.toStdWString();
        HKEY key = nullptr;
        const bool exists =
            RegOpenKeyExW(HKEY_CURRENT_USER, progIdPathW.c_str(), 0, KEY_READ, &key)
            == ERROR_SUCCESS;
        if (exists) {
            RegCloseKey(key);
        }
        QVERIFY(!exists);
#else
        QVERIFY(!controller_->unregisterAll());
#endif
    }

private:
    std::unique_ptr<FileAssociationController> controller_;
};

QTEST_MAIN(FileAssociationControllerTest)

#include "file_association_controller_test.moc"
```

- [ ] **Step 2: 在 `tests/CMakeLists.txt` 注册测试**

在 `settings_controller_test` 注册之后（或任意 Qt 测试之后）添加：

```cmake
add_executable(file_association_controller_test
    qt/file_association_controller_test.cpp
)
set_target_properties(file_association_controller_test PROPERTIES AUTOMOC ON)
target_include_directories(file_association_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src"
)
target_link_libraries(file_association_controller_test PRIVATE
    agplayer_qt Qt6::Test
)
agplayer_enable_warnings(file_association_controller_test)

add_test(NAME file_association_controller_test COMMAND file_association_controller_test)
set_tests_properties(file_association_controller_test PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)
```

- [ ] **Step 3: 构建并运行测试**

```powershell
cmake --build build/debug --config Debug --target file_association_controller_test
ctest -C Debug -R "^file_association_controller_test$" --output-on-failure
```

Expected: 测试通过。

- [ ] **Step 4: 提交**

```bash
git add tests/qt/file_association_controller_test.cpp tests/CMakeLists.txt
git commit -m "test(qt): add FileAssociationController registry tests"
```

---

## Task 7: 全局回归验证

**Files:**
- 所有已修改文件

**Interfaces:**
- 全项目构建与测试

- [ ] **Step 1: Debug 全量构建与测试**

```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -InstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -Arch amd64
cmake --build build/debug --config Debug
ctest --preset windows-msvc-debug --output-on-failure
```

Expected: 100% tests passed。

- [ ] **Step 2: Release 全量构建与测试**

```powershell
cmake --build build/release --config Release
ctest --preset windows-msvc-release --output-on-failure
```

Expected: 100% tests passed。

- [ ] **Step 3: 提交（如有未提交的变更）**

如果回归过程中没有产生新提交，则跳过；否则提交修复。

---

## 自评检查

**1. Spec coverage:**
- 文件关联注册表绑定 → Task 1 + Task 2
- 双击启动播放 → Task 4 + Task 5
- 离线更新提示 → Task 2 + Task 3
- 非 Windows 平台降级 → Task 1
- 单元测试 → Task 6

**2. Placeholder scan:**
- 无 "TBD" / "TODO" / "implement later"
- 每个步骤包含具体文件路径、代码、命令

**3. Type consistency:**
- `FileAssociationController` 方法签名与设计文档一致
- `updateCheckFinished(message, success)` 信号签名一致
- `indexForLocalFile` 返回 `int`
