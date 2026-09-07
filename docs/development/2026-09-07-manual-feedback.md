# 2026-09-07 候选包人工反馈修复

基线：`dcb8aff`。本记录不代表已生成或发布新安装包。

## 本轮范围

- 用户可见“滚动播放”模式名称统一为“专业模式”，中英文菜单、设置和快捷键标题同步；内部 `rolling-player` 标识和历史配置不变。
- 保持首次配置主波形为纯色、列表缩略图为频彩；删除 QA 启动路径强制切换频彩的覆盖，不覆盖用户已有选择。
- 标签边框 2.4 → 1.4 px，字体 13 → 12 px。
- 专业模式三行信息等高、均匀间距；收藏/评分略增大；波形上下内边距 6 → 4 px。
- 主题菜单使用公共主题菜单项，12 px 字体、28 px 行高，深浅主题选中前景/背景配对。
- 网格校准窗口标题可拖动、限制窗口边界，每次打开定位在校准按钮上方；BPM 框 24 px 高。
- 主播放、音频编辑、单轨及多轨预览在播放前同步交接，暂停其他播放源并保留位置；异步 DSP 完成不能恢复已被暂停的试听。
- 模型/运行时校验结果按会话内文件身份缓存，变化后失效；保留目录安全检查。一键配置在校验中可接管，取消不产生假拒绝缓存。外置 VR 环境仍支持即时配置/暂停。
- 三种主播放器共享的音量百分比改为左对齐，靠近滑条；图标鼠标焦点不显示紫边，键盘焦点提示保留；迷你窗口外圈描边取消。

## 验证证据

- Release 构建：`build/qa/manual-feedback-final-build.txt`。
- 专业模式 QML：36/36，`build/qa/rolling-professional-final.txt`。
- 校准窗口定位/拖动截图：`build/qa/rolling-calibration-above.png`、`rolling-calibration-dragged.png`。
- 首次配置真实应用截图：`build/qa/feedback-default-main.png`，启动退出码 0。
- 主题菜单 Windows 截图：`build/qa/feedback-menu-0.png`、`feedback-menu-1.png`。
- 播放互斥集成测试：`build/qa/playback-exclusion-green.txt`，使用 NULL 音频后端，不等于人工听音验收。
- 分离控制器最终 direct：75 通过、0 失败、2 跳过，`build/qa/model-verification-final-controller-full-green.txt`。跳过项为显式 opt-in 的真实 Python/CUDA 安装。
- 其余 Release CTest 首轮 170/171；唯一原生鼠标左裁剪测试首轮失败，未改裁剪逻辑，独立复测通过（与分离 QML 合计 2/2）。日志：`build/qa/manual-feedback-final-ctest.txt`、`manual-feedback-final-recheck.txt`。保留首次失败记录，不将其描述为首次全量通过。
- 补充样式改动后主窗口、集成主题、迷你测试通过；设计系统焦点测试加入异步等待后通过，确认鼠标无边框、键盘保留焦点框。日志：`build/qa/manual-feedback-extra-tests.txt`、`design-feedback-recheck.txt`。
- 最新菜单与无边迷你窗口实际截图已检查：`build/qa/feedback-menu-1.png`、`feedback-borderless-mini.png`；QA 应用退出码 0。

本轮未重新下载安装大型推理环境，未执行人工听音，未发布远程或替换桌面旧候选包。
