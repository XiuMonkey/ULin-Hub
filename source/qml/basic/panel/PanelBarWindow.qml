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
    // 不挂到主窗口的 transient 链：否则 Windows 把副窗口当 tool/transient，
    // 任务栏没有入口，最小化也只会缩成贴在主窗口上的条。
    transientParent: null

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

    readonly property bool maximized: win.visibility === Window.Maximized   // 窗口状态仍由 Qt 报，按钮图标用

    // 窗口几何/最大化/缩放的算法在 WindowBridge（C++）；QML 只负责把热区事件转过去。
    function applyGeometry(x, y, w, h) {
        WindowBridge.auxApplyGeometry(x, y, w, h)
    }

    function prepareForLift(x, y, w, h) {
        WindowBridge.auxPrepareLift(x, y, w, h)
    }

    function normalizeForLift() {
        WindowBridge.auxNormalizeLift()
    }

    function minimizeWindow() {
        WindowBridge.auxMinimize()
    }

    function toggleMaximize() {
        WindowBridge.auxToggleMaximize()
    }

    // 关闭 = 回附 + 关窗：与 `onClosing`（系统 ✕ / Alt+F4 / 任务栏关闭）**同一语义**。
    function closeWindow() {
        if (win.host)
            win.host.attachPanelBar(false)
    }

    function resizeBegin(sx, sy) {
        WindowBridge.auxResizeBegin(sx, sy)
    }

    function resizeApply(edges, sx, sy, handOffToSystem) {
        WindowBridge.auxResizeApply(edges, sx, sy, handOffToSystem, win.minTotalW, win.minTotalH)
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
