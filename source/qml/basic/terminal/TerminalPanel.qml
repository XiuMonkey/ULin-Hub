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

    // 被扩展顶替时的后端（空 = 内置引擎）。**不再"创建时读一次"**：自绘路径下由 Terminal.bindBackend
    // 把这条缝接上，而扩展的登记可能比本面板晚 ~0.2s（它在 Component.onCompleted 里登记），
    // 所以下面有一个低频轮询兜底（见 syncBackend 的注释）。
    readonly property var backend: HostUi.panelBackend("terminal")

    // ANSI 16 色（索引语义固定）：与 TerminalView 内置的默认表同值（两份是有意的 —— 视图那份是
    // "没有宿主时的默认"，这份是"宿主主题的现行值"）。
    // ⚠️ 单行数组：QML 的 JS 对 `[` 换行按语句结束处理（ASI），多行字面量是语法错。
    readonly property var ansiDark: ["#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5", "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF"]
    readonly property var ansiLight: ["#000000", "#CD3131", "#00BC00", "#949800", "#0451A5", "#BC05BC", "#0598BC", "#555555", "#666666", "#CD3131", "#14CE12", "#B5BA00", "#0451A5", "#BC05BC", "#0598BC", "#A5A5A5"]

    // 后端缝：页面时代是 registerOn(channel, backend)，自绘路径没有页面了 ⇒ 走 Terminal.bindBackend。
    // **幂等**（同一个后端反复接不动作），所以可以在多个时机反复调：
    //   ① 面板就绪 / 换块；② 低频轮询（HostUi 没有"后端变了"的信号，而扩展可能晚于面板登记，
    //      也可能在运行期被卸载撤销 —— 轮询是唯一不引入新信号就能同时覆盖"晚到"和"撤销"的办法，
    //      一次调用只是两次哈希查找，1 秒一次可以忽略）。
    function syncBackend() {
        // 直接调：attachTransport 自己就是幂等的（同一个后端不重复动作、null→null 也不动作），
        // 所以不需要在这里记"上次是谁"。这样"晚到"与"撤销"两个方向都被覆盖。
        Terminal.bindBackend(HostUi.panelBackend("terminal"))
    }

    Timer {
        interval: 1000
        repeat: true
        running: root.blockId.length > 0
        onTriggered: root.syncBackend()
    }

    // 绑定：给视图要模型、把"这个块属于哪个会话"推给桥、把扩展后端接上。两个 onChanged 都会走到这里（幂等）。
    function bind() {
        if (root.blockId.length === 0) {
            view.model = null
            return
        }
        // 先接后端缝：接上之后会话只吃后端的帧、不再去引擎建终端
        root.syncBackend()
        view.model = Terminal.bindView(root.blockId)
        // 有扩展后端时，引擎那条路（terminal/environment → create）不该跑：不调 start。
        // ⚠️ 扩展的登记可能比本面板晚 ~0.2s（它在 Component.onCompleted 里登记），那时这里读到的是 null，
        //    于是 start() 照旧跑一次（内置引擎）。等轮询接上后端后，会话改用后端、引擎那次失败不会影响画面。
        if (HostUi.panelBackend("terminal") === null)
            Terminal.start(root.blockId, root.sessionId)
        // 换块/首次打开都要把**键盘焦点**交给视图：终端一打开用户第一件事就是打字，
        // 而壳里 Composer / 输入框 / Popup 都会抢焦点。callLater = 等这一帧别处的 focus 请求先落定。
        Qt.callLater(view.grabFocus)
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

    // 后端被扩展顶替时**照常渲染**：Terminal.bindBackend 把后端的 frameReady 接进本地 VT，
    // 输入走它的 write —— 页面时代那张"两边接不上"的说明牌已经不需要了。
    // 后端缺 frameReady 一类的方法时，会话的状态行会如实说明（TerminalSession::attachBackend 里设的）。
}
