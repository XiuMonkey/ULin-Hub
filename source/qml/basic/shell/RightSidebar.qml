// 右侧栏（面板坞）：顶栏那颗 ◫ 开关；展开时把「顶栏卡片 + 会话卡片」一起压窄。
//
// 为什么"压窄"由 Shell 驱动、面板自己只做位移 + 淡入（见 Shell.qml 的 rightInset）：
//   · 压窄是**布局**（顶栏与会话区必须一起变窄），只有父层能推动；
//   · 面板是固定宽的卡片，滑动期间若改自己的 width，里面的文字每帧都要重排（白花 CPU 且会闪），
//     只改 x/opacity 的话每帧就一次平移。
//
// 内容用默认属性注入：`RightSidebar { 你的面板 {} }`；没注入时显示占位文案。
// 后续要挂的东西（见 build/temp/dsh-official/GAP-REPORT.md）：终端 / 工作区文件 / 文档预览 / 轨迹。

import QtQuick
import QtQuick.Window
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 内容注入点：外部子项落到 body 里（不是落到卡片根上）
    default property alias contentData: body.data
    // 头部标题
    property string title: qsTrId("right_sidebar_title")
    // 没有注入内容时的占位
    property string emptyHint: qsTrId("right_sidebar_empty_hint")
    // 关闭请求：Shell 接到 Hub.closeRightSidebar()
    signal closeRequested()

    // ---------------- 整个右栏脱离成独立窗口（本次的驱动路径）----------------
    // 状态在 Shell（它拥有独立窗口），这里只拿两条注入进来的事实：
    //   · sideDetached —— **确认脱离之后**为真（本 Item 此刻在独立窗口里，reparent 过去了，
    //     见 Shell.liftSidebarForDrag / finishSidebarDrag）。三个点于是改成"搬那扇窗 + 落回右栏位置就回附"；
    //   · attachZoneProvider —— 取"主窗口里右栏附着时所在的**屏幕矩形**"的函数。
    //     跨窗口判断只能用屏幕坐标（各自窗口的场景原点不是一个）。
    //     ⚠️ 用函数而不是 rect 属性：主窗口被搬动/改尺寸后属性绑定不会重算，拖动那一刻现算才是真的。
    property bool sideDetached: false
    property var attachZoneProvider: null
    // 几何的唯一出口（Shell.moveSidebarWindowTo）——**由 Shell 注入的函数引用**。
    // ⚠️ 为什么必须注入、不能沿 parent 链现找：本 Item 在脱离时被 reparent 进副窗口的
    //    panelSlot，parent 链变成 `panelSlot → 窗口 contentItem → QQuickRootItem` ——
    //    **壳层不在链上**（实测打印出来的链里一个带 moveSidebarWindowTo 的都没有），
    //    于是"父链查找"永远返回 null ⇒ 按住三个点拖动时整段逻辑被 `if (!owner) return` 吞掉，
    //    表现就是"完全不动"。函数引用由 Shell 在装载时给（那时还能看见 shell），
    //    reparent 之后照样可调（与 attachZoneProvider 同一个道理）。
    property var moveWindowProvider: null

    // 现找几何出口：注入的优先；没有注入时才退回父链（附着态下父链上是壳层，能找得到）
    function moveOwner() {
        if (moveWindowProvider)
            return moveWindowProvider
        for (let p = root.parent; p; p = p.parent) {
            if (p.moveSidebarWindowTo)
                return p
        }
        return null
    }
    // 搬窗（只改 x/y，不动尺寸）：两个拖动入口共用这一条
    function moveWindowTo(x, y) {
        const owner = moveOwner()
        if (owner) {
            owner.moveSidebarWindowTo(x, y)
            return
        }
        // 兜底：拿不到壳层就直接写窗口坐标（等价于 HEAD 那版的行为，至少不会"完全不动"）
        const w = root.Window.window
        if (w) {
            w.x = Math.round(x)
            w.y = Math.round(y)
        }
    }

    // 副窗口态下头部那一行**就是**副窗口的标题栏（Shell 那边不再画标题栏）：所以窗口的
    // 最小化 / 最大化-还原两颗按钮就并进这一行，放在既有的 ✕ **左边**（用户原话：
    // "你给另外两个按钮放在删除按钮旁边不就行了，为什么要多加个标题栏呢"）。
    // 实现不在这里 —— 窗口归 Shell 所有，这里只发请求，由 Shell 转发给 SidebarWindow 的
    // minimizeWindow() / toggleMaximize()（绝不在这里复制一份窗口操作）。
    signal minimizeWindowRequested()
    signal toggleMaximizeRequested()

    // 副窗口当前是不是最大化态：只用来选字形（□ / ❐），判定就是真窗口状态本身
    readonly property bool windowMaximized: {
        const w = root.Window.window
        return w ? w.visibility === Window.Maximized : false
    }

    // 抓手的四条请求，全部由 Shell 接住（它才知道窗口与壳层几何）：
    //   · sidebarLiftRequested —— 附着态下三点越过手感阈值：**现在**就把右栏"拿起来"
    //     （进无边框临时窗口跟手，需求 ①）。跟手期间主窗口布局一动不动（壳层保证）；
    //   · sidebarDropRequested —— 松手（壳层按落点决定回附还是独立）；
    //   · sidebarAttachRequested / sidebarPreviewChanged —— 独立态：拖回右栏 + 落点提示。
    signal sidebarLiftRequested()
    signal sidebarDropRequested(real screenX, real screenY)
    signal sidebarAttachRequested()
    // 拖动中的落点预览开关（拖动期间主窗口收不到 hover，只能由正在拖的抓手推过去）
    signal sidebarPreviewChanged(bool over)

    Card {
        anchors.fill: parent
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true

        Column {
            anchors.fill: parent
            spacing: 0

            // ---- 头部：标题 + 抓手 + 副窗口的 − / □❐ / ✕ ----
            // 副窗口（无边框）**没有**自己的标题栏：这一行就是它的标题栏（拖动 + 三颗窗口按钮）。
            Item {
                id: header
                width: parent.width
                height: 44          // 与 TopBar 同高，两条卡片看起来是一套

                // 标题栏的那条"空白处拖动"：按住这一行的空白拖整扇副窗口。
                // ⚠️ 只在副窗口态（root.sideDetached）可用：附着态这一行属于**主窗口**，
                //    打开就会变成"拖主窗口"（罪状级 bug）。
                // ⚠️ 必须声明在最前面：后声明的兄弟在上面 —— 三点抓手的拖拽、三颗按钮的点击
                //    都在它之上，不会被这条整行热区截胡（与原先自绘标题栏里 Text 压在
                //    拖动热区上、照样能拖是同一个道理：没有 MouseArea 的 Text 不吃鼠标事件）。
                // 位移一律用**屏幕坐标差**：窗口自己在动，用局部坐标会自己追自己（同 PanelGrip 那条坑）。
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
                        //    壳层记着的 sidebarWinX/Y 可能比窗口实际位置旧一拍。
                        pressWinX = win.x
                        pressWinY = win.y
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const g = headerDragArea.mapToGlobal(mouse.x, mouse.y)
                        // 窗口自己搬自己：把**按下时**的位置 + 屏幕位移交给壳层（1:1 跟手）。
                        // ⚠️ 走 root.moveWindowTo（现注入的几何出口）：脱离之后 parent 链上
                        //    **没有**壳层（见 root.moveWindowProvider 的说明），父链现找会返回 null
                        //    ⇒ 这条"拖面板头部搬窗"的路会整段静默失效。
                        root.moveWindowTo(Math.round(pressWinX + (g.x - pressScreenX)),
                                          Math.round(pressWinY + (g.y - pressScreenY)))
                    }
                }

                Text {
                    id: titleText
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing.columnClearance
                    anchors.right: headerButtons.left
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }

                // 面板的「三个点」抓手：从 PanelDock.qml 的页签条搬到这里（需求 ②）。
                //
                // 为什么搬：原来它锚在页签条 `⌂` 右边 6px，而页签条 Flickable 锚在 `⌂` 右边 4px、
                // **声明在后面 ⇒ 画在上层**，把只有 ~30px 宽的三点整个盖住了（用户看不见三点）。
                // 标题目右边的空地**没有别的子项**，天然不会再被谁盖住。
                //
                // 位置取**标题行正中间**（用户两次反馈的落点：
                //   ① "还是在最右边而不是中间" ⇒ 先改成"标题右缘 ↔ ✕ 左缘"的中点；
                //   ② "还是不够中间啊，要在正中间" ⇒ 就是整条 header 的水平中线，不再相对左右元素取中点）。
                // 所以用 `anchors.horizontalCenter: header.horizontalCenter`（锚在头部 Item 自己身上，
                // 不掺任何兄弟元素的宽度）；`x` 不再手算 —— 两套定位同时留会互相打架。
                // ⚠️ 不要退回"贴着标题右边"：那正是用户否掉的位置（看着像贴着 ✕ 那一侧）。
                PanelGrip {
                    id: dockGrip
                    anchors.horizontalCenter: header.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    // 三点**常驻**（需求 ①）：只要右栏这张卡片在显示，三个点就在。
                    // 上一版那条 `!Dock.homeVisible && Dock.activeIndex >= 0 && !activeDetached` 是
                    // "按块脱离"时代的可见性（坞里没块就没得拖）—— 现在拖的是**整个右栏**，
                    // 与坞里有没有活动块无关，所以整条去掉（`enabled: visible` 照旧）。
                    visible: root.visible
                    enabled: visible
                    // 尺寸用 PanelGrip 自己的缺省（4 / 间距 5，与 PageOverlay 那颗一致），这里**不赋值**：
                    // 曾经在这里赋 readonly 属性 ⇒ 整张右栏实例化失败 ⇒ 壳层什么都画不出来（空白窗口）。
                    // 要改大小就改 widgets/PanelGrip.qml 的缺省（那是这个控件的规格，只有一份）。
                    // ⚠️ 这里刻意**不**登记进原生命中测试（nativeHost 保持缺省 false）：
                    //    原生只在 `m_captionItem`（自绘标题栏，Main.qml 的 WindowBridge.setCaptionItem）
                    //    的矩形里才判 HTCAPTION，而右栏标题行在 caption 矩形之下、收到的本来就是
                    //    普通客户端事件（同一个 header 里的 ✕ 按钮一直就是这么收点击的）。
                    //    在无边框窗口里登记是为了抢在标题栏之前，这里没有要抢的东西。

                    // 阈值（沿用搬家前的数值）：附着态只留"手感阈值"（PanelGrip 的 6px，越过就
                    // **立刻拿起来**跟手，见下）；独立态"光标落回右栏原位置 ±24px ⇒ 够回附"。
                    readonly property real attachSlack: 24
                    // 已经越过回附阈值（独立态拖动中实时算，松手时按它决定做什么）
                    property bool armed: false
                    // 这一次按下之后真的拖动过吗？⚠️ 不能看 PanelGrip.dragging ——
                    // 它发 dragFinished 之前**先把 dragging 置 false**，这边读到的一定是 false。
                    property bool dragged: false
                    // 这一次按压是否已经"交接"给壳层（附着态越过阈值 ⇒ 右栏进临时窗口跟手）。
                    // ⚠️ 交接那一瞬间 reparent 会让本 MouseArea **立刻**收到 cancel（item 换窗口时
                    //    Qt 必然 un-grab），那次 cancel 不是松手 —— 不能让它去定结局（否则松手前
                    //    就把面板定成回附/独立了）。所以交接后把 finished/cancel 一律丢掉。
                    property bool handedOff: false
                    // 独立态搬窗口：按下那一刻窗口在哪（后面用"它 + 屏幕位移"1:1 跟手）
                    property real pressWinX: 0
                    property real pressWinY: 0

                    // 「右栏附着时所在的屏幕矩形」：回附方向的落点判定基准。
                    // 由 Shell 注入（它才知道主窗口与壳层几何）；**现算**而不是缓存成属性 ——
                    // 主窗口被搬动/改尺寸后属性绑定不会重算（那是 C++ 侧改的窗口坐标）。
                    // ⚠️ 这里必须用 root（本文件根 id）：上一轮从 PanelDock.qml 抄过来时写的是 `dock`，
                    //    未定义 ⇒ 整张 RightSidebar 编译失败 ⇒ Shell 报 "Type RightSidebar unavailable"，
                    //    表现是**整个窗口空白**（踩过一次）。
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
                        // 没过 PanelGrip 的手感阈值（6px）之前什么都不做：那只是"点了一下"
                        if (!dockGrip.dragging)
                            return
                        dragged = true

                        if (root.sideDetached) {
                            // 独立态：三个点**照样能拖**（需求要保留）—— 拖的就是那扇独立窗口。
                            // 位移用**屏幕坐标差**加在按下时的窗口位置上：窗口自己在动，
                            // 拿局部坐标算会自己追自己。
                            // ⚠️ 几何只走一个出口：root.moveWindowTo → Shell.moveSidebarWindowTo
                            //    （不直接写 win.x/win.y，避免绕开壳层的记录）。
                            // ⚠️ 这里曾经写成 `owner = findShell()` —— 那个函数**不存在**，每次
                            //    dragMoved 都抛 `ReferenceError: findShell is not defined` ⇒ 副窗口态
                            //    按住三个点**完全不动**；改成"沿 parent 链现找"也不行：reparent 之后
                            //    父链上是 `panelSlot → 窗口 contentItem → QQuickRootItem`，
                            //    **壳层不在链上**（实测）。所以出口由 Shell 注入（见 root.moveWindowProvider）。
                            // ⚠️ 拖动路径上**不许打日志**：每次 move 都拼字符串 + 写盘，
                            //    实测会把拖动变成幻灯片（这里曾经有 4 条 console.log）。
                            // 出错只记一次（拖动中的重复日志本身就是卡顿源）。
                            try {
                                root.moveWindowTo(Math.round(pressWinX + dx), Math.round(pressWinY + dy))
                            } catch (e) {
                                if (!root.moveWindowErrorLogged) {
                                    root.moveWindowErrorLogged = true
                                    console.log("[sidebar-drag] moveWindowTo 失败（只记一次）: " + e)
                                }
                            }
                            // 光标落回右栏原位置附近 ⇒ 亮起落点提示（松手才真的回附）
                            const over = insideAttachZone(globalPos)
                            if (over !== armed) {
                                armed = over
                                root.sidebarPreviewChanged(over)
                            }
                            return
                        }

                        // 附着态：越过手感阈值 ⇒ **立刻拿起来**（需求 ①）。
                        // 之后这扇窗跟着光标 1:1 走、主窗口布局一动不动，松手才定结局；
                        // 位置由壳层按"屏幕坐标差"跟（见 Shell.sidebarFollowTick）。
                        if (!handedOff) {
                            handedOff = true
                            root.sidebarLiftRequested()
                        }
                    }
                    onDragFinished: (dx, dy, globalPos) => {
                        const wasArmed = armed
                        armed = false
                        // 预览一律先收（松手之后就没有"正在拖"这件事了）
                        root.sidebarPreviewChanged(false)
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
                                root.sidebarAttachRequested()   // 落回右栏位置 ⇒ 重新附着
                        } else {
                            // 附着态没交接就松手 = 没过手感阈值（点了一下）⇒ 什么都不做。
                            // 这里仍然报一次"松手"，壳层按落点决定（它自己会判"没在跟手"直接返回），
                            // 免得日后有人把阈值调大后忘了这条线。
                            root.sidebarDropRequested(globalPos.x, globalPos.y)
                        }
                    }
                }

                // 副窗口的三颗窗口按钮（− 最小化 / □❐ 最大化-还原 / ✕ 关闭）：**不另起一条标题栏**，
                // 就并进这一行，两颗新按钮在既有的 ✕ **左边**（用户原话见 root 上的说明）。
                // 外观逐条照同一个头里的 ✕ —— 同一个 IconButton、同样的缺省尺寸（28×28）与
                // 悬停/按下配色，不自己写一份规格。
                // ⚠️ 两颗新按钮**只在副窗口存在时可见**（root.sideDetached）：附着态显示它们既是
                //    没用的按钮，也会让这一行凭空多出两颗图标。
                //    Row 会把 visible:false 的子项整个排掉（不占位）⇒ 附着态这一行的宽度、
                //    ✕ 的位置与标题的可用宽度与改动前逐像素一致（已用探针验证过）。
                Row {
                    id: headerButtons
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    // 最小化：走 Shell → SidebarWindow.minimizeWindow()（showMinimized）
                    IconButton {
                        id: minimizeButton
                        visible: root.sideDetached
                        glyph: "−"
                        onClicked: root.minimizeWindowRequested()
                    }

                    // 最大化/还原：走 Shell → SidebarWindow.toggleMaximize()
                    IconButton {
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

            // ---- 内容区：注入点 + 空态 ----
            Item {
                id: bodyArea
                width: parent.width
                height: parent.height - header.height

                // 注入点自己是空的：占位文案靠 body.children.length 判断（所以占位不能放进 body）
                //
                // ⚠️ 这里要**按卡片圆角裁一刀**：终端/网页块是 QtWebEngineView，本身就是个**直角**
                //    矩形 ✗ —— 直接铺到底会把卡片的圆角盖掉（现象：打开终端或浏览器时，右栏底部
                //    没有圆角）。用 MultiEffect 的 mask 把注入内容裁成"下方两角 = 卡片圆角"；
                //    上方顶着标题行，本来就不在圆角处，所以不裁上边。
                Item {
                    id: body
                    anchors.fill: parent

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: bodyMask
                    }
                }

                // 遮罩源：白 = 保留、透明 = 裁掉。visible: false + layer.enabled 是 MultiEffect
                // 取源的固定用法（它要的是一张纹理，不是屏幕上的可见项）。
                Item {
                    id: bodyMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true

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
