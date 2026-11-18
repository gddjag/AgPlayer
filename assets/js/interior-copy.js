(() => {
  'use strict';
  const copy = {
    zh: {
      downloadTitle: '下载 AgPlayer', androidPrivacy: 'Android 隐私政策', downloadIntro: '为你的电脑或手机选择安装包。',
      faqTitle: "常见问题与简易教程",
      faqUsing: "使用教程",
      faqSetup: "安装与注意事项",
      faqLibraryQ: "怎么把音乐加入曲库？",
      faqLibraryA: "在桌面版曲库的侧栏点「添加资源文件夹」，选中存放音乐的文件夹。歌曲出现后，可以打星评分、加入收藏，再按评分或标签筛选。播放与整理都在本机完成，无需注册账号。",
      faqWaveQ: "网页试听怎样切换波形和跳播？",
      faqWaveA: "在「波形试听室」点「添加本地音乐」，选一首歌后按播放。点四种波形名称可切换显示，点波形中的位置可跳到相应时间。若浏览器无法读取文件，可在桌面版中打开。",
      faqClipQ: "怎样拖出歌曲中的一小段？",
      faqClipA: "在桌面版单窗口模式中打开歌曲，在底部的整曲波形上框选需要的范围。把选区拖到桌面，即可得到该片段，也可直接拖到支持接收文件的剪辑软件时间线。",
      faqEditQ: "怎样剪掉片段并制作淡出？",
      faqEditA: "在桌面版「音频工具」打开「音频编辑」，点击「导入音频」。将播放头移到剪切点，按 S 分割，再拖动片段边缘裁剪。在音量线上双击添加两个控制点，把后一个向下拖，试听后点「导出音频」。",
      faqStemsQ: "怎样单独导出人声或伴奏？",
      faqStemsA: "在「音频工具」打开分离工具，点「选择文件」，选好模型后点「开始分离」。处理完成后先试听，再点「导出人声」或「导出伴奏」并选择目录。鼓、贝斯等其他音轨取决于所选模型。",
      faqEditionQ: "Windows 和 Mac 应该选哪个安装包？",
      faqEditionA: "Windows 可在「设置 → 系统 → 关于」查看系统类型。Intel 或 AMD 的 64 位电脑选 x64，基于 ARM 的 Windows 电脑选 ARM64。Mac 在苹果菜单的「关于本机」查看芯片，M 系列选 Apple，Intel 处理器选 Intel。",
      faqModelsQ: "需要离线分离，应该选哪个版本？",
      faqModelsA: "官网 Windows 和 macOS 可以选「内置伴奏分离模型版本」，安装后即可离线分离。使用常规纯净版时，先在「音频工具」里联网下载所需模型。Android 和原生鸿蒙版已内置模型。",
      faqEdgeQ: "Edge 提示「通常不会下载」，怎么办？",
      faqEdgeA: "官网 Windows 包暂未使用受信任的代码签名证书。如果只是下载信誉提示，请先确认文件来自本页官网链接或官方 GitHub 镜像，再从下载项菜单选择「保留」。打开前核对该安装包的 SHA-256。如明确报出恶意软件，请停止运行。",
      faqMacQ: "macOS 首次打开时出现安全提示，怎么办？",
      faqMacA: "此版本尚未经过 Apple 公证。先核对下载来源和所选安装包的 SHA-256，再查看下载卡片中的 Apple 官方教程，按提示处理。校验值不同请重新下载。",
      faqMobileQ: "安卓和鸿蒙在哪里安装？",
      faqMobileA: "64 位 Android 8.0 及以上设备可从本页下载 Android APK。要安装原生鸿蒙 PC 或手机版，请到鸿蒙应用市场搜索 AgPlayer。使用华为卓易通时，也可以选择安卓版本。",
      aboutTitle: '关于 AgPlayer', aboutIntro: 'AgPlayer 是一款独立开发的本地音乐播放器。打开自己的音乐文件，看波形、整理曲库，也能直接剪辑声音或分离人声与伴奏。',
      localTitle: '本地处理', accountBody: '打开文件即可播放，无需注册或登录。', contact: '意见与反馈',
      missing: '页面未找到', missingDetail: '这个地址可能已变更，或输入有误。', backHome: '返回首页',
    },
    en: {
      downloadTitle: 'Download AgPlayer', androidPrivacy: 'Android privacy policy (Chinese)', downloadIntro: 'Choose an installer for your computer or phone.',
      faqTitle: "Help and quick guides",
      faqUsing: "Using AgPlayer",
      faqSetup: "Installation and precautions",
      faqLibraryQ: "How do I add my music to the library?",
      faqLibraryA: "In the desktop library sidebar, choose Add resource folder and select the folder with your music. When the tracks appear, rate them with stars or mark favorites. Use ratings or tags to filter the list. Your files stay on your device, and no account is required.",
      faqWaveQ: "How do I switch waveforms and seek in the browser?",
      faqWaveA: "In the waveform studio, add a local audio file and press Play. Select one of the four waveform views, or click a position in the waveform to seek. If your browser cannot read the file, try opening it in the desktop app.",
      faqClipQ: "How do I drag out part of a track?",
      faqClipA: "Open the song in the desktop app’s single-window layout. Select the range you want on the full-track waveform at the bottom. Drag the selection onto your desktop, or drop it onto an editor timeline that accepts audio files.",
      faqEditQ: "How do I trim a clip and add a fade-out?",
      faqEditA: "Open Audio editing in the desktop Audio tools and import your audio. Move the playhead to the cut and press S to split. Drag a clip edge to trim it. Double-click the volume line twice to add two points, then lower the second point. Preview the fade and export the audio.",
      faqStemsQ: "How do I export the vocals or the backing track?",
      faqStemsA: "Open the separation tool under Audio tools, choose a file and a model, then start separation. When it finishes, preview the result and choose Export vocals or Export accompaniment. Select a folder for the file. Additional stems such as drums and bass depend on the model.",
      faqEditionQ: "Which installer fits my Windows PC or Mac?",
      faqEditionA: "On Windows, open Settings → System → About and check System type. Choose x64 for a 64-bit Intel or AMD PC, or ARM64 for a Windows on Arm PC. On a Mac, open About This Mac from the Apple menu. Choose Apple for an M-series chip or Intel for an Intel processor.",
      faqModelsQ: "Which edition works for offline stem separation?",
      faqModelsA: "For Windows or macOS, choose the installer with separation models included to work offline after installation. With the standard desktop edition, download the model you need in Audio tools while online. Android and native HarmonyOS editions include the models.",
      faqEdgeQ: "What if Edge says the installer is uncommon?",
      faqEdgeA: "The Windows installer currently lacks a trusted code-signing certificate. For an uncommon-download warning, first confirm that you used this site or its official GitHub mirror. Choose Keep from the download menu and check the installer’s SHA-256 before opening it. If Edge explicitly detects malware, stop.",
      faqMacQ: "What should I do if macOS warns me on first launch?",
      faqMacA: "This build is not Apple-notarized. Verify the download source and compare the file’s SHA-256 with the value for your chosen installer. Follow the Apple Support guide linked on the macOS download card. If the values differ, download a fresh copy.",
      faqMobileQ: "Where can I get the Android and HarmonyOS apps?",
      faqMobileA: "For a 64-bit device with Android 8.0 or later, download the Android APK on this page. Search for AgPlayer in the HarmonyOS app marketplace for the native PC or phone version. Huawei Zhuoyitong users can also download the Android edition.",
      aboutTitle: 'About AgPlayer', aboutIntro: 'AgPlayer is an independent music player for local files. Follow the waveform, organize your library, edit a recording, or separate vocals from accompaniment.',
      localTitle: 'Local processing', accountBody: 'Open a file and play it without signing up or logging in.', contact: 'Feedback',
      missing: 'Page not found', missingDetail: 'This address may have changed. Check the URL or return to the home page.', backHome: 'Back to home',
    },
  };
  const metadata = {
    zh: {
      support: { title: '支持作者 · AgPlayer', description: '自愿支持 AgPlayer 的开发与测试。全部功能永久免费。' },
      privacy: { title: 'AgPlayer Android 隐私政策', description: 'AgPlayer Android 官网与华为应用市场渠道的现行和历史隐私政策。' },
      '404': { title: '页面未找到 · AgPlayer', description: '此地址可能已变更。返回 AgPlayer 官网或下载页。' },
    },
    en: {
      support: { title: 'Support AgPlayer', description: 'Help fund development and testing. AgPlayer is free to use.' },
      privacy: { title: 'Android Privacy Policy · AgPlayer', description: 'The Chinese privacy policies for the official website and Huawei editions of AgPlayer for Android.' },
      '404': { title: 'Page not found · AgPlayer', description: 'Return to the AgPlayer home page or downloads.' },
    },
  };
  for (const lang of ['zh', 'en']) {
    window.AG_COPY[lang].interior = copy[lang];
    Object.assign(window.AG_COPY[lang].meta.pages, metadata[lang]);
  }
})();
