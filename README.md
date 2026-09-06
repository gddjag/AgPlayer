# AgPlayer

**听见音乐，看见声音。**

AgPlayer 是一款以轻量、简洁为设计方向的本地音频播放器，通过波形展示声音细节，将音乐播放、曲库管理和常用音频工具放在一起。

[官网（部署准备中）](https://agplayer.com) · [版本发布](https://github.com/gddjag/AgPlayer/releases)

首个正式版本 **v1.0.0** 正在准备中，Windows 安装包尚未开放下载。后续正式安装包将按版本号发布到 GitHub Releases，并提供 R2 下载地址。

## 主要特点

- **四种波形显示模式**：纯色波形、频彩波形、RGB 渐变与动态频谱，从振幅和频段等不同角度观察音乐。
- **多种界面布局**：经典双窗口、滚动播放、单窗口和迷你播放器，适应曲库整理、细节查看与日常聆听。
- **本地音乐管理**：用歌单、标签、收藏与评分整理曲库，按歌曲、艺人、标签、评分和 BPM 搜索筛选。
- **常用音频工具**：伴奏分离、格式转换、音频编辑、元数据修改、文件名处理和无损鉴别。

## 界面预览

### 经典双窗口

播放器与音乐列表分开呈现，兼顾播放控制和曲库浏览。

![AgPlayer 经典双窗口：上方播放器与波形，下方曲库、评分和收藏](assets/images/player-classic.png)

### 滚动播放

大幅波形与播放速度、BPM、波形缩放控件集中显示，便于查看声音细节。

![AgPlayer 滚动播放：大幅彩色波形、播放控制、音乐列表和标签面板](assets/images/player-scrolling.png)

### 单窗口

音乐库、标签面板、整曲波形和播放控制集中在一个窗口中。

![AgPlayer 单窗口：音乐列表、标签面板与底部整曲波形](assets/images/player-single.png)

### 迷你播放器

紧凑窗口保留封面、歌曲信息、可视化和常用播放控制。

![AgPlayer 迷你播放器：封面、歌曲信息、频谱和播放按钮](assets/images/player-mini.png)

## Cloudflare 自动部署配置

此仓库包含官网静态页面和发布说明。网站使用 HTML、CSS 和原生 JavaScript；构建脚本仅依赖 Node.js 标准库。

源文件为 `index.html`、`download.html`、`about.html` 和 `assets/`。构建只将这些文件复制到 `public/`，Cloudflare 仅发布该目录。

将 Cloudflare Workers 项目 `agplayer` 连接到 GitHub 仓库 `gddjag/AgPlayer`，生产分支设为 `main`，项目根目录使用仓库根目录：

- 构建命令：`node scripts/build-site.mjs`
- 部署命令：`npx wrangler deploy`

连接并完成首次部署后，提交到 GitHub `main` 的网页更新将触发 Cloudflare 自动构建和部署。上述配置文件不代表线上部署已经完成，实际状态以 Cloudflare 构建结果为准。

仓库原有 `CNAME` 保留；Cloudflare 的自定义域名仍需在项目和域名管理中绑定。
