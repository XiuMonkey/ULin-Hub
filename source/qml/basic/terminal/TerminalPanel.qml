// 右栏面板坞里的「终端」块：一个 Qt WebEngine 视图，页面上跑 xterm.js。
//
// 为什么是 WebEngine + xterm.js 而不是自绘：引擎只给"原始 PTY 字节 + 一份序列化 VT 重放串"
// （terminal/follow 的 snapshot.screen），没有网格接口 —— 自绘等于重写一个 xterm（详见
// build/temp/terminal-probe/TERMINAL-REPORT.md §2/§4/§5）。页面里的 xterm.js 是从引擎自带包
// 抽出来的 6.0.0（misc/tools/extract-xterm.README.txt），与官方客户端同版本。
//
// 传输**不在页面里**：引擎要求 Origin == Host，qrc: 页面发请求一律 403 ⇒ 页面一个网络请求都不发，
// 帧与输入都走 QWebChannel（注册对象是 C++ 的 TerminalBridge 单例 `Terminal`）。
//
// 本面板只做三件事：加载页面、把"我属于哪个块 / 哪个会话"推给页面、块关掉时通知 C++ 收尾。
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

    readonly property url pageUrl: "qrc:/ULinHub/Terminal/terminal.html"

    // 把绑定推给页面里的 JS（幂等：同一对 (blockId, sessionId) 页面自己会跳过）
    function pushBinding() {
        if (root.blockId.length === 0)
            return
        view.runJavaScript("window.__ulintermBind && window.__ulintermBind("
            + JSON.stringify(root.blockId) + ", " + JSON.stringify(root.sessionId) + ")")
    }

    onBlockIdChanged: pushBinding()
    onSessionIdChanged: pushBinding()

    // 页面刚接上信道会回喊一次（bridge.pageHello → Terminal.pageHelloReceived），
    // 这时补推一次：正常路径下 LoadSucceeded 已经推过，重复推是空操作
    Connections {
        target: Terminal
        function onPageHelloReceived() { root.pushBinding() }
    }

    WebEngineView {
        id: view
        anchors.fill: parent
        backgroundColor: "#0b0d10"   // 与页面/终端配色一致，避免加载瞬间闪白
        settings.javascriptEnabled: true
        // webChannel 与 url 都**不在这里绑**：加载顺序有硬要求（先信道后页面），
        // 见下面 Component.onCompleted；声明式绑定的求值顺序不保证这一点。

        onLoadingChanged: (request) => {
            if (request.status === WebEngineView.LoadSucceededStatus)
                root.pushBinding()
        }
        // 渲染进程挂了：让 C++ 解绑（终端进程照旧活着，页面回来时取新快照）
        onRenderProcessTerminated: (status, code) => {
            console.warn("[TerminalPanel] render process terminated", status, code)
            Terminal.pageGone(root.blockId)
        }
    }

    // 注册对象是 C++ 的 TerminalBridge 单例（objectName = "ulinterm"，在 TerminalBridge::init 里设的）。
    // 注意：**不能**写 `registeredObjects: [Terminal]` —— 见 TerminalBridge::registerOn 的注释
    // （JS 数组里的单例是类型包装，转 QObject* 得到 null）。
    WebChannel {
        id: channel
    }

    Component.onCompleted: {
        // ① 先注册对象 + 挂信道：WebEngineView 是把 webChannel 的 transport 注入到**新文档**里的；
        // ② 再加载页面。顺序反了页面里就没有 qt.webChannelTransport（会显示"QWebChannel 没接上"）。
        Terminal.registerOn(channel)
        view.webChannel = channel
        view.url = root.pageUrl
        pushBinding()   // 页面脚本可能还没跑（runJavaScript 会静默丢弃），LoadSucceeded 时会补推
    }

    Component.onDestruction: {
        // 切块/收右栏都会走到这里：只解绑，**不关终端**（关块由 PanelDock 显式调 Terminal.closeBlock）
        if (root.blockId.length > 0)
            Terminal.pageGone(root.blockId)
    }
}
