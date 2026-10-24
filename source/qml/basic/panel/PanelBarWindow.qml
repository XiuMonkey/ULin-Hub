// 「面板」右栏脱离出来的**独立窗口**：无边框 + 自绘四边缩放，最小化/最大化自己实现。
// ⚠️ 本窗口没有自己的标题栏：副窗口的标题栏就是**面板自己的头部行**，所以窗口几何 = 右栏那块
// 矩形本身（逐像素一致），内容"零跳变"落进窗口。
// 四边缩放 QML 自己实现（8 条热区）；原生只负责"样式位"：Component.onCompleted 走
// WindowBridge.attachAuxWindow 补上和主窗口同一排原生样式位（WS_THICKFRAME/WS_CAPTION + 关 Win11
// 系统圆角）—— 缺了它每帧改原生尺寸会露黑块；代价是没有系统贴边吸附（由头部空白拖动自绘搬窗承担）。
// ⚠️ 本文件不装内容：PanelBar 是 reparent 进 `panelSlot` 的**同一份实例**（两个宿主各放一份会让终端/网页各跑一个会话）。
// ⚠️ 头部窗口按钮**不要**登记进 WindowBridge.addWindowControl：那是主窗口的原生命中测试表
// （表只有一份，矩形按各自窗口场景坐标算），登记进来会让主窗口某处莫名变成窗口按钮。
// 显示状态全由 Shell 推：panelBarFloating = 跟手中，panelBarDetached = 确认脱离。

import QtQuick
import QtQuick.Window
import ULinHub 1.0

Window {
    id: win

    required property var host   // 由 Shell 注入：状态与几何都在壳层（本文件只把窗摆出来 + 自己拖/自己缩放）

    readonly property alias panelSlot: slot   // 内容槽：Shell 把 PanelBar reparent 到这里（填满整扇窗 —— 副窗口没有自己的标题栏）

    title: qsTrId("panelBar_title")   // 任务栏/Alt+Tab 上显示的名字（窗口没有自绘标题栏，这是唯一的窗口名来源）
    flags: Qt.Window | Qt.FramelessWindowHint   // 无边框：系统标题栏/边框/按钮全不要，最小化/最大化/关闭与四边缩放都在本文件里自己做
    visible: win.host ? win.host.panelBarInWindow : false   // 显示条件：跟着光标走（拿起来）或已确认脱离（独立）。两态都由 Shell 给

    // 窗口底色：**必须和主窗口一样用 transparent**（Main.qml 也是 color: "transparent"）。
    // 不透明的无边框顶层窗口在缩放时，DWM 不做逐帧合成 ⇒ 没画到的那块就露黑（矩形、方角，
    // 表现为"圆角卡片旁边的黑块"）。透明窗口由 DWM 合成，缩放干净；圆角照旧由面板卡片自己画
    // （角上透出桌面，和主窗口的表现一致）。面板卡片本身不透明且铺满整扇窗 ⇒ 平时看不到透出。
    color: "transparent"
    minimumWidth: Theme.size.panelBarMinWidth
    minimumHeight: 200   // ⚠️ 这里**不再**加 `Theme.size.titleBarHeight`：自绘标题栏那条已删，窗口就是面板矩形那么大

    // 面板内容的下限（与 Window.minimumWidth 同源）：左/上边缩放时 Qt 一钳最小尺寸，x 就会与
    // 宽度脱钩（左边跟着鼠标走、右边不动）⇒ 这里先自己钳死，绝不让请求越界。
    readonly property int minTotalW: Theme.size.panelBarMinWidth
    readonly property int minTotalH: 200

    readonly property bool maximized: win.visibility === Window.Maximized   // 最大化态（= 真窗口状态，图标与"能不能拖/缩放"都看它）
    property rect windowedGeometry: Qt.rect(0, 0, 0, 0)
    property rect liftGeometry: Qt.rect(0, 0, 0, 0)   // 「拿起来」的目标几何：由 Shell 在 prepareForLift 里给（见 normalizeForLift 的说明）

    // 唯一的几何写入口：八条缩放热区（以及"拿起来/跟手"）都走它。
    // 面板头部拖动热区（PanelBar.qml 的 headerDragArea）与三点抓手直接写 win.x/win.y（拖动不碰宽高，
    // 同一套"按下时位置 + 屏幕位移"算法）。⚠️ 这里**不**给自己写 x/y/width/height 的绑定：
    // 绑上去会被拖动摘掉，或反过来每帧盖回来。
    function applyGeometry(x, y, w, h) {
        win.x = Math.round(x)
        win.y = Math.round(y)
        win.width = Math.round(w)
        win.height = Math.round(h)
    }

    // 「拿起来」的收尾（Shell 按序调，见 Shell.liftPanelBarForDrag）：prepareForLift 先记下目标几何，
    // normalizeForLift 再保证窗口是普通态 —— 上一次可能是最大化/最小化被收起的（隐藏时 Qt 只把
    // visibility 记成 Hidden，平台窗口还留着放大的摆放），showNormal() 会还原"最大化前的几何"，
    // 所以它之后再按目标几何摆一次。
    function prepareForLift(x, y, w, h) {
        win.liftGeometry = Qt.rect(Math.round(x), Math.round(y), Math.round(w), Math.round(h))
        win.windowedGeometry = Qt.rect(0, 0, 0, 0)
    }

    function normalizeForLift() {
        win.showNormal()   // 无条件回普通态：隐藏时 Qt 只记 visibility=Hidden，平台窗口可能还留着最大化摆放
        const g = win.liftGeometry
        if (g.width > 0 && g.height > 0)
            win.applyGeometry(g.x, g.y, g.width, g.height)
    }

    function minimizeWindow() {   // 最小化 = showMinimized()（需求 ③ 原话）
        win.showMinimized()
    }

    function toggleMaximize() {   // 最大化/还原 = Window.Maximized / Window.Windowed（需求 ③ 原话）+ 自己记的还原几何
        if (win.maximized) {
            const g = win.windowedGeometry   // 先记好的那个几何；真窗口状态回普通态时 Qt 也会还原一次，这里再显式写一遍更稳
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
            win.host.attachPanelBar(false)
    }

    property real resizePressScreenX: 0
    property real resizePressScreenY: 0
    property rect resizePressGeom: Qt.rect(0, 0, 0, 0)
    property bool resizeHandedOff: false

    function resizeBegin(sx, sy) {
        win.resizePressScreenX = sx
        win.resizePressScreenY = sy
        win.resizePressGeom = Qt.rect(win.x, win.y, win.width, win.height)
        win.resizeHandedOff = false
    }

    // edges 用 Qt.LeftEdge / Qt.TopEdge / Qt.RightEdge / Qt.BottomEdge 的位或
    // 两档实现，用 handOffToSystem 选（默认 = 跟手自己算，见下）：
    // · true  —— 交系统 sizing loop（`Window.startSystemResize`）。只在**真有按下**的手势里有效
    // · false —— 自己按屏幕坐标差算 x/y/w/h 写回窗口（**当前默认**）。它每帧改一次原生窗口尺寸，
    // 于是**必须有**原生样式位（见文件头 attachAuxWindow），否则每帧露出黑块。
    function resizeApply(edges, sx, sy, handOffToSystem) {
        if (handOffToSystem) {
            if (win.resizeHandedOff)
                return
            win.resizeHandedOff = true
            win.startSystemResize(edges)
            return
        }
        const dx = sx - win.resizePressScreenX   // 位移一律用**屏幕坐标差**（热区自己会跟着窗口动，用局部坐标会自己追自己 —— 同 PanelGrip 那条坑）
        const dy = sy - win.resizePressScreenY
        let x = win.resizePressGeom.x
        let y = win.resizePressGeom.y
        let w = win.resizePressGeom.width
        let h = win.resizePressGeom.height

        // 左/上边：动的同时改宽/高；到下限就把那条边钳住。
        // ⚠️ 必须自己钳死：请求一旦低于 Qt 的 minimumWidth/Height，Qt 会钳**宽**而 x 照走
        // （右边不动、左边跟着鼠标 ⇒ 看着像"缩放坏了"）。
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
        // 放在删除按钮旁边不就行了"）。副窗口的标题栏 = 面板自己的头部行（PanelBar.qml 里
        // 那行「面板」+ 三点抓手 + − / □❐ / ✕）：按钮在那边，拖动也在那边（头部空白处拖窗口）。
        // 于是本窗口只需要一个"填满整扇窗"的内容槽。

        Item {   // 内容槽：PanelBar reparent 到这里（`anchors.fill: parent` 于是自动填满整扇窗）
            id: slot
            anchors.fill: parent
        }

        MouseArea {   // 四条边：各 5px（与主窗口原生 kResizeBorder 同宽）
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

        MouseArea {   // 四个角：12px 方块，z 抬到边之上（角落永远赢，不会只缩一条边）
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
    // （panelBarWinApplying）⇒ 不会自激（"模型→窗口→模型"来回弹）。
    onXChanged: pushGeometry()
    onYChanged: pushGeometry()
    onWidthChanged: pushGeometry()
    onHeightChanged: pushGeometry()

    function pushGeometry() {
        if (!win.host || !win.host.panelBarDetached)   // 只有"确认脱离"之后才算独立窗口的几何：跟手期间（floating）窗口位置是光标跟出来的
            return
        win.host.setPanelBarWindowGeometry(Math.round(win.x), Math.round(win.y),
                                          Math.round(win.width), Math.round(win.height))
    }

    // 系统 ✕ / Alt+F4 / 任务栏"关闭窗口"：与"点顶栏 ◫ 关副窗口"**同一语义**（需求 ③）——
    // 右栏回"已附着但隐藏"（内容 reparent 回 Shell、窗口隐藏），下一次点 ◫ 默认以**附着**状态打开。
    // close.accepted = true：不让系统直接收掉这扇窗（它还要把内容还回去），隐藏由
    // host.panelBarInWindow 那条绑定完成。
    onClosing: (close) => {
        close.accepted = true
        console.log("[panelBar-detach] window closing")
        win.closeWindow()
    }

    Component.onCompleted: {
        // 补上和主窗口**同一排**原生样式位（WS_THICKFRAME/WS_CAPTION + 关 Win11 系统圆角）：
        // · 黑块的真凶是**窗口底色不透明**（见上面的 color: "transparent"）✗ 不是缺样式位；
        // · 但样式位仍要补：有 WS_THICKFRAME，缩放/搬窗才走系统原生快路径，否则拖动会一卡一卡（这行不能撤）。
        // WindowBridge 的窗口动作与可绑定属性都只认主窗口，换掉就是罪状级 bug。
        WindowBridge.attachAuxWindow(win)
        console.log("[panelBar-detach] window created " + win.width + "x" + win.height
                    + " at " + win.x + "," + win.y)
    }
}
