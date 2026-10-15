// 「面板」右栏脱离出来的**独立窗口**（需求 ③）：无边框 + 自绘标题栏 + 自绘四边缩放。
//
// 为什么从"系统装饰"改成自绘（上一版是 `flags: Qt.Window` 系统标题栏）：
//   需求要的是"拖出去后是无边框窗口，最小化/最大化自己实现"，且尺寸/图标/悬停要跟主窗口
//   `skin/window/TitleBar.qml` 那套一致 —— 系统装饰给不了这些。
//
// 取舍（写在 PROGRESS 里，也别来回改）：
//   · 四边缩放**自己实现**（QML 侧 8 条热区改 x/y/width/height），**没有**动
//     `common/platform/win/NativeWindowFilter`。原因：那套原生机制是**单窗口**绑定的
//     （`m_captionItem` 与窗口控件表都是全局一份，靠 `WindowBridge` 绑在主窗口上），
//     硬套到第二扇窗要先拆它；而自绘缩放的最小尺寸钳制在 QML 里也能做得很干净。
//     代价：没有系统的贴边吸附/Aero Snap，缩放跟手程度取决于 Qt 的窗口重建（实测见 PROGRESS）。
//   · 最大化用**真窗口状态** `Window.Maximized`（探针实测：无边框窗口最大化取的是**工作区**
//     1280x752，不是整块屏幕 ⇒ 不会盖任务栏），还原回 `Window.Windowed` 并显式写回记下的几何。
//
// ⚠️ 本文件**不装内容**：右栏那块 RightSidebar 是 reparent 进 `panelSlot` 的**同一份实例**
//    （见 Shell.liftSidebarForDrag / attachSidebar）—— 两个宿主各放一份会让终端/网页各跑一个会话。
//
// ⚠️ 标题栏的按钮**不要**登记进 `WindowBridge.addWindowControl`：那是**主窗口**的原生命中测试
//    （`NativeWindowFilter` 里窗口控件表只有一份，矩形按各自窗口的场景坐标算），登记进来会让
//    主窗口的某个位置莫名变成窗口按钮。本窗口是无边框但**没有**原生命中测试介入，QML 自己收点击。
//
// 状态都由 Shell 推（本文件不存"该不该显示"）：
//   · `sidebarFloating` —— 三点刚"拿起来"，跟着光标走、结局未定；
//   · `sidebarDetached` —— 确认脱离（松手在右栏原区域之外）后才为真。
// 标题栏**一直**都在：窗口比右栏高一条标题栏、位置相应上移（见 Shell.liftSidebarForDrag），
// 于是右栏内容"零跳变"地落进窗口，而"这已经是一扇窗"从拿起来那一刻就看得出来。

import QtQuick
import QtQuick.Window
import ULinHub 1.0

Window {
    id: win

    // 由 Shell 注入：状态与几何都在壳层（本文件只把窗摆出来 + 自己拖/自己缩放）
    required property var host

    // 内容槽：Shell 把 RightSidebar reparent 到这里（自绘标题栏之下）
    readonly property alias panelSlot: slot

    title: qsTrId("right_sidebar_title")
    // 无边框：系统标题栏/边框/按钮全不要，最小化/最大化/关闭与四边缩放都在本文件里自己做
    flags: Qt.Window | Qt.FramelessWindowHint
    // 显示条件：跟着光标走（拿起来）或已确认脱离（独立）。两态都由 Shell 给
    visible: win.host ? win.host.sidebarInWindow : false
    // 窗口底色（标题栏那条与卡片同色）：缩放/重绘的那一瞬间不闪白
    color: Theme.color.panelBg
    minimumWidth: Theme.size.rightSidebarMinWidth
    minimumHeight: Theme.size.titleBarHeight + 200

    // 面板内容的下限（与 Window.minimumWidth 同源）：左/上边缩放时 Qt 一钳最小尺寸，x 就会与
    // 宽度脱钩（左边跟着鼠标走、右边不动）⇒ 这里先自己钳死，绝不让请求越界。
    readonly property int minTotalW: Theme.size.rightSidebarMinWidth
    readonly property int minTotalH: Theme.size.titleBarHeight + 200

    // 最大化态（= 真窗口状态，图标与"能不能拖/缩放"都看它）
    readonly property bool maximized: win.visibility === Window.Maximized
    // 最大化/最小化**之前**的几何：还原时显式写回去（要求里说的"记住还原前的几何"）
    property rect windowedGeometry: Qt.rect(0, 0, 0, 0)
    // 「拿起来」的目标几何：由 Shell 在 prepareForLift 里给（见 normalizeForLift 的说明）
    property rect liftGeometry: Qt.rect(0, 0, 0, 0)

    // 唯一的几何写入口：标题栏拖动、八条缩放热区都走它。
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

    function resizeBegin(sx, sy) {
        win.resizePressScreenX = sx
        win.resizePressScreenY = sy
        win.resizePressGeom = Qt.rect(win.x, win.y, win.width, win.height)
    }

    // edges 用 Qt.LeftEdge / Qt.TopEdge / Qt.RightEdge / Qt.BottomEdge 的位或
    function resizeApply(edges, sx, sy) {
        const dx = sx - win.resizePressScreenX
        const dy = sy - win.resizePressScreenY
        let x = win.resizePressGeom.x
        let y = win.resizePressGeom.y
        let w = win.resizePressGeom.width
        let h = win.resizePressGeom.height

        // 左/上边：动的同时改宽度/高度；到下限就把那条边钉住。
        // ⚠️ 必须自己钳死：请求一旦低于 Qt 的 minimumWidth/Height，Qt 会钳住宽度而 x 照走
        //    ⇒ 左边跟着鼠标、右边不动（看着像"缩放坏了"）。
        if (edges & Qt.LeftEdge) {
            const right = win.resizePressGeom.x + win.resizePressGeom.width
            x = Math.min(win.resizePressGeom.x + dx, right - win.minTotalW)
            w = right - x
        }
        else if (edges & Qt.RightEdge) {
            w = Math.max(win.minTotalW, win.resizePressGeom.width + dx)
        }
        if (edges & Qt.TopEdge) {
            const bottom = win.resizePressGeom.y + win.resizePressGeom.height
            y = Math.min(win.resizePressGeom.y + dy, bottom - win.minTotalH)
            h = bottom - y
        }
        else if (edges & Qt.BottomEdge) {
            h = Math.max(win.minTotalH, win.resizePressGeom.height + dy)
        }

        win.applyGeometry(x, y, w, h)
    }

    Item {
        id: frame
        anchors.fill: parent

        // ---- 自绘标题栏：标题「面板」+ 最小化 / 最大化-还原 / 关闭 ----
        // 尺寸与配色照 skin/window/TitleBar.qml（同一个 metrics token / Theme token 家族）。
        Item {
            id: titleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.size.titleBarHeight

            // 空白处拖动窗口：位移一律用**屏幕**坐标差（窗口自己在动，用局部坐标会自己追自己 ——
            // 与 PanelGrip 里那条踩坑注释同一个道理）。
            MouseArea {
                id: dragArea
                anchors.fill: parent
                enabled: !win.maximized     // 最大化态不拖（要拖先还原，与系统的行为对齐）
                property real pressScreenX: 0
                property real pressScreenY: 0
                property real pressWinX: 0
                property real pressWinY: 0

                onPressed: (mouse) => {
                    const g = dragArea.mapToGlobal(mouse.x, mouse.y)
                    pressScreenX = g.x
                    pressScreenY = g.y
                    pressWinX = win.x
                    pressWinY = win.y
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const g = dragArea.mapToGlobal(mouse.x, mouse.y)
                    // 窗口自己搬自己：把**按下时**的位置 + 屏幕位移写回去（1:1 跟手）
                    win.applyGeometry(pressWinX + (g.x - pressScreenX),
                                      pressWinY + (g.y - pressScreenY),
                                      win.width, win.height)
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.size.titleBarPadLeft
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("right_sidebar_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
                font.weight: Theme.font.weightBold
            }

            // 三个窗口按钮：字形/尺寸/悬停/按下配色逐条照 skin/window/TitleBar.qml
            // （差别只有一处：这里点自己这一份窗口的槽，不走 WindowBridge —— 那是主窗口的单例）
            Row {
                id: buttons
                anchors.right: parent.right
                anchors.rightMargin: Theme.size.windowButtonRightMargin
                anchors.top: parent.top
                anchors.topMargin: Theme.size.windowButtonTopMargin
                spacing: 0

                Repeater {
                    model: ["min", "max", "close"]

                    delegate: Item {
                        id: button

                        required property string modelData
                        readonly property string kind: modelData
                        readonly property bool hovered: mouseArea.containsMouse
                        readonly property bool pressed: mouseArea.pressed

                        implicitWidth: Theme.size.windowButtonWidth
                        implicitHeight: Theme.size.windowButtonHeight

                        // 字形与主窗口一致（Segoe UI Symbol：− / □ / ❐ / ✕）
                        readonly property string glyph: kind === "min"
                            ? "−"
                            : (kind === "close" ? "✕" : (win.maximized ? "❐" : "□"))

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius.small
                            color: {
                                if (button.kind === "close") {
                                    if (button.pressed)
                                        return Theme.color.danger
                                    if (button.hovered)
                                        return Theme.color.dangerButtonBg
                                    return "transparent"
                                }
                                if (button.pressed)
                                    return Theme.color.primaryButtonPressed
                                if (button.hovered)
                                    return Theme.color.captionHoverBg
                                return "transparent"
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: button.glyph
                            font.family: "Segoe UI Symbol"
                            font.pixelSize: Theme.font.glyph
                            color: (button.kind === "close" && (button.hovered || button.pressed))
                                ? Theme.color.textOnAccent
                                : (button.hovered ? Theme.color.textPrimary : Theme.color.textSecondary)
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.ArrowCursor
                            onClicked: {
                                if (button.kind === "min")
                                    win.minimizeWindow()
                                else if (button.kind === "max")
                                    win.toggleMaximize()
                                else
                                    win.closeWindow()
                            }
                        }
                    }
                }
            }

            // 标题栏与面板内容之间的一条分隔线（没有系统边框，靠它把两部分分开）
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.color.border
            }
        }

        // 内容槽：RightSidebar reparent 到这里（`anchors.fill: parent` 于是自动填满槽）
        Item {
            id: slot
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
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

    Component.onCompleted: console.log("[sidebar-detach] window created " + win.width + "x" + win.height
                                       + " at " + win.x + "," + win.y)
}
