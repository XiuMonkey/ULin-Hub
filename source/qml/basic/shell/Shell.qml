// 壳层布局：左侧栏卡片，右侧顶栏卡片 + 会话卡片；卡片间留 panelGap，窗口边留 windowMargin。
// 本层只做装配与遮罩/Toast 宿主，具体内容由 Style.url() 装载。

import QtQuick
import QtQuick.Window
import QtQml.Models
import ULinHub 1.0
import "../../widgets"
// 跨目录使用裸组件名必须显式 import。
import "../session"
import "../topbar"
import "../browser"
import "../panel"

Item {
    id: shell

    Component.onCompleted: HostUi.registerItem("shell", this)   // 扩展注入点

    function notify(text) {
        toast.show(text)
    }

    // 落点提示：矩形在拖动开始时算一次，开关由正在拖动的窗口推过来。
    property rect dropZone: Qt.rect(0, 0, 0, 0)
    property bool dropHintActive: false

    // 右栏整体：拖出后回附或脱离。内容不是副本，PanelBar 实例在槽位与独立窗口之间 reparent，
    // 避免终端/网页各跑一份会话。时序：越过手感阈值先进入无边框临时窗口并 1:1 跟手，
    // 此时不改主窗口布局；松手落在原区域则回附，落在区域外才确认脱离并腾出空间。
    property bool panelBarDetached: false
    // 独立窗口几何统一经 applyPanelBarWindowGeometry 设置。
    property real panelBarWinX: 0
    property real panelBarWinY: 0
    property real panelBarWinW: 0
    property real panelBarWinH: 0
    property bool panelBarWinApplying: false   // 程序化摆窗口期间不回写状态，防止回环

    property bool panelBarFloating: false   // 拿起后的临时窗口状态，尚未决定回附或脱离
    property real floatOriginX: 0   // 跟手基准：拿起时的光标屏幕坐标
    property real floatOriginY: 0
    property real floatWinX: 0      // 跟手基准：拿起时的窗口位置
    property real floatWinY: 0
    readonly property int dropSlack: 24   // 松手落点容差
    readonly property bool panelBarInWindow: panelBarDetached || panelBarFloating

    // 用打开状态的几何现算右栏场景矩形，避免收起/脱离后读不到它应在的位置。
    function panelBarSceneRect() {
        return Qt.rect(shell.width - Theme.spacing.windowMargin - shell.panelBarEffW,
                       panelBarSlot.y, shell.panelBarEffW, panelBarSlot.height)
    }
    // 跨窗口判定只能用屏幕坐标；每次需要时现算，主窗口移动后绑定不会自动重算。
    function panelBarScreenRect() {
        const xOpen = shell.width - Theme.spacing.windowMargin - shell.panelBarEffW
        const p = shell.mapToGlobal(xOpen, panelBarSlot.y)
        return Qt.rect(p.x, p.y, shell.panelBarEffW, panelBarSlot.height)
    }

    function insidePanelBarZone(p) {
        const r = panelBarScreenRect()
        return p.x >= r.x - dropSlack && p.x <= r.x + r.width + dropSlack
               && p.y >= r.y - dropSlack && p.y <= r.y + r.height + dropSlack
    }

    function applyPanelBarWindowGeometry(x, y, w, h) {
        panelBarWinApplying = true
        panelBarWinX = Math.round(x)
        panelBarWinY = Math.round(y)
        panelBarWinW = Math.round(w)
        panelBarWinH = Math.round(h)
        panelBarWindow.x = panelBarWinX
        panelBarWindow.y = panelBarWinY
        panelBarWindow.width = panelBarWinW
        panelBarWindow.height = panelBarWinH
        panelBarWinApplying = false
    }
    // 独立窗口 move/resize 回写口（PanelBarWindow 调用）
    function setPanelBarWindowGeometry(x, y, w, h) {
        if (panelBarWinApplying)
            return
        panelBarWinX = x
        panelBarWinY = y
        panelBarWinW = w
        panelBarWinH = h
    }

    // 拿起右栏：搬进无边框临时窗口并 1:1 跟手，期间不碰主窗口布局。
    function liftPanelBarForDrag() {
        if (panelBarInWindow)
            return
        // 窗口几何与右栏矩形逐像素一致，内容零跳变地落进新窗口。
        const r = panelBarScreenRect()
        const w = Math.max(Math.round(r.width), Theme.size.panelBarMinWidth)
        const h = Math.max(Math.round(r.height), 240)
        const x = Math.round(r.x)
        const y = Math.round(r.y)

        panelBarWindow.prepareForLift(x, y, w, h)
        // ⚠️ 必须在 reparent 前置位：换宿主会让抓手的 MouseArea 丢掉抓取。
        panelBarFloating = true
        panelBarWindow.normalizeForLift()   // 清掉上一次可能残留的最大化/最小化
        applyPanelBarWindowGeometry(x, y, w, h)

        const p = WindowBridge.cursorScreenPos()
        floatOriginX = p.x
        floatOriginY = p.y
        floatWinX = x
        floatWinY = y
        beginDetachPreview(false)
        panelBar.parent = panelBarWindow.panelSlot
        console.log("[panelBar-drag] lift win " + w + "x" + h + " @" + x + "," + y
                    + " cursor " + Math.round(p.x) + "," + Math.round(p.y))
    }

    // 跟手到光标屏幕位置：窗口位置 = 拿起时的位置 + 光标位移。
    function panelBarFollowTo(screenX, screenY) {
        if (!panelBarFloating)
            return
        applyPanelBarWindowGeometry(floatWinX + (screenX - floatOriginX),
                                   floatWinY + (screenY - floatOriginY),
                                   panelBarWinW, panelBarWinH)
        if (dropZone.width <= 0 || dropZone.height <= 0)
            dropZone = panelBarSceneRect()
        dropHintActive = insidePanelBarZone(Qt.point(screenX, screenY))
    }

    // 每帧按光标跟一次，并按物理左键状态收尾；跟手期间副窗口收不到鼠标事件。
    function panelBarFollowTick() {
        if (!panelBarFloating)
            return
        const p = WindowBridge.cursorScreenPos()
        panelBarFollowTo(p.x, p.y)
        if (!WindowBridge.leftButtonDown())
            finishPanelBarDrag(p.x, p.y)
    }

    // 松手：落回原区域则回附，否则确认脱离并腾出主窗口空间。
    function finishPanelBarDrag(atScreenX, atScreenY) {
        if (!panelBarFloating)
            return
        const over = insidePanelBarZone(Qt.point(atScreenX, atScreenY))
        if (over) {
            panelBar.parent = panelBarSlot
            panelBarFloating = false
            console.log("[panelBar-drag] drop in zone → 回附（布局未动过）")
        }
        else {
            panelBarDetached = true
            panelBarFloating = false
            Hub.closePanelBar()
            console.log("[panelBar-drag] drop outside → 独立窗口 @" + panelBarWindow.x + "," + panelBarWindow.y
                        + " " + panelBarWindow.width + "x" + panelBarWindow.height)
        }
        endDetachPreview()
    }

    // 回附：内容搬回槽位并关闭独立窗口；open 控制回附后是否展开右栏。
    function attachPanelBar(open) {
        if (!panelBarDetached)
            return
        if (panelBarWindow.visibility === Window.Minimized)
            panelBarWindow.showNormal()
        panelBarDetached = false
        panelBar.parent = panelBarSlot
        if (open)
            Hub.openPanelBar()
        else
            Hub.closePanelBar()
        console.log("[panelBar-detach] attach open=" + open)
    }

    // 窗口按钮的转发口；具体实现只在 PanelBarWindow 里。
    function minimizePanelBarWindow() {
        if (panelBarDetached)
            panelBarWindow.minimizeWindow()
    }
    function toggleMaximizePanelBarWindow() {
        if (panelBarDetached)
            panelBarWindow.toggleMaximize()
    }

    // 抓手拖动时点亮落点提示；矩形按右栏附着时的几何现算。
    function beginDetachPreview(over) {
        dropZone = panelBarSceneRect()
        dropHintActive = over
    }
    function endDetachPreview() {
        dropHintActive = false
        dropZone = Qt.rect(0, 0, 0, 0)
    }
    // 跟手期间副窗口收不到鼠标事件，只能定时向系统询问光标位置与左键状态。
    Timer {
        id: panelBarFollowTimer
        interval: 8
        repeat: true
        running: shell.panelBarFloating
        onTriggered: shell.panelBarFollowTick()
    }

    // 右侧栏宽度可拖；压窄量 = 宽度 + 间距，宽度归壳层所有。
    property bool resizing: false   // 拖动期间关闭压窄动画，避免布局追着光标爬

    // 宽度公式只做四则运算，不要取整：拖动需要逐像素跟手。
    function conversationMinWidth(shellWidth, tokenMinConversationWidth) {
        return Math.min(tokenMinConversationWidth, shellWidth * 0.25)
    }
    // 上限不低于最小宽度，保证窗口极小时夹取仍能收敛。
    function maxWidth(shellWidth, minWidth, convMin, windowMargin, sessionBarWidth, panelGap) {
        return Math.max(minWidth, shellWidth - 2 * windowMargin - sessionBarWidth - panelGap - convMin)
    }
    function clampWidth(w, minW, maxW) {
        return Math.max(minW, Math.min(w, maxW))
    }
    readonly property real panelBarMinW: Theme.size.panelBarMinWidth
    readonly property real panelBarConvMin: conversationMinWidth(width, Theme.size.minConversationWidth)
    readonly property real panelBarMaxW: maxWidth(width, panelBarMinW, panelBarConvMin,
        Theme.spacing.windowMargin, Theme.size.sessionBarWidth, Theme.spacing.panelGap)
    property real panelBarW: Math.min(Theme.size.sessionBarWidth, panelBarMaxW)
    readonly property real panelBarEffW: clampWidth(panelBarW, panelBarMinW, panelBarMaxW)
    readonly property real panelBarFull: panelBarEffW + Theme.spacing.panelGap
    readonly property real panelBarTargetInset: Hub.panelBarOpen ? panelBarFull : 0
    property real panelBarInset: panelBarTargetInset

    // 压窄用 OutCubic 不做过冲；拖拽时禁用补间，宽度必须跟手。
    Behavior on panelBarInset {
        enabled: !shell.resizing
        NumberAnimation {
            duration: Theme.motion.normal
            easing.type: Easing.OutCubic
        }
    }
    // 右栏开合期间内容列持续变宽，阴影与列表定位需要按停；阈值取半像素。
    readonly property bool panelBarAnimating: !shell.resizing
        && Math.abs(panelBarInset - panelBarTargetInset) > 0.5

    // 内容列宽度按 15px 分步跟随右栏；贴底阅读时逐帧跟随，把 contentY 锁在底部。
    property real panelBarReflowStep: 15
    property real panelBarContentInset: 0
    function messageListAtBottom() {
        const lv = messageLoader.item
        return !!(lv && lv.followBottom === true)
    }
    function syncPanelBarContentInset() {
        if (shell.resizing || messageListAtBottom()) {
            shell.panelBarContentInset = shell.panelBarInset
            return
        }
        if (Math.abs(shell.panelBarInset - shell.panelBarContentInset) >= shell.panelBarReflowStep)
            shell.panelBarContentInset = shell.panelBarInset
    }
    onPanelBarInsetChanged: {
        syncPanelBarContentInset()
        if (!shell.panelBarAnimating)   // 补间结束，补齐不足一档的余量
            shell.panelBarContentInset = shell.panelBarTargetInset
    }
    // 内容列宽度正在被驱动：开合补间、拖右栏宽度或左栏收起展开。
    readonly property bool contentWidthDriven: shell.resizing || shell.panelBarAnimating
        || shell.sessionBarAnimating

    readonly property real panelBarT: panelBarFull > 0   // 0 = 收起，1 = 完全展开
        ? Math.min(1, panelBarInset / panelBarFull) : 0

    // 往左拖时 cursorX < pressX，宽度变大；夹取后的结果由 setPanelBarWidth 写回。
    function widthFromDrag(pressX, pressWidth, cursorX) {
        return clampWidth(pressWidth - (cursorX - pressX), panelBarMinW, panelBarMaxW)
    }
    function setPanelBarWidth(w) {
        panelBarW = clampWidth(w, panelBarMinW, panelBarMaxW)
    }

    // 左栏开合：卡片向窗口左缘外滑动并淡出，内容列同步收下让出的宽度。
    property bool sessionBarCollapsed: false
    function setSessionBarCollapsed(collapsed) {
        sessionBarCollapsed = collapsed
    }
    readonly property real sessionBarFull: Theme.size.sessionBarWidth + Theme.spacing.panelGap
    readonly property real sessionBarTargetInset: sessionBarCollapsed ? sessionBarFull : 0
    property real sessionBarInset: sessionBarTargetInset
    Behavior on sessionBarInset {
        NumberAnimation {
            duration: Theme.motion.normal
            easing.type: Easing.OutCubic
        }
    }
    readonly property bool sessionBarAnimating: Math.abs(sessionBarInset - sessionBarTargetInset) > 0.5
    // ⚠️ 与右栏方向相反，必须取反；直接除会让启动时左栏整块不可见。
    readonly property real sessionBarT: sessionBarFull > 0
        ? 1 - Math.min(1, sessionBarInset / sessionBarFull) : 1
    property real sessionBarContentInset: 0
    function syncSessionBarContentInset() {
        if (Math.abs(sessionBarInset - sessionBarContentInset) >= panelBarReflowStep)
            sessionBarContentInset = sessionBarInset
    }
    onSessionBarInsetChanged: {
        syncSessionBarContentInset()
        if (!sessionBarAnimating)   // 补间结束，补齐不足一档的余量
            sessionBarContentInset = sessionBarTargetInset
    }

    // 左侧侧栏卡片
    Card {
        id: sessionBarCard
        // T = 1 贴左留白；T = 0 时右缘正好落在窗口左缘。
        x: Theme.spacing.windowMargin
           - (1 - shell.sessionBarT) * (Theme.size.sessionBarWidth + Theme.spacing.windowMargin)
        width: Theme.size.sessionBarWidth
        opacity: shell.sessionBarT
        visible: shell.sessionBarT > 0.001   // 收起后退出命中测试与绘制
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true
        // 删除按钮/确认卡是侧栏内的浮层；侧栏本身默认 z=0，会被右侧输入卡盖住。
        // 出现待删除项时把整张侧栏抬到内容区之上，确认卡才能跨出侧栏边界显示。
        z: (sessionBarView.pendingId.length > 0 || sessionBarView.confirmOpen) ? 50 : 0
        color: Theme.color.sessionBarBg || Theme.color.cardBg   // 与内容面区分一档
        bordered: false   // 原版侧栏没有描边

        SessionBar {
            id: sessionBarView
            anchors.fill: parent
            onNotify: shell.notify(text)
            onCollapseRequested: shell.setSessionBarCollapsed(true)
        }
    }

    // 输入区最大宽度取左栏展开时的列宽：左栏收起后内容列变宽，输入框不应跟着无限拉长。
    readonly property real conversationInputMaxWidth: Math.max(
        Theme.size.minConversationWidth,
        contentColumn.width - shell.sessionBarContentInset
            - 2 * Theme.spacing.columnClearance)

    // 右侧顶栏卡片 + 会话卡片
    Item {
        id: contentColumn
        // 不锚在侧栏卡片右缘上，避免侧栏滑动时内容列逐帧重排。
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.windowMargin + Theme.size.sessionBarWidth
                          + Theme.spacing.panelGap - shell.sessionBarContentInset
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.windowMargin + shell.panelBarContentInset
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset

        Card {
            id: topBarCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacing.columnClearance
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.top: parent.top
            implicitHeight: topBar.implicitHeight
            radius: Theme.radius.panel
            padding: 0
            shadowEnabled: !shell.contentWidthDriven   // 宽度动画期间按停阴影

            TopBar {
                id: topBar
                anchors.fill: parent
                widthAnimating: shell.contentWidthDriven   // 避免叠第二段高度/位置补间
                sessionBarCollapsed: shell.sessionBarCollapsed
                onSessionBarRequested: shell.setSessionBarCollapsed(false)
                // Hub 属性缺失时用空值兜底，保证老宿主也能正常显示。
                peerTabs: Hub.peerTabs || []
                peerRootId: Hub.peerRootId || ""
                peerRootTitle: Hub.peerRootTitle || ""
                subagentCount: Hub.subagentCount || 0
                subagentRunning: Hub.subagentRunning === true
                jobItems: Hub.jobs || []
                jobCount: Hub.jobCount || 0
                jobRunning: Hub.jobRunning === true
                onPeerSelected: (sessionId) => Hub.selectSession(sessionId)
                onToolsFilterRequested: Hub.openToolsFilter()
                // 副窗口存在时先关闭；否则走正常开合。关闭后再次点击会附着展开。
                onPanelBarRequested: {
                    if (shell.panelBarDetached)
                        shell.attachPanelBar(false)
                    else
                        Hub.togglePanelBar()
                }
            }
        }

        // 会话区：圆角只给消息气泡，这里用普通 Item。
        Item {
            id: conversationCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBarCard.bottom
            anchors.topMargin: Theme.shadow.topBarGap
            anchors.bottom: parent.bottom

            // 选中会话时显示消息列表与输入区
            Loader {
                id: composerLoader
                // 宽度不超过上限，多出的空间水平居中留白。
                width: Math.max(0, Math.min(parent.width - 2 * Theme.spacing.columnClearance,
                                            shell.conversationInputMaxWidth))
                x: Math.round((parent.width - width) / 2)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 0   // 底部对齐会话栏
                height: item ? item.implicitHeight : 0

                visible: Hub.currentSessionId.length > 0
                source: Style.url("basic/chat/Composer.qml")
                // 用 typeof 探测属性，兼容旧风格包。
                onLoaded: {
                    if (!item)
                        return
                    if (typeof item.widthAnimating !== "undefined")
                        item.widthAnimating = Qt.binding(function () { return shell.contentWidthDriven })
                }
            }

            // 提问 / 审批面板，排在输入区上方，宽度与输入区对齐。
            Loader {
                id: interactionLoader
                width: composerLoader.width
                x: composerLoader.x
                anchors.bottom: composerLoader.top
                anchors.bottomMargin: 0
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
                anchors.bottomMargin: 0
                // 切到轨迹时用 visible 让位，而不是销毁列表，保留各自滚动位置。
                visible: Hub.currentSessionId.length > 0 && topBar.activeView !== "trace"
                // 宽度定下来后再创建列表，避免首帧按假宽度排版；用单向闩防止缩窗时重建。
                property bool everWide: false
                onWidthChanged: if (width > 200) everWide = true
                active: everWide
                source: Style.url("basic/chat/MessageListView.qml")
                onLoaded: {
                    if (!item)
                        return
                    if (typeof item.widthAnimating !== "undefined")
                        item.widthAnimating = Qt.binding(function () { return shell.contentWidthDriven })
                }
            }

            // 轨迹视图与消息列表共用同一套会话数据；首次切到轨迹才创建，之后保留。
            Loader {
                id: traceLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: interactionLoader.top
                anchors.bottomMargin: 0
                visible: topBar.activeView === "trace" && Hub.currentSessionId.length > 0
                property bool everShown: false   // 单向闩：建过就保留滚动位置与委托
                property bool everWide: false
                onWidthChanged: if (width > 200) everWide = true
                active: (visible || everShown) && (width > 200 || everWide)
                source: Style.url("basic/trace/TraceView.qml")
                onLoaded: {
                    if (!item)
                        return
                    if (typeof item.widthAnimating !== "undefined")
                        item.widthAnimating = Qt.binding(function () { return shell.contentWidthDriven })
                }
                onVisibleChanged: {
                    Hub.setTracePaused(!visible)   // 不可见时只暂停，不重建
                    if (!visible)
                        return
                    everShown = true
                    if (item)
                        Qt.callLater(item.pinToBottom)
                }
                // Loader 异步创建：visible 先变、item 后到，补一次贴底。
                onItemChanged: if (visible && item)
                    Qt.callLater(item.pinToBottom)
            }

            // 已有会话但还没有任何消息：显示欢迎空状态。
            // ConversationPlaceholder 只管“没选中会话”；选中了空会话时不能跟着空着。
            Item {
                id: emptyConversation
                anchors.fill: messageLoader
                visible: Hub.currentSessionId.length > 0
                    && Hub.messageCount === 0
                    && !Hub.sessionLoading
                    && topBar.activeView !== "trace"
                z: 3

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.spacing.windowMargin, 460)
                    spacing: Theme.spacing.sectionGap

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 54
                        height: 54
                        source: Theme.isDark
                            ? "qrc:/ULinHub/icons/ulin-hub-icon-dark.png"
                            : "qrc:/ULinHub/icons/ulin-hub-icon.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }

                    Text {
                        width: parent.width
                        text: qsTrId("empty_conversation_title")
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.title
                        font.weight: Theme.font.weightMedium
                    }

                    Text {
                        width: parent.width
                        text: qsTrId("empty_conversation_hint")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        lineHeight: 1.4
                    }
                }
            }

            ConversationPlaceholder {
                anchors.fill: parent
                // 启动/自动选会话期间 currentSessionId 短暂为空，这不是“请开始会话”；
                // 只有初始化结束且确实没有会话时才显示占位。
                visible: Hub.currentSessionId.length === 0 && !Hub.initializing
            }
        }
    }

    // 右侧栏从窗口右缘外滑入并淡入，只改 x/opacity，不改 width。
    // 位置与尺寸放在附着槽位上，PanelBar 始终 anchors.fill，reparent 到独立窗口也无需改锚定。
    Item {
        id: panelBarSlot
        anchors.top: sessionBarCard.top
        anchors.bottom: sessionBarCard.bottom
        width: shell.panelBarEffW
        x: shell.width - Theme.spacing.windowMargin - width
           + (1 - shell.panelBarT) * (width + Theme.spacing.windowMargin)

        PanelBar {
            id: panelBar
            anchors.fill: parent
            visible: shell.panelBarInWindow ? true : shell.panelBarT > 0.001
            opacity: shell.panelBarInWindow ? 1 : shell.panelBarT
            sideDetached: shell.panelBarDetached
            attachZoneProvider: shell.panelBarScreenRect
            // 只有铺满卡片的块内容需要按圆角裁切，避免把 WebEngine 内容整块栅格化。
            contentNeedsMask: !PanelBarDock.homeVisible
            onCloseRequested: {
                if (shell.panelBarInWindow)
                    shell.attachPanelBar(false)
                else
                    Hub.closePanelBar()
            }
            // 抓手的拿起 / 松手 / 回附 / 落点预览
            onPanelBarLiftRequested: shell.liftPanelBarForDrag()
            onPanelBarDropRequested: (screenX, screenY) => shell.finishPanelBarDrag(screenX, screenY)
            onPanelBarAttachRequested: shell.attachPanelBar(true)
            // 窗口按钮转发给 PanelBarWindow
            onMinimizeWindowRequested: shell.minimizePanelBarWindow()
            onToggleMaximizeRequested: shell.toggleMaximizePanelBarWindow()
            onPanelBarPreviewChanged: (over) => {
                if (over)
                    shell.beginDetachPreview(true)
                else
                    shell.endDetachPreview()
            }

            // 右栏内容：面板坞
            PanelBarDockView {
                id: dock
                anchors.fill: parent
            }
        }
    }

    // 独立窗口：右栏拿起后住在这里；PanelBar 实例在 panelSlot 间 reparent。
    PanelBarWindow {
        id: panelBarWindow
        host: shell
    }

    // 拖回时的落点提示，画在原右栏位置，z 抬到面板之上。
    DockDropHint {
        id: dropHint
        z: 30
        zone: shell.dropZone
        active: shell.dropHintActive
        fromDock: !shell.panelBarDetached
        floating: shell.panelBarFloating
    }

    // 右栏宽度拖柄：贴在面板左缘的 10px 热区，拖动的是 shell.panelBarW。
    // ⚠️ 可见性只挂 Hub.panelBarOpen，不能挂 panelBarT：宽度变化时 T 会瞬时抖动并取消鼠标抓取。
    // 拖动期间位置必须冻结，否则拖柄跟着面板移动也会打断抓取。
    Item {
        id: resizeHandle
        anchors.top: panelBarSlot.top
        anchors.bottom: panelBarSlot.bottom
        width: 10
        visible: Hub.panelBarOpen
        enabled: visible
        property real frozenX: 0   // 按下时冻结的 x
        // ⚠️ 贴附着槽位的左缘；脱离时 PanelBar 的 x 是独立窗口里的 0。
        x: shell.resizing ? frozenX : panelBarSlot.x - width / 2

        // 灰边画在面板内侧、上下让开圆角；拖动期间不画，避免停留在旧位置。
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
            // 用窗口坐标记录按下点，避免拖柄自身移动导致自我参照。
            property real pressWindowX: 0
            property real pressWidth: 0

            onPressed: (mouse) => {
                pressWindowX = resizeHandle.mapToItem(null, mouse.x, 0).x
                pressWidth = shell.panelBarEffW
                resizeHandle.frozenX = resizeHandle.x   // 先冻结位置，再置 resizing
                shell.resizing = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                shell.setPanelBarWidth(shell.widthFromDrag(
                    pressWindowX, pressWidth, resizeHandle.mapToItem(null, mouse.x, 0).x))
            }
            onReleased: shell.resizing = false
            onCanceled: shell.resizing = false
        }
    }

    // 初始化中的遮罩：只提示状态，不挡住操作。
    // 启动初始化 / Master 后端切换共用同一套遮罩。
    // 初始化阶段只提示；Master 切换期间必须挡住操作，避免旧后端已停还继续发消息。
    Rectangle {
        anchors.fill: parent
        visible: Hub.initializing || Hub.masterSwitching
        color: Theme.color.overlayBg
        z: 80

        MouseArea {
            anchors.fill: parent
            enabled: Hub.masterSwitching
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => { event.accepted = true }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacing.sectionGap

            BusySpinner {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32
                height: 32
                color: Theme.isDark ? Theme.color.textPrimary : Theme.color.textOnAccent
                // 必须门控 running：父级不可见时动画仍在跑会持续标脏场景。
                running: Hub.initializing || Hub.masterSwitching
                visible: Hub.initializing || Hub.masterSwitching
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(760, Math.max(320, shell.width * 0.68))
                text: Hub.masterSwitching ? "正在切换 Agent 后端…" : Hub.statusText
                color: Theme.isDark ? Theme.color.textPrimary : Theme.color.textOnAccent
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
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

    // 插件市场：整壳覆盖，侧栏 ◧ 打开；PageOverlay 负责遮罩与卡片展开。
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.plugins.open
        source: Style.url("basic/plugins/PluginMarketPage.qml")
    }

    // 扩展管理：整壳覆盖，侧栏扩展按钮打开。
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.extensions.open
        source: Style.url("basic/extensions/ExtensionManagerPage.qml")
    }

    // 主题切换过渡
    Loader {
        anchors.fill: parent
        active: Hub.themeSwitching
        visible: Hub.themeSwitching
        source: Style.url("skin/shell/ThemeSwitchOverlay.qml")
        z: 40
    }

    // 设置页：整壳覆盖，侧栏 ⚙ 打开。
    PageOverlay {
        anchors.fill: parent
        z: 30
        shown: Hub.settings.open
        source: Style.url("basic/settings/SettingsPage.qml")
    }

    // 璐︽埛绠＄悊锛氬乏涓嬭澶村儚鎵撳紑锛堟槸鐙珛 PageOverlay锛屼笉鍦ㄨ缃〉閲岋級
    PageOverlay {
        anchors.fill: parent
        z: 32
        shown: Hub.settings.accountOpen
        source: Style.url("basic/account/AccountPage.qml")
    }

    // 工具过滤面板：顶栏 ☰ 打开，整壳覆盖。
    // 工具过滤面板：顶栏图标打开，沿用 PageOverlay 的统一遮罩/卡片动画。
    // 单块脱离为独立窗口：模型只存 id，窗口内容仍走 PanelBarDock 的 kind→view 路由。
    Instantiator {
        model: PanelBarDock.detachedBlocks
        delegate: Component {
            DetachedBlockWindow {
                required property string modelData
                blockId: modelData
            }
        }
    }

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

    // 自动化任务：侧栏底排时钟按钮打开，数据面是 Hub.schedule*。
    PageOverlay {
        anchors.fill: parent
        z: 34
        shown: Hub.scheduleOpen
        source: Style.url("basic/schedule/SchedulePage.qml")
    }
}
