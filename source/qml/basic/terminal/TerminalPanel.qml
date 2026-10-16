// 右栏面板坞里的「终端」块：一个 Qt WebEngine 视图，页面上跑 xterm.js。
//
// 为什么是 WebEngine + xterm.js 而不是自绘：引擎只给"原始 PTY 字节 + 一份序列化 VT 重放串"
// （terminal/follow 的 snapshot.screen），没有网格接口 —— 自绘等于重写一个 xterm（详见
// build/temp/terminal-probe/TERMINAL-REPORT.md §2/§4/§5）。页面里的 xterm.js 是从引擎自带包
// 抽出来的 6.0.0（misc/tools/extract-xterm.README.txt），与官方客户端同版本。
//
// 传输**不在页面里**：引擎要求 Origin == Host，qrc: 页面发请求一律 403 ⇒ 页面一个网络请求都不发，
// 帧与输入都走 QWebChannel。注册到信道上的后端默认是 C++ 的 TerminalBridge 单例；若扩展经
// VirtualPanelHost 顶替了 "terminal" 的后端（HostUi.panelBackend），就改注册它的对象 ——
// **界面与配色都是宿主的**，扩展只出后端、不出 QML。
//
// 本面板只做三件事：加载页面、把"我属于哪个块 / 哪个会话"推给页面、块关掉时通知后端收尾。
//
// ⚠️ 页面必须**在 WebChannel 之后**才加载：WebEngineView 是把 webChannel 的 transport 注入到
//    新文档里的，先加载再设信道会拿不到 qt.webChannelTransport（页面里会报"QWebChannel 没接上"）。

import QtQuick
import QtWebEngine
import QtWebChannel
import ULinHub 1.0

Item {
    id: root

    // 块的稳定 id（模型给的，见 Dock.activeBlockId / delegate 的 blockId 角色）
    property string blockId: ""
    // 终端是**会话作用域**的：跟着当前会话走（换会话就改挂到那个会话的终端上）
    property string sessionId: Hub.currentSessionId

    // 被扩展顶替时的后端（空 = 用内置桥）。只读一次：登记在启动期（attachHost）完成，
    // 而本面板是用户点开块才建的，顺序上必然早于这里。
    readonly property var backend: HostUi.panelBackend("terminal")

    // 页面底色跟主题：暗色纯黑；亮色用 3% 灰而不是纯白 —— 面板本身在亮色下就是 #FFFFFF，
    // 纯白终端底会和面板糊成一片、看不出块的边界。
    readonly property color terminalBg: Theme.isDark ? "#000000" : "#F7F7F7"

    // 主题只在建页时定一次：改 pageUrl 会整页重载、终端内容全丢。运行期切换走 pushTheme()。
    readonly property url pageUrl: "qrc:/ULinHub/Terminal/terminal.html?theme="
                                   + (Theme.isDark ? "dark" : "light")

    // 把绑定推给页面里的 JS（幂等：同一对 (blockId, sessionId) 页面自己会跳过）
    function pushBinding() {
        if (root.blockId.length === 0)
            return
        view.runJavaScript("window.__ulintermBind && window.__ulintermBind("
            + JSON.stringify(root.blockId) + ", " + JSON.stringify(root.sessionId) + ")")
    }

    // 主题切换：页面自己重画底色与前景（xterm 的 theme 是运行期可改的选项）
    function pushTheme() {
        view.runJavaScript("window.__ulintermTheme && window.__ulintermTheme("
            + (Theme.isDark ? "true" : "false") + ")")
    }

    onBlockIdChanged: pushBinding()
    onSessionIdChanged: pushBinding()

    // 页面刚接上信道会回喊一次（bridge.pageHello → 后端发 pageHelloReceived）：这时补推一次绑定。
    // 后端被扩展顶替时这条不会响 —— 页面在信道接好的回调里自己会 apply()，绑定不会丢。
    Connections {
        target: Terminal
        function onPageHelloReceived() { root.pushBinding() }
    }

    Connections {
        target: Theme
        function onChanged() { root.pushTheme() }
    }

    WebEngineView {
        id: view
        anchors.fill: parent
        backgroundColor: root.terminalBg   // 与页面底色一致，避免加载瞬间闪一帧另一种底色
        settings.javascriptEnabled: true
        // webChannel 与 url 都**不在这里绑**：加载顺序有硬要求（先信道后页面），
        // 见下面 Component.onCompleted；声明式绑定的求值顺序不保证这一点。

        onLoadingChanged: (request) => {
            if (request.status === WebEngineView.LoadSucceededStatus) {
                root.pushBinding()
                root.pushTheme()
            }
        }
        // 渲染进程挂了：让后端解绑（终端进程照旧活着，页面回来时取新快照）
        onRenderProcessTerminated: (status, code) => {
            console.warn("[TerminalPanel] render process terminated", status, code)
            Terminal.pageGone(root.blockId)
        }
    }

    // 注册对象是后端（内置 = C++ 的 TerminalBridge 单例，objectName = "ulinterm"，在 init 里设的）。
    // 注意：**不能**写 `registeredObjects: [Terminal]` —— 见 TerminalBridge::registerOn 的注释
    // （JS 数组里的单例是类型包装，转 QObject* 得到 null）。
    WebChannel {
        id: channel
    }

    Component.onCompleted: {
        // ① 先注册对象 + 挂信道：WebEngineView 是把 webChannel 的 transport 注入到**新文档**里的；
        // ② 再加载页面。顺序反了页面里就没有 qt.webChannelTransport（会显示"QWebChannel 没接上"）。
        Terminal.registerOn(channel, root.backend)
        view.webChannel = channel
        view.url = root.pageUrl
        pushBinding()   // 页面脚本可能还没跑（runJavaScript 会静默丢弃），LoadSucceeded 时会补推
    }

    Component.onDestruction: {
        // 切块/收右栏都会走到这里：只解绑，**不关终端**（关块由 PanelDock 显式调 Terminal.closeBlock；
        // 后端被顶替时桥会把这两个调用都转给扩展的后端）
        if (root.blockId.length > 0)
            Terminal.pageGone(root.blockId)
    }
}
