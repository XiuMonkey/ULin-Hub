// 顶栏：两行 —— 上面「当前会话标题 + 右侧两颗图标按钮」，下面一排「对话 / 轨迹」视图胶囊。
// 面板本体在 basic/chat/ToolsFilterPanel.qml（由 Shell.qml 按 Hub.toolsFilterOpen 装载），
// 数据与形状全在 C++ 的 common/Agent/ToolsFilter 里 —— 这里只负责"点一下"。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: topBar

    // 扩展注入点：扩展用 HostUi.item("topBar") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("topBar", this)

    signal toolsFilterRequested()   // ⚠️ 保留这个信号与 `qml.topbar.toolsFilter` 登记项：宿主侧栏/扩展里还有人用"请求打开工具过滤"
    signal panelBarRequested()   // 右侧栏开合（面板本体在 basic/panel/PanelBar.qml，由 Shell.qml 装载；这里只发"点了一下"）

    // 左侧栏开合：只在侧栏**收起**时露出（展开时那颗收起键在侧栏自己右上角）。
    // 语义与 panelBarRequested 对称，动作归 Shell（左边列宽 / 侧栏位置都是壳层的布局）
    signal sessionBarRequested()

    property bool sessionBarCollapsed: false   // 左侧栏是否已收起（Shell 注入）：收起时标题/页签行右移，给"展开侧栏"键腾出位置

    // 展开键占的位置（键宽 + 一个 buttonGap；收起才占，展开时归零 ⇒ 展开态布局与改动前逐像素一致）
    // 取按钮的 implicitWidth 而不是写死 26：按钮尺寸改了这里自动跟上
    readonly property int sessionBarSlot: sessionBarCollapsed
        ? Math.round(sessionBarButton.implicitWidth + Theme.spacing.buttonGap) : 0

    // 标题行/页签行的左缩进。走数值属性 + Behavior（Behavior 挂不到 anchors.leftMargin 上）：
    // 展开键出现与标题右移是同一段短过渡，不会在收起第一帧"啪"地跳 32px。
    property real titleLeftMargin: Theme.spacing.columnClearance + sessionBarSlot
    Behavior on titleLeftMargin {
        NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
    }

    property string activeView: "conversation"
    signal viewSelected(string view)

    property var peerTabs: []
    property string peerRootId: ""
    property string peerRootTitle: ""
    property int subagentCount: 0
    property bool subagentRunning: false
    property var jobItems: []
    property int jobCount: 0
    property bool jobRunning: false
    signal subagentRequested()
    signal jobsRequested()
    signal peerSelected(string sessionId)   // 点主/子页签（或子智能体列表里的一行）：Shell 接到 Hub.selectSession(id)

    // 内容列宽度是否正被外层驱动（右栏开合 / 拖右栏宽度，由 Shell 注入 contentWidthDriven）。
    // 这一档里列宽每帧在变，`chipsWrap` 可能中途翻档 —— 若仍让两条 Behavior 补间，顶栏高度/
    // 入口行的过渡会拖到开合动画之后（motion.fast≈100ms），列表跟着再变一次高、再贴一次底。
    // 动画期间直接禁掉补间：变化全部落在开合的那 200ms 里，落定后不再二次布局。
    property bool widthAnimating: false

    readonly property int headerRowHeight: 44
    readonly property int viewRowHeight: 22
    readonly property int viewRowBottomMargin: 8

    readonly property real rowAvailable: width - 2 * Theme.spacing.columnClearance
    readonly property real chipsFullWidth: subagentChip.fullWidth + jobsChip.fullWidth + entryRow.spacing
    readonly property real chipsCompactWidth: subagentChip.compactWidth + jobsChip.compactWidth
        + entryRow.spacing
    readonly property bool chipsWrap: rowAvailable
        < viewRow.width + 2 * Theme.spacing.buttonGap + chipsFullWidth
    readonly property bool chipsCompact: chipsWrap && rowAvailable < chipsCompactWidth

    implicitHeight: headerRowHeight + viewRowHeight + 6 + viewRowBottomMargin
        + (chipsWrap ? Theme.size.selectorChipHeight + 6 : 0)

    // 行高变化（换行/回行）时补一段短过渡，别让顶栏卡片"啪"地跳一下。
    // 但内容列宽度正在被驱动时禁掉：换档只是动画内部的中间态，别让它再拖出第二段动画。
    Behavior on implicitHeight {
        enabled: !topBar.widthAnimating
        NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
    }

    // 页签行什么时候接管标题（否则用下面那条普通标题）：
    // · 当前在子代理上（current ≠ 组根）⇒ 必显示，两颗里第二颗就是它自己；
    // · 父会话有子代理目录              ⇒ 显示，一颗父标题，子代理从下拉框进；
    // · 其余（普通会话 / 空态）          ⇒ 普通标题。
    readonly property bool showPeerTabs: topBar.peerRootId.length > 0
        && (topBar.peerTabs.length > 0 || Hub.currentSessionId !== topBar.peerRootId)

    function selectView(view) {   // 选中一颗：值真的变了才发信号（连点同一颗不产生事件，外面接命令时不会被重复触发）
        if (view !== "conversation" && view !== "trace")
            return
        if (activeView === view)
            return
        activeView = view
        viewSelected(view)
    }

    function subagentModeLabel(mode) {
        if (mode === "one-shot")
            return qsTrId("subagent_mode_one_shot")
        if (mode === "continuable")
            return qsTrId("subagent_mode_continuable")
        return qsTrId("subagent_mode_unknown")
    }

    function jobStatusLabel(status) {
        switch (status) {
        case "running": return qsTrId("job_status_running")
        case "stopping": return qsTrId("job_status_stopping")
        case "completed": return qsTrId("job_status_completed")
        case "killed": return qsTrId("job_status_killed")
        case "failed": return qsTrId("job_status_failed")
        default: return status
        }
    }

    readonly property var subagentRows: {
        const rows = []
        const tabs = topBar.peerTabs || []
        for (let i = 0; i < tabs.length; ++i) {
            const tab = tabs[i]
            const activity = tab.running ? qsTrId("subagent_activity_running")
                                         : qsTrId("subagent_activity_inactive")
            rows.push({
                key: tab.id,
                title: tab.label,
                subtitle: subagentModeLabel(tab.mode) + " · " + activity,
                tone: tab.running ? "running" : "muted"
            })
        }
        return rows
    }

    readonly property var jobRows: {
        const rows = []
        const jobs = topBar.jobItems || []
        for (let i = 0; i < jobs.length; ++i) {
            const job = jobs[i]
            const detail = job.progress && job.progress.length > 0 ? job.progress : job.detail
            let subtitle = (job.kind ? job.kind + " · " : "") + jobStatusLabel(job.status)
            if (detail && detail.length > 0)
                subtitle += " · " + detail
            rows.push({
                key: job.id,
                title: job.label && job.label.length > 0 ? job.label : job.id,
                subtitle: subtitle,
                tone: job.status === "failed" ? "error"
                    : (job.live === true ? "running" : "muted")
            })
        }
        return rows
    }

    // 标题行：整块顶栏里"上半部分"的坐标原点。
    // ⚠️ 标题与两颗按钮**必须**挂在这一层里居中：挂到 parent（整块顶栏）上会沉到多出来的那半截。
    Item {
        id: headerRow

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: topBar.headerRowHeight

        Column {
            id: titleBlock

            // 页签行接管时，这颗普通标题让位（见 topBar.showPeerTabs）；
            // 没有页签时显示标题 + 工作区/模型副行。
            visible: !topBar.showPeerTabs
            anchors.left: parent.left
            anchors.leftMargin: topBar.titleLeftMargin

            // 右边界 = 两颗按钮里**左边那颗**（换位之后是 toolsButton）：
            // 锚到 panelBarButton 会把标题压到工具按钮底下（那两颗现在顺序反了）。
            anchors.right: toolsButton.left
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                id: titleText
                anchors.left: parent.left
                anchors.right: parent.right
                text: Hub.currentSessionTitle.length > 0 ? Hub.currentSessionTitle : "ULin Hub"
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightMedium
                elide: Text.ElideRight
            }

            Row {
                spacing: 4
                height: Theme.size.statsLineHeight

                Text {
                    text: "●"
                    color: Hub.streaming ? Theme.color.accent : Theme.color.textTertiary
                    font.pixelSize: 7
                }
                Text {
                    text: {
                        const parts = []
                        if (Hub.currentWorkspaceTitle.length > 0)
                            parts.push(Hub.currentWorkspaceTitle)
                        if (Hub.currentModelName.length > 0)
                            parts.push(Hub.currentModelName)
                        if (Hub.currentLevelName.length > 0)
                            parts.push(Hub.currentLevelName)
                        return parts.join(" · ")
                    }
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.small
                    elide: Text.ElideRight
                }
            }
        }

        // 父 ↔ 子页签（点标题切会话）：父会话只有一颗父标题，子代理才显示两颗。
        // 数据只有父标题 + "当前子代理"那一行；全部子代理在「子智能体」下拉框里。
        PeerTabs {
            id: peerTabsBar

            anchors.left: parent.left
            anchors.leftMargin: topBar.titleLeftMargin
            anchors.right: toolsButton.left   // 同 titleBlock：右边界跟着"两颗里左边那颗"
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.verticalCenter: parent.verticalCenter

            visible: topBar.showPeerTabs
            enabled: visible
            rootId: topBar.peerRootId
            rootTitle: topBar.peerRootTitle.length > 0 ? topBar.peerRootTitle : topBar.peerRootId
            peers: topBar.peerTabs
            currentId: Hub.currentSessionId
            currentTitle: Hub.currentSessionTitle
            onSelected: (sessionId) => topBar.peerSelected(sessionId)
        }

        // 左侧栏展开键：**只在侧栏收起时出现**（展开时收起入口是侧栏右上角那颗）。
        // 图标沿用 panel-collapse.svg：顶栏那颗右栏开关也是开/合同一个图标，这里保持一致；
        // 图标本身是"左栏 + 朝左的箭头"，这里不镜像 —— 点击的语义由 tooltip 说清楚（展开侧栏）。
        IconButton {
            id: sessionBarButton
            visible: topBar.sessionBarCollapsed
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.columnClearance
            anchors.verticalCenter: parent.verticalCenter
            boxSize: 26
            iconSize: 15
            iconSource: "qrc:/ULinHub/icons/ui/panel-collapse.svg"
            iconColor: Theme.color.iconButtonText
            tooltip: qsTrId("sessionBar_expand")
            onClicked: topBar.sessionBarRequested()
        }

        // 右侧栏开关：开合状态直接读 Hub.panelBarOpen（面板开着时高亮，当开关的手感）。
        // 工具过滤退到它左边。顺序 = 触发对象离它所影响的区域越近越靠右，视觉上更好找。
        IconButton {
            id: panelBarButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.verticalCenter: parent.verticalCenter
            boxSize: 26
            iconSize: 15
            iconSource: "qrc:/ULinHub/icons/ui/panel-collapse.svg"
            iconColor: Hub.panelBarOpen ? Theme.color.accent : Theme.color.iconButtonText
            tooltip: qsTrId("topbar_panelBar_tip")
            selected: Hub.panelBarOpen
            onClicked: topBar.panelBarRequested()
        }

        IconButton {
            id: toolsButton
            anchors.right: panelBarButton.left
            anchors.rightMargin: Theme.spacing.buttonGap
            anchors.verticalCenter: parent.verticalCenter
            boxSize: 26
            iconSize: 15
            iconSource: "qrc:/ULinHub/icons/ui/tools.svg"
            iconColor: Hub.toolsFilterOpen ? Theme.color.accent : Theme.color.iconButtonText
            tooltip: qsTrId("toolfilter_title")
            selected: Hub.toolsFilterOpen   // 面板开着时高亮这颗按钮（当开关用的手感）

            // ⚠️ 只发信号，**不要**在这里同时直调 Hub.openToolsFilter()：Shell 已经接管该信号，
            // 两处都调 = 一次点击开一次又收一次，表现就是"点了没反应"。
            onClicked: topBar.toolsFilterRequested()
        }
    }

    // 视图切换：对话 / 轨迹。
    // 左缘与标题对齐（同为 columnClearance）—— 一排看过去是同一条竖线。
    // 形状按官方 SegmentedControl（轨道 + 滑动指示器），见 widgets/SegmentedTabs.qml。
    SegmentedTabs {
        id: viewRow

        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.columnClearance
        anchors.top: headerRow.bottom
        tabHeight: topBar.viewRowHeight
        currentKey: topBar.activeView
        model: [
            { key: "conversation", label: qsTrId("topbar_view_conversation") },
            { key: "trace", label: qsTrId("topbar_view_trace") }
        ]
        onSelected: (key) => topBar.selectView(key)
    }

    // 两个入口：默认与胶囊同一行、靠右；一行容不下时整排落到胶囊下面一行（右对齐）。
    // 位置判定全在文件头那三个 readonly 里（chipsWrap / chipsCompact），这里只负责摆放。
    // ⚠️ 不要再用 `visible: !Hub.panelBarOpen` 之类的"窄了就藏"：入口一消失用户就以为功能没了。
    Row {
        id: entryRow

        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.columnClearance
        spacing: Theme.spacing.buttonGap
        y: topBar.chipsWrap   // 同一行 ⇒ 与胶囊垂直居中；换行 ⇒ 贴在胶囊下方 6px
            ? headerRow.height + viewRow.height + 6
            : headerRow.height + (viewRow.height - height) / 2

        Behavior on y {
            enabled: !topBar.widthAnimating   // 与 implicitHeight 同一条理由：宽度被驱动时不做第二段补间
            NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
        }

        EntryChip {
            id: subagentChip

            text: qsTrId("topbar_subagent")
            count: topBar.subagentCount
            running: topBar.subagentRunning
            items: topBar.subagentRows
            compact: topBar.chipsCompact
            tooltip: qsTrId("topbar_subagent_tip")
            popupTitle: qsTrId("topbar_subagent")
            emptyHint: qsTrId("topbar_subagent_empty")

            // 打开下拉前刷一次运行态：running 只存在于 session.list（引擎按 agents.status 现算），
            // 客户端不会自己变 —— 否则列表里永远写着"未在运行"。
            onClicked: {
                if (Hub.refreshSessionStates)
                    Hub.refreshSessionStates()
                topBar.subagentRequested()
            }
            onItemActivated: (id) => topBar.peerSelected(id)   // 点列表里的一行 = 切到那个子智能体会话（与点页签同一条路）
        }

        EntryChip {
            id: jobsChip

            text: qsTrId("topbar_jobs")
            count: topBar.jobCount
            running: topBar.jobRunning
            items: topBar.jobRows
            compact: topBar.chipsCompact
            tooltip: qsTrId("topbar_jobs_tip")
            popupTitle: qsTrId("topbar_jobs")
            emptyHint: qsTrId("topbar_jobs_empty")
            onClicked: topBar.jobsRequested()
        }
    }
}
