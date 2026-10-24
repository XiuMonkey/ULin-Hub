// 右栏面板坞里的「终端」块：宿主**自绘**的终端视图（不再有 WebEngine / xterm.js）。
// 为什么不再走页面：VT 解析 + 网格 + 键盘编码都在 C++（TerminalVt ← libvterm；TerminalVtModel 是 QML 桥），
// 页面那条路等于把同一件事在第二个语言里再做一遍。传输也在 C++：本文件不发网络请求，帧由
// TerminalBridge → TerminalSession 喂进 VT；本面板只做三件事：建/绑视图（Terminal.bindView）、
// 把"我属于哪个块 / 哪个会话"告诉桥、块关掉时通知桥解绑。
// ⚠️ 对外契约不变：`blockId`（模型给的稳定 id）、`sessionId`（默认跟当前会话）、扩展后端缝
// （HostUi::SetBlockBackend(owner, "terminal", backend)）。后端被扩展顶替时还是装自绘视图：按
// `apiVersion` 选面 —— >=2 字节面（hostWrite/hostData，裸字节直通），否则页面面（兼容层，deprecated）；
// 两条面都由 TerminalSession 接进同一个 VT。

import QtQuick
import ULinHub 1.0

pragma ComponentBehavior: Bound   // 与 TerminalView.qml 同一条理由：视图里引用外层 id（root / Theme）走编译期绑定

Item {
    id: root

    property string blockId: ""   // 块的稳定 id（模型给的，见 PanelBarDock.activeBlockId / delegate 的 blockId 角色）
    property string sessionId: Hub.currentSessionId   // 终端是**会话作用域**的：跟着当前会话走（换会话就改挂到那个会话的终端上）

    // 被扩展顶替时的后端（空 = 内置引擎）。**不再"创建时读一次"**：自绘路径下由 Terminal.bindBackend
    // 把这条缝接上，而扩展的登记可能比本面板晚 ~0.2s（它在 Component.onCompleted 里登记），
    // 所以下面有一个低频轮询兜底（见 syncBackend 的注释）。
    readonly property var backend: HostUi.blockBackend("terminal")

    // ANSI 16 色（索引语义固定）：与 TerminalView 内置的默认表同值（两份是有意的 —— 视图那份是
    // "没有宿主时的默认"，这份是"宿主主题的现行值"）。
    // ⚠️ 单行数组：QML 的 JS 对 `[` 换行按语句结束处理（ASI），多行字面量是语法错。
    readonly property var ansiDark: ["#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5", "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF"]
    readonly property var ansiLight: ["#000000", "#CD3131", "#00BC00", "#949800", "#0451A5", "#BC05BC", "#0598BC", "#555555", "#666666", "#CD3131", "#14CE12", "#B5BA00", "#0451A5", "#BC05BC", "#0598BC", "#A5A5A5"]

    // 后端缝：页面时代是 registerOn(channel, backend)，自绘路径没有页面了 ⇒ 走 Terminal.bindBackend。
    // **幂等**（同一个后端反复接不动作），所以可以在多个时机反复调：
    // ① 面板就绪 / 换块；② 低频轮询（HostUi 没有"后端变了"的信号，而扩展可能晚于面板登记，
    // 也可能在运行期被卸载撤销 —— 轮询是唯一不引入新信号就能同时覆盖"晚到"和"撤销"的办法，
    // 一次调用只是两次哈希查找，1 秒一次可以忽略）。
    function syncBackend() {
        Terminal.bindBackend(HostUi.blockBackend("terminal"))   // 直接调：attachTransport 自己就是幂等的（同一个后端不重复动作、null→null 也不动作）
    }

    Timer {
        interval: 1000
        repeat: true
        running: root.blockId.length > 0
        onTriggered: root.syncBackend()
    }

    function bind() {   // 绑定：给视图要模型、把"这个块属于哪个会话"推给桥、把扩展后端接上。两个 onChanged 都会走到这里（幂等）
        if (root.blockId.length === 0) {
            view.model = null
            return
        }
        root.syncBackend()   // 先接后端缝：接上之后会话只吃后端的帧、不再去引擎建终端
        view.model = Terminal.bindView(root.blockId)

        // 有扩展后端时，引擎那条路（terminal/environment → create）不该跑：不调 start。
        // ⚠️ 扩展的登记可能比本面板晚 ~0.2s（它在 Component.onCompleted 里登记），那时这里读到的是 null，
        // 于是 start() 照旧跑一次（内置引擎）。等轮询接上后端后，会话改用后端、引擎那次失败不会影响画面。
        // 后端在场时仍要把"块属于哪个会话"告诉桥：字节面的 hostStart(blockId, sessionId) 要这个参数，
        // 而 bindSession 是**只记 id、不碰引擎**的旁路（不会建终端、不会发任何 terminal/*）。
        if (HostUi.blockBackend("terminal") === null)
            Terminal.start(root.blockId, root.sessionId)
        else
            Terminal.bindSession(root.blockId, root.sessionId)

        // 换块/首次打开都要把**键盘焦点**交给视图：终端一打开用户第一件事就是打字，
        // 而壳里 Composer / 输入框 / Popup 都会抢焦点。callLater = 等这一帧别处的 focus 请求先落定。
        Qt.callLater(view.grabFocus)
    }

    // 主题切换：**不需要重画** —— 视图里每一处颜色都是 `darkTheme` / `defaultFg` / `defaultBg` 的绑定，
    // 而 `darkTheme` 绑在 Theme.isDark 上，主题一变各行的 canvas 自己就重绘（onCursorHereChanged 那条）。
    // 这里只需把 ANSI 调色板重设一次：libvterm 是把"索引色"落成 RGB 存进格子的，换了表要重放。
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
    // 终端壳按网页 demo 的「工作台终端」：圆角深色卡 + 路径/状态头 + 网格/编码尾。
    readonly property var vtModel: view.model
    readonly property bool terminalAttached: vtModel !== null && String(vtModel.state) === "attached"
    readonly property string connectionText: {
        if (vtModel === null)
            return qsTrId("terminal_connecting")
        var state = String(vtModel.state)
        if (state === "attached")
            return qsTrId("terminal_connected")
        var status = String(vtModel.statusText || "")
        return status.length > 0 ? status
            : (state.length > 0 ? state : qsTrId("terminal_connecting"))
    }

    Rectangle {
        id: terminalFrame
        anchors.fill: parent
        anchors.margins: 8
        radius: 8
        color: Theme.isDark ? "#000000" : "#F7F7F7"
        border.width: 1
        border.color: Theme.color.border
        clip: true

        Column {
            anchors.fill: parent
            anchors.margins: 1

            Item {
                id: terminalHead
                width: parent.width
                height: 28

                Rectangle {
                    anchors.fill: parent
                    topLeftRadius: 7
                    topRightRadius: 7
                    color: Theme.isDark ? "#1B1B1E" : "#F2F4F7"
                }

                Row {
                    id: headPathRow
                    anchors.left: parent.left
                    anchors.right: statusRow.left
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5

                    Text {
                        text: Hub.currentWorkspaceTitle.length > 0
                            ? Hub.currentWorkspaceTitle
                            : qsTrId("sessionBar_ungrouped")
                        color: Theme.color.textTertiary
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                Row {
                    id: statusRow
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Rectangle {
                        width: 5
                        height: 5
                        radius: 3
                        color: root.terminalAttached ? "#69DB7C" : Theme.color.textCaption
                    }
                    Text {
                        id: statusLabel
                        text: root.connectionText
                        color: root.terminalAttached ? Theme.color.textSecondary : Theme.color.textCaption
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.color.border
                }
            }

            Item {
                width: parent.width
                height: parent.height - terminalHead.height - terminalFoot.height

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
                        // 切块/收右栏都会走到这里：只解绑，**不关终端**（关块由 PanelBarDock 显式调 Terminal.closeBlock；
                        // 后端被顶替时桥会把这两个调用都转给扩展的后端）
                        if (root.blockId.length > 0)
                            Terminal.viewGone(root.blockId)
                    }
                }
            }

            Item {
                id: terminalFoot
                width: parent.width
                height: 24

                Rectangle {
                    anchors.fill: parent
                    bottomLeftRadius: 7
                    bottomRightRadius: 7
                    color: Theme.isDark ? "#141416" : "#F2F4F7"
                }

                Rectangle {   // 底部小字：网格尺寸做成一颗轻 pill，编码信息单独靠右，中间加细线
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    height: 17
                    width: gridText.implicitWidth + 12
                    radius: 4
                    color: Theme.color.chipHoverBg

                    Text {
                        id: gridText
                        anchors.centerIn: parent
                        text: (view.gridCols > 0 && view.gridRows > 0)
                            ? view.gridCols + " × " + view.gridRows
                            : "—"
                        color: Theme.color.textTertiary
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                        font.letterSpacing: 0.2
                    }
                }

                Rectangle {
                    anchors.right: encodingText.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 10
                    color: Theme.color.border
                }

                Text {
                    id: encodingText
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "UTF-8"
                    color: Theme.color.textCaption
                    font.family: "Cascadia Mono"
                    font.pixelSize: Theme.font.small
                    font.letterSpacing: 0.3
                }
            }
        }
    }

    // 后端被扩展顶替时**照常渲染**：TerminalSession 按面接线 ——
    // 字节面（默认）：hostData → 本地 VT → 本视图；输入 → hostWrite（裸字节）。
    // 页面面（兼容层，deprecated）：frameReady → 本地 VT；输入 → write。
    // 后端缺帧/缺信号的，会话的状态行会如实说明（在 TerminalSession::attachBackend 里设）。
}
