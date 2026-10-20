// 顶栏：两行 —— 上面「当前会话标题 + 右侧两颗图标按钮」，下面一排「对话 / 轨迹」视图胶囊。
// 面板本体在 basic/chat/ToolsFilterPanel.qml（由 Shell.qml 按 Hub.toolsFilterOpen 装载），
// 数据与形状全在 C++ 的 common/session/ToolsFilter 里 —— 这里只负责"点一下"。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: topBar

    // 扩展注入点：扩展用 HostUi.item("topBar") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("topBar", this)

    // ⚠️ 保留这个信号与 `qml.topbar.toolsFilter` 登记项：宿主侧栏/扩展里还有人用"请求打开工具过滤"
    //    这条通用入口（自己直接开面板就成了）；改名的代价大于收益。
    signal toolsFilterRequested()
    // 右侧栏开合（面板本体在 basic/panel/PanelBar.qml，由 Shell.qml 装载；这里只发"点了一下"）
    signal panelBarRequested()

    // ---- 视图胶囊：对话 / 轨迹 ----
    // activeView 取值只有两个：'conversation'（对话）/ 'trace'（轨迹）。
    // 切换的落点在 Shell.qml（它按 activeView 决定会话区装 MessageListView 还是 TraceView）；
    // 本文件**不关心**会话区长什么样，只维护这个值 + 发 viewSelected。
    property string activeView: "conversation"
    signal viewSelected(string view)

    // ---- 入口 chip + 主/子页签：数据全部由 Shell 从 Hub 绑进来（本文件不碰 RPC）----
    // 形状与交互参考官方两个 session-header 触发器（ui-subagent 的后代数量、ui-jobs 的任务列表，
    // 都注册在 conversation.session.header.actions）：12px 标签 + 数字 + ▾，running 时带状态点。
    // 位置按本项目要求放在视图胶囊右侧（官方是在标题行的操作带里，我们这里没有面包屑那一层）。
    //
    // 数据来源（都是引擎真实数据，客户端不造）：
    //   · peerTabs = 当前"页签组主会话"的直接子智能体目录（会话投影 subagentCatalog）：
    //     既是「子智能体」下拉框的列表，也是页签第二颗（当前子代理）的来源；
    //   · jobItems = 当前会话能看到的后台任务名册（job/list 流）。
    // 它们由 Shell 从 Hub.peerTabs / Hub.jobs 绑；Hub 上没有这些属性时兜底空值，
    // 老宿主/引擎不可用时只是入口显示 0 与空态，不会崩。
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
    // 点主/子页签（或子智能体列表里的一行）：Shell 接到 Hub.selectSession(id)
    signal peerSelected(string sessionId)

    // 内容列宽度是否正被外层驱动（右栏开合 / 拖右栏宽度，由 Shell 注入 contentWidthDriven）。
    // 这一档里列宽每帧在变，`chipsWrap` 可能中途翻档 —— 若仍让两条 Behavior 补间，顶栏高度/
    // 入口行的过渡会拖到开合动画之后（motion.fast≈100ms），列表跟着再变一次高、再贴一次底。
    // 动画期间直接禁掉补间：变化全部落在开合的那 200ms 里，落定后不再二次布局。
    property bool widthAnimating: false

    // ---- 两行的高度 ----
    // 44 = 原来那一条顶栏的高度，标题与两颗图标按钮的垂直居中基准；**别改它**，
    // 改了标题/按钮会跟着整块高度一起沉。
    // 下面那条视图切换行按官方分段控件收小（轨道高 28 = 标签 22 + 上下各 3）：
    // 它仍是副信息，不能把顶栏撑回早期那版 82 的高度。
    readonly property int headerRowHeight: 44
    readonly property int viewRowHeight: 22
    readonly property int viewRowBottomMargin: 8

    // ---- 两个入口（子代理 / 后台任务）的排布 ----
    // 右栏展开会把顶栏卡片压窄（Shell.qml 的 contentColumn.rightMargin 跟着 panelBarInset 走），
    // 这一排跟其它控件一样**跟着容器走**，不做"藏起来"：
    //   ① 一行放得下（胶囊 + 两枚 chip）      ⇒ 与胶囊同一行，靠右；
    //   ② 放不下                              ⇒ 整排换到胶囊下面一行，右对齐，顶栏随之长高一行；
    //   ③ 换行后还放不下（窗口极窄）          ⇒ 再降级成「计数 + ▾」，tooltip 里仍有全名。
    // 三级都是"位置/尺寸跟着让"，任何一级都不会让入口消失。
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
    //   · 当前在子代理上（current ≠ 组根）⇒ 必显示，两颗里第二颗就是它自己；
    //   · 父会话有子代理目录              ⇒ 显示，一颗父标题，子代理从下拉框进；
    //   · 其余（普通会话 / 空态）          ⇒ 普通标题。
    readonly property bool showPeerTabs: topBar.peerRootId.length > 0
        && (topBar.peerTabs.length > 0 || Hub.currentSessionId !== topBar.peerRootId)

    // 选中一颗：值真的变了才发信号（连点同一颗不产生事件，外面接命令时不会被重复触发）
    function selectView(view) {
        if (view !== "conversation" && view !== "trace")
            return
        if (activeView === view)
            return
        activeView = view
        viewSelected(view)
    }

    // ---- 入口弹层的行数据（引擎数据 → EntryChip 的 {key,title,subtitle,tone}）----
    // 文案只在这里按界面语言取（qsTrId 在绑定里，切语言会自动重取）；C++ 侧只给原始字段。
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
    // ⚠️ 标题与两颗按钮**必须**挂在这一层里居中：它们原来按 parent（= 整块顶栏）垂直居中，
    //    顶栏变高之后若还挂 parent，会整体沉到多加出来的那半截上（实测偏下 19px）。
    Item {
        id: headerRow

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: topBar.headerRowHeight

        Text {
            id: titleText
            // 页签行接管时，这颗普通标题让位（见 topBar.showPeerTabs）；
            // 没有页签时还是这条普通标题（与接线前完全一致）。
            visible: !topBar.showPeerTabs
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.columnClearance
            anchors.right: panelBarButton.left
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.verticalCenter: parent.verticalCenter
            text: Hub.currentSessionTitle.length > 0 ? Hub.currentSessionTitle : "ULin Hub"
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightMedium
            elide: Text.ElideRight
        }

        // 父 ↔ 子页签（点标题切会话）：父会话只有一颗父标题，子代理才显示两颗。
        // 数据只有父标题 + "当前子代理"那一行；全部子代理在「子智能体」下拉框里。
        PeerTabs {
            id: peerTabsBar

            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.columnClearance
            anchors.right: panelBarButton.left
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

        // 右侧栏开关：开合状态直接读 Hub.panelBarOpen（面板开着时高亮，当开关的手感）
        IconButton {
            id: panelBarButton
            anchors.right: toolsButton.left
            anchors.rightMargin: Theme.spacing.buttonGap
            anchors.verticalCenter: parent.verticalCenter
            glyph: "◫"
            tooltip: qsTrId("topbar_panelBar_tip")
            selected: Hub.panelBarOpen
            onClicked: topBar.panelBarRequested()
        }

        IconButton {
            id: toolsButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.verticalCenter: parent.verticalCenter
            glyph: "☰"
            tooltip: qsTrId("toolfilter_title")
            // 面板开着时高亮这颗按钮（当开关用的手感）
            selected: Hub.toolsFilterOpen
            // ⚠️ 只发信号，**不要**在这里同时直调 Hub.openToolsFilter()：
            //    信号在 Shell.qml 里已经接到 Hub.openToolsFilter() 上了，两处都调 = 一次点击开一次又收一次，
            //    表现就是"点了没反应"（实测日志：同一次点击出现两条 openToolsFilter: open=false / open=true）。
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
        // 同一行 ⇒ 与胶囊垂直居中；换行 ⇒ 贴在胶囊下方 6px。
        // （原来用 anchors.verticalCenter 只能表达第一种；换成 y 绑定才做得成两种形态。）
        y: topBar.chipsWrap
            ? headerRow.height + viewRow.height + 6
            : headerRow.height + (viewRow.height - height) / 2

        Behavior on y {
            // 与 implicitHeight 同一条理由：宽度被驱动时不做第二段补间
            enabled: !topBar.widthAnimating
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
            // 点列表里的一行 = 切到那个子智能体会话（与点页签同一条路）
            onItemActivated: (id) => topBar.peerSelected(id)
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
