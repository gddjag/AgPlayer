# AgPlayer Soft Gradient Glass Skins Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有单色 Theme Engine 修正为肉眼可辨识的整窗柔和渐变皮肤系统，并提供五套推荐渐变、自定义单色/三色、紧凑应用内 Picker、完整事务与 Windows 交付证据。

**Architecture:** C++ `ThemeManager` 仍是唯一颜色计算源，以 `appearanceMode + skinMode + skinKind + skinStops[3]` 原子生成不透明回退 Palette、三段背景和半透明玻璃 Token；QML 只通过共享 `SkinBackdrop` 绘制 Token，不计算 HSL、对比度或派生色。`SettingsController` 保留现有事务和旧单色设置兼容，用一次完整配置通知驱动 `ThemeSettingsSynchronizer`，避免三个 Stop 产生中间 Palette。

**Tech Stack:** C++17、Qt 6.7 (`QColor`, `QPalette`, Qt Quick, Qt Quick Controls Basic)、QML、Qt Test/Quick Test、CMake/CTest、PowerShell、现有 Windows/Inno Setup 打包链。

**Spec:** `docs/superpowers/specs/2026-08-29-soft-gradient-glass-skins-design.md`

## Global Constraints

- Default 的中性 Light/Dark 表面、`#007AFF` 激活色和 `QColor(143, 87, 201, 87)` 播放中行必须精确回归；System 仍实时选择 Light/Dark。
- 推荐皮肤只显示并按顺序保存 `aurora`、`seaGlass`、`sunset`、`lavenderMist`、`morningGlow`；旧 10 个单色 ID 只作兼容解析，不重新显示为推荐项。
- Generated 皮肤必须改变整窗普通 UI：背景、卡片、Elevated、Hover、Pressed、边界、文字、普通图标、Accent、Highlight、Focus 和播放控制环。
- 波形、频谱、音频编辑器选区/游标、Equalizer 曲线、CUE、用户标签，以及收藏/录音/错误/成功/警告/评分语义色不得参与皮肤派生。
- 最差玻璃合成面的对比度：主文字 `>= 7.0:1`，次文字与按钮文字 `>= 4.5:1`，三级文字、Focus、重要图标和边界 `>= 3.0:1`。
- 不新增依赖、线程、轮询、缓存、Shader、真实背景模糊、平台材质层、动画框架或第三方颜色库。
- 不把颜色算法复制到 QML；普通 QML 不新增 `Qt.lighter/darker`、HSL/HSV 混色或未归类 Hex。
- 一次完整配置只允许一次实际 `paletteChanged`；相同输入不得重复通知。
- 在 `D:/ai/AgPlayer/.worktrees/full-custom-theme-engine` 实施，保留其他 worktree 的用户改动；跨分支集成只在功能与 QA 完成后执行。
- 每个任务先写失败测试、再做最小实现、单独审查并提交；任何未执行的实机或跨平台验证必须明确记录。

---

## Baseline and File Responsibility Map

实施前执行并记录：

```powershell
Set-Location D:\ai\AgPlayer\.worktrees\full-custom-theme-engine
git status --short --branch
git log -3 --oneline
git worktree list
```

期望基线：分支 `codex/full-custom-theme-engine`，规格提交 `bb6331e`，工作树除本计划提交外无生产代码改动。

| 文件 | 单一职责 |
| --- | --- |
| `qt/src/theme_manager.hpp/.cpp` | 皮肤配置类型、五套推荐输入、旧 ID 解析、完整 Palette 计算、去重通知和原生不透明 `QPalette`。 |
| `qt/src/settings_controller.hpp/.cpp` | 新三字段持久化、合法化、旧设置迁移、编辑事务和一次完整配置通知。 |
| `app/qml/AgPlayer/theme/Theme.qml` | C++ Token 的只读兼容门面；Generated 时旧 `panel/elevated/hoverSurface` 指向玻璃 Token。 |
| `app/qml/AgPlayer/components/SkinBackdrop.qml` | 只绘制三段横向渐变和低透明纵向亮暗层，不接受外部颜色算法。 |
| `app/qml/AgPlayer/components/AgColorPicker.qml` | 约 `292x248` 的应用内工作色编辑、明确 Apply/Cancel、键盘与 Overlay 边界。 |
| `app/qml/AgPlayer/components/ColorField.qml` | 打开 Picker、传递编辑标签、只在 Apply 时提交一个颜色。 |
| `app/qml/AgPlayer/components/ThemeColorSelector.qml` | Default、五套渐变、Custom Solid/Gradient 和三个 Stop 的可访问编辑 UI。 |
| 六个窗口与 `DockedWindowFrame.qml` | 将共享背景放到最底层并继续消费 Theme Token；不各自实现渐变。 |
| `SettingsPage.qml` | 绑定完整皮肤配置和原子选择方法，按 Custom 状态调整设置行高度。 |
| `tests/qt/*.cpp`, `tests/qml/*.qml`, `cmake/CheckQmlThemeColors.cmake` | 算法、设置事务、Picker/选择器、窗口接入及静态颜色合同。 |
| `app/main.cpp`, `scripts/qa-final-ui-matrix.ps1` | 可重复表达推荐、Solid、Gradient 和 Picker 打开态的视觉 QA。 |
| `translations/agplayer_{zh,en,th,vi}.ts` | 五个皮肤名、Solid/Gradient、Stop、Apply/Cancel 的四语言文案。 |
| `docs/development/*`, `docs/qa/*` | 需求到证据的追踪、视觉/播放/性能/平台边界和最终包信息。 |

### Shared interface contract

后续任务统一使用以下稳定值和签名，不在各层发明别名：

```cpp
class ThemeManager final : public QObject {
public:
    using SkinStops = std::array<QColor, 3>;
    enum class SkinKind { Solid = 0, Gradient = 1 };
};

struct ThemeManager::Preferences {
    AppearanceMode appearanceMode = AppearanceMode::System;
    SkinMode skinMode = SkinMode::Default;
    SkinKind skinKind = SkinKind::Solid;
    SkinStops skinStops{defaultSeed(), defaultSeed(), defaultSeed()};
};

Q_INVOKABLE void SettingsController::selectDefaultSkin();
Q_INVOKABLE void SettingsController::selectSkinPreset(const QString& id);
Q_INVOKABLE void SettingsController::setSkinCustomConfiguration(
    int kind, const QString& start, const QString& middle, const QString& end);
```

`skinColorMode` 的磁盘值保持 `0=Default / 1=Preset / 2=Custom`；`skinCustomKind` 固定 `0=Solid / 1=Gradient`。QML 可以收到各字段的属性通知，但 `ThemeSettingsSynchronizer` 只监听 `themeModeChanged` 和新的 `skinConfigurationChanged`，从而一次原子方法调用只重算一次 Palette。

---

### Task 1: Lock the Theme Configuration and Recommended Preset Contract

**Files:**
- Modify: `tests/qt/theme_manager_test.cpp:90-227`
- Modify: `qt/src/theme_manager.hpp:1-182`
- Modify: `qt/src/theme_manager.cpp:104-163, 278-332`

**Interfaces:**
- Consumes: existing `ThemeManager::applyPreferences(const Preferences&)`, one `paletteChanged()` signal, and opaque `QPalette` fallback behavior.
- Produces: `SkinKind`, `SkinStops`, five gradient `Preset` values, `legacyPresetSeed(QString)`, ten new Palette properties, and a read-only `recommendedPresets` QML list.

- [ ] **Step 1: Replace the old preset seed test with exact five-gradient and legacy-resolution RED tests**

Add data rows and assertions containing every approved value:

```cpp
void ThemeManagerTest::recommendedGradientPresetsAreStable()
{
    const QList<ThemeManager::Preset> expected{
        {QStringLiteral("aurora"), {QColor(QStringLiteral("#73A6FF")), QColor(QStringLiteral("#A98BFF")), QColor(QStringLiteral("#F0A8D8"))}},
        {QStringLiteral("seaGlass"), {QColor(QStringLiteral("#71D9D0")), QColor(QStringLiteral("#82C9F4")), QColor(QStringLiteral("#A7B7FF"))}},
        {QStringLiteral("sunset"), {QColor(QStringLiteral("#F49BC2")), QColor(QStringLiteral("#FF9B86")), QColor(QStringLiteral("#FFC97A"))}},
        {QStringLiteral("lavenderMist"), {QColor(QStringLiteral("#8295F2")), QColor(QStringLiteral("#B89BE8")), QColor(QStringLiteral("#E8B7D5"))}},
        {QStringLiteral("morningGlow"), {QColor(QStringLiteral("#8EDFCB")), QColor(QStringLiteral("#D4E9C2")), QColor(QStringLiteral("#FFD995"))}},
    };
    QCOMPARE(ThemeManager::presets(), expected);
    const auto purple = ThemeManager::legacyPresetSeed(QStringLiteral("purple"));
    QVERIFY(purple.has_value());
    QCOMPARE(*purple, QColor(QStringLiteral("#AF52DE")));
    QVERIFY(!ThemeManager::legacyPresetSeed(QStringLiteral("aurora")).has_value());
}
```

Extend `defaultPaletteIsExact` so all three backdrop Stop equal `background`, and glass tokens equal their opaque counterparts.

- [ ] **Step 2: Run the focused test and verify the contract fails before implementation**

Run:

```powershell
cmake --build build/release --target theme_manager_test --parallel
ctest --test-dir build/release -R "^theme_manager_test$" --output-on-failure
```

Expected: compile failure because `Preset::stops`, `SkinKind`, `backdropStart` and `legacyPresetSeed` do not exist.

- [ ] **Step 3: Add the minimal public types and Palette outputs**

Add `<array>`, `<optional>`, `<QVariantList>`, then implement this contract without renaming existing opaque tokens:

```cpp
using SkinStops = std::array<QColor, 3>; // inside ThemeManager's public block

enum class SkinKind { Solid = 0, Gradient = 1 };
Q_ENUM(SkinKind)

struct Preset final {
    QString id;
    SkinStops stops;
    bool operator==(const Preset& other) const
    { return id == other.id && stops == other.stops; }
};

struct Preferences final {
    AppearanceMode appearanceMode = AppearanceMode::System;
    SkinMode skinMode = SkinMode::Default;
    SkinKind skinKind = SkinKind::Solid;
    SkinStops skinStops{defaultSeed(), defaultSeed(), defaultSeed()};
};

static std::optional<QColor> legacyPresetSeed(const QString& id);
Q_PROPERTY(QVariantList recommendedPresets READ recommendedPresets CONSTANT)
QVariantList recommendedPresets() const;
```

Add `backdropStart/Middle/End`, `glassSurface/Elevated/Hover/Pressed`, `glassBorder/Divider/InnerHighlight` to `ThemePalette`, its `operator==`, `Q_PROPERTY` block and inline getters.

- [ ] **Step 4: Implement the single C++ preset source and Default fallback values**

Return exactly five new presets from `presets()`. Keep the old ten seeds only in `legacyPresetSeed()`:

```cpp
std::optional<QColor> ThemeManager::legacyPresetSeed(const QString& id)
{
    static const QHash<QString, QColor> legacy{
        {QStringLiteral("systemBlue"), QColor(QStringLiteral("#007AFF"))},
        {QStringLiteral("indigo"), QColor(QStringLiteral("#5856D6"))},
        {QStringLiteral("purple"), QColor(QStringLiteral("#AF52DE"))},
        {QStringLiteral("pink"), QColor(QStringLiteral("#FF2D55"))},
        {QStringLiteral("red"), QColor(QStringLiteral("#FF3B30"))},
        {QStringLiteral("orange"), QColor(QStringLiteral("#FF9500"))},
        {QStringLiteral("gold"), QColor(QStringLiteral("#FFCC00"))},
        {QStringLiteral("green"), QColor(QStringLiteral("#34C759"))},
        {QStringLiteral("teal"), QColor(QStringLiteral("#30B0C7"))},
        {QStringLiteral("cyan"), QColor(QStringLiteral("#32ADE6"))},
    };
    const auto it = legacy.constFind(id);
    return it == legacy.cend() ? std::nullopt
                               : std::optional<QColor>(*it);
}
```

In Default calculation set backdrop Stop and glass values after the existing exact colors:

```cpp
palette.backdropStart = palette.background;
palette.backdropMiddle = palette.background;
palette.backdropEnd = palette.background;
palette.glassSurface = palette.surface;
palette.glassSurfaceElevated = palette.surfaceElevated;
palette.glassSurfaceHover = palette.surfaceHover;
palette.glassSurfacePressed = palette.surfacePressed;
palette.glassBorder = palette.border;
palette.glassDivider = palette.divider;
palette.glassInnerHighlight = palette.border;
```

Map `recommendedPresets` from `presets()` to `{id,start,middle,end}` `QVariantMap` entries; do not duplicate stop values in QML.

- [ ] **Step 5: Update existing aggregate initializers and run ThemeManager tests green**

Replace old `{appearance, skinMode, seed}` initializers with the shared helper:

```cpp
ThemeManager::Preferences generated(
    ThemeManager::AppearanceMode appearance,
    ThemeManager::SkinKind kind,
    ThemeManager::SkinStops stops)
{
    return {appearance, ThemeManager::SkinMode::Generated, kind, stops};
}
```

Run the same focused test. Expected: `theme_manager_test` passes and Default exact values remain unchanged.

- [ ] **Step 6: Review and commit the configuration contract**

```powershell
git diff --check
git diff -- qt/src/theme_manager.hpp qt/src/theme_manager.cpp tests/qt/theme_manager_test.cpp
git add qt/src/theme_manager.hpp qt/src/theme_manager.cpp tests/qt/theme_manager_test.cpp
git commit -m "feat(theme): define gradient skin contract"
```

---

### Task 2: Generate Soft Backdrops, Glass Surfaces, and Accessible Tokens

**Files:**
- Modify: `tests/qt/theme_manager_test.cpp:1-430`
- Modify: `qt/src/theme_manager.cpp:1-445`

**Interfaces:**
- Consumes: `Preferences::skinKind`, `skinStops`, existing relative luminance/contrast helpers, and all Palette outputs from Task 1.
- Produces: complete Light/Dark Generated palettes, worst-composite contrast solver, minimum visual-distance checks, one-notification behavior, and unchanged semantic/media contracts.

- [ ] **Step 1: Add table-driven RED tests for Solid, Gradient, extreme colors and composite contrast**

Add helpers that composite alpha exactly as QML does and measure the worst of all three backdrop Stop:

```cpp
QColor composite(const QColor& over, const QColor& under);

QList<QColor> glassComposites(const ThemePalette& palette)
{
    QList<QColor> values;
    for (const QColor& backdrop : {palette.backdropStart,
                                   palette.backdropMiddle,
                                   palette.backdropEnd}) {
        values << composite(palette.glassSurface, backdrop)
               << composite(palette.glassSurfaceElevated, backdrop)
               << composite(palette.glassSurfaceHover, backdrop)
               << composite(palette.glassSurfacePressed, backdrop);
    }
    return values;
}
```

Add assertions:

```cpp
QVERIFY(worstContrast(palette.textPrimary, glassComposites(palette)) >= 7.0);
QVERIFY(worstContrast(palette.textSecondary, glassComposites(palette)) >= 4.5);
QVERIFY(worstContrast(palette.textTertiary, glassComposites(palette)) >= 3.0);
QVERIFY(worstContrast(palette.focus, glassComposites(palette)) >= 3.0);
QVERIFY(maxRgbDistance({palette.backdropStart,
                        palette.backdropMiddle,
                        palette.backdropEnd}) >= 0.08);
```

Use all five presets plus `{black,black,black}`, `{white,white,white}`, `{gray,gray,gray}`, `{red,green,blue}`, `{cyan,magenta,yellow}` in Light and Dark. Add a Solid test asserting the three generated Stop keep the input hue (or remain achromatic) and are not identical.

- [ ] **Step 2: Run RED and confirm the current 22% surface algorithm fails visibly**

```powershell
cmake --build build/release --target theme_manager_test --parallel
ctest --test-dir build/release -R "^theme_manager_test$" --output-on-failure
```

Expected: failures for absent glass composites and `maxRgbDistance`, demonstrating that merely unequal RGB tokens are insufficient.

- [ ] **Step 3: Add pure synchronous color helpers for normalization, softening and Solid expansion**

Implement helpers in the existing anonymous namespace. Achromatic inputs must keep saturation zero:

```cpp
QColor withSoftTone(const QColor& input, bool dark, double lightness)
{
    const QColor hsl = normalizedSeed(input).toHsl();
    const bool achromatic = hsl.hslSaturationF() < 0.01 || hsl.hslHueF() < 0.0;
    const double sourceSaturation = achromatic ? 0.0 : hsl.hslSaturationF();
    const double saturation = achromatic ? 0.0
        : std::clamp(sourceSaturation * 0.48,
                     dark ? 0.28 : 0.16,
                     dark ? 0.50 : 0.36);
    return QColor::fromHslF(achromatic ? 0.0 : hsl.hslHueF(),
                            saturation,
                            std::clamp(lightness, 0.0, 1.0));
}

ThemeManager::SkinStops expandedSolidStops(const QColor& seed, bool dark)
{
    return dark
        ? ThemeManager::SkinStops{withSoftTone(seed, true, 0.09),
                                  withSoftTone(seed, true, 0.15),
                                  withSoftTone(seed, true, 0.11)}
        : ThemeManager::SkinStops{withSoftTone(seed, false, 0.94),
                                  withSoftTone(seed, false, 0.88),
                                  withSoftTone(seed, false, 0.96)};
}
```

For Gradient, soften the three identities with the same Light/Dark target lightness values. Normalize every input to uppercase opaque sRGB before calculation; if any Stop is invalid or has alpha, replace all three with `{defaultSeed(), defaultSeed(), defaultSeed()}`.

- [ ] **Step 4: Build translucent glass tokens and opaque fallbacks from one calculation**

Use `skinStops[1]` as the representative action identity. Derive glass alpha from the existing opaque surface family rather than adding a second hue algorithm:

```cpp
palette.glassSurface = alphaColor(palette.surface, dark ? 0.72 : 0.68);
palette.glassSurfaceElevated = alphaColor(
    palette.surfaceElevated, dark ? 0.78 : 0.74);
palette.glassSurfaceHover = alphaColor(
    palette.surfaceHover, dark ? 0.82 : 0.78);
palette.glassSurfacePressed = alphaColor(
    palette.surfacePressed, dark ? 0.86 : 0.82);
palette.glassBorder = alphaColor(palette.borderStrong, dark ? 0.52 : 0.42);
palette.glassDivider = alphaColor(palette.border, dark ? 0.42 : 0.34);
palette.glassInnerHighlight = QColor(255, 255, 255, dark ? 18 : 82);
```

Calculate opaque `background/surface/...` as the composite of those families over the middle backdrop so native controls and detached Popup/Menu/Tooltip remain fully opaque.

- [ ] **Step 5: Solve text, action, focus and selection against the worst composite**

Feed all 12 surface composites into the existing `contrastTone` search rather than testing only `background/surface`. Choose button foreground with the existing `readableForeground`, then assert its result against Accent itself and the surface set. Keep Default `currentTrackSurface` fixed purple, but set Generated to a softened translucent action tone:

```cpp
const QList<QColor> readableSurfaces = compositeSurfaces(palette);
palette.textPrimary = contrastTone(representative, 0.12,
    dark ? 245 : 20, dark ? 1 : -1, readableSurfaces, 7.0);
palette.textSecondary = contrastTone(representative, 0.16,
    dark ? 210 : 55, dark ? 1 : -1, readableSurfaces, 4.5);
palette.textTertiary = contrastTone(representative, 0.18,
    dark ? 175 : 90, dark ? 1 : -1, readableSurfaces, 3.0);
palette.accent = contrastTone(representative, 0.72,
    representative.toHsl().lightness(), dark ? 1 : -1,
    readableSurfaces, 4.5);
palette.highlight = palette.accent;
palette.focus = palette.accent;
palette.currentTrackSurface = softTone(palette.accent, 0.28);
```

Do not modify the semantic color assignments at the top of `calculatePalette`, nor any media Token in `Theme.qml`.

- [ ] **Step 6: Extend equality, native palette and notification regression tests**

Assert one `paletteChanged` for one complete Preferences change, zero for identical Preferences, and verify every `QPalette` role has alpha 255. Confirm changing only Stop 2 or Stop 3 changes backdrop/glass and is not swallowed by `ThemePalette::operator==`.

- [ ] **Step 7: Run focused tests, inspect the algorithm diff, and commit**

```powershell
cmake --build build/release --target theme_manager_test --parallel
ctest --test-dir build/release -R "^theme_manager_test$" --output-on-failure
git diff --check
git diff -- qt/src/theme_manager.cpp tests/qt/theme_manager_test.cpp
git add qt/src/theme_manager.cpp tests/qt/theme_manager_test.cpp
git commit -m "feat(theme): generate soft glass palettes"
```

Expected: all table rows pass in Light/Dark; no timer, thread, cache or dependency appears in the diff.

---

### Task 3: Persist and Apply Complete Skin Configurations Atomically

**Files:**
- Modify: `tests/qt/settings_controller_test.cpp:60-700`
- Modify: `qt/src/settings_controller.hpp:53-60, 196-200, 280-300, 378-381, 484-488`
- Modify: `qt/src/settings_controller.cpp:35-132, 433-464, 939-1040, 1273-1291, 1590-1630`
- Modify: `qt/src/theme_manager.cpp:104-132, 447-468`

**Interfaces:**
- Consumes: `ThemeManager::presets()`, `legacyPresetSeed()`, `Preferences`, and the current QSettings edit transaction.
- Produces: `skinCustomKind`, `skinCustomColorMiddle`, `skinCustomColorEnd`, atomic selection methods, one `skinConfigurationChanged`, legacy migration and complete startup/preview/cancel/commit/reset behavior.

- [ ] **Step 1: Add RED tests for defaults, round trip, legacy, invalid group fallback and transaction atomicity**

Extend the test class with exact defaults:

```cpp
QCOMPARE(settings.skinColorMode(), 0);
QCOMPARE(settings.skinCustomKind(), 0);
QCOMPARE(settings.skinCustomColor(), QStringLiteral("#D27722"));
QCOMPARE(settings.skinCustomColorMiddle(), QStringLiteral("#D27722"));
QCOMPARE(settings.skinCustomColorEnd(), QStringLiteral("#D27722"));
```

Test `setSkinCustomConfiguration(1, "#73a6ff", "#a98bff", "#f0a8d8")` persists uppercase values, emits one `skinConfigurationChanged`, previews immediately, cancels to all previous fields, and commits all fields. Seed the INI with old `skinColorMode=1, skinPreset=purple`; assert runtime becomes Custom+Solid with `#AF52DE` while the old `skinPreset` key is not deleted. Seed one invalid/alpha/missing Gradient Stop; assert all three custom colors fall back to `#D27722` together and waveform/spectrum keys remain byte-for-byte unchanged.

- [ ] **Step 2: Run settings and synchronizer tests RED**

```powershell
cmake --build build/release --target settings_controller_test theme_manager_test --parallel
ctest --test-dir build/release -R "settings_controller_test|theme_manager_test" --output-on-failure
```

Expected: compile failures for the three new properties and atomic methods.

- [ ] **Step 3: Add properties, methods, one complete-config signal and stable defaults**

Add:

```cpp
Q_PROPERTY(int skinCustomKind READ skinCustomKind WRITE setSkinCustomKind
           NOTIFY skinCustomKindChanged)
Q_PROPERTY(QString skinCustomColorMiddle READ skinCustomColorMiddle
           WRITE setSkinCustomColorMiddle NOTIFY skinCustomColorMiddleChanged)
Q_PROPERTY(QString skinCustomColorEnd READ skinCustomColorEnd
           WRITE setSkinCustomColorEnd NOTIFY skinCustomColorEndChanged)

Q_INVOKABLE void selectDefaultSkin();
Q_INVOKABLE void selectSkinPreset(const QString& id);
Q_INVOKABLE void setSkinCustomConfiguration(
    int kind, const QString& start, const QString& middle, const QString& end);

signals:
    void skinCustomKindChanged();
    void skinCustomColorMiddleChanged();
    void skinCustomColorEndChanged();
    void skinConfigurationChanged();
```

Default the new member strings to `#D27722` and kind to `0`.

- [ ] **Step 4: Implement atomic normalization and persistence**

Normalize all incoming fields before assigning. The atomic method must either accept the entire group or replace the entire group:

```cpp
void SettingsController::setSkinCustomConfiguration(
    int kind, const QString& start, const QString& middle, const QString& end)
{
    const int normalizedKind = kind == 1 ? 1 : 0;
    const QString normalizedStart = normalizedOpaqueThemeColor(start);
    const QString normalizedMiddle = normalizedOpaqueThemeColor(middle);
    const QString normalizedEnd = normalizedOpaqueThemeColor(end);
    const bool valid = !normalizedStart.isEmpty()
        && (normalizedKind == 0
            || (!normalizedMiddle.isEmpty() && !normalizedEnd.isEmpty()));
    const QString fallback = defaultThemeCustomColor();
    applySkinFields(2, skinPreset_, normalizedKind,
        valid ? normalizedStart : fallback,
        valid && normalizedKind == 1 ? normalizedMiddle : fallback,
        valid && normalizedKind == 1 ? normalizedEnd : fallback);
}

void SettingsController::selectDefaultSkin()
{
    applySkinFields(0, skinPreset_, 0, skinCustomColor_,
                    skinCustomColorMiddle_, skinCustomColorEnd_);
}

void SettingsController::selectSkinPreset(const QString& id)
{
    const QList<ThemeManager::Preset> values = ThemeManager::presets();
    const auto preset = std::find_if(values.cbegin(), values.cend(),
        [&id](const ThemeManager::Preset& value) { return value.id == id; });
    if (preset == values.cend()) {
        selectDefaultSkin();
        return;
    }
    applySkinFields(1, id, 1,
                    preset->stops[0].name(QColor::HexRgb).toUpper(),
                    preset->stops[1].name(QColor::HexRgb).toUpper(),
                    preset->stops[2].name(QColor::HexRgb).toUpper());
}
```

`applySkinFields` updates changed members, writes all six appearance skin keys only when not editing, emits individual property signals, then emits exactly one `skinConfigurationChanged`.

- [ ] **Step 5: Extend load/save/cancel/reset and legacy resolution**

Read/write `appearance/skinCustomKind`, `skinCustomColorMiddle`, `skinCustomColorEnd`. In `load()`, validate all fields as a group. For a persisted old preset ID:

```cpp
if (skinColorMode_ == 1) {
    if (const auto legacy = ThemeManager::legacyPresetSeed(skinPreset_)) {
        skinColorMode_ = 2;
        skinCustomKind_ = 0;
        skinCustomColor_ = legacy->name(QColor::HexRgb).toUpper();
        skinCustomColorMiddle_ = defaultThemeCustomColor();
        skinCustomColorEnd_ = defaultThemeCustomColor();
    }
}
```

Keep the old `skinPreset` key present for downgrade compatibility. Add the three fields to `saveAll()`, `restoreDefaults()` and `emitAllChanged()`, with one final `skinConfigurationChanged()` after individual notifications.

- [ ] **Step 6: Update ThemeSettingsSynchronizer to consume only complete configurations**

Remove per-skin-field connections and connect one signal:

```cpp
connect(&settings_, &SettingsController::themeModeChanged,
        this, apply);
connect(&settings_, &SettingsController::skinConfigurationChanged,
        this, apply);
```

`preferencesFromSettings()` maps Default, a new five-gradient preset, Custom Solid and Custom Gradient to a complete `Preferences`. Unknown new preset IDs fall back to Default; old IDs should already have been normalized to Custom Solid by SettingsController.

- [ ] **Step 7: Run focused tests and commit settings persistence**

```powershell
cmake --build build/release --target settings_controller_test theme_manager_test --parallel
ctest --test-dir build/release -R "settings_controller_test|theme_manager_test" --output-on-failure
git diff --check
git add qt/src/settings_controller.hpp qt/src/settings_controller.cpp qt/src/theme_manager.cpp tests/qt/settings_controller_test.cpp tests/qt/theme_manager_test.cpp
git commit -m "feat(settings): persist complete skin configurations"
```

Expected: Commit/Cancel/Reset are atomic; media settings do not emit or change.

---

### Task 4: Replace the Oversized Two-Stage Picker with One Compact Popup

**Files:**
- Modify: `tests/qml/tst_color_picker.qml:22-375, 524-575`
- Modify: `app/qml/AgPlayer/components/AgColorPicker.qml:1-712`
- Modify: `app/qml/AgPlayer/components/ColorField.qml:1-150`

**Interfaces:**
- Consumes: a single initial `QColor` and optional translated editing label.
- Produces: `openForColor(initialColor, editingLabel)`, `applied(color)`, `cancelled()`, compact SV/hue/HEX/RGB editing and one explicit confirmation path.

- [ ] **Step 1: Replace system-dialog/candidate tests with compact Apply/Cancel RED tests**

Remove tests that require `colorPickerSystemDialog`, `systemSwatch` or candidate-card acceptance. Add:

```qml
function test_picker_compact_size_hex_rgb_apply_cancel_and_escape() {
    picker.openForColor("#22C55E", qsTr("Start color"))
    tryCompare(picker, "visible", true)
    verify(picker.width <= 292)
    verify(picker.height <= 248)
    verify(!findChild(picker, "colorPickerSystemDialog"))

    var hex = findChild(picker, "colorPickerHex")
    var apply = findChild(picker, "colorPickerApply")
    verify(hex && apply)
    hex.text = "#73A6FF"
    keyClick(Qt.Key_Enter)
    compare(appliedSpy.count, 0)
    mouseClick(apply, apply.width / 2, apply.height / 2)
    compare(appliedSpy.count, 1)
    compare(appliedSpy.signalArguments[0][0].toString().toUpperCase(), "#73A6FF")

    picker.openForColor("#A98BFF", qsTr("Middle color"))
    keyClick(Qt.Key_Escape)
    compare(cancelledSpy.count, 1)
    compare(appliedSpy.count, 1)
}
```

Retain and tighten `test_bottom_right_field_popup_stays_inside_overlay` for a minimum `860x900` host.

- [ ] **Step 2: Run QML RED**

```powershell
cmake --build build/release --target qml_main_window_test --parallel
ctest --test-dir build/release -R "^qml_color_picker_test$" --output-on-failure
```

Expected: missing `colorPickerApply`, oversized content, and the still-present native `ColorDialog` fail the test.

- [ ] **Step 3: Remove `QtQuick.Dialogs` and build the 292x248 application Popup**

Delete the native `ColorDialog` import/object and 10 candidate cards. Keep only local working state:

```qml
Popup {
    id: root
    objectName: "agColorPicker"
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(292, Overlay.overlay ? Overlay.overlay.width - 20 : 292)
    height: Math.min(248, Overlay.overlay ? Overlay.overlay.height - 20 : 248)

    property color workingColor: "#D27722"
    property string editingLabel: ""
    signal applied(color color)
    signal cancelled()

    function openForColor(initialColor, label) {
        workingColor = initialColor
        editingLabel = label || ""
        open()
    }
}
```

Use one SV rectangle with horizontal white-to-hue gradient and vertical transparent-to-black overlay, a thin fixed hue rail, HEX/R/G/B inputs, current-color preview, Apply and Cancel. Picker-local `Qt.hsva` calculations remain confined to this whitelisted component.

- [ ] **Step 4: Make every close path explicit and single-shot**

Apply validates/normalizes `#RRGGBB`, emits `applied(workingColor)` once, then closes. Cancel button, Esc and outside press emit `cancelled()` once and never emit `applied`. Enter in an input updates the working color but does not silently commit; Enter/Space on the Apply button commits. Preserve Tab order and `Accessible.name` for SV plane, hue rail, fields and buttons.

- [ ] **Step 5: Update ColorField without changing its external color contract**

Add an `editingLabel` property. On click call `picker.openForColor(root.color, editingLabel)`; on `applied` emit the existing `colorEdited(color)`. Do not emit on working edits or cancel. Keep Overlay clamping in one place inside Picker.

- [ ] **Step 6: Run QML tests, lint the changed components, and commit**

```powershell
cmake --build build/release --target qml_main_window_test agplayer_app_qml_qmllint --parallel
ctest --test-dir build/release -R "^qml_color_picker_test$" --output-on-failure
git diff --check
git add app/qml/AgPlayer/components/AgColorPicker.qml app/qml/AgPlayer/components/ColorField.qml tests/qml/tst_color_picker.qml
git commit -m "fix(theme): make color picker compact and explicit"
```

Expected: no system dialog object/import; Apply and Cancel each have one unambiguous outcome.

---

### Task 5: Build the Five-Gradient and Custom Solid/Gradient Settings Experience

**Files:**
- Modify: `tests/qml/tst_color_picker.qml:39-145, 602-690`
- Modify: `app/qml/AgPlayer/components/ThemeColorSelector.qml:1-220`
- Modify: `app/qml/AgPlayer/SettingsPage.qml:1490-1545`

**Interfaces:**
- Consumes: `ThemeManager.recommendedPresets`, SettingsController properties and three atomic selection methods, compact `ColorField`.
- Produces: Default + five `46x30` cards + Custom; an expandable Solid/Gradient editor; accessible, keyboard-operable, immediate full-palette preview.

- [ ] **Step 1: Add RED tests for exact card order, preview Stop, accessibility and atomic updates**

Add tests using stable object names:

```qml
var ids = ["aurora", "seaGlass", "sunset", "lavenderMist", "morningGlow"]
for (var i = 0; i < ids.length; ++i) {
    var card = findChild(skinSelector, "skinSelectorPreset-" + ids[i])
    verify(card)
    compare(card.width, 46)
    compare(card.height, 30)
    verify(card.Accessible.name.indexOf("#") >= 0)
}
verify(!findChild(skinSelector, "skinSelectorPreset-purple"))
```

Test Space/Enter selection emits one Palette change, Custom expands a second row, Solid shows one field plus generated preview, Gradient shows `skinCustomStart/Middle/End`, each Stop picker identifies its target, and one edit updates all three SettingsController fields through one configuration signal.

- [ ] **Step 2: Run selector tests RED**

```powershell
cmake --build build/release --target qml_main_window_test --parallel
ctest --test-dir build/release -R "^qml_color_picker_test$" --output-on-failure
```

Expected: old 10 circular presets and missing kind/Stop controls fail.

- [ ] **Step 3: Render the recommended model without duplicating color values**

Use `ThemeManager.recommendedPresets` directly as the Repeater model. Keep only translated label lookup in QML:

```qml
function presetName(id) {
    switch (id) {
    case "aurora": return qsTr("Aurora")
    case "seaGlass": return qsTr("Sea Glass")
    case "sunset": return qsTr("Sunset")
    case "lavenderMist": return qsTr("Lavender Mist")
    case "morningGlow": return qsTr("Morning Glow")
    default: return id
    }
}
```

Each card draws a three-Stop `Gradient`, preserves the preview colors, and overlays separate border/check/focus cues. Build `Accessible.name` from translated name plus all three uppercase Stop; color must not be the only selection cue.

- [ ] **Step 4: Add the Custom second row and one complete-config signal**

Keep existing external selection properties and add `customKind`, `customColorMiddle`, `customColorEnd`. Replace `customRequested(color)` with:

```qml
signal customConfigurationRequested(
    int kind, string start, string middle, string end)
```

Use a two-button segmented Solid/Gradient control. Solid edits only Start but emits `{0,start,middle,end}` so stored Gradient colors remain available; its preview uses `ThemeManager` generated backdrop Token after the immediate preview. Gradient shows all three fields and a literal three-Stop input preview. Label the fields Start/Middle/End and pass those labels to Picker.

- [ ] **Step 5: Bind SettingsPage only through atomic methods**

Use:

```qml
onDefaultRequested: SettingsController.selectDefaultSkin()
onPresetRequested: function(id) {
    SettingsController.selectSkinPreset(id)
}
onCustomConfigurationRequested: function(kind, start, middle, end) {
    SettingsController.setSkinCustomConfiguration(kind, start, middle, end)
}
```

Never assign `skinColorMode` and `skinPreset` sequentially in QML. Set the appearance row height to the selector's `implicitHeight`, allowing Custom to add the second row without overlapping “歌曲列表”.

- [ ] **Step 6: Run settings runtime/cancel tests and commit**

```powershell
cmake --build build/release --target qml_main_window_test settings_controller_test theme_manager_test --parallel
ctest --test-dir build/release -R "qml_color_picker_test|settings_controller_test|theme_manager_test" --output-on-failure
git diff --check
git add app/qml/AgPlayer/components/ThemeColorSelector.qml app/qml/AgPlayer/SettingsPage.qml tests/qml/tst_color_picker.qml
git commit -m "feat(theme): add gradient skin settings"
```

Expected: settings Cancel restores the entire pre-entry Palette and configuration; one card or custom edit causes one actual Palette notification.

---

### Task 6: Introduce the Shared Backdrop and Route Ordinary UI Through Glass Tokens

**Files:**
- Create: `app/qml/AgPlayer/components/SkinBackdrop.qml`
- Modify: `app/CMakeLists.txt:123-160`
- Modify: `app/qml/AgPlayer/theme/Theme.qml:1-100`
- Modify: `app/qml/AgPlayer/components/DockedWindowFrame.qml:1-55`
- Modify: `app/qml/AgPlayer/Main.qml:1-250`
- Modify: `app/qml/AgPlayer/ListWindow.qml:1-430`
- Modify: `app/qml/AgPlayer/MiniPlayerWindow.qml:1-90`
- Modify: `app/qml/AgPlayer/SettingsWindow.qml:1-80`
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml:1-100`
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml:1-80`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_mini_player.qml`

**Interfaces:**
- Consumes: all new C++ Token and unchanged opaque native palette roles.
- Produces: one reusable `SkinBackdrop { anchors.fill: parent; radius: ... }`, glass-compatible aliases, all six windows with visible generated skin, and Generated playback ring using Accent.

- [ ] **Step 1: Add RED window contract tests before creating the component**

In main/mini QML tests find `skinBackdrop` and assert its three Stop equal ThemeManager outputs after selecting `aurora`. Assert Default Start/Middle/End equal and Generated differ. Add a PlayerControls assertion: Default control ring retains its current exact behavior; Generated uses `Theme.accent`.

- [ ] **Step 2: Run the window tests RED**

```powershell
cmake --build build/release --target qml_main_window_test qml_mini_player_test --parallel
ctest --test-dir build/release -R "qml_main_window_test|qml_mini_player_test" --output-on-failure
```

Expected: `skinBackdrop` is absent and generated playback ring still uses the fixed color.

- [ ] **Step 3: Create one deep `SkinBackdrop` component and register it**

Create the component with only a public `radius`:

```qml
import QtQuick
import AgPlayer.theme 1.0

Item {
    id: root
    objectName: "skinBackdrop"
    property real radius: 0
    clip: radius > 0

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.backdropStart }
            GradientStop { position: 0.5; color: Theme.backdropMiddle }
            GradientStop { position: 1.0; color: Theme.backdropEnd }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.glassInnerHighlight }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
```

Add the file beside `DockedWindowFrame.qml` in `app/CMakeLists.txt`; do not create a second QML module or hand-written `qmldir`.

- [ ] **Step 4: Forward Token and switch only compatibility aliases to glass**

In `Theme.qml`, add direct properties for backdrop/glass outputs. Keep `background/surface/...` as opaque values, then set:

```qml
readonly property color panel: glassSurface
readonly property color elevated: glassSurfaceElevated
readonly property color hoverSurface: glassSurfaceHover
readonly property color pressedSurface: glassSurfacePressed
readonly property color border: ThemeManager.glassBorder
readonly property color divider: ThemeManager.glassDivider
```

Do not change media or semantic constants.

- [ ] **Step 5: Place the component once at each true root**

Put it inside shared `DockedWindowFrame` for Main/List; as the first clipped child of Mini; and before page content in Settings/AudioTools/Equalizer. Do not duplicate it over Main/List frame border layers. Leave each window's native `palette.window/base/button` bound to opaque Theme tokens.

- [ ] **Step 6: Migrate only high-visual-weight ordinary controls**

Change the Generated play/pause ring and other ordinary fixed action colors to `Theme.accent`, while Default remains covered by ThemeManager's fixed blue action. Audit Button/Switch/Slider/Tab/selection/focus/icon consumers by semantic role; do not touch waveform, spectrum, editor, CUE, rating, favorite, recording or user labels.

- [ ] **Step 7: Run window tests and QML lint, then commit**

```powershell
cmake --build build/release --target qml_main_window_test qml_mini_player_test agplayer_app_qml_qmllint --parallel
ctest --test-dir build/release -R "qml_main_window_test|qml_mini_player_test|qml_color_picker_test" --output-on-failure
git diff --check
git add app/CMakeLists.txt app/qml/AgPlayer tests/qml/tst_main_window.qml tests/qml/tst_mini_player.qml
git commit -m "feat(theme): render shared glass skin backdrop"
```

Expected: all six windows share the same Token, native palettes remain opaque, and media/semantic colors are absent from the diff unless a test explicitly locks them unchanged.

---

### Task 7: Enforce Static Color, Translation, and Deterministic QA Contracts

**Files:**
- Modify: `cmake/CheckQmlThemeColors.cmake`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/qml/tst_color_picker.qml`
- Modify: `translations/agplayer_zh.ts`
- Modify: `translations/agplayer_en.ts`
- Modify: `translations/agplayer_th.ts`
- Modify: `translations/agplayer_vi.ts`
- Modify: `app/main.cpp:268-375, 650-760`
- Modify: `scripts/qa-final-ui-matrix.ps1:1-367`

**Interfaces:**
- Consumes: final setting names, selector object names, existing screenshot routes.
- Produces: four complete catalogs, static prohibition of page-local color derivation, backward-compatible QA CLI and deterministic picker screenshot state.

- [ ] **Step 1: Make static and translation tests fail on the new contract first**

Update static checks to require `skinCustomKind`, `skinCustomColorMiddle`, `skinCustomColorEnd`, `SkinBackdrop.qml` registration, and absence of `QtQuick.Dialogs`/`ColorDialog` in Picker. Add checks forbidding `Qt.lighter`, `Qt.darker`, `Qt.hsla`, `Qt.hsva` outside the Picker/preview whitelist. Extend translation expectations with Aurora, Sea Glass, Sunset, Lavender Mist, Morning Glow, Solid, 3-color gradient, Start/Middle/End color, Apply and Cancel.

- [ ] **Step 2: Run RED static contracts**

```powershell
cmake -DROOT=D:/ai/AgPlayer/.worktrees/full-custom-theme-engine -P cmake/CheckQmlThemeColors.cmake
ctest --test-dir build/release -R "qml_theme_color_contract_test|translation_catalog_test" --output-on-failure
```

Expected: missing new bindings/translations and stale old contract assumptions fail.

- [ ] **Step 3: Complete all four translation catalogs**

Add real translations, not source-language fallbacks. Use these Chinese labels: `极光 / 海盐 / 日落 / 薰衣草 / 晨光 / 单色 / 三色渐变 / 起点颜色 / 中点颜色 / 终点颜色 / 应用 / 取消`; add equivalent natural English, Thai and Vietnamese strings. Run the existing `release_translations` target so `.qm` generation is verified without committing generated build artifacts.

- [ ] **Step 4: Extend QA CLI without breaking old `--qa-skin`**

Keep `--qa-skin default|id|#RRGGBB`. Add:

```text
--qa-skin-kind solid|gradient
--qa-skin-start #RRGGBB
--qa-skin-middle #RRGGBB
--qa-skin-end #RRGGBB
--qa-open-skin-picker start|middle|end
```

After SettingsController construction, route preset IDs through `selectSkinPreset`, old IDs/single Hex through `setSkinCustomConfiguration(0, value, value, value)`, and a complete three-Stop set through `setSkinCustomConfiguration(1, start, middle, end)`. Reject an incomplete triple with a logged warning and nonzero QA exit rather than accepting half a configuration.

- [ ] **Step 5: Extend the PowerShell matrix inputs and evidence naming**

Add `-SkinKind`, `-SkinStart`, `-SkinMiddle`, `-SkinEnd`, `-OpenSkinPicker`. Write these exact values to `matrix.csv` and filename stems. Preserve the existing `-Skin` path. Pass the new CLI flags only when the full required configuration is present; keep screenshot dimensions, blank-image detection and log scanning unchanged.

- [ ] **Step 6: Run focused contracts, QML lint and commit**

```powershell
cmake --build build/release --target release_translations agplayer_app_qml_qmllint AgPlayer --parallel
cmake -DROOT=D:/ai/AgPlayer/.worktrees/full-custom-theme-engine -P cmake/CheckQmlThemeColors.cmake
ctest --test-dir build/release -R "qml_theme_color_contract_test|translation_catalog_test|qml_color_picker_test" --output-on-failure
git diff --check
git add cmake/CheckQmlThemeColors.cmake tests/CMakeLists.txt tests/qml/tst_color_picker.qml translations app/main.cpp scripts/qa-final-ui-matrix.ps1
git commit -m "test(theme): automate gradient skin acceptance"
```

---

### Task 8: Build, Review, and Capture Product/Runtime Acceptance Evidence

**Files:**
- Create: `docs/development/2026-08-29-soft-gradient-glass-skins.md`
- Create: `docs/qa/2026-08-29-soft-gradient-glass-skins-acceptance.md`
- Generated, not committed: `build/qa/2026-08-29-soft-gradient-glass-skins/**`

**Interfaces:**
- Consumes: completed feature branch, QA CLI/matrix, test WAV, user reference PNG paths from the task.
- Produces: requirements traceability, exact pass/fail counts, visual review, playback/performance evidence and honest platform boundary.

- [ ] **Step 1: Run clean Debug and Release configure/builds in the real VS environment**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
Set-Location D:\ai\AgPlayer\.worktrees\full-custom-theme-engine
cmake --fresh --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel
cmake --fresh --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
```

Record compiler/Qt versions and any baseline failures verbatim.

- [ ] **Step 2: Run focused and full CTest plus lint/static checks**

```powershell
ctest --test-dir build/release -R "theme_manager_test|settings_controller_test|qml_color_picker_test|qml_theme_color_contract_test|translation_catalog_test|qml_main_window_test|qml_mini_player_test" --output-on-failure
ctest --preset windows-msvc-release
ctest --preset windows-msvc-debug
cmake --build build/release --target all_qmllint --parallel
cmake -DROOT=D:/ai/AgPlayer/.worktrees/full-custom-theme-engine -P cmake/CheckQmlThemeColors.cmake
git diff --check
```

Do not describe Debug as green if any known or new failure remains; rerun failed tests serially and classify reproducible baseline versus regression.

- [ ] **Step 3: Capture the required visual matrix**

For each of the five preset IDs, run Chinese Light/Dark across all 10 surfaces. Then run Default in four languages across Light/Dark/System, plus custom black/white Solid and one RGB triple in Light/Dark. Use this exact command shape:

```powershell
.\scripts\qa-final-ui-matrix.ps1 `
  -BuildDirectory build/release `
  -OutputDirectory build/qa/2026-08-29-soft-gradient-glass-skins/aurora `
  -Languages zh `
  -Themes dark,light `
  -Skin aurora `
  -Surfaces startup,playback,mini,settings,list,details,tool-0,tool-1,tool-2,tool-3
```

Repeat with the other stable IDs and custom arguments; capture `start/middle/end` Picker open states separately.

- [ ] **Step 4: Perform Product Design visual review against the supplied references**

Open actual screenshots, not just CSV. Check full-window color difference, soft saturation, glass hierarchy, selected/focus cues, text/icon contrast, settings row spacing, `46x30` cards and Picker proportion. Compare representative AgPlayer screenshots side-by-side with the supplied reference PNGs and record where AgPlayer intentionally differs because it uses no true blur/Shader. Screenshots are evidence, not a substitute for keyboard tests.

- [ ] **Step 5: Run real WAV playback smoke while switching skins and sample resources**

```powershell
.\scripts\qa-main-smoke.ps1 -BuildDirectory build/release
```

Additionally run the QA app on `build/release/tests/fixtures/sine-440hz.wav` for Default, aurora, sunset, black Solid, white Solid and the RGB triple. During active playback switch skin, confirm continuous position/state, capture exit code/log, and sample CPU/Working Set before and after using `Get-Process`. Record that this verifies the normal WAV path, not subjective audio quality or all hardware.

- [ ] **Step 6: Write traceability and acceptance records with exact evidence**

The development record maps every spec requirement to files/tests/screenshots. The QA record lists exact commands, pass counts, known baseline failures, manual visual results, Windows system Light/Dark live switch and cold-start first-frame result. Explicitly state macOS/Linux are automated shared-code coverage only when those hosts were unavailable.

- [ ] **Step 7: Request independent code and visual review, fix findings, and commit evidence**

Use `superpowers:requesting-code-review` for the complete diff and Product Design audit for screenshots. Resolve correctness/accessibility findings with a fresh RED test; reject unrelated expansion. Then:

```powershell
git diff --check
git add docs/development/2026-08-29-soft-gradient-glass-skins.md docs/qa/2026-08-29-soft-gradient-glass-skins-acceptance.md
git commit -m "docs(qa): record gradient skin acceptance"
```

---

### Task 9: Integrate Other Session Branches, Reverify, Package, and Place the EXE on Desktop

**Files:**
- Modify if integration requires it: only files from reviewed, non-obsolete active branch commits
- Update: `docs/qa/2026-08-29-soft-gradient-glass-skins-acceptance.md`
- Generated: `build/installer/AgPlayer-Setup-*-x64.exe`（`*` 为 `release-version.ps1` 返回的实际版本号）
- Copy: `C:/Users/Administrator/Desktop/AgPlayer-Setup-*-x64.exe`（保留同一实际版本号）

**Interfaces:**
- Consumes: fully accepted theme branch and all current local branch heads.
- Produces: reviewed integration result, post-merge full verification, versioned x64 installer, desktop copy, file size/SHA-256/launch/playback evidence and final commit hash.

- [ ] **Step 1: Inventory all live branch heads and identify only uncovered valid commits**

```powershell
$reviewedCandidates = [System.Collections.Generic.List[string]]::new()
$refs = git for-each-ref --format='%(refname:short)|%(objectname)' refs/heads/main refs/heads/codex/
$refs | Sort-Object
git branch --no-merged HEAD
```

For every unmerged branch, inspect ancestry, patch equivalence and actual diff:

```powershell
$branches = git branch --format='%(refname:short)' |
    Where-Object { $_ -ne 'codex/full-custom-theme-engine' }
foreach ($branch in $branches) {
    git log --cherry-mark --left-right --oneline "HEAD...$branch" -n 30
    git diff --stat "HEAD...$branch"
}
```

Document each branch as already covered, obsolete/superseded, unrelated, or valid/uncovered, and append only the last category to an in-memory `$reviewedCandidates` list. Do not merge old theme routes, dead resources or another worktree's uncommitted files.

- [ ] **Step 2: Merge each reviewed valid branch one at a time**

For each branch classified valid/uncovered, merge the reviewed list:

```powershell
foreach ($branchToMerge in $reviewedCandidates) {
    git show-ref --verify --quiet "refs/heads/$branchToMerge"
    if ($LASTEXITCODE -ne 0) {
        throw "Reviewed branch no longer exists: $branchToMerge"
    }
    git merge --no-ff $branchToMerge
    if ($LASTEXITCODE -ne 0) {
        throw "Merge requires conflict resolution: $branchToMerge"
    }
}
```

If conflicts occur, use `superpowers:resolving-merge-conflicts`, preserve the new Theme Engine contract, and rerun the affected focused tests before proceeding. If no branch qualifies, record that no merge commit was necessary.

- [ ] **Step 3: Repeat clean post-integration verification**

Run both configure/builds, focused theme tests, full Debug/Release CTest, `all_qmllint`, static color check, visual matrix and WAV smoke exactly as Task 8. Update the QA record with post-integration—not pre-integration—results and the final HEAD.

- [ ] **Step 4: Build the versioned installer with the actual Release directory**

```powershell
.\scripts\package-windows.ps1 -BuildDirectory build/release -Configuration Release
```

Verify the script's Qt/VC deployment and PE x64 checks, then launch from a separate install/stage directory with the real WAV fixture. Do not rely only on the build-tree executable.

- [ ] **Step 5: Copy the newest versioned installer to Desktop and calculate delivery metadata**

```powershell
$installer = Get-ChildItem -LiteralPath build\installer -Filter 'AgPlayer-Setup-*-x64.exe' |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
$desktopTarget = Join-Path ([Environment]::GetFolderPath('Desktop')) $installer.Name
Copy-Item -LiteralPath $installer.FullName -Destination $desktopTarget -Force
Get-Item -LiteralPath $desktopTarget | Select-Object FullName, Length, LastWriteTime
Get-FileHash -LiteralPath $desktopTarget -Algorithm SHA256
```

Expected: the versioned `AgPlayer-Setup-*-x64.exe` selected by the command exists on Desktop and hash output is nonempty.

- [ ] **Step 6: Final diff review, acceptance amendment, and final commit**

Update the QA record with integration decisions, installer path/size/SHA-256, independent-directory WAV result, final pass/failure counts and unexecuted platform checks. Then:

```powershell
git status --short --branch
git diff --check
git diff --stat bb6331e..HEAD
git add docs/qa/2026-08-29-soft-gradient-glass-skins-acceptance.md
git commit -m "docs(release): record gradient skin delivery"
git status --short --branch
git log -5 --oneline
```

Expected: clean worktree, no automatic push, and the final response reports final commit hash, installer metadata, exact validation scope, baseline failures and unavailable macOS/Linux hardware verification.

---

## Plan Self-Review Checklist

- [ ] Every requirement in the approved spec maps to at least one task and one explicit acceptance action.
- [ ] `ThemeManager` is the only ordinary UI color calculator; Picker-local HSV interaction is the only declared QML exception.
- [ ] Default, semantic colors and all media colors have exact non-regression tests.
- [ ] New preset IDs/Stop/order exist once in C++; QML only translates names and renders supplied Stop.
- [ ] Custom Solid and Gradient always apply three fields atomically; Cancel restores complete persisted/runtime state.
- [ ] Native `QPalette` never receives alpha; detached menus/popups/tooltips remain readable.
- [ ] Picker has no system `ColorDialog`, hidden candidate confirmation or out-of-overlay path.
- [ ] No unfinished implementation marker, speculative dependency, new background worker or platform-only theme layer was introduced.
- [ ] Final claims distinguish automated tests, screenshots, Windows real runtime, real WAV path and unexecuted platform/hardware checks.
