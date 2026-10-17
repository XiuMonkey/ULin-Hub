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

    // ---- 视图胶囊：只有选中状态，主区域内容还没接 ----
    // activeView 取值只有两个：'conversation'（对话）/ 'trace'（轨迹）。
    // ⚠️ 现在**只做选中状态**：点「轨迹」只改这个值与外观，中间的会话区仍照旧显示对话。
    //    以后接轨迹视图时在 Shell.qml 里连 viewSelected（信号带新值，与读 activeView 同义），
    //    这里不用再动 —— 所以别把视图切换逻辑写进本文件（顶栏不该知道会话区长什么样）。
    property string activeView: "conversation"
    signal viewSelected(string view)

    // ---- 两行的高度 ----
    // 44 = 原来那一条顶栏的高度，标题与两颗图标按钮的垂直居中基准；**别改它**，
    // 改了标题/按钮会跟着整块高度一起沉。
    // 下面那条胶囊行刻意做小（20 高、12px 字，比标题行小一号）：它是副信息，
    // 早先按 26 高 + 12 底留白做出来顶栏 82，视觉上把整块撑得太高了（用户否掉的那版）。
    readonly property int headerRowHeight: 44
    readonly property int viewRowHeight: 20
    readonly property int viewRowBottomMargin: 8
    implicitHeight: headerRowHeight + viewRowHeight + viewRowBottomMargin

    // 选中一颗：值真的变了才发信号（连点同一颗不产生事件，外面接命令时不会被重复触发）
    function selectView(view) {
        if (view !== "conversation" && view !== "trace")
            return
        if (activeView === view)
            return
        activeView = view
        viewSelected(view)
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

        // 右侧栏开关：开合状态直接读 Hub.panelBarOpen（面板开着时高亮，当开关的手感）
        IconButton {
            id: panelBarButton
            anchors.right: toolsButton.left
            anchors.rightMargin: Theme.spacing.buttonGap
            anchors.verticalCenter: parent.verticalCenter
            glyph: "◫"
            tooltip: qsTrId("topbar_right_sidebar_tip")
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

    // 视图胶囊：对话 / 轨迹。
    // 左缘与标题对齐（同为 columnClearance），两颗之间用 buttonGap —— 一排看过去是同一条竖线。
    Row {
        id: viewRow

        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.columnClearance
        anchors.top: headerRow.bottom
        spacing: Theme.spacing.buttonGap

        PillButton {
            text: qsTrId("topbar_view_conversation")
            selected: topBar.activeView === "conversation"
            pillHeight: topBar.viewRowHeight
            onClicked: topBar.selectView("conversation")
        }

        PillButton {
            text: qsTrId("topbar_view_trace")
            selected: topBar.activeView === "trace"
            pillHeight: topBar.viewRowHeight
            onClicked: topBar.selectView("trace")
        }
    }
}
