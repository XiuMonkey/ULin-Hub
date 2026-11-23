// 侧栏（Quiet Rail 版）：200×69 Logo + 唯一主按钮「新建工作区」+ 搜索 + 置顶/工作区/会话列表 + 底部工具条。
// 视觉与结构照 build/temp/sidebar-redesign/index.html（concept a）+ styles.css 落地；
// 外层圆角卡片（radius 12、窗口留白、Card 阴影）仍由 Shell.qml 的 sessionBarCard 提供，这里只排卡片内部。
// 数据仍是 Hub.sessions（摊平的工作区/会话行模型，见 common/session/SessionListModel.h）：
// UpdatedAtRole（updatedAtMs）/ filterText / setFilterText 都由模型直接提供；置顶段由模型给 kind="pinned"。
// 命名契约：扩展注入点 "sessionBar" / "sessionBar.extDock" 与四条 HostConnections 保持不变

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/session"

Item {
    id: sessionBar

    // 账户入口信号：内置逻辑默认开宿主账户页；接管扩展可 claim 后自行开窗口。
    signal accountEntryRequested()

    Component.onCompleted: {
        HostUi.registerItem("sessionBar", this)
        HostUi.registerItem("sessionBar.extDock", footer.extDock)   // 底排的扩展停靠位仍是 Row，注入项自动排在末尾
        HostConnections.RegisterConnection("qml.sessionBar.sessionCreateFailed", Hub, "sessionCreateFailed(QString)", sessionBar, "handleSessionCreateFailed(QString)")
        HostConnections.RegisterConnection("qml.sessionBar.openSettings", sessionBar, "openSettingsRequested()", sessionBar, "handleOpenSettings()")
        HostConnections.RegisterConnection("qml.sessionBar.openPluginMarket", sessionBar, "openPluginMarketRequested()", sessionBar, "handleOpenPluginMarket()")
        HostConnections.RegisterConnection("qml.sessionBar.clearSessions", sessionBar, "clearSessionsRequested()", sessionBar, "handleClearSessions()")
    }

    // 四条连接的默认处理器（没人接管时 = 宿主原本行为）
    // 签名手写：带回参的信号要写全类型；接收端必须具名且参数带类型标注
    // （未标注在元对象里是 QVariant，接不上）。
    function handleSessionCreateFailed(message: string) {
        sessionBar.notify(message)
    }
    function handleOpenSettings() {
        Hub.settings.openPage()
    }
    function handleOpenPluginMarket() {
        Hub.plugins.openPage()
    }
    function handleClearSessions() {   // 清空会话：业务全在 core（ClientRuntime::clearAllSessions），这里只转调
        Hub.clearAllSessions()
    }

    signal notify(string text)   // 侧栏自己不弹 Toast：只把文案抛给壳层（Shell.qml 里统一放一个 Toast）

    // 操作失败（新建/删除工作区、目录非法…）：core 发 operationFailed，这里转成壳层 Toast。
    // ⚠️ 缺了它，操作失败没有任何提示（用户只看到"点了没反应"）。
    Connections {
        target: Hub

        function onOperationFailed(message) {
            sessionBar.notify(message)
        }

        function onMasterSwitchFinished(name) {
            if (name)
                sessionBar.notify("已切换到 " + name)
        }
    }

    signal openSettingsRequested()
    signal openPluginMarketRequested()
    signal clearSessionsRequested()   // 清空会话（危险动作）：同样不直连，谁响应由连接表决定

    // 收起侧栏：只发请求，开合的落点在壳层（Shell.setSessionBarCollapsed —— 侧栏往窗口左缘外滑、
    // 内容列吃回它的宽度，与右栏开合同一套动效）。形态与其它入口一致：这里不自己改布局。
    signal collapseRequested()

    function startNewSession() {
        Hub.createSession("")   // 与现有实现「新建会话」同一条路：没有工作区参数 = 当前/默认工作区
    }

    property var archivedModel: []
    function openArchivedSessions() {
        archivedModel = Hub.archivedSessions()
        archivedPopup.open()
    }

    // 工作区识别色：从 id/title 稳定散列出一个受控色相（蓝/绿/橙/紫），
    // 只偏移 accent 的色相，不新增语义色 token（深浅主题都跟着 accent 走）。
    function workspaceColorFor(key) {
        if (!key || key.length === 0)
            return Theme.color.accent
        var hash = 0
        for (var i = 0; i < key.length; ++i)
            hash = (hash * 31 + key.charCodeAt(i)) % 997
        var hueOffsets = [0.0, 0.72, 0.47, 0.17]
        var hue = (Theme.color.accent.hsvHue + hueOffsets[hash % hueOffsets.length] + 1) % 1
        return Qt.hsla(hue, 0.62, 0.64, 1.0)
    }

    // 会话行时间：updatedAtMs → 现在 / N 分钟前 / N 小时前 / N 天前 / 日期（7 天前改日期）。
    // 调用方已按"ms > 0 才算已知"过滤，这里只管格式化。
    function relativeTimeText(ms) {
        var diff = Date.now() - ms
        if (diff < 0)
            diff = 0
        var minute = 60 * 1000
        var hour = 60 * minute
        var day = 24 * hour
        if (diff < minute)
            return qsTrId("sessionBar_time_now")
        if (diff < hour)
            return qsTrId("sessionBar_time_minutes_fmt").arg(Math.floor(diff / minute))
        if (diff < day)
            return qsTrId("sessionBar_time_hours_fmt").arg(Math.floor(diff / hour))
        if (diff < 7 * day)
            return qsTrId("sessionBar_time_days_fmt").arg(Math.floor(diff / day))
        return Qt.formatDate(new Date(ms), "yyyy-MM-dd")
    }

    function openRowContext(id, isWorkspace, y) {   // 右键/⋯ 删除：交给下面那套"贴行小按钮 + 确认卡"（原版形态，不遮罩）
        sessionBar.pendingId = id
        sessionBar.pendingIsWorkspace = isWorkspace
        sessionBar.pendingY = y
    }

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

    // 收起侧栏：浮在 Logo 右上角，不跟品牌抢位置（开合由壳层实现，见 Shell.setSessionBarCollapsed）
    // 悬停只让图标直接变亮：不要 iconButtonHoverBg/PressedBg 那块深色方块 ——
    // 否则鼠标移入/按下时会先出现一块深色底再恢复，视觉上"先变深再变浅"。
    IconButton {
        id: collapseButton
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.top: parent.top
        anchors.topMargin: 6
        boxSize: 24
        iconSize: 14
        iconSource: "qrc:/ULinHub/icons/ui/panel-collapse.svg"
        iconColor: collapseButton.hovered ? Theme.color.textPrimary : Theme.color.textTertiary
        hoverBackground: "transparent"
        pressedBackground: "transparent"
        tooltip: qsTrId("sessionBar_collapse")
        onClicked: sessionBar.collapseRequested()
    }

    Rectangle {
        id: newChatButton
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topRow.bottom
        anchors.topMargin: 4
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        height: 36
        radius: 10
        color: newChatMouse.pressed
            ? Theme.color.primaryButtonPressed
            : (newChatMouse.containsMouse ? Theme.color.primaryButtonHover : Theme.color.primaryButtonBg)
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

        LineIcon {
            id: newChatIcon
            name: "plus"
            size: 14
            color: Theme.color.primaryButtonText
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: newChatLabel
            anchors.left: newChatIcon.right
            anchors.leftMargin: 8
            anchors.right: newChatKeycap.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("新建工作区")
            color: Theme.color.primaryButtonText
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.body
            font.weight: Theme.font.weightBold
            elide: Text.ElideRight
        }

        Text {
            id: newChatKeycap
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrId("sessionBar_shortcut_new_chat")
            color: Theme.color.primaryButtonText
            opacity: 0.55
            font.family: Theme.font.monoFamily
            font.pixelSize: Theme.font.small
        }

        MouseArea {
            id: newChatMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Hub.createWorkspace()
        }
    }

    Shortcut {   // 新建工作区（与主按钮同一条路）：StandardKey，macOS 自动映射为 Cmd+N
        sequence: StandardKey.New
        onActivated: Hub.createWorkspace()
    }

    SessionSearchBox {
        id: searchBox
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: newChatButton.bottom
        anchors.topMargin: 8
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        onTextEdited: (text) => Hub.sessions.setFilterText(text)
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: searchBox.bottom
        anchors.topMargin: 8
        anchors.bottom: footer.top
        anchors.bottomMargin: 8
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        clip: true
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
            required property int index

            // UpdatedAtRole（role 名 "updatedAtMs"，qint64）：delegate 里不能读 model（会报
            // `ReferenceError: model is not defined`），role 只有声明成 required property 才读得到。
            // 数值约定：0 = 时间未知（行内不显示时间）。
            required property var updatedAtMs

            width: list.width
            implicitHeight: {
                if (rowRoot.kind === "workspace")
                    return workspaceHeader.implicitHeight + (rowRoot.index === 0 ? 0 : 8)
                if (rowRoot.kind === "pinned")
                    return pinnedHeader.height
                return sessionRow.implicitHeight
            }

            WorkspaceHeader {
                id: workspaceHeader
                visible: rowRoot.kind === "workspace"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                title: rowRoot.workspaceTitle
                expanded: rowRoot.expanded
                sessionCount: rowRoot.sessionCount
                workspaceColor: sessionBar.workspaceColorFor(rowRoot.workspaceId.length > 0 ? rowRoot.workspaceId : rowRoot.workspaceTitle)
                onToggleRequested: Hub.sessions.toggleWorkspace(rowRoot.workspaceId)
                onAddSessionRequested: Hub.createSession(rowRoot.workspaceId)
                onMenuRequested: sessionBar.openRowContext(rowRoot.workspaceId, true, rowRoot.y)
                onDeleteRequested: sessionBar.openRowContext(rowRoot.workspaceId, true, rowRoot.y)
            }

            Item {
                id: pinnedHeader
                visible: rowRoot.kind === "pinned"
                anchors.left: parent.left
                anchors.right: parent.right
                height: 26

                LineIcon {
                    id: pinnedIcon
                    name: "pin"
                    size: 12
                    color: Theme.color.textCaption
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: pinnedLabel
                    anchors.left: pinnedIcon.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowRoot.workspaceTitle.length > 0   // 置顶标题由界面层翻译；模型对 pinned 行只给 kind（workspaceTitle 为空）
                        ? rowRoot.workspaceTitle
                        : qsTrId("sessionBar_pinned")
                    color: Theme.color.textCaption
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowRoot.sessionCount > 0 ? String(rowRoot.sessionCount) : ""
                    color: Theme.color.textCaption
                    opacity: 0.7
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.small
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
                timeText: rowRoot.updatedAtMs > 0   // 0 = 时间未知，与上面 updatedAtMs 的约定一致
                    ? sessionBar.relativeTimeText(rowRoot.updatedAtMs)
                    : ""
                onSelectRequested: Hub.selectSession(rowRoot.sessionId)
                onMenuRequested: sessionBar.openRowContext(rowRoot.sessionId, false, rowRoot.y)
                onDeleteRequested: sessionBar.openRowContext(rowRoot.sessionId, false, rowRoot.y)
            }
        }
    }

    // 空列表引导：必须放在 ListView **外面**。挂在 ListView 里的声明子项会落到 contentItem（空列表时
    // contentHeight≈0），anchors.centerIn 会跑到视口上方；这里用与 list 同几何的兄弟 Item 居中。
    Item {
        anchors.fill: list
        visible: list.count === 0 && !Hub.initializing
        z: 1

        Text {
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.spacing.cardPadding
            horizontalAlignment: Text.AlignHCenter
            text: Hub.sessions.filterText.length > 0
                ? qsTrId("sessionBar_no_matches")
                : qsTrId("sessionBar_no_sessions")
            color: Theme.color.textCaption
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
            lineHeight: 1.4
        }
    }

    SessionFooter {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        busy: Hub.initializing
        accountInitial: qsTrId("sessionBar_account_initial")
        onScheduleRequested: Hub.openSchedulePage()
        onSettingsRequested: sessionBar.openSettingsRequested()
        onPluginsRequested: sessionBar.openPluginMarketRequested()
        onThemeRequested: Hub.toggleTheme()
        onExtensionsRequested: Hub.extensions.openPage()
        // 头像 = 账户管理入口：页面里显示真实登录态，未登录时页内按钮直接拉起登录窗口
        // （workbuddy-backend 接管后 getState/startSignIn 都是真实数据，含"已登录防误登出"）
        onAccountRequested: {
            sessionBar.accountEntryRequested()
            if ((HostUi.accountEntryOwner || "").length === 0)
                Hub.settings.openAccountPage()
        }
        onMoreRequested: {
            overflowMenu.extensionEntries = footer.overflowEntries
            overflowMenu.open()
        }
    }

    Rectangle {
        id: accountLoginBubble
        visible: Hub.settings.accountLoginHint
        z: 50
        anchors.left: footer.left
        anchors.leftMargin: 10
        anchors.bottom: footer.top
        anchors.bottomMargin: 8
        width: Math.min(220, sessionBar.width - 20)
        height: 42
        radius: 11
        color: Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            text: "登录完成后点这里返回账户"
            color: Theme.color.textPrimary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
            wrapMode: Text.WordWrap
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Hub.settings.openAccountPage()
        }
    }

    Popup {
        id: archivedPopup
        parent: sessionBar
        width: Math.min(320, sessionBar.width + 360)   // 放在侧栏右侧，不占列表本身
        height: Math.min(430, sessionBar.height - 40)
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // 锚在侧栏右侧、靠近底部“已归档会话”入口的位置。
        x: sessionBar.width + 8
        y: Math.max(8, Math.min(sessionBar.height - height - 8,
            sessionBar.height - footer.height - height - 24))

        property string confirmDeleteId: ""

        Connections {
            target: Hub
            function onArchivedSessionsChanged() {
                sessionBar.archivedModel = Hub.archivedSessions()
            }
        }

        background: Rectangle {
            radius: 10
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: archivedPopup.width
            spacing: 0

            Item {
                width: parent.width
                height: 46

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "已归档会话"
                    color: Theme.color.textPrimary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.body
                    font.weight: Theme.font.weightBold
                }

                Item {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 30

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: closeArchivedMouse.containsMouse
                            ? Theme.color.textPrimary : Theme.color.textTertiary
                        font.family: Theme.font.uiFamily
                        font.pixelSize: 18
                    }

                    MouseArea {
                        id: closeArchivedMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: archivedPopup.close()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.color.border
            }

            ListView {
                id: archivedList
                width: parent.width
                height: archivedPopup.height - 48
                clip: true
                model: sessionBar.archivedModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: Item {
                    required property var modelData
                    width: archivedList.width
                    height: archivedPopup.confirmDeleteId === modelData.sessionId ? 92 : 64

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        anchors.topMargin: 3
                        anchors.bottomMargin: 3
                        radius: 7
                        color: archivedRowMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.top: parent.top
                        anchors.topMargin: 9
                        spacing: 3

                        Text {
                            width: parent.width
                            text: modelData.title && String(modelData.title).length > 0
                                ? String(modelData.title) : "未命名会话"
                            color: Theme.color.textPrimary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: modelData.updatedAt > 0
                                ? sessionBar.relativeTimeText(modelData.updatedAt) : "已归档"
                            color: Theme.color.textCaption
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small
                        }
                    }

                    MouseArea {
                        id: archivedRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {}   // 行本身不打开会话；只能通过右侧按钮操作
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        spacing: 10
                        visible: archivedPopup.confirmDeleteId !== modelData.sessionId

                        Text {
                            text: "还原"
                            color: restoreMouse.containsMouse ? Theme.color.accent : Theme.color.textSecondary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small

                            MouseArea {
                                id: restoreMouse
                                anchors.fill: parent
                                anchors.margins: -5
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Hub.unarchiveArchivedSession(modelData.sessionId)
                                    archivedPopup.close()
                                }
                            }
                        }

                        Text {
                            text: "彻底删除"
                            color: deleteMouse.containsMouse ? Theme.color.danger : Theme.color.textTertiary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small

                            MouseArea {
                                id: deleteMouse
                                anchors.fill: parent
                                anchors.margins: -5
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: archivedPopup.confirmDeleteId = modelData.sessionId
                            }
                        }
                    }

                    Item {
                        visible: archivedPopup.confirmDeleteId === modelData.sessionId
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 54
                        height: 34

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "彻底删除后无法恢复，确定？"
                            color: Theme.color.textSecondary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Text {
                                text: "取消"
                                color: cancelDeleteMouse.containsMouse
                                    ? Theme.color.textPrimary : Theme.color.textSecondary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.small
                                MouseArea {
                                    id: cancelDeleteMouse
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: archivedPopup.confirmDeleteId = ""
                                }
                            }

                            Text {
                                text: "删除"
                                color: Theme.color.danger
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.small
                                font.weight: Theme.font.weightBold
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        Hub.deleteArchivedSession(modelData.sessionId)
                                        archivedPopup.confirmDeleteId = ""
                                        archivedPopup.close()
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: archivedList.count === 0
                    text: "没有已归档会话"
                    color: Theme.color.textCaption
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }
            }
        }
    }

    SessionOverflowMenu {   // 溢出菜单：危险动作（清空会话）与低频动作（新建工作区）收在这里，不再常驻首屏
        id: overflowMenu
        parent: sessionBar
        x: Math.round(sessionBar.width - width - 9)
        y: Math.round(sessionBar.height - footer.height - height - 4)
        onClearSessionsRequested: sessionBar.clearSessionsRequested()
        onNewWorkspaceRequested: Hub.createWorkspace()
        onArchivedSessionsRequested: sessionBar.openArchivedSessions()
    }

    // 行内删除的两步状态：pendingId 非空 = 弹出「删除」小按钮；confirmOpen = 换成确认卡。
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
        width: Math.min(168, sessionBar.width - 16)
        height: 38
        radius: 8
        color: deleteHover.containsMouse ? Theme.color.dangerBg : Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder
        x: sessionBar.width - width - 8
        readonly property real rowBottom: list.y + sessionBar.pendingY - list.contentY + 41
        y: Math.max(8, Math.min(sessionBar.height - height - 8,
            rowBottom + height <= sessionBar.height - 8 ? rowBottom : rowBottom - height - 49))

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: sessionBar.pendingIsWorkspace ? qsTrId("workdir_delete_label") : qsTrId("session_delete_label")
            color: deleteHover.containsMouse ? Theme.color.danger : Theme.color.textPrimary
            font.family: Theme.font.uiFamily
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

    Rectangle {   // 确认：一张居中的小卡片，**不压暗背景**（原版 QMessageBox 也不压暗）
        id: confirmCard

        z: 42
        visible: sessionBar.confirmOpen
        width: 320
        x: Math.max(8, sessionBar.width - 160)
        readonly property real rowTop: list.y + sessionBar.pendingY - list.contentY
        y: Math.max(8, Math.min(sessionBar.height - height - 8,
            rowTop + 42 + height <= sessionBar.height - 8
                ? rowTop + 42
                : rowTop - height - 8))
        implicitHeight: confirmColumn.implicitHeight + 28
        radius: 12
        color: Theme.color.panelBg
        border.width: 1
        border.color: Theme.color.inputBorder   // 同上：确认卡是浮层，用面板级环线 inputBorder(10%)，别跟着 border(4%) 一起淡到看不见

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
                font.family: Theme.font.uiFamily
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
