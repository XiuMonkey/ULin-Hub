// 「面板」右栏脱离出来的**独立窗口**（需求 ③）：无边框 + 自绘四边缩放。
//
// 为什么从"系统装饰"改成自绘（上一版是 `flags: Qt.Window` 系统标题栏）：
//   需求要的是"拖出去后是无边框窗口，最小化/最大化自己实现"。
//
// ⚠️ 本窗口**没有**自己的标题栏（用户否掉了那一版）：副窗口的标题栏就是**面板自己的头部行**
//    —— 那一行里的「面板」+ 三点抓手 + − / □❐ / ✕（按钮在 RightSidebar.qml 里，✕ 左边两颗只在
//    副窗口态可见）。所以窗口几何 = 右栏那块矩形**本身**（尺寸/位置逐像素一致、不上移、不多留高度，
//    见 Shell.liftSidebarForDrag），内容"零跳变"落进窗口，也不会多出一条谁也不认的横杠。
//
// 取舍（写在 PROGRESS 里，也别来回改）：
//   · 四边缩放**自己实现**（QML 侧 8 条热区改 x/y/width/height），原生那套只负责"样式位"。
//     ⚠️ 2026-10-16 修正（旧注释说"没有动 NativeWindowFilter"，那是错的）：本窗口在
//     Component.onCompleted 里走 `WindowBridge.attachAuxWindow` 补上和**主窗口同一排**原生
//     样式位（WS_THICKFRAME/WS_CAPTION + 关 Win11 系统圆角）。原因：主窗口之所以"改尺寸不出黑块"
//     不是因为它改得少（它的窗口也会被子窗口/最大化改尺寸），而是因为它有这排样式位、走的是
//     DWM 非客户区路径；无边框窗口缺了它们，每帧改原生尺寸就会露出一块还没画的黑色区域
//     （实测：同一手势 263 帧里 120 帧有黑块，最长连续黑 486px）。
//     代价：没有系统的贴边吸附/Aero Snap（这一点由"头部空白处拖动"的自绘搬窗承担）。
//   · 最大化用**真窗口状态** `Window.Maximized`（探针实测：无边框窗口最大化取的是**工作区**
//     1280x752，不是整块屏幕 ⇒ 不会盖任务栏），还原回 `Window.Windowed` 并显式写回记下的几何。
//
// ⚠️ 本文件**不装内容**：右栏那块 RightSidebar 是 reparent 进 `panelSlot` 的**同一份实例**
//    （见 Shell.liftSidebarForDrag / attachSidebar）—— 两个宿主各放一份会让终端/网页各跑一个会话。
//
// ⚠️ 头部那两颗窗口按钮**不要**登记进 `WindowBridge.addWindowControl`：那是**主窗口**的原生命中测试
//    （`NativeWindowFilter` 里窗口控件表只有一份，矩形按各自窗口的场景坐标算），登记进来会让
//    主窗口的某个位置莫名变成窗口按钮。本窗口是无边框但**没有**原生命中测试介入，QML 自己收点击。
//
// 状态都由 Shell 推（本文件不存"该不该显示"）：
//   · `sidebarFloating` —— 三点刚"拿起来"，跟着光标走、结局未定；
//   · `sidebarDetached` —— 确认脱离（松手在右栏原区域之外）后才为真 —— 头部那两颗按钮也只在
//     这一态才出现（跟手期间还没"成为"独立窗口）。

import QtQuick
import QtQuick.Window
import ULinHub 1.0

Window {
    id: win

    // 由 Shell 注入：状态与几何都在壳层（本文件只把窗摆出来 + 自己拖/自己缩放）
    required property var host

    // 内容槽：Shell 把 RightSidebar reparent 到这里（填满整扇窗 —— 副窗口没有自己的标题栏）
    readonly property alias panelSlot: slot

    // 任务栏/Alt+Tab 上显示的名字（窗口没有自绘标题栏，这是唯一的窗口名来源）
    title: qsTrId("right_sidebar_title")
    // 无边框：系统标题栏/边框/按钮全不要，最小化/最大化/关闭与四边缩放都在本文件里自己做
    flags: Qt.Window | Qt.FramelessWindowHint
    // 显示条件：跟着光标走（拿起来）或已确认脱离（独立）。两态都由 Shell 给
    visible: win.host ? win.host.sidebarInWindow : false
    // 窗口底色：**必须和主窗口一样用 transparent**（Main.qml 也是 color: "transparent"）。
    //   不透明的无边框顶层窗口在缩放时，DWM 不做逐帧合成 ⇒ 没画到的那块就露黑（矩形、方角，
    //   表现为"圆角卡片旁边的黑块"）。透明窗口由 DWM 合成，缩放干净；圆角照旧由面板卡片自己画
    //   （角上透出桌面，和主窗口的表现一致）。面板卡片本身不透明且铺满整扇窗 ⇒ 平时看不到透出。
    color: "transparent"
    minimumWidth: Theme.size.rightSidebarMinWidth
    // ⚠️ 这里**不再**加 `Theme.size.titleBarHeight`：自绘标题栏那条已删，窗口就是面板矩形那么大
    minimumHeight: 200

    // 面板内容的下限（与 Window.minimumWidth 同源）：左/上边缩放时 Qt 一钳最小尺寸，x 就会与
    // 宽度脱钩（左边跟着鼠标走、右边不动）⇒ 这里先自己钳死，绝不让请求越界。
    readonly property int minTotalW: Theme.size.rightSidebarMinWidth
    readonly property int minTotalH: 200

    // 最大化态（= 真窗口状态，图标与"能不能拖/缩放"都看它）
    readonly property bool maximized: win.visibility === Window.Maximized
    // 最大化/最小化**之前**的几何：还原时显式写回去（要求里说的"记住还原前的几何"）
    property rect windowedGeometry: Qt.rect(0, 0, 0, 0)
    // 「拿起来」的目标几何：由 Shell 在 prepareForLift 里给（见 normalizeForLift 的说明）
    property rect liftGeometry: Qt.rect(0, 0, 0, 0)

    // 唯一的几何写入口：八条缩放热区（以及"拿起来/跟手"）都走它。
    // 面板头部那条拖动热区（RightSidebar.qml 的 headerDragArea）与三点抓手直接写 win.x/win.y
    // ——拖动不碰宽高，与这里同一套"按下时位置 + 屏幕位移"的算法。
    // ⚠️ 这里**不**给自己写 x/y/width/height 的绑定（绑上去会被拖动摘掉，或反过来每帧盖回来）；
    //    几何由 Shell 在"拿起来/跟手"时推（applySidebarWindowGeometry），之后以窗口为准。
    function applyGeometry(x, y, w, h) {
        win.x = Math.round(x)
        win.y = Math.round(y)
        win.width = Math.round(w)
        win.height = Math.round(h)
    }

    // 「拿起来」的三步（都由 Shell 按序调，见 Shell.liftSidebarForDrag）：
    //   ① prepareForLift  —— 记下目标几何、清掉上次的还原几何；
    //   ② （Shell 置 sidebarFloating ⇒ 这扇窗才显示出来）
    //   ③ normalizeForLift —— 保证是普通态。
    // 为什么非要③：上一次可能是最大化/最小化态被收起来的（隐藏时 Qt 只把 visibility 记成 Hidden，
    // 平台窗口还留着放大的摆放）⇒ 再显示出来会是放大/最小化的样子；而 showNormal() 会还原
    // "最大化前的几何"，所以它之后再按目标几何摆一次。
    function prepareForLift(x, y, w, h) {
        win.liftGeometry = Qt.rect(Math.round(x), Math.round(y), Math.round(w), Math.round(h))
        win.windowedGeometry = Qt.rect(0, 0, 0, 0)
    }

    function normalizeForLift() {
        // 无条件回普通态：上一次可能是最大化/最小化被收起来的 —— 隐藏时 Qt 只把 `visibility`
        // 记成 Hidden，平台窗口还留着放大的摆放，光看 visibility 判不出来。showNormal() 会还原
        // "最大化前的几何"，所以紧接着按目标几何再摆一次。
        win.showNormal()
        const g = win.liftGeometry
        if (g.width > 0 && g.height > 0)
            win.applyGeometry(g.x, g.y, g.width, g.height)
    }

    // 最小化 = showMinimized()（需求 ③ 原话）
    function minimizeWindow() {
        win.showMinimized()
    }

    // 最大化/还原 = Window.Maximized / Window.Windowed（需求 ③ 原话）+ 自己记的还原几何
    function toggleMaximize() {
        if (win.maximized) {
            // 先记好的那个几何；真窗口状态回普通态时 Qt 也会还原一次，这里再显式写一遍更稳
            const g = win.windowedGeometry
            win.visibility = Window.Windowed
            if (g.width > 0 && g.height > 0)
                win.applyGeometry(g.x, g.y, g.width, g.height)
        }
        else {
            win.windowedGeometry = Qt.rect(win.x, win.y, win.width, win.height)
            win.visibility = Window.Maximized
        }
    }

    // 关闭 = 回附 + 关窗：与 `onClosing`（系统 ✕ / Alt+F4 / 任务栏关闭）**同一语义**，
    // 于是"关闭后再点 ◫ = 默认以附着状态打开"这条三态语义不破（内容 reparent 回槽位、窗口隐藏）。
    function closeWindow() {
        if (win.host)
            win.host.attachSidebar(false)
    }

    // ---- 四边/四角缩放（需求 ③，自己实现，见文件头）----
    // 逻辑放在 Window 上（而不是内联 component 里）：八条热区与"代码驱动取证"都用同一对入口，
    // 也不依赖"内联组件能不能看见外层 id"这种不确定的解析规则。
    // 位移一律用**屏幕坐标差**（热区自己会跟着窗口动，用局部坐标会自己追自己 —— 同 PanelGrip 那条坑）。
    property real resizePressScreenX: 0
    property real resizePressScreenY: 0
    property rect resizePressGeom: Qt.rect(0, 0, 0, 0)
    // 本次手势是否已经把缩放交给系统（见 resizeApply 的说明）
    property bool resizeHandedOff: false

    function resizeBegin(sx, sy) {
        win.resizePressScreenX = sx
        win.resizePressScreenY = sy
        win.resizePressGeom = Qt.rect(win.x, win.y, win.width, win.height)
        win.resizeHandedOff = false
    }

    // edges 用 Qt.LeftEdge / Qt.TopEdge / Qt.RightEdge / Qt.BottomEdge 的位或
    //
    // 两档实现，用 handOffToSystem 选（默认 = 跟手自己算，见下）：
    //   · true  —— 交系统 sizing loop（`Window.startSystemResize`）。只在**真有按下**的手势里有效
    //     （Windows 的 SC_SIZE 循环靠鼠标按钮推进；代码驱动没有按下时它什么都不做 —— 实测）。
    //   · false —— 自己按屏幕坐标差算 x/y/w/h 写回窗口（**当前默认**）。它每帧改一次原生窗口尺寸，
    //     于是**必须有**原生样式位（见文件头 attachAuxWindow），否则每帧露出黑块。
    // 两档都在；哪个更跟手由实测决定，且都不改本函数的**名字**（同名重复会是 Duplicate method name）。
    function resizeApply(edges, sx, sy, handOffToSystem) {
        if (handOffToSystem) {
            if (win.resizeHandedOff)
                return
            win.resizeHandedOff = true
            win.startSystemResize(edges)
            return
        }
        // 位移一律用**屏幕坐标差**（热区自己会跟着窗口动，用局部坐标会自己追自己 —— 同 PanelGrip 那条坑）
        const dx = sx - win.resizePressScreenX
        const dy = sy - win.resizePressScreenY
        let x = win.resizePressGeom.x
        let y = win.resizePressGeom.y
        let w = win.resizePressGeom.width
        let h = win.resizePressGeom.height
        // 左/上边：动的同时改宽/高；到下限就把那条边钳住。
        // ⚠️ 必须自己钳死：请求一旦低于 Qt 的 minimumWidth/Height，Qt 会钳**宽**而 x 照走
        //    （右边不动、左边跟着鼠标 ⇒ 看着像"缩放坏了"）。
        if (edges & Qt.LeftEdge) {
            const right = win.resizePressGeom.x + win.resizePressGeom.width
            x = Math.min(win.resizePressGeom.x + dx, right - win.minTotalW)
            w = right - x
        } else if (edges & Qt.RightEdge) {
            w = Math.max(win.minTotalW, win.resizePressGeom.width + dx)
        }
        if (edges & Qt.TopEdge) {
            const bottom = win.resizePressGeom.y + win.resizePressGeom.height
            y = Math.min(win.resizePressGeom.y + dy, bottom - win.minTotalH)
            h = bottom - y
        } else if (edges & Qt.BottomEdge) {
            h = Math.max(win.minTotalH, win.resizePressGeom.height + dy)
        }
        win.applyGeometry(x, y, w, h)
    }

    Item {
        id: frame
        anchors.fill: parent

        // ⚠️ 这里**没有**自绘标题栏（用户否掉了那一版："面板不是自带删除按钮吗？你给另外两个按钮
        //    放在删除按钮旁边不就行了"）。副窗口的标题栏 = 面板自己的头部行（RightSidebar.qml 里
        //    那行「面板」+ 三点抓手 + − / □❐ / ✕）：按钮在那边，拖动也在那边（头部空白处拖窗口）。
        //    于是本窗口只需要一个"填满整扇窗"的内容槽。

        // 内容槽：RightSidebar reparent 到这里（`anchors.fill: parent` 于是自动填满整扇窗）
        Item {
            id: slot
            anchors.fill: parent
        }

        // ---- 四边 + 四角缩放热区（需求 ③，自己实现，见文件头）----
        // 逻辑全在 Window 上的 resizeBegin / resizeApply（八条热区与"代码驱动取证"共用同一对入口）；
        // 这里只有"位置 + 光标形状 + 把屏幕坐标喂进去"。

        // 四条边：各 5px（与主窗口原生 kResizeBorder 同宽）
        MouseArea {
            id: gripLeft
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 5
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripLeft.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripLeft.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.LeftEdge, g.x, g.y)
            }
        }
        MouseArea {
            id: gripRight
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 5
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripRight.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripRight.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.RightEdge, g.x, g.y)
            }
        }
        MouseArea {
            id: gripTop
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 5
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripTop.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripTop.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.TopEdge, g.x, g.y)
            }
        }
        MouseArea {
            id: gripBottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 5
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripBottom.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripBottom.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.BottomEdge, g.x, g.y)
            }
        }

        // 四个角：12px 方块，z 抬到边之上（角落永远赢，不会只缩一条边）
        MouseArea {
            id: gripTopLeft
            z: 2
            anchors.left: parent.left
            anchors.top: parent.top
            width: 12
            height: 12
            hoverEnabled: true
            cursorShape: Qt.SizeFDiagCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripTopLeft.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripTopLeft.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.LeftEdge | Qt.TopEdge, g.x, g.y)
            }
        }
        MouseArea {
            id: gripTopRight
            z: 2
            anchors.right: parent.right
            anchors.top: parent.top
            width: 12
            height: 12
            hoverEnabled: true
            cursorShape: Qt.SizeBDiagCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripTopRight.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripTopRight.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.RightEdge | Qt.TopEdge, g.x, g.y)
            }
        }
        MouseArea {
            id: gripBottomLeft
            z: 2
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: 12
            height: 12
            hoverEnabled: true
            cursorShape: Qt.SizeBDiagCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripBottomLeft.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripBottomLeft.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.LeftEdge | Qt.BottomEdge, g.x, g.y)
            }
        }
        MouseArea {
            id: gripBottomRight
            z: 2
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 12
            height: 12
            hoverEnabled: true
            cursorShape: Qt.SizeFDiagCursor
            enabled: !win.maximized
            onPressed: (mouse) => {
                const g = gripBottomRight.mapToGlobal(mouse.x, mouse.y)
                win.resizeBegin(g.x, g.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const g = gripBottomRight.mapToGlobal(mouse.x, mouse.y)
                win.resizeApply(Qt.RightEdge | Qt.BottomEdge, g.x, g.y)
            }
        }
    }

    // 被搬动/改尺寸：回写 Shell。只在值真的变了时响；Shell 程序化摆窗口期间有自己的 applying 门
    // （sidebarWinApplying）⇒ 不会自激（"模型→窗口→模型"来回弹）。
    onXChanged: pushGeometry()
    onYChanged: pushGeometry()
    onWidthChanged: pushGeometry()
    onHeightChanged: pushGeometry()

    function pushGeometry() {
        // 只有"确认脱离"之后才算独立窗口的几何：跟手期间（floating）窗口位置是光标跟出来的，
        // 不是用户摆的，回写只会把上次的几何覆盖成中间态 —— 而那时的值 Shell 自己就有。
        if (!win.host || !win.host.sidebarDetached)
            return
        win.host.setSidebarWindowGeometry(Math.round(win.x), Math.round(win.y),
                                          Math.round(win.width), Math.round(win.height))
    }

    // 系统 ✕ / Alt+F4 / 任务栏"关闭窗口"：与"点顶栏 ◫ 关副窗口"**同一语义**（需求 ③）——
    // 右栏回"已附着但隐藏"（内容 reparent 回 Shell、窗口隐藏），下一次点 ◫ 默认以**附着**状态打开。
    // close.accepted = true：不让系统直接收掉这扇窗（它还要把内容还回去），隐藏由
    // host.sidebarInWindow 那条绑定完成。
    onClosing: (close) => {
        close.accepted = true
        console.log("[sidebar-detach] window closing")
        win.closeWindow()
    }

    Component.onCompleted: {
        // 曾经在这里调 WindowBridge.attachAuxWindow(win) 给副窗补 WS_THICKFRAME/WS_CAPTION，
        // 想让它像主窗那样走 DWM 非客户区路径 —— 那条路**没解决问题**（黑块照旧）✗，
        // 真正的原因是窗口底色不透明（见上面的 color: "transparent"）。所以这里不再补样式：
        // 少一套"多窗口原生样式"机制，副窗回到简单态。
        console.log("[sidebar-detach] window created " + win.width + "x" + win.height
                    + " at " + win.x + "," + win.y)
    }
}
