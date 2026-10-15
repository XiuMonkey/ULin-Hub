// 壳层布局：卡片式（与旧壳一致）——
//   左边一张侧栏卡片，右边一列：顶栏卡片 + 会话卡片；卡片之间留 panelGap、与窗口之间留 windowMargin。
// 旧壳的对应关系：`#sidebar`/`#topBar` 的 radius 12 + `ShadowPanel` 的 CardShadow{8,2,12}；
// 这里由 components/Card.qml 的 MultiEffect 阴影承担，尺寸/颜色全取 token。
// 这一层只做装配与遮罩/Toast 宿主，具体内容都在被 Style.url() 装载的单元里（可被风格包覆盖）。

import QtQuick
import QtQuick.Window
import ULinHub 1.0
import "../../widgets"
// ui/shell 与 ui/sidebar、ui/topbar 是并列目录：QML 只按同目录/已 import 的目录解析组件名，
// 所以跨目录用裸组件名（Sidebar / TopBar）必须显式 import
import "../sidebar"
import "../topbar"
import "../browser"

Item {
    id: shell

    // 扩展注入点：扩展用 HostUi.item("shell") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("shell", this)

    // 供子界面报一句轻提示（未落地的入口用它，别放哑按钮）
    function notify(text) {
        toast.show(text)
    }

    // ---------------- 块脱离 / 回附（独立窗口）----------------
    // 状态在 common/shell/PanelDockModel（QML 面是单例 Dock），窗口对象在这里按 Dock.detachedBlocks
    // 一扇一块地建；本层只做"把壳层的坐标与右栏几何喂给它"这种接线。
    //
    // 落点提示的矩形与开关：矩形在**拖动开始时**算一次（拖动期间右栏可能已经收起，每帧重算会指错地方）；
    // 开关由正在拖的那个独立窗口推过来（拖动中主窗口收不到 hover，只能这样推）。
    property rect dropZone: Qt.rect(0, 0, 0, 0)
    property bool dropHintActive: false

    // 右栏（面板坞）在**本窗口场景坐标**里的矩形 —— 回附方向（独立窗口拖回来）的判定基准，
    // 也是落点提示要画的矩形。脱离方向的判定已搬进抓手自己（RightSidebar 里按屏幕坐标算），
    // 这里只剩"要画在哪"这一个用点。
    function dockSceneRect() {
        const tl = rightSidebar.mapToGlobal(0, 0)
        const win = rightSidebar.Window.window
        const origin = win ? win.mapToGlobal(0, 0) : Qt.point(0, 0)
        return Qt.rect(tl.x - origin.x, tl.y - origin.y, rightSidebar.width, rightSidebar.height)
    }

    // 抓手正在拖（此时落点就是右栏自己）：提示跟着亮起来当"松手就不会跑"的反馈。
    // 原来由坞里那只抓手的 `detachDragPreview` 推过来；抓手搬到右栏标题行后，这条由
    // "抓手往单例 Dock 发的预览开关"推（见右栏下面那段 Connections）。
    function beginDetachPreview(over) {
        dropZone = dockSceneRect()
        dropHintActive = over
    }
    function endDetachPreview() {
        dropHintActive = false
        dropZone = Qt.rect(0, 0, 0, 0)
    }
    // 右栏标题行那三个点抓手把块拖出去了：在**光标处**建独立窗口（需求 ②）。
    // ⚠️ atScreenX/Y 是**屏幕坐标**（抓手在 RightSidebar 里算的：跨窗口判断只能用屏幕坐标）。
    //    独立窗口的 x/y 本来就是屏幕坐标，所以这里不再做 scene/global 的来回换算 —— 搬家前那版
    //    把屏幕坐标先 mapFromItem 成场景坐标再当窗口位置用，只在"窗口锚在场景原点"这一条上碰巧等价。
    // 几何：宽取右栏宽度与 480 逻辑像素里更宽的那个、高与右栏等高（首次脱离的默认值；之后再脱离沿用上次的）。
    function detachActiveBlock(blockId, atScreenX, atScreenY) {
        const idx = Dock.activeIndex
        if (idx < 0)
            return
        // 拖动的这段时间里活动块可能已经换了（或块被关了）⇒ 只认抓的那个块
        if (Dock.activeBlockId !== blockId)
            return
        const r = dockSceneRect()
        const w = Math.max(r.width, 480)
        const h = Math.max(r.height, 240)
        Dock.detach(idx)
        Dock.setDetachedGeometry(blockId, Math.round(atScreenX - w / 2 + Theme.spacing.windowMargin),
                                 Math.round(atScreenY - 20), Math.round(w), Math.round(h))
        // 脱离 = 这个块有自己的窗口了：右栏收起（留着的话同一块在两处画两份）。
        // 块本身仍是"已附着 + 活动"（Dock 里没动它），所以拖回去就回到原位。
        Hub.closeRightSidebar()
        console.log("[detach] detach block idx=" + idx + " id=" + blockId + " at "
                    + Math.round(atScreenX) + "," + Math.round(atScreenY) + " " + w + "x" + h)
    }

    // ---- 右侧栏：宽度可拖（拖左边缘），压窄量 = 宽度 + 间距 ----
    // 这是本文件里唯一被动画驱动的量：它一变，顶栏卡片与会话卡片**同时**变窄，
    // 消息列表也跟着重新换行 —— 也就是"顶栏和聊天区被压缩"的那段动态。
    // 宽度归壳层所有（面板只管画卡片）：拖柄改的是 rightSidebarW，默认值来自 token。
    // 正在拖：拖动期间必须**关掉**压窄动画，否则布局会追着光标慢慢爬
    property bool resizing: false
    // 钳制策略（会话区保底 / 上限 / 夹取 / 拖动换算）全在 C++：公式在
    // source/src/common/layout/SidebarMetrics.cpp（自由函数、可脱离 QML 单测），
    // QML 面是单例 `SidebarLayout`（source/src/qmlbridge/SidebarLayout.cpp，只转发）。
    // 这里只负责把 token 与窗口尺寸喂进去，不再自己算公式
    // （那几条都是"纯数值规则"，放 C++ 才能脱离窗口验证；拖动仍是 1:1，公式逐位等价）。
    readonly property real rightSidebarMinW: Theme.size.rightSidebarMinWidth
    // 会话区保底宽度：**按窗口比例兜底**（取 token 与 25% 窗口宽里更小的那个）。
    // 为什么不能死守 token 的 320：窗口不大时，固定 240 侧栏 + 320 之后留给右侧栏的额度
    // 可能比默认宽度 360 还小 ⇒ 面板一开局就顶在上限、只能往右（变窄）拖，往左没反应。
    readonly property real rightSidebarConvMin: SidebarLayout.conversationMinWidth(
        width, Theme.size.minConversationWidth)
    readonly property real rightSidebarMaxW: SidebarLayout.maxWidth(width, rightSidebarMinW, rightSidebarConvMin,
        Theme.spacing.windowMargin, Theme.size.sidebarWidth, Theme.spacing.panelGap)
    // 用户宽度（默认值也不能超过上限，否则同上）
    property real rightSidebarW: Math.min(Theme.size.rightSidebarWidth, rightSidebarMaxW)
    // 生效宽度：用户值先按上下限夹一次（窗口变小、或被拖到极端都不会越界）
    readonly property real rightSidebarEffW: SidebarLayout.clampWidth(
        rightSidebarW, rightSidebarMinW, rightSidebarMaxW)
    readonly property real rightSidebarFull: rightSidebarEffW + Theme.spacing.panelGap
    property real rightInset: Hub.rightSidebarOpen ? rightSidebarFull : 0
    // 缓动取 OutCubic（不做过冲）：压窄是"腾地方"，回弹会让人以为界面抖了一下。
    // ⚠️ 拖拽时 enabled=false：宽度是用户手指的位置，不能再去补间。
    Behavior on rightInset {
        enabled: !shell.resizing
        NumberAnimation {
            duration: Theme.motion.normal
            easing.type: Easing.OutCubic
        }
    }
    // 归一化进度（0 = 收起，1 = 完全展开）：面板的位移/淡入跟着它走
    readonly property real rightSidebarT: rightSidebarFull > 0
        ? Math.min(1, rightInset / rightSidebarFull) : 0

    // 拖拽换算：按下时的光标窗口 x / 当时宽度 + 当前光标窗口 x ⇒ 新宽度。
    // 公式在 C++（SidebarLayout.widthFromDrag），这里只把壳层的上下限喂进去；保留这层包装是因为
    // 上下限是壳层的属性，调用点（拖柄）不该自己知道它们。
    // 往左拖 ⇒ cursorX < pressX ⇒ 宽度变大。
    function widthFromDrag(pressX, pressWidth, cursorX) {
        return SidebarLayout.widthFromDrag(pressX, pressWidth, cursorX, rightSidebarMinW, rightSidebarMaxW)
    }
    // 夹取入口（拖柄调它；QML 外部/扩展要设宽度也走它）
    function setRightSidebarWidth(w) {
        rightSidebarW = SidebarLayout.clampWidth(w, rightSidebarMinW, rightSidebarMaxW)
    }

    // ---- 左侧：侧栏卡片 ----
    Card {
        id: sidebarCard
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.windowMargin
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset
        width: Theme.size.sidebarWidth
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true
        // 与旧壳对齐：sidebar.qss 的 `#sidebar` 只有 background + border-radius，**没有描边**；
        // 只有 `#topBar` 有 `border: 1px solid {{border}}`。少画这一条，壁纸皮肤下才不会多出一圈线。
        bordered: false

        Sidebar {
            anchors.fill: parent
            onNotify: shell.notify(text)
        }
    }

    // ---- 右侧：顶栏卡片 + 会话卡片 ----
    Item {
        id: rightColumn
        anchors.left: sidebarCard.right
        anchors.leftMargin: Theme.spacing.panelGap
        anchors.right: parent.right
        // 右边距跟着右侧栏的展开量走 ⇒ 顶栏卡片与会话卡片一起被压窄（动画由 rightInset 的 Behavior 提供）
        anchors.rightMargin: Theme.spacing.windowMargin + shell.rightInset
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset

        Card {
            id: topBarCard
            // 与输入区对齐：Composer 是 columnClearance 内缩的（见下面 composerLoader），
            // 顶栏卡片不缩就会比输入框宽出一圈，看着和其它控件不协调。
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacing.columnClearance
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.top: parent.top
            implicitHeight: topBar.implicitHeight
            radius: Theme.radius.panel
            padding: 0
            shadowEnabled: true

            TopBar {
                id: topBar
                anchors.fill: parent
                onToolsFilterRequested: Hub.openToolsFilter()
                // ◫ 的三态状态机（需求 ⑤）：
                //   ① 有"已独立且窗口还显示"的块 ⇒ 这一次点击是**关掉那些独立窗口**
                //      （块回到"已附着但隐藏"：模型里 detached 状态与几何都留着，右栏不开）；
                //   ② 否则走原来的开合（本轮刚关完独立窗口 ⇒ rightSidebarOpen 是 false ⇒
                //      "默认以附着状态打开"，正是需求要的第三态）。
                onRightSidebarRequested: {
                    if (Dock.anyDetachedVisible)
                        Dock.closeDetachedWindows()
                    else
                        Hub.toggleRightSidebar()
                }
            }
        }

        // 会话区：**不要圆角卡片背景**（圆角只给消息自己的气泡），所以这里用普通 Item 而非 Card
        Item {
            id: conversationCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBarCard.bottom
            // 与旧壳同值：顶栏卡片与会话卡片之间由阴影留白 + 这个间距共同决定
            anchors.topMargin: Theme.shadow.topBarGap
            anchors.bottom: parent.bottom

            // 选中会话时是消息列表 + 输入区，没选会话时给引导
            Loader {
                id: composerLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.spacing.columnClearance
                anchors.rightMargin: Theme.spacing.columnClearance
                anchors.bottomMargin: Theme.spacing.columnClearance
                height: item ? item.implicitHeight : 0
                visible: Hub.currentSessionId.length > 0
                source: Style.url("basic/chat/Composer.qml")
            }

            // 提问 / 审批面板（M4b）：排在输入区上方。
            // 旧壳是把它插进消息列表里（MessageHost::handleMuxFrame → InteractionHandler 建面板），
            // QML 侧放在输入区上方更稳：不用改消息模型的角色，也不会打乱列表的滚动与高度。
            Loader {
                id: interactionLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: composerLoader.top
                anchors.bottomMargin: Theme.spacing.columnClearance
                anchors.leftMargin: Theme.spacing.columnClearance
                anchors.rightMargin: Theme.spacing.columnClearance
                height: item ? item.implicitHeight : 0
                active: Hub.questionActive || Hub.approvalActive
                source: Style.url("basic/chat/InteractionPanel.qml")
            }

            Loader {
                id: messageLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: interactionLoader.top
                anchors.bottomMargin: Theme.spacing.labelGap
                visible: Hub.currentSessionId.length > 0
                // ⚠️ 必须等宽度定下来再创建列表：首帧宽度≈0 时委托里的 Text 会按假宽度排版
                //    （行数翻几倍），量出的 contentHeight 是最终的 ~3 倍 → 滑块长度先长后短，
                //    表现就是"一启动滚动条乱跳、然后稳定"（实测 contentH 2400 → 903）。
                active: width > 200
                source: Style.url("basic/chat/MessageListView.qml")
            }

            ConversationPlaceholder {
                anchors.fill: parent
                visible: Hub.currentSessionId.length === 0
            }
        }
    }

    // 右侧栏（面板坞）：顶栏 ◫ 开关。
    // 从窗口右缘外滑进来 + 淡入，位置与透明度都跟 shell.rightSidebarT 走（同一条动画曲线，
    // 所以"面板滑入"和"顶栏/会话区被压窄"是同一段动态，不会互相错拍）。
    // 只改 x/opacity、不改 width：滑动期间面板内部不重排。
    RightSidebar {
        id: rightSidebar

        visible: shell.rightSidebarT > 0.001
        opacity: shell.rightSidebarT
        width: shell.rightSidebarEffW
        anchors.top: sidebarCard.top
        anchors.bottom: sidebarCard.bottom
        x: shell.width - Theme.spacing.windowMargin - width
           + (1 - shell.rightSidebarT) * (width + Theme.spacing.windowMargin)
        onCloseRequested: Hub.closeRightSidebar()

        // 右栏内容：面板坞（主界面 + 块/页签；网页块内部才用 WebEngine）
        PanelDock {
            id: dock
            anchors.fill: parent
        }
    }

    // ---- 抓手的脱离接线：两条广播都落在壳层（见 PanelDockModel 的 requestDetach / setDetachPreview）----
    // 抓手在 RightSidebar 的标题行里，够不着 PanelDock 的内部信号 —— 所以它把请求发给单例 Dock，
    // 这里接住：提示由壳层画（DockDropHint）、独立窗口也由壳层建（下面的 Repeater）。
    // ⚠️ 两条都挂在 shell 上（不是挂在某个面板实例上）：抓手换了宿主，这段接线不该跟着换。
    Connections {
        target: Dock

        // 拖动中：拖出右栏（>48px）或水平位移 >60px ⇒ 亮起落点提示。
        // 开关就是抓手算出来的那个布尔量，这里不再由壳层叠一条"正在拖"的状态：
        // 抓手在右栏里按下时本来就在落点矩形内，overTarget=false ⇒ 提示不亮，
        // 与搬家前同一个观感（拖动前不亮、拖出去才亮）；叠一条反而会让提示一开始就亮着。
        function onDetachPreviewChanged(overTarget) {
            if (overTarget)
                shell.beginDetachPreview(overTarget)
            else
                shell.endDetachPreview()
        }

        // 松手且够阈值：在光标处把活动块脱离成独立窗口
        function onDetachRequested(blockId, atScreenX, atScreenY) {
            shell.detachActiveBlock(blockId, atScreenX, atScreenY)
        }
    }

    // ---- 独立窗口：一块一扇窗（需求 ②③④）----
    // 清单来自模型（Dock.detachedBlocks）：哪一项没了窗口就跟着销毁、visible=false 就只是隐藏
    // （"已附着但隐藏"，需求 ⑤ 的第二态）。系统装饰窗口自带四边缩放与最小化/最大化按钮。
    // geometry 由窗口自己回写模型，下一次脱离沿用它 ⇒ "拖回去再拖出来"不会跳回原位。
    Repeater {
        model: Dock.detachedBlocks

        delegate: DetachedBlockWindow {
            required property var modelData
            entry: modelData
            mainWindow: shell.Window.window
            host: shell
        }
    }

    // ---- 拖回时的落点提示：画在原右栏位置（需求：建议给落点提示）----
    // z 抬到面板之上：拖动期间块还在独立窗口里，主窗口这边只有这块轮廓要露出来给人看。
    // `fromDock: true` = 提示文案用"松手：块留在这里"那一档（坞里/右栏标题行的抓手往外拖）；
    // 回附方向（独立窗口往主窗口拖）由 DetachedBlockWindow 直接推 dropHintActive，
    // 它推的时候本项是 false ⇒ 文案是"松手：重新附着回右栏"。
    DockDropHint {
        id: dropHint
        z: 30
        zone: shell.dropZone
        active: shell.dropHintActive
        fromDock: true
    }

    // 右侧栏宽度拖柄：贴在面板左缘的一条 10px 热区（平时完全透明，悬停才露一条灰边）。
    // 为什么放在 Shell 而不是面板里：宽度是壳层布局的一部分（压窄量 = 宽度 + 间距），
    // 面板自己只管画卡片；拖柄改的是 shell.rightSidebarW。
    //
    // ⚠️ 两条踩过的坑（都在真机拖拽日志里实测到，症状都是"只能往右拖一点、往左没反应"）：
    //   ① 可见性/可用性**绝对不能挂在 rightSidebarT 上**：宽度一变，rightSidebarFull 先更新、
    //      rightInset 差一拍，T 会瞬时掉到 302/311≈0.97 ⇒ visible 抖成 false ⇒ MouseArea 抓取
    //      被取消，后续 move 全丢（日志：[drag-ev] move … → [drag-ev] canceled）。
    //      所以这里只认 Hub.rightSidebarOpen 这个稳定布尔量。
    //   ② 拖动期间位置要**冻结**：拖柄跟着面板走 = 在光标底下位移，同样会打断抓取。
    //      空闲时才贴面板左缘（frozenX 在按下那一刻算好）。
    Item {
        id: resizeHandle

        anchors.top: rightSidebar.top
        anchors.bottom: rightSidebar.bottom
        width: 10
        visible: Hub.rightSidebarOpen
        enabled: visible
        // 按下时冻结的那个 x（见 ②）
        property real frozenX: 0
        x: shell.resizing ? frozenX : rightSidebar.x - width / 2

        // 灰边：⚠️ 画在**面板内侧**、上下各让开一个圆角半径 ——
        // 贴左缘画满全高的话，两端会越过圆角弧线（被指出过）；颜色也只用灰色系（原来用蓝色被否了）。
        // 拖动期间不画：拖柄是冻住的，画出来会停在旧位置误导人；此时"面板边自己在动"就是反馈。
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: parent.width / 2 + 1
            width: 2
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.radius.panel + 4
            anchors.bottomMargin: Theme.radius.panel + 4
            radius: 1
            color: Theme.color.textTertiary
            opacity: (shell.resizing || !resizeMouse.containsMouse) ? 0 : 0.55
            Behavior on opacity {
                NumberAnimation { duration: Theme.motion.fast }
            }
        }

        MouseArea {
            id: resizeMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor

            // 用**窗口坐标**记录按下点：拖柄自己会随宽度移动，用相对位移会自我参照
            property real pressWindowX: 0
            property real pressWidth: 0

            onPressed: (mouse) => {
                pressWindowX = resizeHandle.mapToItem(null, mouse.x, 0).x
                pressWidth = shell.rightSidebarEffW
                // 先冻结位置再置 resizing：顺序反了的话第一拍就会按"贴边"算一次 x
                resizeHandle.frozenX = resizeHandle.x
                shell.resizing = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                // 换算全在 widthFromDrag 里（往左拖 = 变宽），这里只负责把光标位置喂进去
                shell.setRightSidebarWidth(shell.widthFromDrag(
                    pressWindowX, pressWidth, resizeHandle.mapToItem(null, mouse.x, 0).x))
            }
            onReleased: shell.resizing = false
            onCanceled: shell.resizing = false
        }
    }

    // 服务端还没连上时的遮罩：不挡住操作，只提示状态
    Rectangle {
        anchors.fill: parent
        visible: Hub.initializing
        color: Theme.color.overlayBg
        z: 10

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacing.sectionGap

            BusySpinner {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32
                height: 32
                color: Theme.color.textOnAccent
                // ⚠️ 必须门控：BusySpinner.running 默认 true，而 RotationAnimator 是无限循环。
                // 只靠父级 visible 是不够的 —— 不可见的 item 上的动画照样每帧改属性、
                // 把场景标脏，于是应用**空闲时也在满速重绘**（实测：空闲 5.4% 单核 + GPU 3D 25~29%，
                // 换成壁纸皮肤后每帧还要多跑两个全窗离屏层，移动窗口就卡）。
                // Sidebar / PluginMarketPage 里的同类转圈都传了 running，这里漏了。
                running: Hub.initializing
                visible: Hub.initializing
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Hub.statusText
                color: Theme.color.textOnAccent
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3 * Theme.spacing.cardPadding
        z: 20
    }

    // 插件市场（M6）：覆盖整壳，侧栏 ◧ 打开。
    // 三块整壳覆盖页统一走 PageOverlay —— 遮罩 + 从**点击处**长出来的卡片 + 收回播完才卸载，
    // 于是"点侧栏那颗图标"与"卡片从哪儿长出来"天然对得上（页面自己不用接线）。
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.plugins.open
        source: Style.url("basic/plugins/PluginMarketPage.qml")
    }

    // 扩展管理：覆盖整壳，侧栏扩展按钮打开（工具 / 客户端 / Qt Quick 运行时三种来源）
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.extensions.open
        source: Style.url("basic/extensions/ExtensionManagerPage.qml")
    }

    // 主题切换过渡卡片（原版 #themeSwitchBody）
    Loader {
        anchors.fill: parent
        active: Hub.themeSwitching
        visible: Hub.themeSwitching
        source: Style.url("skin/shell/ThemeSwitchOverlay.qml")
        z: 40
    }

    // 设置页（模型 / API key）：覆盖整壳，侧栏 ⚙ 打开
    PageOverlay {
        anchors.fill: parent
        z: 30
        shown: Hub.settings.open
        source: Style.url("basic/settings/SettingsPage.qml")
    }

    // 工具过滤面板：顶栏 ☰ 打开，覆盖整壳 —— 与另外三个覆盖页**完全同一套** PageOverlay：
    // 遮罩只淡入（不缩放）、卡片从**点击处**长出来、收回播完才卸载。
    // （面板自身已改成"固定尺寸卡片 + 不自带遮罩"，见 ToolsFilterPanel.qml 里的说明。）
    PageOverlay {
        anchors.fill: parent
        z: 36
        shown: Hub.toolsFilterOpen
        source: Style.url("basic/chat/ToolsFilterPanel.qml")

        // 面板自己读 Hub.toolsFilter（import ULinHub 1.0），这里只转它的关闭请求
        onPageLoaded: (item) => {
            if (item && item.closeRequested)
                item.closeRequested.connect(Hub.closeToolsFilter)
        }
    }
}
