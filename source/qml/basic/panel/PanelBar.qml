// 右侧栏（面板坞）：顶栏那颗 ◫ 开关；展开时把「顶栏卡片 + 会话卡片」一起压窄。
// 压窄是布局（顶栏与会话区必须一起变窄），只有父层能推动 ⇒ 由 Shell 驱动（见 panelBarInset）；
// 面板自己只改 x/opacity —— 卡片宽度固定，滑动中改 width 会让里面文字每帧重排。
// 内容用默认属性注入：`PanelBar { 你的面板 {} }`；没注入时显示占位文案。

import QtQuick
import QtQuick.Window
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    Component.onCompleted: HostUi.registerItem("panelBar", root)

    default property alias contentData: body.data   // 内容注入点：外部子项落到 body 里（不是落到卡片根上）
    property string title: qsTrId("panelBar_title")
    property string emptyHint: qsTrId("panelBar_empty_hint")
    signal closeRequested()   // 关闭请求：Shell 接到 Hub.closePanelBar()

    property bool sideDetached: false   // 已脱离成独立副窗口（Shell 推的状态，决定头部按钮与拖动语义）
    property var attachZoneProvider: null   // 回调：返回右栏附着时所在的屏幕矩形，回附落点按它判定

    // 注入内容是否需要按卡片圆角裁掉下方两角（见 bodyArea 里 body 的 layer+mask）。
    // 默认 false：启动器/空态没有铺满的直角内容，不必为它把整块内容栅格化到离屏纹理。
    property bool contentNeedsMask: false

    // 几何的唯一出口（Shell.movePanelBarWindowTo）：由 Shell 在装载时注入函数引用。
    // ⚠️ 不能沿 parent 链现找：脱离后本 Item 被 reparent 进副窗口的 panelSlot，
    // 父链上永远没有壳层 ⇒ 拖动整段被 `if (!owner) return` 吞掉（表现为"完全不动"）。
    property var moveWindowProvider: null

    function moveOwner() {   // 现找几何出口：注入的优先；没有注入时才退回父链（附着态下父链上是壳层，能找得到）
        if (moveWindowProvider)
            return moveWindowProvider
        for (let p = root.parent; p; p = p.parent) {
            if (p.movePanelBarWindowTo)
                return p
        }
        return null
    }
    function moveWindowTo(x, y) {   // 搬窗（只改 x/y，不动尺寸）：两个拖动入口共用这一条
        const owner = moveOwner()
        if (owner) {
            owner.movePanelBarWindowTo(x, y)
            return
        }
        const w = root.Window.window   // 兜底：拿不到壳层就直接写窗口坐标（至少不会"完全不动"）
        if (w) {
            w.x = Math.round(x)
            w.y = Math.round(y)
        }
    }

    // 副窗口态下头部这一行就是副窗口的标题栏：最小化 / 最大化-还原并进这一行、放在 ✕ 左边。
    // 窗口归 Shell 所有，这里只发请求，由它转发给 PanelBarWindow 的 minimizeWindow() / toggleMaximize()。
    signal minimizeWindowRequested()
    signal toggleMaximizeRequested()

    readonly property bool windowMaximized: {   // 副窗口当前是不是最大化态：只用来选字形（□ / ❐），判定就是真窗口状态本身
        const w = root.Window.window
        return w ? w.visibility === Window.Maximized : false
    }

    // 抓手的四条请求全部由 Shell 接住（它才知道窗口与壳层几何）：
    // panelBarLiftRequested —— 附着态越过手感阈值：立刻把右栏拿起来跟手（主窗口布局不动）；
    // panelBarDropRequested —— 松手；panelBarAttachRequested / panelBarPreviewChanged —— 独立态拖回右栏与落点提示。
    signal panelBarLiftRequested()
    signal panelBarDropRequested(real screenX, real screenY)
    signal panelBarAttachRequested()
    signal panelBarPreviewChanged(bool over)   // 拖动中的落点预览开关（拖动期间主窗口收不到 hover，只能由正在拖的抓手推过去）

    // 外框面（B 层可替换单元）：几何归本件，外观走 Style.url
    Item {
        anchors.fill: parent

        Loader {
            anchors.fill: parent
            source: Style.url("skin/panel/PanelSurface.qml")
        }

        Column {
            anchors.fill: parent
            spacing: 0

            Item {
                id: header
                width: parent.width
                height: 44          // 与 TopBar 同高，两条卡片看起来是一套

                // 标题栏「空白处拖动」：按住这一行的空白拖整扇副窗口。
                // ⚠️ 只在副窗口态可用：附着态这一行属于主窗口，打开会变成拖主窗口。
                // ⚠️ 必须声明在最前面：后声明的兄弟画在它上面，三点抓手与三颗按钮的点击热区照常生效。
                MouseArea {
                    id: headerDragArea
                    anchors.fill: parent
                    enabled: root.sideDetached && !root.windowMaximized
                    property real pressScreenX: 0
                    property real pressScreenY: 0
                    property real pressWinX: 0
                    property real pressWinY: 0

                    onPressed: (mouse) => {
                        const win = root.Window.window
                        if (!win)
                            return
                        const g = headerDragArea.mapToGlobal(mouse.x, mouse.y)
                        pressScreenX = g.x
                        pressScreenY = g.y

                        // ⚠️ 读窗口自己的 x/y（绝对真值）：副窗口的几何是延后一帧下发的，
                        // 壳层记着的 panelBarWinX/Y 可能比窗口实际位置旧一拍。
                        pressWinX = win.x
                        pressWinY = win.y
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const g = headerDragArea.mapToGlobal(mouse.x, mouse.y)

                        // 窗口自己搬自己：按下时的窗口位置 + 屏幕位移交给壳层（1:1 跟手）。
                        // ⚠️ 走 root.moveWindowTo：脱离后父链上没有壳层（见 moveWindowProvider），现找会返回 null。
                        root.moveWindowTo(Math.round(pressWinX + (g.x - pressScreenX)), Math.round(pressWinY + (g.y - pressScreenY)))
                    }
                }

                // 扩展停靠位（右栏，登记名 `panelBarDock.extDock`，与 `sessionBar.extDock` 同构）：
                // 一个 Row 位置器，扩展只给宽高、不写 x/y；没有注入时宽度为 0，标题照常占满。
                Row {
                    id: extDock
                    anchors.right: headerButtons.left
                    anchors.rightMargin: Theme.spacing.labelGap
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacing.labelGap
                    Component.onCompleted: HostUi.registerItem("panelBarDock.extDock", extDock)
                }

                Rectangle {   // 标题前的状态点：右栏"工作台感"最小的一个信号（不影响拖拽/标题布局）
                    id: titleDot
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.color.accent
                }

                Text {
                    id: titleText
                    anchors.left: titleDot.right
                    anchors.leftMargin: 7
                    anchors.right: extDock.left
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    color: Theme.color.textPrimary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }

                // 面板的「三个点」抓手（从 PanelBarDockView 的页签条搬来）：声明在后面 ⇒ 画在上层。
                // 位置取整条 header 的水平中线（不掺兄弟元素宽度）；不要再手算 x —— 两套定位会互相打架。
                PanelGrip {
                    id: dockGrip
                    anchors.horizontalCenter: header.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter

                    // 三点常驻：只要右栏卡片在显示就在；拖的是整个右栏，与坞里有没有活动块无关。
                    visible: root.visible
                    enabled: visible

                    // 尺寸沿用 PanelGrip 自己的缺省，这里不赋值：规格只有 widgets/PanelGrip.qml 一份。
                    // ⚠️ 刻意不登记进原生命中测试：原生拖拽只认自绘标题栏（Main.qml 的 WindowBridge.setCaptionItem）。

                    // 阈值：附着态只留 PanelGrip 的 6px 手感阈值（越过立刻拿起来跟手）；
                    // 独立态光标落回右栏原位置 ±24px 即够回附。
                    readonly property real attachSlack: 24
                    property bool armed: false   // 已经越过回附阈值（独立态拖动中实时算，松手时按它决定做什么）
                    property bool dragged: false   // 这一次按下后是否真的拖动过（不能只看 PanelGrip.dragging）

                    // 本次按压是否已交接给壳层（附着态越过阈值 ⇒ 右栏进临时窗口跟手）。
                    // ⚠️ 交接那一瞬间 reparent 会让本 MouseArea 立刻收到 cancel（Qt 必然 un-grab），
                    // 那次不是松手，所以交接后把 finished/cancel 一律丢掉。
                    property bool handedOff: false
                    property real pressWinX: 0   // 独立态搬窗口：按下那一刻窗口在哪（后面用"它 + 屏幕位移"1:1 跟手）
                    property real pressWinY: 0

                    // 「右栏附着时所在的屏幕矩形」：回附方向的落点判定基准。由 Shell 注入，
                    // 现算不缓存（主窗口被搬动/改尺寸是 C++ 侧改的，缓存属性不会重算）。
                    // ⚠️ 必须用 root：写成未定义的 id 会让整张 PanelBar 编译失败、整个窗口空白。
                    function attachZone() {
                        if (!root.attachZoneProvider)
                            return null
                        return root.attachZoneProvider()
                    }
                    function insideAttachZone(gp) {
                        const r = attachZone()
                        if (!r)
                            return false
                        const s = attachSlack
                        return gp.x >= r.x - s && gp.x <= r.x + r.width + s
                               && gp.y >= r.y - s && gp.y <= r.y + r.height + s
                    }

                    onPressStarted: {
                        armed = false
                        dragged = false
                        handedOff = false
                        const win = root.Window.window
                        pressWinX = win ? win.x : 0
                        pressWinY = win ? win.y : 0
                    }

                    onDragMoved: (dx, dy, globalPos, scenePos) => {
                        if (!dockGrip.dragging)
                            return
                        dragged = true

                        if (root.sideDetached) {
                            // 独立态：三个点照样能拖，拖的就是这扇独立窗口。位移用屏幕坐标差加在
                            // 按下时的窗口位置上（窗口自己在动，拿局部坐标算会自己追自己）；几何只走 root.moveWindowTo。
                            try {
                                root.moveWindowTo(Math.round(pressWinX + dx), Math.round(pressWinY + dy))
                            } catch (e) {
                                if (!root.moveWindowErrorLogged) {
                                    root.moveWindowErrorLogged = true
                                    console.log("[panelBar-drag] moveWindowTo 失败（只记一次）: " + e)
                                }
                            }
                            const over = insideAttachZone(globalPos)   // 光标落回右栏原位置附近 ⇒ 亮起落点提示（松手才真的回附）
                            if (over !== armed) {
                                armed = over
                                root.panelBarPreviewChanged(over)
                            }
                            return
                        }

                        // 附着态：越过手感阈值就立刻拿起来 —— 之后这扇窗跟着光标 1:1 走、
                        // 主窗口布局一动不动，松手才定结局（位置由壳层按屏幕坐标差跟，见 Shell.panelBarFollowTick）。
                        if (!handedOff) {
                            handedOff = true
                            root.panelBarLiftRequested()
                        }
                    }
                    onDragFinished: (dx, dy, globalPos) => {
                        const wasArmed = armed
                        armed = false
                        root.panelBarPreviewChanged(false)   // 预览一律先收（松手之后就没有"正在拖"这件事了）
                        const wasDragged = dragged
                        dragged = false
                        if (handedOff) {
                            // 已经交接给壳层：这次 finished/cancel（reparent 引起的那次）不作数，
                            // 收尾由壳层的跟手 Timer（物理按键状态）负责。
                            handedOff = false
                            return
                        }
                        if (!wasDragged)      // 只点击、没拖动：不动任何状态
                            return
                        if (root.sideDetached) {
                            if (wasArmed)
                                root.panelBarAttachRequested()   // 落回右栏位置 ⇒ 重新附着
                        } else {
                            // 附着态没交接就松手 = 没过手感阈值（点了一下）。仍报一次松手，由壳层按落点判断
                            // （它自己会判"没在跟手"直接返回），日后阈值调大也不会漏掉这条线。
                            root.panelBarDropRequested(globalPos.x, globalPos.y)
                        }
                    }
                }

                // 副窗口的两颗窗口按钮（− 最小化 / □❐ 最大化-还原）并进这一行、排在既有的 ✕ 左边，
                // 不另起一条标题栏；外观与缺省尺寸（28×28）照 IconButton，不自己写一份规格。
                Row {
                    id: headerButtons
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    IconButton {   // 最小化：走 Shell → PanelBarWindow.minimizeWindow()（showMinimized）
                        id: minimizeButton
                        visible: root.sideDetached
                        iconSource: "qrc:/ULinHub/icons/ui/window-minimize.svg"
                        iconColor: Theme.color.textSecondary
                        iconSize: 12
                        onClicked: root.minimizeWindowRequested()
                    }

                    IconButton {   // 最大化/还原：走 Shell → PanelBarWindow.toggleMaximize()
                        id: maximizeButton
                        visible: root.sideDetached
                        iconSource: root.windowMaximized
                            ? "qrc:/ULinHub/icons/ui/window-restore.svg"
                            : "qrc:/ULinHub/icons/ui/window-maximize.svg"
                        iconColor: Theme.color.textSecondary
                        iconSize: 12
                        onClicked: root.toggleMaximizeRequested()
                    }

                    // 面板自己的 ✕：语义不变（附着态收起右栏；副窗口态回附 + 关窗，走 Shell.onCloseRequested）
                    IconButton {
                        id: closeButton
                        iconSource: "qrc:/ULinHub/icons/ui/window-close.svg"
                iconColor: Theme.color.textSecondary
                iconSize: 12
                        tooltip: qsTrId("common_close")
                        onClicked: root.closeRequested()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.color.border
            }

            Item {
                id: bodyArea
                width: parent.width
                height: parent.height - header.height - 1

                // 注入点自己是空的：占位文案靠 body.children.length 判断（所以占位不能放进 body）。
                // ⚠️ 这里要按卡片圆角裁一刀：终端/网页块是直角的 QtWebEngineView，直接铺到底会盖掉
                // 卡片下方两角；mask 要把整块内容栅格化进离屏纹理（常驻开销），只在确实开着块时打开。
                Item {
                    id: body
                    anchors.fill: parent

                    layer.enabled: root.contentNeedsMask
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: bodyMask
                    }
                }

                // 遮罩源：白 = 保留、透明 = 裁掉。visible: false + layer.enabled 是 MultiEffect 取源的固定用法；
                // layer.enabled 与 body 同一条开关，不裁的时候连这张遮罩纹理也不必存在。
                Item {
                    id: bodyMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: root.contentNeedsMask

                    Rectangle {
                        anchors.fill: parent
                        color: "white"
                        bottomLeftRadius: Theme.radius.panel
                        bottomRightRadius: Theme.radius.panel
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacing.columnClearance
                    visible: body.children.length === 0
                    text: root.emptyHint
                    color: Theme.color.textTertiary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    lineHeight: 1.4
                }
            }
        }
    }
}
