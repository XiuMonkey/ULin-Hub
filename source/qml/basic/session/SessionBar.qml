// 侧栏：Logo + 新建会话 + 工作区/会话列表 + 底部图标行。
// 数据来自 Hub.sessions（摊平的工作区/会话行模型，见 include/common/session/SessionListModel.h）。
// 布局用锚点而不是 Column：列表要吃掉剩余高度，锚点比"减一圈高度"可靠。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/session"

Item {
    id: sessionBar

    // 扩展注入点（名字契约见 assets/resources/docs/EXTENSION_DEV.zh-CN.md §11.3）；四条连接交给宿主建：
    // 有真实 handle ⇒ 可挂起/恢复/接管/交还。
    // 签名手写：带回参的信号要写全类型；接收端必须具名且**参数带类型标注**（未标注在元对象里是 QVariant，接不上）。
    Component.onCompleted: {
        HostUi.registerItem("sidebar", this)
        HostConnections.RegisterConnection("qml.sidebar.sessionCreateFailed", Hub, "sessionCreateFailed(QString)", sessionBar, "handleSessionCreateFailed(QString)")
        HostConnections.RegisterConnection("qml.sidebar.openSettings", sessionBar, "openSettingsRequested()", sessionBar, "handleOpenSettings()")
        HostConnections.RegisterConnection("qml.sidebar.openPluginMarket", sessionBar, "openPluginMarketRequested()", sessionBar, "handleOpenPluginMarket()")
        // 清空会话与上面两条同一条路：默认处理器在本组件里，扩展可接管这条 index 换成自己的实现
        HostConnections.RegisterConnection("qml.sidebar.clearSessions", sessionBar, "clearSessionsRequested()", sessionBar, "handleClearSessions()")
    }
    // 四条连接的默认处理器（没人接管时 = 宿主原本行为）
    function handleSessionCreateFailed(message: string) {
        sessionBar.notify(message)
    }
    function handleOpenSettings() {
        Hub.settings.openPage()
    }
    function handleOpenPluginMarket() {
        Hub.plugins.openPage()
    }
    // 清空会话：业务全在 core（ClientRuntime::clearAllSessions），这里只转调
    function handleClearSessions() {
        Hub.clearAllSessions()
    }

    // 侧栏自己不弹 Toast：只把文案抛给壳层（Shell.qml 里统一放一个 Toast）
    signal notify(string text)

    // 两个入口的请求信号：按钮不直接开页面，谁响应由连接表决定（扩展接管后就换成扩展自己的面板）
    signal openSettingsRequested()
    signal openPluginMarketRequested()
    // 清空会话（危险动作）：同样不直连，谁响应由连接表决定
    signal clearSessionsRequested()

    // 背景与圆角由外层 Card 提供（Shell.qml）：这里再画一层方块会盖掉卡片的圆角。
    // 侧栏只负责内部排布的留白。

    // ---- 顶部：Logo（居中）----
    // 原版 SidebarLogo 是**横版标识**：ulin-hub-logo-tiny@2x.png 400×138、dpr=2 → 逻辑 200×69，
    // 且用 addWidget(..., Qt::AlignHCenter) **居中**（Sidebar.cpp:42-48 / 516）。
    Item {
        id: topRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 10
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        implicitHeight: logo.height

        Image {
            id: logo
            anchors.horizontalCenter: parent.horizontalCenter
            width: 200
            height: 69
            source: Theme.isDark
                ? "qrc:/ULinHub/icons/ulin-hub-logo-tiny-dark@2x.png"
                : "qrc:/ULinHub/icons/ulin-hub-logo-tiny@2x.png"
            sourceSize.width: 400
            sourceSize.height: 138
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
    }

    // 原版顺序（Sidebar.cpp:517-518）：先「清空会话」(danger)，再「新建工作区」(主色)，都是全宽按钮
    ActionButton {
        id: clearButton
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topRow.bottom
        anchors.topMargin: 4
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        text: qsTrId("sidebar_clear_sessions")
        variant: "danger"
        // 语义已核对（旧壳 Sidebar::clearAllSessions → deleteAllThen → callSessionCreate）：
        // 清本地会话数据 + 删服务端全部工作区 + 建一个新的默认会话。请求交给连接表，默认处理器是
        // handleClearSessions（扩展可接管 "qml.sidebar.clearSessions"）。
        onClicked: sessionBar.clearSessionsRequested()
    }

    ActionButton {
        id: newWorkspaceButton
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: clearButton.bottom
        anchors.topMargin: 4
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        text: qsTrId("sidebar_new_workspace")
        variant: "primary"
        onClicked: Hub.createWorkspace()
    }

    // ---- 底排：原版是**横排**（QHBoxLayout、spacing 0、四个 32×32 键靠左，
    //      末尾 addSpacing(15) 给扩展按钮留位，再 addStretch(1) 把它们顶到左边；
    //      见 Sidebar.cpp:521-533）。我上一轮误读成竖排，这里改回横排。
    Row {
        id: navColumn
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.bottomMargin: 10
        spacing: 0

        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/icons/setting-icon.png"
            tooltip: qsTrId("settings_title")
            onClicked: sessionBar.openSettingsRequested()
        }
        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/icons/plugin-icon.png"
            tooltip: qsTrId("sidebar_plugins")
            onClicked: sessionBar.openPluginMarketRequested()
        }
        // 插件转译器的入口按钮**不在这里** —— 它归扩展自己（PluginTranspiler 自绘
        // ui/EntryButton.qml，运行时注入下方的 extDock）。插件转译器不是宿主的功能，
        // 宿主不该在扩展没装的时候就摆一颗点了没反应的哑按钮。
        // 主题：原版 Sidebar::themeToggleRequested -> ULinHub::toggleTheme（切模式 + 过渡卡片）
        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/icons/theme-icon.png"
            tooltip: qsTrId("sidebar_switch_theme")
            onClicked: Hub.toggleTheme()
        }
        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/icons/extension-icon.png"
            tooltip: qsTrId("ext_manager_title")
            onClicked: Hub.extensions.openPage()
        }
    }

    // 扩展按钮停靠位：宿主只给"一排空位"，**按钮与图标都归扩展自己**
    //（PanelAnimDemo 的 ▤、PluginTranspiler 的 ⇄ 都注入到这里）。
    // 它是 Row（位置器）⇒ 注入项自动左起排，扩展因此不用算 x/y，也不会跟宿主那几颗抢位。
    // ⚠️ 与 navColumn 拆成两个 Row 是有意的：注入项只会**追加**到宿主 Row 的末尾，
    //    混在一起的话扩展按钮会排到 spinner 后面去。
    // ⚠️ 没有扩展装进来时这里宽度为 0，底排与"只有四个键"时一模一样。
    Row {
        id: extDock
        anchors.left: navColumn.right
        anchors.verticalCenter: navColumn.verticalCenter
        spacing: 0
        // 扩展注入点（名字契约见 assets/resources/docs/EXTENSION_DEV.zh-CN.md §11.3）
        Component.onCompleted: HostUi.registerItem("sidebar.extDock", extDock)
    }

    // 尾排：原版 addSpacing(15)（给扩展按钮留位）+ 初始化转圈
    Row {
        id: navTrailing
        anchors.left: extDock.right
        anchors.verticalCenter: navColumn.verticalCenter
        spacing: 0

        Item { width: 15; height: 1 }

        BusySpinner {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            lineWidth: 2
            running: Hub.initializing
            visible: Hub.initializing
        }
    }

    // ---- 会话列表（工作区表头与会话行在同一层，见 SessionListModel 的说明）----
    // 原版侧栏内边距 10/10/10/10、列表行间距 2（Sidebar.cpp:514-515 / 300）
    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: newWorkspaceButton.bottom   // 必须排在两个按钮之下，否则被它们盖住
        anchors.topMargin: 10
        anchors.bottom: navColumn.top
        anchors.bottomMargin: 10
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        model: Hub.sessions
        ScrollBar.vertical: AppScrollBar { }

        delegate: Item {
            id: rowRoot

            required property string kind
            required property string workspaceId
            required property string workspaceTitle
            required property int sessionCount
            required property bool expanded
            required property string sessionId
            required property string title
            required property bool running
            required property bool selected

            width: list.width
            implicitHeight: (rowRoot.kind === "workspace")
                ? workspaceHeader.implicitHeight
                : sessionRow.implicitHeight

            WorkspaceHeader {
                id: workspaceHeader
                visible: rowRoot.kind === "workspace"
                anchors.left: parent.left
                anchors.right: parent.right
                title: rowRoot.workspaceTitle
                expanded: rowRoot.expanded
                onToggleRequested: Hub.sessions.toggleWorkspace(rowRoot.workspaceId)
                onAddSessionRequested: Hub.createSession(rowRoot.workspaceId)
                onDeleteRequested: {
                    // 原版：右键就地弹出一个**小按钮**（不是菜单、不是遮罩）
                    sessionBar.pendingId = rowRoot.workspaceId
                    sessionBar.pendingIsWorkspace = true
                    sessionBar.pendingY = rowRoot.y
                }
            }

            SessionRow {
                id: sessionRow
                visible: rowRoot.kind === "session"
                anchors.left: parent.left
                anchors.right: parent.right
                title: rowRoot.title
                running: rowRoot.running
                selected: rowRoot.selected
                onSelectRequested: Hub.selectSession(rowRoot.sessionId)
                onDeleteRequested: {
                    sessionBar.pendingId = rowRoot.sessionId
                    sessionBar.pendingIsWorkspace = false
                    sessionBar.pendingY = rowRoot.y
                }
            }
        }

        // 空列表时的引导
        Text {
            anchors.centerIn: parent
            visible: list.count === 0 && !Hub.initializing
            width: list.width - 2 * Theme.spacing.cardPadding
            horizontalAlignment: Text.AlignHCenter
            text: qsTrId("sidebar_no_sessions")
            color: Theme.color.textCaption
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            lineHeight: 1.4
        }
    }

    // ===================== 右键删除：原版形态 =====================
    // 原版 Sidebar.cpp:159-200 是**贴鼠标的一个小按钮**（QDialog(Qt::Popup) 里只放一个
    // QPushButton#sessionContextDeleteAction：panelBg 底、1px 边框、圆角 10、padding 8px 20px、
    // 13px、hover 变 dangerBg 底 + danger 字）。它**不是菜单、更没有任何遮罩**。
    //
    // 这里刻意**不用 Popup**：Qt 的 Popup 会经 Overlay 铺一层（modal 时还是暗色），
    // 想要"就是一个小按钮"最稳的做法是普通 Item 自己画。
    //
    // 点别处 = 关掉：用一个**透明**的全尺寸 MouseArea 接住点击（透明不等于遮罩，不画任何颜色）。

    property string pendingId: ""
    property bool pendingIsWorkspace: false
    property real pendingY: 0
    property bool confirmOpen: false

    MouseArea {
        anchors.fill: parent
        z: 40
        visible: sessionBar.pendingId.length > 0
        onClicked: {
            sessionBar.pendingId = ""
            sessionBar.confirmOpen = false
        }
    }

    Rectangle {
        id: deleteButton

        z: 41
        visible: sessionBar.pendingId.length > 0 && !sessionBar.confirmOpen
        width: 132
        height: 36
        radius: 10
        color: deleteHover.containsMouse ? Theme.color.dangerBg : Theme.color.panelBg
        border.width: 1
        // 浮在列表上的小按钮：走"面板级环线" inputBorder(10%)。border 已按官方 DSH 收到 4%，
        // 当浮层边缘太浅（官方浮层是 4% 环 + elevation 阴影，这里没有阴影，取深一档补回来）。
        border.color: Theme.color.inputBorder
        x: Math.max(4, sessionBar.width - width - 12)
        y: Math.max(4, Math.min(sessionBar.height - height - 4,
            list.y + sessionBar.pendingY - list.contentY + list.spacing))

        Text {
            anchors.centerIn: parent
            // 会话与工作区各有一句删除确认（工作区是"只移出列表、不动它下面的会话"）
            text: sessionBar.pendingIsWorkspace ? qsTrId("workdir_delete_label") : qsTrId("session_delete_label")
            color: deleteHover.containsMouse ? Theme.color.danger : Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }

        MouseArea {
            id: deleteHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sessionBar.confirmOpen = true    // 第二步：确认（原版是原生 QMessageBox，同样不遮罩）
        }
    }

    // 确认：一张居中的小卡片，**不压暗背景**（原版 QMessageBox 也不压暗）
    Rectangle {
        id: confirmCard

        z: 42
        visible: sessionBar.confirmOpen
        anchors.centerIn: parent
        width: 320
        implicitHeight: confirmColumn.implicitHeight + 28
        radius: Theme.radius.panel
        color: Theme.color.panelBg
        border.width: 1
        // 同上：确认卡是浮层，用面板级环线 inputBorder(10%)，别跟着 border(4%) 一起淡到看不见
        border.color: Theme.color.inputBorder

        Column {
            id: confirmColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 14

            Text {
                width: parent.width
                text: sessionBar.pendingIsWorkspace
                    ? qsTrId("workdir_delete_confirm")
                    : qsTrId("session_delete_confirm")
                wrapMode: Text.WordWrap
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
            }

            Item {
                width: parent.width
                height: 34

                Row {
                    anchors.right: parent.right
                    spacing: 8

                    ActionButton {
                        width: 88
                        height: 34
                        text: qsTrId("common_cancel")
                        onClicked: {
                            sessionBar.confirmOpen = false
                            sessionBar.pendingId = ""
                        }
                    }

                    ActionButton {
                        width: 88
                        height: 34
                        text: qsTrId("common_delete")
                        variant: "danger"
                        onClicked: {
                            if (sessionBar.pendingIsWorkspace)
                                Hub.deleteWorkspace(sessionBar.pendingId)
                            else
                                Hub.deleteSession(sessionBar.pendingId)
                            sessionBar.confirmOpen = false
                            sessionBar.pendingId = ""
                        }
                    }
                }
            }
        }
    }
}
