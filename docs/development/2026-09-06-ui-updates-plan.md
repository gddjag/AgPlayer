# UI 与官网更新提醒实施记录

基线 main@0b6fb1f。用户已确认圆形地形、原生轻量 UI 修复及官网 https://www.agplayer.com；版本接口由本项目提供，部署地址稍后提供。不部署、不发布网站、不自动下载安装。

## 执行范围

- [x] 共享列表：TrackList.qml 表头/数字同列同对齐；仅经典收藏图标缩小。运行布局回归。
- [x] RollingPlayerShell.qml：波形增加有效高度且保留上下空隙；右侧设置区域整体垂直居中，BPM 输入缩短。保持网格关闭与 32 拍等现有语义。
- [x] terrain_reactor_state/item 与 shaders：圆形实例边界及径向淡出，提升克制材质层次。先圆形失败测试，再状态/GPU 验证。
- [x] RGB：复核公共混色和用户调色板，不改变真实几何、频段关系或滚动稳定性；使用色彩及几何回归和截图验证。
- [ ] AudioToolsWindow.qml：无损鉴别标题栏统一公共高度/颜色/排版。测试默认/窄窗口、深浅主题。
- [x] 新增 UpdateChecker，SettingsController 暴露给关于页；静态 JSON（schemaVersion: 1、version）为只读版本接口，官网入口固定。空配置不联网；HTTPS、超时、响应大小限制、严格版本字段、固定官网导航、失败/较新/最新状态均需测试。关于页打开检查，不常驻轮询。
- [ ] 网站模板在 deployment/updates/latest.json，文档描述发布步骤及 CMake 接口地址配置；等待真实地址后做线上端到端验证。
- [ ] 核实首次配置纯色/关闭网格，不覆盖升级用户；翻译新增文案及具体漏译/主题问题。
- [ ] 集成构建、相关 CTest、设计规范检查、真实截图与最终 diff review；仅报告实际验收范围。本轮用户未要求新打包。

接口地址默认空，未部署不显示“已是最新”。不读取音频、不发送设备或曲库信息，只 GET 版本文件。网站模板不是已经上线的服务。

## 本轮验证记录

- 共享列表/滚动布局：`qml_rolling_theme_test` 34/34；波形有效区域上下各 6 DIP，BPM 26 DIP。右侧 Flow 使用内容高度并垂直居中，保留换行。
- 圆形地形：状态测试 49/49，真实 Direct3D11 GPU 测试 25/25，无跳过；仅裁掉圆外实例与径向淡出，材质高光调整未新增渲染 pass。不是 C4D/UE5 画质等价承诺。
- RGB：公共相对频段对比指数 1.10→1.15；中等偏频样本饱和度 0.496544→0.512474。完整波形测试 56/56，保持几何与弱高频可见度，不使用伪造随机颜色。
- 更新：设置与更新服务 CTest 2/2，含真实 10 秒超时、重试、响应限制、错误 JSON、严格版本与语言刷新。独立审查发现显示版本带 v 前缀的问题已修复，配置端点构建的测试不访问线上服务。
- 集成 UI/设计规范 CTest 7/7：无损鉴别、关于页、滚动主题、播放器布局、共享列表与工具布局规范。
- 真实 Release 截图已查看：`build/qa/round-ui-rolling.png`、`round-ui-immersive.png`、`round-ui-about-light.png`、`round-ui-about-dark.png`、`round-ui-lossless-light.png`。覆盖本次默认尺寸界面，不代表所有 DPI/弹窗均已验收。
- 新配置纯色与关闭网格已有实现和设置回归覆盖，本轮没有覆盖升级用户的设置。
- 尚未进行网站线上验证、全量 Release/Debug 回归、全软件所有弹窗人工验收；本轮不提交、不打包。
- 翻译审查：当前触达的 SettingsPage、UpdateChecker、RollingPlayerShell、ImmersiveControlPanel 已补齐。完整源提取仍发现 584 条英文待译（含分离 267、元数据 68、编辑 24、歌词 24）；不把缺项从提取目录移除解释为已经完成。发布目录必须保留原有兼容上下文，并仅合入已审校译文。全软件英文完整性仍未通过验收。
- 最终翻译回归已闭环：恢复旧兼容消息后，`translation_manager_test`、`translation_catalog_test`、`phase6_translation_coverage_test` 3/3 通过；没有修改测试断言。发布 catalog 英文 1277 / 中文 1861 条，均无 unfinished，原 HEAD 消息缺失数为 0。全量源缺项见 `2026-09-06-localization-backlog.md`，不等同于发布目录契约测试。
- 最终 AgPlayer Release 构建与 lrelease 成功；更新/设置 2 项、布局/设计 7 项、翻译 3 项针对性 CTest 均通过。独立审查已关闭版本前缀与配置构建离线测试两项问题。保留原安装包和无关工作树修改，未提交或打包。
