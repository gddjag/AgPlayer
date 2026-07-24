# AgPlayer Phase 3-1 系统级能力设计文档

> **目标**：完成 Windows 文件关联注册表绑定（含 ProgID / 图标 / Open 动词）、双击音频文件启动播放、以及“检查更新”按钮改为离线提示“当前已是最新版本”。
> **范围**：仅系统级能力，不包含音频工具、BPM、性能优化等后续 Phase 3 子项目。
> **平台**：以 Windows x64 为主实现平台，非 Windows 平台所有注册表相关方法安全降级为无操作。

---

## 1. 设计约束

- 必须保持现有三层架构：Core（C++17 / C ABI）保持干净，不处理任何平台 Shell 行为；Qt Bridge 负责 Windows 注册表；QML 只负责 UI 与信号绑定。
- 不打包 EXE，开发/测试继续使用现有构建产物 `AgPlayer.exe`。
- 所有功能必须可离线工作，不访问网络。
- 错误必须静默处理，不能阻断启动或弹出未请求的错误窗。
- 注册表操作限定在 `HKEY_CURRENT_USER`，避免需要管理员权限。

---

## 2. 文件结构

### 2.1 新增文件

| 文件 | 职责 |
|------|------|
| `qt/src/file_association_controller.hpp` | `FileAssociationController` 类定义 |
| `qt/src/file_association_controller.cpp` | Windows 注册表读写实现；非 Windows 平台降级 |
| `qt/tests/file_association_controller_test.cpp` | 注册/反注册/回读单元测试 |

### 2.2 修改文件

| 文件 | 修改内容 |
|------|----------|
| `qt/src/settings_controller.hpp` | 添加 `FileAssociationController*` 成员；`checkForUpdates()` 改为发射 `updateCheckFinished` 信号 |
| `qt/src/settings_controller.cpp` | `setSetAsDefaultPlayer` / `setFileAssociations` / `rebindFileAssociations` / `load` 中调用 `FileAssociationController`；`checkForUpdates()` 改为离线提示 |
| `app/qml/AgPlayer/SettingsPage.qml` | “检查更新”点击后弹出 `MessageDialog` 显示 `updateCheckFinished` 内容 |
| `app/main.cpp` | 解析命令行非选项参数，启动后导入并播放该文件 |
| `qt/src/CMakeLists.txt`（或对应构建文件） | 添加新源文件与测试目标 |

---

## 3. `FileAssociationController` 接口

```cpp
class FileAssociationController : public QObject {
    Q_OBJECT
public:
    explicit FileAssociationController(QObject* parent = nullptr);

    // 为指定扩展名注册 AgPlayer 为默认打开程序（写入 ProgID 与扩展名关联）
    Q_INVOKABLE bool registerForExtensions(const QStringList& extensions);

    // 移除指定扩展名与 AgPlayer 的关联（不恢复其他程序默认设置）
    Q_INVOKABLE bool unregisterForExtensions(const QStringList& extensions);

    // 删除 AgPlayer ProgID 以及所有关联的扩展名
    Q_INVOKABLE bool unregisterAll();

    // 查询某个扩展名当前是否指向 AgPlayer ProgID
    Q_INVOKABLE bool isAssociated(const QString& extension) const;

    // 上一次操作的错误信息；成功为空字符串
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

### 3.1 注册表结构

ProgID 根节点：`HKEY_CURRENT_USER\Software\Classes\AgPlayerAudioFile`

```
AgPlayerAudioFile
  (默认)        REG_SZ  "AgPlayer Audio File"
  DefaultIcon   REG_SZ  "<appPath>",0
  shell
    open
      command   REG_SZ  "<appPath>" "%1"
```

扩展名节点示例：`HKEY_CURRENT_USER\Software\Classes\.mp3`

```
.mp3
  (默认)              REG_SZ  "AgPlayerAudioFile"
  PerceivedType       REG_SZ  "audio"
  OpenWithProgids
    AgPlayerAudioFile REG_NONE
```

### 3.2 非 Windows 平台行为

所有公开 `Q_INVOKABLE` 方法返回 `false`，`lastError()` 返回 `"not supported on this platform"`。`supportedAudioExtensions()` 仍返回音频扩展名列表，供 UI 显示使用。

---

## 4. `SettingsController` 集成

### 4.1 新增成员与信号

```cpp
class SettingsController final : public QObject {
    Q_OBJECT
    // ... 原有属性 ...

signals:
    // ... 原有信号 ...
    void updateCheckFinished(const QString& message, bool success);

private:
    std::unique_ptr<FileAssociationController> fileAssociationController_;
};
```

### 4.2 关联逻辑

- `setSetAsDefaultPlayer(bool value)`
  - `value == true`：调用 `fileAssociationController_->registerForExtensions(fileAssociations_)`
  - `value == false`：调用 `fileAssociationController_->unregisterForExtensions(fileAssociations_)`
  - 失败时调用 `RuntimeLog::log` 记录，不弹窗。

- `setFileAssociations(const QStringList& value)`
  - 保存新列表后，若 `setAsDefaultPlayer_ == true`，先反注册旧列表，再注册新列表。

- `rebindFileAssociations()`
  - 若 `setAsDefaultPlayer_ == true`，执行 `unregisterAll()` 后重新注册当前 `fileAssociations_`。
  - 若 `setAsDefaultPlayer_ == false`，执行 `unregisterAll()`。

- `load()`
  - 读取完设置后，若 `setAsDefaultPlayer_ == true`，自动重绑一次，保证重装或拷贝后设置生效。

### 4.3 自动更新离线化

```cpp
void SettingsController::checkForUpdates()
{
    emit updateCheckFinished(tr("当前已是最新版本"), true);
}
```

`openOfficialWebsite()` 保持打开 GitHub 主页不变。

---

## 5. 命令行文件启动

### 5.1 解析规则

在 `main.cpp` 中解析 `QGuiApplication::arguments()`：

1. 跳过程序名本身。
2. 跳过所有 `--qa-*` 选项及其参数。
3. 跳过以 `-` 开头的未知选项。
4. 第一个非选项参数视为音频文件路径。

### 5.2 启动流程

QML 引擎加载完成后：

1. 若 `initialFilePath` 非空且文件存在，调用 `importer.importUrls({QUrl::fromLocalFile(initialFilePath)})`。
2. 连接 `ImportController::finished` 到 lambda：
   - 在 `library` 中查找与 `initialFilePath` 匹配的行号。
   - 若找到，调用 `playback.playRow(row)`。

### 5.3 限制

- 仅处理单个文件；多个文件通过拖放或导入对话框处理。
- 如果文件已在曲库中，直接播放；否则先导入。

---

## 6. QML 交互

### 6.1 文件关联设置页

保持现有 UI：

- “设为系统默认音频播放器” `SettingSwitch` 绑定 `SettingsController.setAsDefaultPlayer`
- 扩展名复选框绑定 `SettingsController.fileAssociations`
- “重新绑定文件关联与图标”按钮调用 `SettingsController.rebindFileAssociations()`

无需新增 UI 控件。

### 6.2 检查更新弹窗

在 `AboutSection` 的“检查更新”按钮点击处理中：

```qml
Button {
    text: qsTr("检查更新")
    onClicked: SettingsController.checkForUpdates()
}

MessageDialog {
    id: updateDialog
    title: qsTr("检查更新")
    buttons: MessageDialog.Ok
}

Connections {
    target: SettingsController
    function onUpdateCheckFinished(message, success) {
        updateDialog.text = message
        updateDialog.open()
    }
}
```

---

## 7. 错误处理

| 场景 | 处理 |
|------|------|
| 注册表写入失败 | `RuntimeLog` 记录；`FileAssociationController::lastError()` 返回详情；UI 不弹窗 |
| 非 Windows 平台 | 所有方法返回 `false`；UI 设置项仍可显示但操作无效果 |
| 用户无注册表权限 | 限定 `HKEY_CURRENT_USER`，普通用户通常可写 |
| 命令行路径不存在 | 静默忽略 |
| 导入失败 | 由 `ImportController` 原有逻辑处理 |

---

## 8. 测试策略

### 8.1 单元测试

- `file_association_controller_test.cpp`
  - 使用 `.agptest` 扩展名避免污染真实音频扩展名。
  - 测试 `registerForExtensions` 后 `isAssociated(".agptest") == true`。
  - 测试 `unregisterForExtensions` 后 `isAssociated(".agptest") == false`。
  - 测试 `unregisterAll` 后 ProgID 与扩展名均不存在。
  - 每个测试结束后清理注册表。

### 8.2 QML 测试

- 验证点击“检查更新”后弹窗文本为“当前已是最新版本”。
- 验证 `setAsDefaultPlayer` 切换时 SettingsController 调用 FileAssociationController（可通过信号或 C++ 侧 mock 验证）。

### 8.3 手动测试

- 在 Windows 上勾选扩展名并打开“设为系统默认音频播放器”，检查注册表项存在。
- 双击关联扩展名音频文件，确认 AgPlayer 启动并播放。
- 反注册后确认右键“打开方式”中不再出现 AgPlayer（或不再为默认）。

---

## 9. 依赖与不变项

- 依赖：Qt 6.7、Windows API（`advapi32`）、现有 `RuntimeLog`、`ImportController`、`PlaybackController`、`LibraryModel`。
- 不新增第三方库。
- 不改变 Core C API。
- 不修改现有音频播放、波形、歌词逻辑。
