// 右侧栏（面板坞）：顶栏那颗 ◫ 开关；展开时把「顶栏卡片 + 会话卡片」一起压窄。
// 为什么"压窄"由 Shell 驱动、面板自己只做位移 + 淡入（见 Shell.qml 的 panelBarInset）：
// · 压窄是**布局**（顶栏与会话区必须一起变窄），只有父层能推动；
// · 面板是固定宽的卡片，滑动期间若改自己的 width，里面的文字每帧都要重排（白花 CPU 且会闪），
// 只改 x/opacity 的话每帧就一次平移。
// 内容用默认属性注入：`PanelBar { 你的面板 {} }`；没注入时显示占位文案。

import QtQuick
import QtQuick.Window
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    default property alias contentData: body.data   // 内容注入点：外部子项落到 body 里（不是落到卡片根上）
    property string title: qsTrId("panelBar_title")   // 头部标题
    property string emptyHint: qsTrId("panelBar_empty_hint")   // 没有注入内容时的占位
    signal closeRequested()   // 关闭请求：Shell 接到 Hub.closePanelBar()

    property bool sideDetached: false
    property var attachZoneProvider: null

    // 注入内容是否需要"按卡片圆角裁下方两角"（见 bodyArea 里 body 的 layer+mask）。
    // 默认 false = 不裁：启动器/空态没有铺满的直角内容，没必要为它把整块内容栅格化到离屏
    // 纹理。Shell 按面板坞状态（有没有开着块）喂进来，见 Shell.qml 的 PanelBar 实例化处。
    property bool contentNeedsMask: false

    // 几何的唯一出口（Shell.movePanelBarWindowTo）——**由 Shell 注入的函数引用**。
    // ⚠️ 为什么必须注入、不能沿 parent 链现找：本 Item 在脱离时被 reparent 进副窗口的
    // panelSlot，parent 链变成 `panelSlot → 窗口 contentItem → QQuickRootItem` ——
    // 于是"父链查找"永远返回 null ⇒ 按住三个点拖动时整段逻辑被 `if (!owner) return` 吞掉，
    // 表现就是"完全不动"。函数引用由 Shell 在装载时给（那时还能看见 shell），
    // reparent 之后照样可调（与 attachZoneProvider 同一个道理）。
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

    // 副窗口态下头部那一行**就是**副窗口的标题栏（Shell 那边不再画标题栏）：所以窗口的
    // 最小化 / 最大化-还原两颗按钮就并进这一行，放在既有的 ✕ **左边**（用户原话：
    // "你给另外两个按钮放在删除按钮旁边不就行了，为什么要多加个标题栏呢"）。
    // 实现不在这里 —— 窗口归 Shell 所有，这里只发请求，由 Shell 转发给 PanelBarWindow 的
    // minimizeWindow() / toggleMaximize()（绝不在这里复制一份窗口操作）。
    signal minimizeWindowRequested()
    signal toggleMaximizeRequested()

    readonly property bool windowMaximized: {   // 副窗口当前是不是最大化态：只用来选字形（□ / ❐），判定就是真窗口状态本身
        const w = root.Window.window
        return w ? w.visibility === Window.Maximized : false
    }

    // 抓手的四条请求，全部由 Shell 接住（它才知道窗口与壳层几何）：
    // · panelBarLiftRequested —— 附着态下三点越过手感阈值：**现在**就把右栏"拿起来"
    // （进无边框临时窗口跟手，需求 ①）。跟手期间主窗口布局一动不动（壳层保证）；
    // · panelBarDropRequested —— 松手（壳层按落点决定回附还是独立）；
    // · panelBarAttachRequested / panelBarPreviewChanged —— 独立态：拖回右栏 + 落点提示。
    signal panelBarLiftRequested()
    signal panelBarDropRequested(real screenX, real screenY)
    signal panelBarAttachRequested()
    signal panelBarPreviewChanged(bool over)   // 拖动中的落点预览开关（拖动期间主窗口收不到 hover，只能由正在拖的抓手推过去）

    Card {
        anchors.fill: parent
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true

        Column {
            anchors.fill: parent
            spacing: 0

            Item {
                id: header
                width: parent.width
                height: 44          // 与 TopBar 同高，两条卡片看起来是一套

                // 标题栏的那条"空白处拖动"：按住这一行的空白拖整扇副窗口。
                // ⚠️ 只在副窗口态（root.sideDetached）可用：附着态这一行属于**主窗口**，
                // 打开就会变成"拖主窗口"（罪状级 bug）。
                // ⚠️ 必须声明在最前面：后声明的兄弟在上面 —— 三点抓手的拖拽、三颗按钮的点击
                // 拖动热区上、照样能拖是同一个道理：没有 MouseArea 的 Text 不吃鼠标事件）。
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

                        // 窗口自己搬自己：把**按下时**的位置 + 屏幕位移交给壳层（1:1 跟手）。
                        // ⚠️ 走 root.moveWindowTo（现注入的几何出口）：脱离之后 parent 链上
                        // **没有**壳层（见 root.moveWindowProvider 的说明），父链现找会返回 null
                        // ⇒ 这条"拖面板头部搬窗"的路会整段静默失效。
                        root.moveWindowTo(Math.round(pressWinX + (g.x - pressScreenX)),
                                          Math.round(pressWinY + (g.y - pressScreenY)))
                    }
                }

                // 扩展停靠位（右栏）—— 扩展自绘的入口注入到这里，登记名 `panelBarDock.extDock`。
                // 与 `sessionBar.extDock` 同构：一个 **Row（位置器）** ⇒ 扩展 QML 只给宽高、不写 x/y/anchors；
                // 没有扩展注入时宽度为 0，标题照常占满。这里只做"提供一个位置"，不加任何方法。
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
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }

                // 面板的「三个点」抓手：从 PanelBarDockView.qml 的页签条搬到这里（需求 ②）。
                // **声明在后面 ⇒ 画在上层**，把只有 ~30px 宽的三点整个盖住了（用户看不见三点）。
                // 标题目右边的空地**没有别的子项**，天然不会再被谁盖住。
                // 位置取**标题行正中间**（用户两次反馈的落点：
                // ② "还是不够中间啊，要在正中间" ⇒ 就是整条 header 的水平中线，不再相对左右元素取中点）。
                // 所以用 `anchors.horizontalCenter: header.horizontalCenter`（锚在头部 Item 自己身上，
                // 不掺任何兄弟元素的宽度）；`x` 不再手算 —— 两套定位同时留会互相打架。
                // ⚠️ 不要退回"贴着标题右边"：那正是用户否掉的位置（看着像贴着 ✕ 那一侧）。
                PanelGrip {
                    id: dockGrip
                    anchors.horizontalCenter: header.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter

                    // 三点**常驻**（需求 ①）：只要右栏这张卡片在显示，三个点就在。
                    // "按块脱离"时代的可见性（坞里没块就没得拖）—— 现在拖的是**整个右栏**，
                    // 与坞里有没有活动块无关，所以整条去掉（`enabled: visible` 照旧）。
                    visible: root.visible
                    enabled: visible

                    // 尺寸用 PanelGrip 自己的缺省（4 / 间距 5，与 PageOverlay 那颗一致），这里**不赋值**：
                    // 要改大小就改 widgets/PanelGrip.qml 的缺省（那是这个控件的规格，只有一份）。
                    // ⚠️ 这里刻意**不**登记进原生命中测试（nativeHost 保持缺省 false）：
                    // 原生只在 `m_captionItem`（自绘标题栏，Main.qml 的 WindowBridge.setCaptionItem）

                    // 阈值：附着态只留"手感阈值"（PanelGrip 的 6px，越过就
                    // **立刻拿起来**跟手，见下）；独立态"光标落回右栏原位置 ±24px ⇒ 够回附"。
                    readonly property real attachSlack: 24
                    property bool armed: false   // 已经越过回附阈值（独立态拖动中实时算，松手时按它决定做什么）
                    property bool dragged: false   // 这一次按下之后真的拖动过吗？⚠️ 不能看 PanelGrip.dragging ——

                    // 这一次按压是否已经"交接"给壳层（附着态越过阈值 ⇒ 右栏进临时窗口跟手）。
                    // ⚠️ 交接那一瞬间 reparent 会让本 MouseArea **立刻**收到 cancel（item 换窗口时
                    // Qt 必然 un-grab），那次 cancel 不是松手 —— 不能让它去定结局（否则松手前
                    // 就把面板定成回附/独立了）。所以交接后把 finished/cancel 一律丢掉。
                    property bool handedOff: false
                    property real pressWinX: 0   // 独立态搬窗口：按下那一刻窗口在哪（后面用"它 + 屏幕位移"1:1 跟手）
                    property real pressWinY: 0

                    // 「右栏附着时所在的屏幕矩形」：回附方向的落点判定基准。
                    // 由 Shell 注入（它才知道主窗口与壳层几何）；**现算**而不是缓存成属性 ——
                    // 主窗口被搬动/改尺寸后属性绑定不会重算（那是 C++ 侧改的窗口坐标）。
                    // ⚠️ 这里必须用 root（本文件根 id）：上一轮从 PanelBarDockView.qml 抄过来时写的是 `dock`，
                    // 未定义 ⇒ 整张 PanelBar 编译失败 ⇒ Shell 报 "Type PanelBar unavailable"，
                    // 表现是**整个窗口空白**（踩过一次）。
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
                            // 独立态：三个点**照样能拖**（需求要保留）—— 拖的就是那扇独立窗口。
                            // 位移用**屏幕坐标差**加在按下时的窗口位置上：窗口自己在动，
                            // 拿局部坐标算会自己追自己。
                            // ⚠️ 几何只走一个出口：root.moveWindowTo → Shell.movePanelBarWindowTo
                            // （不直接写 win.x/win.y，避免绕开壳层的记录）。
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

                        // 附着态：越过手感阈值 ⇒ **立刻拿起来**（需求 ①）。
                        // 之后这扇窗跟着光标 1:1 走、主窗口布局一动不动，松手才定结局；
                        // 位置由壳层按"屏幕坐标差"跟（见 Shell.panelBarFollowTick）。
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
                            // 附着态没交接就松手 = 没过手感阈值（点了一下）⇒ 什么都不做。
                            // 这里仍然报一次"松手"，壳层按落点决定（它自己会判"没在跟手"直接返回），
                            // 免得日后有人把阈值调大后忘了这条线。
                            root.panelBarDropRequested(globalPos.x, globalPos.y)
                        }
                    }
                }

                // 副窗口的三颗窗口按钮（− 最小化 / □❐ 最大化-还原 / ✕ 关闭）：**不另起一条标题栏**，
                // 就并进这一行，两颗新按钮在既有的 ✕ **左边**（用户原话见 root 上的说明）。
                // 外观逐条照同一个头里的 ✕ —— 同一个 IconButton、同样的缺省尺寸（28×28）与
                // 悬停/按下配色，不自己写一份规格。
                Row {
                    id: headerButtons
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    IconButton {   // 最小化：走 Shell → PanelBarWindow.minimizeWindow()（showMinimized）
                        id: minimizeButton
                        visible: root.sideDetached
                        glyph: "−"
                        onClicked: root.minimizeWindowRequested()
                    }

                    IconButton {   // 最大化/还原：走 Shell → PanelBarWindow.toggleMaximize()
                        id: maximizeButton
                        visible: root.sideDetached
                        glyph: root.windowMaximized ? "❐" : "□"
                        onClicked: root.toggleMaximizeRequested()
                    }

                    // 面板自己的 ✕：语义**不变**（附着态 = 收起右栏；副窗口态 = 回附 + 关窗，
                    // 走 Shell.onCloseRequested），这里只是从"单独一个"挪进 Row 里排队
                    IconButton {
                        id: closeButton
                        glyph: "✕"
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

                // 注入点自己是空的：占位文案靠 body.children.length 判断（所以占位不能放进 body）
                // ⚠️ 这里要**按卡片圆角裁一刀**：终端/网页块是 QtWebEngineView（直角矩形），直接铺到底
                // 会把卡片下方两角盖掉；用 MultiEffect 的 mask 裁出下方两角。mask 要把整块内容栅格化进
                // 离屏纹理（常驻开销），所以只由 Shell 喂 contentNeedsMask 在"确实开着块"时打开。
                Item {
                    id: body
                    anchors.fill: parent

                    layer.enabled: root.contentNeedsMask
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: bodyMask
                    }
                }

                // 遮罩源：白 = 保留、透明 = 裁掉。visible: false + layer.enabled 是 MultiEffect
                // 取源的固定用法（它要的是一张纹理，不是屏幕上的可见项）。
                // layer.enabled 跟 body 同一条开关：不裁的时候连这张遮罩纹理也不必存在。
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
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    lineHeight: 1.4
                }
            }
        }
    }
}
