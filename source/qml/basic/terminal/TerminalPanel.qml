// 右栏面板坞里的「终端」块：宿主**自绘**的终端视图（不再有 WebEngine / xterm.js）。
//
// 为什么不再走页面：终端面板要的东西（VT 解析 + 网格 + 键盘编码）现在都在 C++ 侧
// （TerminalVt ← libvterm；TerminalVtModel 是 QML 可见的桥），页面那条路等于把同一件事
// 在第二个语言里再做一遍。分层与取舍见 build/temp/terminal-wire/PROGRESS.md。
//
// 传输仍在 C++：本文件不发任何网络请求，帧由 TerminalBridge → TerminalSession 喂进 VT；
// 本面板只做三件事：① 建/绑视图（Terminal.bindView）；② 把"我属于哪个块 / 哪个会话"告诉桥；
// ③ 块关掉（切换/收栏）时通知桥解绑。
//
// ⚠️ 对外契约不变：`blockId`（模型给的稳定 id）、`sessionId`（默认跟当前会话）、以及**扩展后端那条缝**
//    （HostUi::SetPanelBackend(owner, "terminal", backend)）。后端被扩展顶替时本面板**不装自绘视图** ——
//    扩展后端认的是原来的 QWebChannel/xterm 协议，换了视图它接不上，所以如实给一条状态说明，
//    而不是静默黑屏（详见报告「没做成/不确定」）。

import QtQuick
import ULinHub 1.0

// 与 TerminalView.qml 同一条理由：视图里引用外层 id（root / Theme）走编译期绑定
pragma ComponentBehavior: Bound

Item {
    id: root

    // 块的稳定 id（模型给的，见 Dock.activeBlockId / delegate 的 blockId 角色）
    property string blockId: ""
    // 终端是**会话作用域**的：跟着当前会话走（换会话就改挂到那个会话的终端上）
    property string sessionId: Hub.currentSessionId

    // 被扩展顶替时的后端（空 = 用内置桥）。契约上这是"面板创建时读一次"：登记发生在启动期
    // （扩展的 attachHost），而本面板是用户点开块才建的，顺序上必然早于这里。
    readonly property var backend: HostUi.panelBackend("terminal")

    // 面板底色跟主题：暗色纯黑；亮色用 3% 灰而不是纯白 —— 面板本身在亮色下就是 #FFFFFF，
    // 纯白终端底会和面板糊成一片、看不出块的边界。
    readonly property color terminalBg: Theme.isDark ? "#000000" : "#F7F7F7"

    // ANSI 16 色（索引语义固定）：与 TerminalView 内置的默认表同值（两份是有意的 —— 视图那份是
    // "没有宿主时的默认"，这份是"宿主主题的现行值"）。
    // ⚠️ 单行数组：QML 的 JS 对 `[` 换行按语句结束处理（ASI），多行字面量是语法错。
    readonly property var ansiDark: ["#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5", "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF"]
    readonly property var ansiLight: ["#000000", "#CD3131", "#00BC00", "#949800", "#0451A5", "#BC05BC", "#0598BC", "#555555", "#666666", "#CD3131", "#14CE12", "#B5BA00", "#0451A5", "#BC05BC", "#0598BC", "#A5A5A5"]

    // 绑定：给视图要模型、把"这个块属于哪个会话"推给桥。两个 onChanged 都会走到这里（幂等）。
    function bind() {
        if (root.blockId.length === 0 || root.backend !== null) {
            view.model = null
            return
        }
        view.model = Terminal.bindView(root.blockId)
        Terminal.start(root.blockId, root.sessionId)
    }

    // 主题切换：**不需要重画** —— 视图里每一处颜色都是 `darkTheme` / `defaultFg` / `defaultBg` 的绑定，
    // 而 `darkTheme` 绑在 Theme.isDark 上，主题一变各行的 canvas 自己就重绘（onCursorHereChanged 那条）。
    // 这里只需把 ANSI 调色板重设一次：libvterm 是把"索引色"落成 RGB 存进格子的，换了表要重放。
    // （原来那条 runJavaScript 推 xterm theme 的路已经没有了。）
    function pushTheme() {
        if (view.model !== null)
            view.model.setAnsiPalette(Theme.isDark ? ansiDark : ansiLight)
    }

    onBlockIdChanged: bind()
    onSessionIdChanged: bind()

    Connections {
        target: Theme
        function onChanged() { root.pushTheme() }
    }

    // 视图：直接写组件名（与 qml.qrc / 本目录同源）。早先这里考虑过 Style.url() 装载
    // （<exe>/ui/ 遮蔽内置那个范式），但 Loader 会让每次切块都重建整个网格视图 —— 终端的
    // 回滚历史/光标闪烁都挂在这个 item 上，重建等于闪一下白屏。所以按"组件"直接装。
    TerminalView {
        id: view
        anchors.fill: parent
        blockId: root.blockId
        darkTheme: Theme.isDark

        Component.onCompleted: {
            root.bind()
            root.pushTheme()
        }

        Component.onDestruction: {
            // 切块/收右栏都会走到这里：只解绑，**不关终端**（关块由 PanelDock 显式调 Terminal.closeBlock；
            // 后端被顶替时桥会把这两个调用都转给扩展的后端）
            if (root.blockId.length > 0)
                Terminal.viewGone(root.blockId)
        }
    }

    // 后端被扩展顶替：自绘视图接不上它的 QWebChannel/xterm 契约，如实说明（不静默黑屏）
    Rectangle {
        anchors.fill: parent
        visible: root.backend !== null
        color: root.terminalBg

        Text {
            anchors.centerIn: parent
            width: Math.min(parent.width - 32, 420)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            text: "终端后端已被扩展接管，而宿主终端面板已经换成自绘视图 —— "
                  + "扩展后端认的是原页面（xterm.js / QWebChannel）那套协议，两边接不上。\n\n"
                  + "要恢复：卸载该扩展的终端后端登记（HostUi.ClearPanelBackend(owner, \"terminal\")），"
                  + "或等扩展提供自绘视图版本。"
        }
    }
}
