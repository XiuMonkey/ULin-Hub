// 侧栏：Logo + 新建会话 + 工作区/会话列表 + 底部图标行。
// 数据来自 Hub.sessions（摊平的工作区/会话行模型，见 include/qmlbridge/SessionListModel.h）。
// 布局用锚点而不是 Column：列表要吃掉剩余高度，锚点比"减一圈高度"可靠。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/sidebar"

Item {
    id: sidebar

    // 扩展注入点（见 HostExports.h 的名字契约）；五条连接交给宿主建：有真实 handle ⇒ 可挂起/恢复/接管/交还。
    // 签名手写：带回参的信号要写全类型；接收端必须具名且**参数带类型标注**（未标注在元对象里是 QVariant，接不上）。
    Component.onCompleted: {
        HostUi.registerItem("sidebar", this)
        HostConnections.RegisterConnection("qml.sidebar.sessionCreateFailed", Hub, "sessionCreateFailed(QString)", sidebar, "handleSessionCreateFailed(QString)")
        HostConnections.RegisterConnection("qml.sidebar.openSettings", sidebar, "openSettingsRequested()", sidebar, "handleOpenSettings()")
        HostConnections.RegisterConnection("qml.sidebar.openPluginMarket", sidebar, "openPluginMarketRequested()", sidebar, "handleOpenPluginMarket()")
        HostConnections.RegisterConnection("qml.sidebar.openPluginTranspiler", sidebar, "openTranspilerRequested()", sidebar, "handleOpenTranspiler()")
        // 清空会话与上面三条同一条路：默认处理器在本组件里，扩展可接管这条 index 换成自己的实现
        HostConnections.RegisterConnection("qml.sidebar.clearSessions", sidebar, "clearSessionsRequested()", sidebar, "handleClearSessions()")
    }
    // 五条连接的默认处理器（没人接管时 = 宿主原本行为）
    function handleSessionCreateFailed(message: string) {
        sidebar.notify(message)
    }
    function handleOpenSettings() {
        Hub.settings.openPage()
    }
    function handleOpenPluginMarket() {
        Hub.plugins.openPage()
    }
    // 插件转译器面板由客户端扩展 PluginTranspiler 提供（它接管 qml.sidebar.openPluginTranspiler）；
    // 扩展没装 / 没接上时这里给一句提示，别放一颗点了没反应的哑按钮。
    function handleOpenTranspiler() {
        sidebar.notify("插件转译器扩展未安装")
    }
    // 清空会话：业务全在 core（ClientRuntime::clearAllSessions），这里只转调
    function handleClearSessions() {
        Hub.clearAllSessions()
    }

    // 侧栏自己不弹 Toast：只把文案抛给壳层（Shell.qml 里统一放一个 Toast）
    signal notify(string text)

    // 三个入口的请求信号：按钮不直接开页面，谁响应由连接表决定（扩展接管后就换成扩展自己的面板）
    signal openSettingsRequested()
    signal openPluginMarketRequested()
    signal openTranspilerRequested()
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
                ? "qrc:/ULinHub/ulin-hub-logo-tiny-dark@2x.png"
                : "qrc:/ULinHub/ulin-hub-logo-tiny@2x.png"
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
        onClicked: sidebar.clearSessionsRequested()
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
            iconSource: "qrc:/ULinHub/setting-icon.png"
            tooltip: qsTrId("settings_title")
            onClicked: sidebar.openSettingsRequested()
        }
        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/plugin-icon.png"
            tooltip: qsTrId("sidebar_plugins")
            onClicked: sidebar.openPluginMarketRequested()
        }
        // 插件转译器（客户端扩展 PluginTranspiler 的入口）
        // 图标用来回箭头字形而不是 PNG：assets\resources 里没有转译器专用图标，复用
        // extension-icon.png 会跟旁边那颗「扩展管理」撞图（实测两颗 Ext 并排分不清）。
        // 字形走 IconButton 的 glyph 通道（与 TopBar 的 "☰" 同一条路）。文案暂不加进 .ts，等 M6 统一补 id。
        // ⚠️ 它必须**显式对齐**那排 PNG 的口径：原版图标是"键 32×32、图 20×20、主色 #6B7280"
        //    （颜色烘在 PNG 里，两档主题都是同一个值），而 IconButton 的默认字形口径是
        //    glyphSize = Theme.font.lead = 16px、glyphColor = Theme.color.iconButtonText
        //    （浅色 #666666 / 深色 #9CA3AF）。U+21C4 在 Segoe UI Symbol 里只有约 0.70 em 宽，
        //    16px 时墨迹实测仅 11×10，夹在一排 15~20px 墨迹的图标里就显得又小又细、色也偏中性
        //    —— 这就是这颗按钮"和原版有细微差别"的原因。24px 让墨迹到 17×16，与邻座齐平。
        //    颜色写字面量而不是 Theme.color.textSecondary：只有浅色主题它恰好等于 #6B7280，
        //    深色主题是 #9CA3AF，而 PNG 两档都不变 ⇒ 只有字面量能在两档都对上。
        //    glyphColorHover 一并钉住：PNG 图标悬停只变底色、图标本身不变色（字形默认会变深/变亮）。
        // ⚠️ 垂直方向也得钉：`anchors.centerIn` 居中的是 Text 的**行盒**，而行盒是字体级的，
        //    U+21C4 的墨迹落在盒中线**下方 2.9px**。旁边四个 PNG 图标是精确居中的（实测偏差
        //    ≤0.53px），所以这颗的字形看着明显偏下。专门渲染出来数像素量过（16× 超采样）：
        //    偏移 −2 → 残差 +0.94、−2.5 → +0.44、−3 → −0.06、−3.5 → −0.56，取 −3。
        //    这个值只跟"字形 + glyphSize=24"绑定，换字形或换字号要重新量。
        IconButton {
            boxSize: 32
            glyph: "⇄"
            glyphSize: 24
            glyphColor: "#6B7280"
            glyphColorHover: "#6B7280"
            contentYOffset: -3
            tooltip: "插件转译器"
            onClicked: sidebar.openTranspilerRequested()
        }
        // 主题：原版 Sidebar::themeToggleRequested -> ULinHub::toggleTheme（切模式 + 过渡卡片）
        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/theme-icon.png"
            tooltip: qsTrId("sidebar_switch_theme")
            onClicked: Hub.toggleTheme()
        }
        IconButton {
            boxSize: 32
            iconSize: 20
            iconSource: "qrc:/ULinHub/extension-icon.png"
            tooltip: qsTrId("ext_manager_title")
            onClicked: Hub.extensions.openPage()
        }

        // addSpacing(15)：给扩展按钮留的位置
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
                    sidebar.pendingId = rowRoot.workspaceId
                    sidebar.pendingIsWorkspace = true
                    sidebar.pendingY = rowRoot.y
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
                    sidebar.pendingId = rowRoot.sessionId
                    sidebar.pendingIsWorkspace = false
                    sidebar.pendingY = rowRoot.y
                }
            }
        }

        // 空列表时的引导
        Text {
            anchors.centerIn: parent
            visible: list.count === 0 && !Hub.initializing
            width: list.width - 2 * Theme.spacing.cardPadding
            horizontalAlignment: Text.AlignHCenter
            text: "还没有会话\n点右上角 ＋ 新建"
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
        visible: sidebar.pendingId.length > 0
        onClicked: {
            sidebar.pendingId = ""
            sidebar.confirmOpen = false
        }
    }

    Rectangle {
        id: deleteButton

        z: 41
        visible: sidebar.pendingId.length > 0 && !sidebar.confirmOpen
        width: 132
        height: 36
        radius: 10
        color: deleteHover.containsMouse ? Theme.color.dangerBg : Theme.color.panelBg
        border.width: 1
        border.color: Theme.color.border
        x: Math.max(4, sidebar.width - width - 12)
        y: Math.max(4, Math.min(sidebar.height - height - 4,
            list.y + sidebar.pendingY - list.contentY + list.spacing))

        Text {
            anchors.centerIn: parent
            // 会话用既有 id；工作区没有专门 id，先写字面量（TODO(M6)：等 .ts 补）
            text: sidebar.pendingIsWorkspace ? "删除工作区" : qsTrId("session_delete_label")
            color: deleteHover.containsMouse ? Theme.color.danger : Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }

        MouseArea {
            id: deleteHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sidebar.confirmOpen = true    // 第二步：确认（原版是原生 QMessageBox，同样不遮罩）
        }
    }

    // 确认：一张居中的小卡片，**不压暗背景**（原版 QMessageBox 也不压暗）
    Rectangle {
        id: confirmCard

        z: 42
        visible: sidebar.confirmOpen
        anchors.centerIn: parent
        width: 320
        implicitHeight: confirmColumn.implicitHeight + 28
        radius: Theme.radius.panel
        color: Theme.color.panelBg
        border.width: 1
        border.color: Theme.color.border

        Column {
            id: confirmColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 14

            Text {
                width: parent.width
                text: sidebar.pendingIsWorkspace
                    ? "确定要删除这个工作区吗？只会把它从工作区列表移除，不删除它下面的会话。"
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
                            sidebar.confirmOpen = false
                            sidebar.pendingId = ""
                        }
                    }

                    ActionButton {
                        width: 88
                        height: 34
                        text: qsTrId("common_delete")
                        variant: "danger"
                        onClicked: {
                            if (sidebar.pendingIsWorkspace)
                                Hub.deleteWorkspace(sidebar.pendingId)
                            else
                                Hub.deleteSession(sidebar.pendingId)
                            sidebar.confirmOpen = false
                            sidebar.pendingId = ""
                        }
                    }
                }
            }
        }
    }
}
