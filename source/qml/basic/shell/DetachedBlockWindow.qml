// 面板坞里"脱离出来"的块：**一个块一个独立窗口**。
//
// 为什么用系统装饰（`flags: Qt.Window`，不加 FramelessWindowHint）：需求要的"四边随意缩放 + 最小化 /
// 最大化按钮"系统本来就全给了；主窗口那套无边框皮肤（自绘标题栏 + NativeWindowFilter 的
// WM_NCHITTEST 补位 + WindowBridge 的单窗口绑定）是**绑在一个窗口上的单例**，硬套到多窗口上要先拆它，
// MVP 先不动那条路（取向见任务书）。于是 `setFlag` 之后窗口标志就定死了，这里**不用**也**不试**
// Qt.WindowTransparentForInput 那一类运行期改标志的做法。
//
// 分工：
//   · 模型（Dock.detachedBlocks）说"哪个块脱离了、窗口显示不显示、窗口多大在哪"；
//   · 本文件只负责"把模型说的那扇窗摆出来"：几何从模型读、变化写回模型；
//   · 内容路由与 PanelDock.qml **同一套**（viewPath 空 → 说明页；blockScoped → 推稳定 blockId），
//     路径都来自 C++ 的视图表，这里不出现任何 kind 字符串。
//
// ⚠️ 内容**重新装载**一次，不是把坞里那个 item 搬过来：WebEngineView 跨窗口重挂会重建渲染进程，
//    搬同一份的收益（保留现场）不保证拿得到，代价却是"坞里那份与独立窗口那份同时活着"。
//    所以坞里那份在脱离期间 active=false（卸载），进程内的会话状态在 C++ 桥里照旧
//    （终端重新装载后拿 terminal/follow 的 snapshot，见任务书对内容连续性的要求）。
//
// 独立窗口上仍有三个点抓手：拖它落回**主窗口**范围 ⇒ 松手重新附着（需求 ④）。
// 落点提示画在**主窗口**里（那里才有右栏那块位置），拖回期间由本文件直接推给主窗口的壳层
// （`host.dropHintRect`）—— 拖动中光标被本窗口攥着，主窗口收不到 hover，只能这样推。

import QtQuick
import QtQuick.Window
import ULinHub 1.0
import "../../widgets"

Window {
    id: canvas

    // 由 Shell 的 Repeater 注入（模型里那一项 / 主窗口 / 主窗口的壳层）
    required property var entry
    required property var mainWindow
    required property var host

    // 这个块的稳定 id：模型那几条脱离/回附的接口都用**id**做 key（行号会随关块前移，
    // 长命的独立窗口不能拿行号当身份）
    readonly property string blockId: String(entry ? entry.blockId : "")

    // 下限与默认值：宽取主窗口宽度的 38% 与 480 物理像素（换回逻辑像素）里更宽的那个 ——
    // 窄于 480 物理像素的浏览器/终端不好用；高与主窗口等高。硬编码逻辑像素在 200% 缩放下会太窄。
    readonly property int minW: 320
    readonly property int minH: 240
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property int defaultW: Math.max(minW, Math.round(
        Math.max(mainWindow ? mainWindow.width * 0.38 : 0, 480 / dpr)))
    readonly property int defaultH: Math.max(minH, mainWindow ? mainWindow.height : 640)

    readonly property bool hasView: String(entry.viewPath).length > 0

    visible: entry.visible
    title: String(entry.title)
    flags: Qt.Window          // 系统装饰：四边缩放 + 最小化/最大化/关闭按钮都由系统提供
    color: Theme.color.panelBg
    minimumWidth: minW
    minimumHeight: minH

    // 抓手记住的按下点（屏幕坐标 + 窗口当时的左上角）：位移用屏幕坐标差算，1:1 跟手
    property real pressGlobalX: 0
    property real pressGlobalY: 0
    property real pressWinX: 0
    property real pressWinY: 0
    // 本次拖动是否已经定过"按下点"（见 winGrip.onDragMoved）
    property bool pressResolved: false
    // 正在按模型摆窗口：这期间的变化不回写模型（防回环）
    property bool applyingGeometry: false

    // ---- 几何：模型是唯一真源 ----
    // ⚠️ 刻意**不**给 x/y/width/height 写声明式绑定：拖动要在按下期间直接搬窗口，而给已绑定的属性
    //    赋一次值就会把绑定摘掉（Qt 的规矩，`Binding` 的 restoreMode 那一套正是为绕它而存在）。
    //    这里的做法是"模型变化 → onEntryChanged 摆窗口；窗口自己的变化 → 回写模型"，
    //    两个方向都只有一条路，且同值不发通知（模型侧挡住），所以不会来回弹。
    function pushGeometry() {
        if (applyingGeometry || !entry || !entry.visible)
            return
        Dock.setDetachedGeometry(blockId, Math.round(canvas.x), Math.round(canvas.y),
                                 Math.round(canvas.width), Math.round(canvas.height))
    }

    // 把窗口摆回模型说的那个位置（换风格包重载 / 隐藏后再显示 / 重新脱离都走这里）
    function applyModelGeometry() {
        if (!entry)
            return
        applyingGeometry = true
        if (entry.w > 0 && entry.h > 0) {
            canvas.width = entry.w
            canvas.height = entry.h
            canvas.x = entry.x
            canvas.y = entry.y
            clampToScreen()
        } else {
            canvas.width = defaultW
            canvas.height = defaultH
            canvas.x = Math.round(Screen.width / 2 - canvas.width / 2)
            canvas.y = Math.round(Screen.height / 2 - canvas.height / 2)
        }
        applyingGeometry = false
    }

    // 让独立窗口留在屏幕可用区域内（多显示器/小屏上默认尺寸可能超出去）
    function clampToScreen() {
        const scr = canvas.screen
        if (!scr)
            return
        const vx = scr.virtualX !== undefined ? scr.virtualX : 0
        const vy = scr.virtualY !== undefined ? scr.virtualY : 0
        canvas.x = Math.max(vx, Math.min(canvas.x, vx + scr.width - canvas.width))
        canvas.y = Math.max(vy, Math.min(canvas.y, vy + scr.height - canvas.height))
    }

    // 窗口被搬动/改尺寸（用户拖系统边框、最大化、贴边）：回写模型。
    // 用这组现成的属性通知，是因为 QML 的 Window 没有 xChanged/widthChanged 之外的原生信号；
    // 它们只在值真的变了时响，回写后模型同值不发通知 ⇒ 不会自激。
    onXChanged: pushGeometry()
    onYChanged: pushGeometry()
    onWidthChanged: pushGeometry()
    onHeightChanged: pushGeometry()

    // 系统按钮关掉它（标题栏 ✕ / Alt+F4）：与"点 ◫ 关掉"同一语义 ——
    // 块回到"已附着但隐藏"，脱离状态与几何都留着（需求 ⑤ 的第三态）
    onClosing: (close) => {
        close.accepted = true
        console.log("[detach] window closing id=" + canvas.blockId)
        Dock.setDetachedVisible(canvas.blockId, false)
    }

    onEntryChanged: {
        applyModelGeometry()
        if (entry && entry.visible)
            pushGeometry()
    }

    Component.onCompleted: {
        applyModelGeometry()
        pushGeometry()
        console.log("[detach] window created id=" + entry.blockId + " " + canvas.width + "x" + canvas.height
                    + " at " + canvas.x + "," + canvas.y + " view=" + entry.viewPath)
    }

    // ---- 外壳：一张跟着主题走的面（Window.color 已给底色，这里加一圈描边收边）----
    Rectangle {
        anchors.fill: parent
        color: Theme.color.panelBg
        border.width: 1
        border.color: Theme.color.border

        // ---- 内容：与 PanelDock.qml 同一套路由（路径来自 C++ 的视图表）----
        Loader {
            id: content
            anchors.fill: parent
            anchors.margins: 1          // 让开那圈描边
            active: canvas.hasView

            sourceComponent: {
                if (!canvas.hasView)
                    return null
                return canvas.entry.viewBlockScoped ? blockScopedComp : plainComp
            }
        }

        Component {
            id: plainComp
            Loader {
                anchors.fill: parent
                source: Style.url(canvas.entry.viewPath)
            }
        }

        // 要跟着"这个块"走的视图（终端）：用 Qt.binding 把稳定 blockId 推给面板。
        // ⚠️ 与 PanelDock.qml 那条同理：宿主不重建 item 时要靠活绑定换块（同一块再次脱离时
        //    item 可能复用同一实例，绑死一次快照会串台）。
        Component {
            id: blockScopedComp
            Loader {
                anchors.fill: parent
                source: Style.url(canvas.entry.viewPath)

                onLoaded: {
                    if (item) {
                        item.blockId = Qt.binding(function () { return String(canvas.entry.blockId) })
                        // 终端是会话作用域的：独立窗口里的面板同样要跟着当前会话走
                        if (item.sessionId !== undefined)
                            item.sessionId = Qt.binding(function () { return Hub.currentSessionId })
                    }
                }
            }
        }

        // 没有视图的块（文件浏览这类）：说明文案由 C++ 给，QML 不拼
        PanelPlaceholder {
            anchors.fill: parent
            visible: !canvas.hasView
            title: String(canvas.entry.title)
            detail: String(canvas.entry.viewDetail)
        }

        // 三个点抓手：独立态拖它**回附**。视觉与规则同一个控件（只有三个浅灰点、不要反馈、命中区外放 10px）。
        PanelGrip {
            id: winGrip
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            z: 20
            nativeHost: false      // 系统装饰窗口：标题栏归系统，客户端区域的抓手本来就能收到事件

            property bool overTarget: false

            // 屏幕坐标下主窗口的矩形：跨窗口判断**只能用屏幕坐标**（各自窗口的场景原点不是一个）
            function mainRect() {
                if (!canvas.mainWindow)
                    return null
                const tl = canvas.mainWindow.mapToGlobal(0, 0)
                return Qt.rect(tl.x, tl.y, canvas.mainWindow.width, canvas.mainWindow.height)
            }

            // 落点判定：光标落在主窗口矩形内（含 24px 容差）就算"要附着回去"。
            // 容差是必要的：用户拖到主窗口边缘、或往右栏那块偏一点，心里想的都是"放回去"。
            function insideAttachZone(gp) {
                const r = mainRect()
                if (!r)
                    return false
                const s = 24
                return gp.x >= r.x - s && gp.x <= r.x + r.width + s
                       && gp.y >= r.y - s && gp.y <= r.y + r.height + s
            }

            onPressStarted: {
                // 按下就先占位，精确值由第一次 dragMoved 反推（见那里）
                canvas.pressResolved = false
                canvas.pressWinX = canvas.x
                canvas.pressWinY = canvas.y
                winGrip.overTarget = false
            }

            onDragMoved: (dx, dy, globalPos, scenePos) => {
                // 第一次拿到位移时把"按下点"定下来：grip 给的是相对按下点的屏幕坐标差，
                // 按下点的屏幕坐标 = 当前点 - 位移（比在 onPressed 里另取一次更稳）
                if (!canvas.pressResolved) {
                    canvas.pressResolved = true
                    canvas.pressGlobalX = globalPos.x - dx
                    canvas.pressGlobalY = globalPos.y - dy
                    canvas.pressWinX = canvas.x
                    canvas.pressWinY = canvas.y
                }

                // 按屏幕坐标差 1:1 搬窗口
                canvas.x = Math.round(canvas.pressWinX + dx)
                canvas.y = Math.round(canvas.pressWinY + dy)

                const inside = winGrip.insideAttachZone(globalPos)
                if (inside !== winGrip.overTarget) {
                    winGrip.overTarget = inside
                    // 落点提示画在主窗口里（拖动中主窗口收不到 hover，只能由这边推过去）
                    if (canvas.host)
                        canvas.host.dropHintActive = inside
                }
            }

            onDragFinished: (dx, dy, globalPos) => {
                // 只点击没拖动（pressResolved 从没被置起）⇒ 交给系统把窗口置前，这里不动状态。
                // 不判这一条的话，"点一下抓手"会被当成一次落在主窗口上的拖动 ⇒ 窗口瞬间被吞掉。
                if (!canvas.pressResolved) {
                    winGrip.overTarget = false
                    return
                }
                canvas.pressResolved = false
                const inside = winGrip.insideAttachZone(globalPos)
                winGrip.overTarget = false
                if (canvas.host)
                    canvas.host.dropHintActive = false
                console.log("[detach] drag finished id=" + canvas.blockId
                            + " g=" + Math.round(globalPos.x) + "," + Math.round(globalPos.y)
                            + " inside=" + inside)
                if (inside)
                    Dock.attach(canvas.blockId)   // 重新附着：清脱离状态（本窗口随之销毁）+ 激活
                else
                    canvas.pushGeometry()
            }
        }
    }
}
