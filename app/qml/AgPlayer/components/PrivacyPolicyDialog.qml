import QtQuick
import QtQuick.Controls
import AgPlayer

ThemedDialog {
    id: dialog
    objectName: "privacyPolicyDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    title: qsTr("AgPlayer 隐私政策")
    implicitWidth: 640
    height: Math.min(600, Math.max(0, (parent ? parent.height : 648) - 48))
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    function openLink(link) {
        if (/^https?:\/\//i.test(link) || /^mailto:/i.test(link))
            return Qt.openUrlExternally(link)
        return false
    }

    onOpened: policyScroll.contentItem.contentY = 0
    contentItem: ScrollView {
        id: policyScroll
        objectName: "privacyPolicyScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        TextArea {
            objectName: "privacyPolicyText"
            width: policyScroll.availableWidth
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.RichText
            text: dialog.policyHtml
            color: Theme.primaryText
            palette.link: Theme.accent
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            background: null
            onLinkActivated: function(link) { dialog.openLink(link) }
        }
    }

    // Supplied privacy policy, bundled for offline reading.
    readonly property string policyHtml: `<p style="margin-top:0px; margin-bottom:12px">AgPlayer 尊重并保护您的隐私。<b>本软件无需注册或登录，音乐播放与音频处理在设备本地完成，不上传您的音乐、录音、工程或处理结果。歌词搜索、模型下载和在线更新需要联网。</b></p>
<p style="margin-top:20px; margin-bottom:10px"><b>一、本地数据与权限</b></p>
<p style="margin-top:0px; margin-bottom:12px">本软件根据您的操作读取相关文件及歌曲信息，用于播放、音乐管理、波形显示和音频处理。歌单、收藏、标签、评分、播放记录、设置及缓存保存在本地，不用于广告跟踪或模型训练，不自动上报行为统计或诊断日志。</p>
<p style="margin-top:0px; margin-bottom:12px">使用录音功能时，仅在您主动开始录音且系统允许访问后读取音频输入，录音保存在本地。选择封面时，仅处理相关图片。文件和麦克风访问受系统设置及本软件功能用途限制；拒绝麦克风访问不影响普通播放。</p>
<p style="margin-top:20px; margin-bottom:10px"><b>二、联网功能</b></p>
<p style="margin-top:16px; margin-bottom:10px"><b>1. 歌词搜索与匹配</b></p>
<p style="margin-top:0px; margin-bottom:12px"><b>歌词来源：</b>Windows 与 macOS 共用 <b>LrcAPI</b>，通过 <code>GET <a href="https://api.lrc.cx/jsonapi">https://api.lrc.cx/jsonapi</a></code> 搜索、匹配和获取歌词，源码项目归属标注为 <code>HisAtri/LrcApi</code>。当前客户端未接入其他歌词备用源或歌词中转代理；精确匹配与搜索使用同一接口。</p>
<p style="margin-top:0px; margin-bottom:12px"><b>发送的信息：</b>歌曲标题（<code>title</code>）、歌手（<code>artist</code>），以及非空时的专辑（<code>album</code>），均去除首尾空白；标题为空时不请求。请求头包含 <code>User-Agent: AgPlayer/&lt;版本&gt;</code> 和 <code>Accept: application/json</code>。手动刷新时还会发送随机 UUID（<code>_refresh</code>）及禁止缓存请求头，用于绕过响应缓存。此歌词请求不包含音频内容、完整文件路径、歌曲时长或歌曲 ID。请避免在歌曲信息中包含无关的私人信息。</p>
<p style="margin-top:0px; margin-bottom:12px"><b>何时联网：</b>开启全局歌词显示，或在整合／滚动模式展开侧栏并选中歌词页时，歌词服务启用。打开歌词或切换歌曲时，优先读取内嵌歌词、本地 LRC 和缓存；没有可用内容才在线请求。服务启用时，<b>下一首歌曲没有可用本地歌词或缓存，也可能提前联网获取</b>。点击“刷新歌词”会主动重新在线搜索。首次启动默认不显示歌词，但后续启动可能恢复此前的歌词和侧栏状态并自动匹配。</p>
<p style="margin-top:0px; margin-bottom:12px"><b>如何停止：</b>须同时满足两个条件：<b>全局歌词显示已关闭；当前整合／滚动模式的侧栏已收起或已切换到非歌词页。</b>仅隐藏一个入口不一定停止请求。<b>当前没有“只禁止在线匹配、继续显示本地歌词”的独立设置。</b>歌词服务禁用后会取消待处理请求，但无法撤回已发送的请求。歌词可缓存在本地，关闭歌词服务不影响普通音乐播放。</p>
<p style="margin-top:16px; margin-bottom:10px"><b>2. 模型下载与在线更新</b></p>
<p style="margin-top:0px; margin-bottom:12px"><b>模型下载：</b>按需从模型发布者或托管平台下载文件，请求包含模型或组件标识、下载路径等必要信息。已取得且可用的模型在本地运行，音频不发送至云端分离。</p>
<p style="margin-top:0px; margin-bottom:12px">模型项目来源参考：<a href="https://github.com/Anjok07/ultimatevocalremovergui">UVR</a>、<a href="https://github.com/TRvlvr/model_repo">UVR 模型仓库</a>、<a href="https://github.com/facebookresearch/demucs">Demucs</a>。项目仓库不等于本版本实际下载地址。</p>
<p style="margin-top:0px; margin-bottom:12px"><b>在线更新：</b>检测更新、下载安装包时访问版本或下载服务，涉及版本清单或安装包路径等信息，不读取您的音乐内容。公开下载渠道包括 <a href="https://download.agplayer.com">download.agplayer.com</a>（Cloudflare 托管分发）及 <a href="https://github.com/gddjag/AgPlayer/releases">GitHub Releases</a>。</p>
<p style="margin-top:0px; margin-bottom:12px">以上服务会接收 IP 地址和必要协议字段，可能产生请求时间、状态等访问日志。实际触发方式、接收方和保存安排见下方清单；不使用相关联网功能，不影响不依赖它们的本地功能。</p>
<p style="margin-top:20px; margin-bottom:10px"><b>三、第三方服务与保存安排</b></p>
<p style="margin-top:0px; margin-bottom:12px">本地文件及应用数据在功能需要期间保存在设备或您指定的位置，您可自行删除。缓存、记录、模型和用户文件分别管理；普通缓存清理不删除原始音乐、已保存工程、录音、导出结果或模型。卸载软件不一定删除全部数据，自定义目录中的文件可能保留。开发者不提供音频云端备份。</p>
<p style="margin-top:0px; margin-bottom:12px">网络日志与本地文件的保存安排不同。GitHub、Cloudflare 等服务可能在境外处理必要的请求信息。相关规则参见 <a href="https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement">GitHub 隐私声明</a>及 <a href="https://www.cloudflare.com/privacypolicy/">Cloudflare 隐私政策</a>。我们不出售个人信息，依法履行第三方处理、跨境告知及适用的同意义务，不以第三方托管免除自身责任。</p>
<p style="margin-top:0px; margin-bottom:12px">主动打开官网或外部链接会产生网页请求，可能涉及网页统计或 Cookie；自营网页另行告知，独立第三方网站同时适用其隐私规则。</p>
<p style="margin-top:20px; margin-bottom:10px"><b>四、您的权利与联系</b></p>
<p style="margin-top:0px; margin-bottom:12px">您可以删除本地数据、管理系统提供的授权、停止相关联网功能，或通过 <a href="mailto:agplayer@foxmail.com">agplayer@foxmail.com</a> 请求查阅、复制、更正、删除个人信息、撤回同意或提出投诉。我们核验必要信息后依法及时答复；无法处理时说明原因。没有用户账户，无需注销。开发者无法远程删除未上传的本地文件。</p>
<p style="margin-top:0px; margin-bottom:12px">您主动发送反馈邮件时，我们仅为回复咨询、排查问题或处理请求使用您的邮箱和所提供内容，在目的实现且无继续保留必要时删除，法律另有要求的除外。邮件传输和存储还涉及邮件服务商；请勿发送无关的私人信息。</p>
<p style="margin-top:20px; margin-bottom:10px"><b>五、安全、未成年人及政策更新</b></p>
<p style="margin-top:0px; margin-bottom:12px">我们采取与处理风险相适应的必要保护措施，发生信息安全事件时依法采取补救及告知措施。涉及不满十四周岁未成年人个人信息时，依法取得监护人同意并实施专门保护。</p>
<p style="margin-top:0px; margin-bottom:12px">处理方式发生重要变化时，我们会更新本政策并提示您；依法需要重新取得同意的，在处理前履行相应程序，不以继续使用视为同意新增处理。您可在“关于 → 隐私政策”查看本政策。</p>`
}
