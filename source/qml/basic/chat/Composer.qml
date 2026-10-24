// 输入区：阴影卡片、内容列（输入框 / 控制行）与统计行。
// 内边距、字号、颜色均取 token；聚焦只加深描边，不加发光或第二层阴影。
// Enter 发送（Shift+Enter 换行，Ctrl+Enter 也发送），空文本静默丢弃。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 工作区 / 模式 / 模型这一行的上下文 chip
    component ContextChip: Rectangle {
        id: ctx
        property string iconName: ""
        property string label: ""
        property bool accent: false
        property bool clickable: false
        property int maxLabelWidth: 120
        signal clicked()

        implicitWidth: ctxRow.implicitWidth + 12
        implicitHeight: 22
        radius: 5
        color: ctx.accent
            ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.10)
            : (ctx.clickable && ctxMouse.containsMouse ? Theme.color.hoverBg : "transparent")
        border.width: 1
        border.color: ctx.accent
            ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.22)
            : Theme.color.border

        Row {
            id: ctxRow
            anchors.centerIn: parent
            spacing: 4
            LineIcon {
                name: ctx.iconName
                size: 11
                color: ctx.accent ? Theme.color.accent : Theme.color.textTertiary
                visible: ctx.iconName.length > 0
            }
            Text {
                text: ctx.label
                width: Math.min(implicitWidth, ctx.maxLabelWidth)
                color: ctx.accent ? Theme.color.accent : Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
                elide: Text.ElideRight
            }
            Text {
                visible: ctx.clickable
                text: "▾"
                color: ctx.accent ? Theme.color.accent : Theme.color.textTertiary
                font.pixelSize: 9
            }
        }

        MouseArea {
            id: ctxMouse
            anchors.fill: parent
            enabled: ctx.clickable
            hoverEnabled: true
            cursorShape: ctx.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: ctx.clicked()
        }
    }

    // 底栏小工具按钮：图标前缀 + 文字标签
    component CompactToolButton: Rectangle {
        id: toolButton
        property string prefix: ""
        property string label: ""
        property string tip: ""
        signal clicked()

        implicitWidth: toolContent.implicitWidth + 12
        implicitHeight: 26
        radius: 6
        color: toolMouse.containsMouse ? Theme.color.iconButtonHoverBg : "transparent"

        Row {
            id: toolContent
            anchors.centerIn: parent
            spacing: 4
            Text {
                text: toolButton.prefix
                color: Theme.color.textTertiary
                font.family: "Cascadia Code"
                font.pixelSize: Theme.font.small
            }
            Text {
                text: toolButton.label
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }
        }

        MouseArea {
            id: toolMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: toolButton.clicked()
        }
    }

    Component.onCompleted: HostUi.registerItem("composer", this)   // 扩展注入点

    // 拖入图片附件；addImageFiles 会按扩展名筛选
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: (drop) => {
            if (!drop.hasUrls)
                return
            var urls = []
            for (var i = 0; i < drop.urls.length; ++i)
                urls.push(drop.urls[i])
            Hub.addAttachments(urls)
        }
    }

    readonly property var commandMenuRef: commandMenu   // 供外部读取 Popup 状态

    property bool streaming: Hub.streaming
    readonly property bool cancelling: Hub.cancelling === true   // 已停止，等待收尾

    // 宽度正被外层动画驱动时按停阴影，避免反复重做离屏纹理；由 Shell 注入。
    property bool widthAnimating: false

    // 内边距与行距集中定义，保证文字、chip、发送键对齐。
    readonly property real cardPadH: Theme.spacing.cardPadding
    readonly property real cardPadTop: Theme.spacing.sectionGap
    readonly property real cardPadBottom: Theme.spacing.editorPadTop
    readonly property real cardGap: Theme.spacing.panelGap

    // 纯视觉可用状态：按钮仍可点，空文本由宿主静默丢弃。
    readonly property bool canSend: streaming || editor.text.trim().length > 0

    // 当前会话的权限预设（来自 Hub.permissionPreset，投影未到/不支持时为空）。
    readonly property string permissionLabel: {
        if (Hub.permissionPreset === "read-only")
            return qsTrId("permission_read_only")
        if (Hub.permissionPreset === "workspace-write")
            return qsTrId("permission_workspace_write")
        if (Hub.permissionPreset === "danger-full-access")
            return qsTrId("permission_full_access")
        return qsTrId("permission_title")
    }
    readonly property bool permissionIsFull: Hub.permissionPreset === "danger-full-access"
    readonly property var permissionOptions: [
        {
            id: "read-only",
            label: qsTrId("permission_read_only"),
            description: qsTrId("permission_read_only_desc")
        },
        {
            id: "workspace-write",
            label: qsTrId("permission_workspace_write"),
            description: qsTrId("permission_workspace_write_desc")
        },
        {
            id: "danger-full-access",
            label: qsTrId("permission_full_access"),
            description: qsTrId("permission_full_access_desc")
        }
    ]

    implicitHeight: shadowHost.height + Theme.size.statsLineHeight

    function submit() {
        const text = editor.text
        if (text.trim().length === 0)
            return      // 空文本静默丢弃
        Hub.sendPrompt(text)
        editor.clear()
        editor.forceActiveFocus()
    }

    function stop() {
        Hub.cancelPrompt()
    }

    // 按键路由：返回 true 表示已消费。
    function handleKey(event) {
        if (commandMenu.visible) {   // 斜杠菜单优先：↑↓ 选择、Tab 补全、Esc 收起
            if (event.key === Qt.Key_Down) {
                commandMenu.move(1)
                return true
            }
            if (event.key === Qt.Key_Up) {
                commandMenu.move(-1)
                return true
            }
            if (event.key === Qt.Key_Escape) {
                commandMenu.dismissed = true
                commandMenu.close()
                return true
            }
            if (event.key === Qt.Key_Tab) {
                commandMenu.complete()
                return true
            }
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (event.modifiers & Qt.ShiftModifier)   // Shift 换行，Ctrl+Enter 仍发送
                return false
            if (commandMenu.visible && !commandMenu.isExact()) {   // 候选未完整时先补全
                commandMenu.complete()
                return true
            }
            if (root.streaming)
                root.stop()
            else
                root.submit()
            return true
        }
        return false
    }

    // 阴影卡片
    Item {
        id: shadowHost
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: capsule.implicitHeight

        // 阴影与 Card 同档，避免输入卡比顶栏卡片更抢眼。
        MultiEffect {
            anchors.fill: capsule
            anchors.topMargin: Theme.shadow.inputDy
            source: capsule
            visible: !root.widthAnimating   // 动画期间改为直接绘制卡片本体，避免每帧栅格化 source
            shadowEnabled: true
            shadowBlur: Theme.shadow.inputBlur
            shadowColor: Theme.color.shadowSubtle
            shadowVerticalOffset: Theme.shadow.inputDy
            shadowHorizontalOffset: 0
            autoPaddingEnabled: true
        }

        // 输入卡片
        Rectangle {
            id: capsule
            // 静止时只作为阴影源；宽度动画期间直接绘制。
            visible: root.widthAnimating
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            implicitHeight: content.implicitHeight + root.cardPadTop + root.cardPadBottom
            radius: Theme.radius.composer
            // 皮肤可用 composerBg 单独覆盖输入卡底色，未定义时回退 panelBg。
            color: Theme.color.composerBg || Theme.color.panelBg
            // 聚焦只加深灰色描边，不用 accent 蓝，也不加发光或阴影。
            readonly property color restingBorder: Theme.isDark ? "#2E3034" : "#E3E5E9"
            readonly property color activeBorder: Theme.isDark ? "#4A4D53" : "#C6CAD0"
            border.color: (editor.activeFocus || dropArea.containsDrag) ? activeBorder : restingBorder
            border.width: 1
        }

        // 真实内容，与 capsule 同尺寸
        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: root.cardPadTop
            spacing: root.cardGap

            Row {
                id: contextRow
                x: root.cardPadH
                width: Math.max(0, parent.width - 2 * root.cardPadH)
                height: 24
                spacing: 6
                clip: true

                ContextChip {
                    id: workspaceChip
                    clickable: true
                    iconName: "folder"
                    label: Hub.currentWorkspaceTitle.length > 0
                        ? Hub.currentWorkspaceTitle
                        : qsTrId("sessionBar_ungrouped")
                    onClicked: workspaceMenu.open()
                }
                ContextChip {
                    id: agentChip
                    clickable: true
                    iconName: "bot"
                    label: "助手"
                    accent: true
                    onClicked: agentMenu.open()
                }
                ModelSelectorChip {
                    id: chip
                    onSelectModelRequested: (provider, model) => Hub.selectModel(provider, model)
                    onSelectLevelRequested: (levelId) => Hub.selectLevel(levelId)
                }
            }

            // 待发图片（缩略图 + 移除）
            Item {
                id: pendingRow
                width: parent.width
                visible: Hub.pendingAttachments.length > 0
                height: visible ? 54 : 0
                clip: true

                Row {
                    x: root.cardPadH
                    width: Math.max(0, parent.width - 2 * root.cardPadH)
                    spacing: 8

                Repeater {
                    model: Hub.pendingAttachments

                    Rectangle {
                        required property var modelData
                        required property int index
                        width: 52
                        height: 52
                        radius: 8
                        color: Theme.isDark ? "#232427" : "#F2F3F5"
                        border.width: 1
                        border.color: Theme.isDark ? "#3A3D42" : "#D8DBE0"
                        clip: true

                        // 圆角遮罩：clip 只能裁矩形，圆角处会露方角
                        Rectangle {
                            id: thumbMask
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: 7
                            visible: false
                            layer.enabled: true
                        }

                        Image {
                            id: thumb
                            anchors.fill: parent
                            anchors.margins: 1
                            fillMode: Image.PreserveAspectCrop
                            source: modelData.preview !== undefined ? modelData.preview : ""
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: thumbMask
                            }

                            property int retry: 0   // 解码失败时带 cache-bust 重试

                            onStatusChanged: {
                                if (status === Image.Error && retry < 3)
                                    retryTimer.start()
                            }

                            Timer {
                                id: retryTimer
                                interval: 120
                                repeat: false
                                onTriggered: {
                                    thumb.retry += 1
                                    var base = modelData.preview !== undefined ? modelData.preview : ""
                                    if (base !== "")
                                        thumb.source = base + (base.indexOf("?") >= 0 ? "&" : "?") + "r=" + thumb.retry
                                }
                            }
                        }

                        Rectangle {
                            width: 16
                            height: 16
                            radius: 8
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 2
                            color: Theme.color.overlayBg

                            Text {
                                anchors.centerIn: parent
                                text: "\u2715"
                                color: Theme.color.textPrimary
                                font.pixelSize: 9
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Hub.removeAttachment(index)
                            }
                        }
                    }
                }
                }
            }

            // 编辑区：高度随内容增长，封顶后由 Flickable 滚动。
            Flickable {
                id: editorView
                width: parent.width
                height: Math.min(Math.max(editor.contentHeight + editor.topPadding + editor.bottomPadding,
                                          Theme.size.editorMinHeight),
                                 Theme.size.editorMaxHeight)
                contentWidth: width
                contentHeight: editor.height
                clip: true
                interactive: false   // 按住鼠标用于选文字，不让 Flickable 抢走
                boundsBehavior: Flickable.StopAtBounds

                // 只有内容超高时才给滚动条让出宽度。
                readonly property bool needsScroll: contentHeight > height + 0.5

                ScrollBar.vertical: AppScrollBar {
                    id: editorScrollBar
                    objectName: "composerScrollBar"
                    policy: editorView.needsScroll ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                // 光标跑出可视区时滚动内容，上下各留 editorPadTop。
                function ensureCursorVisible() {
                    if (!needsScroll)
                        return
                    const r = editor.cursorRectangle
                    const maxY = Math.max(0, contentHeight - height)
                    if (r.y < contentY)
                        contentY = Math.max(0, r.y - Theme.spacing.editorPadTop)
                    else if (r.y + r.height > contentY + height)
                        contentY = Math.min(maxY, r.y + r.height - height + Theme.spacing.editorPadTop)
                }

                onHeightChanged: ensureCursorVisible()   // 换行/删行后跟随光标

                // 显式处理滚轮，兼容 interactive=false，且不影响拖选文字。
                WheelHandler {
                    onWheel: (event) => {
                        const maxY = Math.max(0, editorView.contentHeight - editorView.height)
                        if (maxY <= 0)
                            return
                        editorView.contentY = Math.min(maxY, Math.max(0,
                            editorView.contentY - event.angleDelta.y / 2))
                    }
                }

                TextEdit {
                    id: editor
                    objectName: "composerEditor"
                    // 超高时为滚动条让出宽度
                    width: editorView.width - (editorView.needsScroll
                                               ? Theme.size.scrollbarThickness : 0)
                    leftPadding: root.cardPadH
                    rightPadding: root.cardPadH
                    topPadding: Theme.spacing.editorPadTop
                    bottomPadding: 0
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    selectByMouse: true
                    persistentSelection: true
                    color: Theme.color.textPrimary
                    selectionColor: Theme.color.selectionBg
                    selectedTextColor: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body   // 与 SearchField 输入文字同档
                    // 高度等于全文高度，夹取与滚动交给外层 editorView。
                    height: contentHeight + topPadding + bottomPadding
                    onCursorRectangleChanged: editorView.ensureCursorVisible()

                    // 占位符与正文同列同基线
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: editor.leftPadding
                        anchors.top: parent.top
                        anchors.topMargin: editor.topPadding
                        visible: editor.text.length === 0
                        text: qsTrId("chat_input_placeholder")
                        color: Theme.color.textCaption
                        font.family: editor.font.family
                        font.pixelSize: editor.font.pixelSize
                    }

                    onTextChanged: {
                        commandMenu.dismissed = false     // 敲字后重新参与候选
                        commandMenu.syncVisibility()
                        if (text === "/")   // 懒拉一次命令表，兼容运行中注册的命令
                            Hub.commands.refresh(Hub.currentSessionId)
                    }

                    Keys.onPressed: (event) => {
                        if (root.handleKey(event))
                            event.accepted = true
                    }
                }
            }

            // 控制行：左侧工具、右侧发送键
            Item {
                id: controlRow
                width: parent.width
                implicitHeight: sendButton.implicitHeight

                // 附件：原生文件对话框，支持多选
                Rectangle {
                    id: attachButton
                    anchors.left: parent.left
                    anchors.leftMargin: root.cardPadH
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    radius: Theme.radius.small
                    color: attachArea.containsMouse ? Theme.color.iconButtonHoverBg : "transparent"

                    LineIcon {
                        anchors.centerIn: parent
                        name: "plus"
                        size: 17
                        color: attachArea.containsMouse ? Theme.color.textPrimary : Theme.color.textTertiary
                    }

                    MouseArea {
                        id: attachArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Hub.chooseImageFiles()
                    }
                }

                // 命令：等价于在输入框敲 “/”
                CompactToolButton {
                    id: commandButton
                    anchors.left: attachButton.right
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    prefix: "/"
                    label: qsTrId("composer_command_tip")
                    tip: qsTrId("composer_command_tip")
                    onClicked: {
                        editor.text = "/"
                        editor.cursorPosition = 1
                        editor.forceActiveFocus()
                    }
                }

                // 上下文：插入 “@” 供后续引用
                CompactToolButton {
                    id: contextButton
                    anchors.left: commandButton.right
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    prefix: "@"
                    label: qsTrId("composer_context_tip")
                    tip: qsTrId("composer_context_tip")
                    onClicked: {
                        editor.text += "@"
                        editor.cursorPosition = editor.text.length
                        editor.forceActiveFocus()
                    }
                }

                // 权限预设直接显示在按钮上，点开切换
                Rectangle {
                    id: permissionButton
                    anchors.left: contextButton.right
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: permissionRow.implicitWidth + 14
                    height: 26
                    radius: 6
                    color: root.permissionIsFull
                        ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.10)
                        : (permissionMouse.containsMouse ? Theme.color.iconButtonHoverBg : "transparent")
                    border.width: 1
                    border.color: root.permissionIsFull
                        ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.24)
                        : "transparent"

                    Row {
                        id: permissionRow
                        anchors.centerIn: parent
                        spacing: 4

                        LineIcon {
                            name: "tools"
                            size: 13
                            color: root.permissionIsFull ? Theme.color.accent : Theme.color.textTertiary
                        }
                        Text {
                            text: root.permissionLabel
                            color: root.permissionIsFull ? Theme.color.accent : Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                        Text {
                            text: "▾"
                            color: Theme.color.textCaption
                            font.pixelSize: 9
                        }
                    }

                    MouseArea {
                        id: permissionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: permissionMenu.open()
                    }
                }

                IconButton {
                    id: sendButton
                    anchors.right: parent.right
                    anchors.rightMargin: root.cardPadH   // 与编辑器文字右缘同列
                    anchors.verticalCenter: parent.verticalCenter
                    boxSize: Theme.size.sendButton
                    // 成对的明暗资源，四条路径写字面量，便于改名与检查。
                    readonly property string sendIcon: Theme.isDark
                        ? "qrc:/ULinHub/buttons/enter-btn-dark.png"
                        : "qrc:/ULinHub/buttons/enter-btn.png"
                    readonly property string stopIcon: Theme.isDark
                        ? "qrc:/ULinHub/buttons/stop-btn-dark.png"
                        : "qrc:/ULinHub/buttons/stop-btn.png"
                    iconSource: root.streaming ? stopIcon : sendIcon
                    iconSize: Theme.size.sendButton
                    opacity: root.canSend && !root.cancelling ? 1.0 : 0.45
                    circleHoverOverlay: root.canSend && !root.cancelling
                    // 暗色主题圆底是白色，悬停蒙版必须用压暗色。
                    circleHoverTint: Theme.isDark ? "#000000" : "#FFFFFF"
                    tooltip: root.cancelling
                        ? qsTrId("chat_stopping")
                        : (root.streaming ? qsTrId("chat_stop_output") : qsTrId("common_send"))
                    // 按钮始终可点；等待收尾期间直接吞掉点击，避免重复取消。
                    onClicked: {
                        if (root.cancelling)
                            return
                        if (root.streaming)
                            root.stop()
                        else
                            root.submit()
                    }
                }
            }
        }
    }

    // 工作区切换：选中一个工作区就打开它最近的会话，没有则新建
    Popup {
        id: workspaceMenu
        parent: root
        width: 264
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.max(8, Math.min(root.width - width - 8, workspaceChip.mapToItem(root, 0, 0).x))
        y: workspaceChip.mapToItem(root, 0, workspaceChip.height).y + 6

        readonly property var items: Hub.sessions ? Hub.sessions.workspaceItems() : []

        function activate(item) {
            const first = Hub.sessions.firstSessionForWorkspace(item.workspaceId)
            if (first.length > 0)
                Hub.selectSession(first)
            else
                Hub.createSession(item.ungrouped ? "" : item.workspaceId)
            close()
        }

        background: Rectangle {
            radius: 10
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: workspaceMenu.width - 12
            spacing: 2

            Text {
                width: parent.width
                height: 24
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("composer_workspace_switch")
                color: Theme.color.textCaption
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }

            Repeater {
                model: workspaceMenu.items

                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 30
                    radius: 5
                    color: itemMouse.containsMouse
                        ? Theme.color.hoverBg
                        : ((modelData.workspaceId === Hub.currentWorkspaceId
                            || (modelData.ungrouped && Hub.currentWorkspaceId.length === 0))
                           ? Theme.color.activeBg : "transparent")

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Text {
                            text: modelData.title
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            elide: Text.ElideRight
                            width: Math.max(0, parent.width - 46)
                        }
                        Text {
                            text: modelData.count
                            color: Theme.color.textCaption
                            font.pixelSize: Theme.font.small
                            width: 16
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            visible: modelData.workspaceId === Hub.currentWorkspaceId
                                || (modelData.ungrouped && Hub.currentWorkspaceId.length === 0)
                            text: "✓"
                            color: Theme.color.accent
                            font.pixelSize: Theme.font.small
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: workspaceMenu.activate(modelData)
                    }
                }
            }

            Text {
                visible: workspaceMenu.items.length === 0
                width: parent.width
                height: 28
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("composer_workspace_empty")
                color: Theme.color.textCaption
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }

            Rectangle { width: parent.width; height: 1; color: Theme.color.border }

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                color: newSessionMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("workdir_new_session_tip")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }
                MouseArea {
                    id: newSessionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Hub.createSession(Hub.currentWorkspaceId)
                        workspaceMenu.close()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                color: newWorkspaceMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("sessionBar_new_workspace")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }
                MouseArea {
                    id: newWorkspaceMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Hub.createWorkspace()
                        workspaceMenu.close()
                    }
                }
            }
        }
    }

    // Agent 菜单：当前模式说明、子智能体切换、工具权限入口
    Popup {
        id: agentMenu
        parent: root
        width: 280
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.max(8, Math.min(root.width - width - 8, agentChip.mapToItem(root, 0, 0).x))
        y: agentChip.mapToItem(root, 0, agentChip.height).y + 6

        readonly property var peers: Hub.peerTabs || []

        background: Rectangle {
            radius: 10
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: agentMenu.width - 12
            spacing: 2

            Text {
                width: parent.width
                height: 24
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("composer_agent_current")
                color: Theme.color.textCaption
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }
            Text {
                width: parent.width
                height: 22
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("composer_agent_desc")
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }

            Repeater {
                model: agentMenu.peers

                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 30
                    radius: 5
                    color: peerMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Rectangle {
                            width: 5
                            height: 5
                            radius: 3
                            color: modelData.running ? Theme.color.accent : Theme.color.textCaption
                        }
                        Text {
                            text: modelData.label
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            elide: Text.ElideRight
                            width: Math.max(0, parent.width - 24)
                        }
                    }

                    MouseArea {
                        id: peerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Hub.selectSession(modelData.id)
                            agentMenu.close()
                        }
                    }
                }
            }

            Text {
                visible: agentMenu.peers.length === 0
                width: parent.width
                height: 28
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("composer_agent_empty")
                color: Theme.color.textCaption
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }

            Rectangle { width: parent.width; height: 1; color: Theme.color.border }

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                color: toolsMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("composer_agent_tools")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }
                MouseArea {
                    id: toolsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Hub.openToolsFilter()
                        agentMenu.close()
                    }
                }
            }
        }
    }

    // 权限菜单：会话级权限预设，完全权限需二次确认
    Popup {
        id: permissionMenu
        parent: root
        width: 292
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.max(8, Math.min(root.width - width - 8, permissionButton.mapToItem(root, 0, 0).x))
        y: permissionButton.mapToItem(root, 0, 0).y - height - 8

        background: Rectangle {
            radius: 10
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: permissionMenu.width - 12
            spacing: 2

            Text {
                width: parent.width
                height: 24
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("permission_title")
                color: Theme.color.textCaption
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
            }

            Repeater {
                model: root.permissionOptions

                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 46
                    radius: 5
                    color: optionMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        anchors.right: optionCheck.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            text: modelData.label
                            color: modelData.id === "danger-full-access"
                                ? Theme.color.danger : Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                        }
                        Text {
                            text: modelData.description
                            width: parent.width
                            color: Theme.color.textCaption
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        id: optionCheck
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Hub.permissionPreset === modelData.id
                        text: "✓"
                        color: Theme.color.accent
                        font.pixelSize: Theme.font.caption
                    }

                    MouseArea {
                        id: optionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            permissionMenu.close()
                            if (modelData.id === "danger-full-access")
                                permissionConfirm.open()
                            else
                                Hub.setPermissionPreset(modelData.id)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: permissionConfirm
        parent: root
        anchors.centerIn: Overlay.overlay
        width: 380
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            radius: 12
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: 380

            Item {
                width: parent.width
                height: 46

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("permission_confirm_title")
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightBold
                }
            }

            Text {
                x: 20
                width: parent.width - 40
                text: qsTrId("permission_confirm_desc")
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                wrapMode: Text.WordWrap
                lineHeight: 1.4
            }

            Row {
                width: parent.width - 40
                x: 20
                height: 32
                spacing: 8

                Item { width: parent.width - 84 - 118 - 16; height: 1 }

                ActionButton {
                    width: 84
                    height: 32
                    text: qsTrId("common_cancel")
                    variant: "ghost"
                    onClicked: permissionConfirm.close()
                }
                ActionButton {
                    width: 118
                    height: 32
                    text: qsTrId("permission_confirm_ack")
                    variant: "primary"
                    onClicked: {
                        Hub.setPermissionPreset("danger-full-access")
                        permissionConfirm.close()
                    }
                }
            }

            Item { width: 1; height: 14 }
        }
    }

    // 统计行：固定高度，始终占位，文本为空时不绘制
    Item {
        id: statsLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 0   // 底边对齐会话栏底部
        height: Theme.size.statsLineHeight

        Row {
            anchors.centerIn: parent
            spacing: 5
            visible: statsText.text.length > 0

            Item {
                width: 4
                height: parent.height
                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: 4
                    radius: 2
                    color: Hub.streaming ? Theme.color.accent : Theme.color.textCaption
                    opacity: Hub.streaming ? 1 : 0.55
                }
            }

            Text {
                id: statsText
                width: Math.min(implicitWidth, statsLine.width - 24)
                height: statsLine.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: Hub.statsLine
                elide: Text.ElideRight
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
                font.letterSpacing: 0.15
            }
        }

        // 仅在文本被截断时显示 tooltip
        AppToolTip {
            visible: statsText.truncated && statsLineMouse.containsMouse
            text: Hub.statsLine
        }

        MouseArea {
            id: statsLineMouse
            anchors.fill: parent
            hoverEnabled: true
        }
    }

    // 斜杠命令菜单：Popup 走 Overlay，避免被后声明的消息列表盖住；不抢输入框焦点。
    // 候选来自 Hub.commands，这里只做前缀过滤。
    Popup {
        id: commandMenu
        objectName: "composerCommandMenu"

        readonly property var candidates: Hub.commands.matching(editor.text)
        property int index: 0
        property bool dismissed: false
        property bool suppressReopen: false   // 补全引起的文本变化不重新弹菜单

        function current() {
            if (candidates.length === 0)
                return null
            return candidates[Math.max(0, Math.min(index, candidates.length - 1))]
        }
        function move(step) {
            if (candidates.length === 0)
                return
            index = Math.max(0, Math.min(index + step, candidates.length - 1))
        }
        function isExact() {
            const item = current()
            return item !== null && editor.text === "/" + item.name
        }
        function complete() {
            const item = current()
            if (item === null)
                return
            suppressReopen = true
            // 有参数提示的命令补一个空格，否则直接补上名字
            editor.text = "/" + item.name + (item.hint ? " " : "")
            editor.cursorPosition = editor.text.length
            suppressReopen = false
            dismissed = true
            close()
            editor.forceActiveFocus()
        }
        // visible 不能直接绑定：closePolicy 关闭时会拆掉绑定，只能显式开关。
        function syncVisibility() {
            if (suppressReopen || dismissed || candidates.length === 0)
                close()
            else
                open()
        }

        onCandidatesChanged: syncVisibility()
        onClosed: dismissed = true   // 等用户再次输入才重新弹出

        parent: root
        x: 16
        y: shadowHost.y - height - 8   // 贴输入卡上沿 8px
        width: Math.min(560, root.width - 32)
        padding: 6
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 12                    // 与 ModelSelectorChip 的菜单同款
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Flickable {
            id: menuScroll
            implicitHeight: Math.min(contentHeight, 320)   // 命令多时封顶，避免顶出屏幕
            contentHeight: menuColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: menuColumn
                width: menuScroll.width
                spacing: 2

                Repeater {
                    model: commandMenu.candidates

                    delegate: Rectangle {
                        id: commandRow
                        required property var modelData
                        required property int index

                        width: menuColumn.width
                        height: 34
                        radius: 8
                        color: (commandRow.index === commandMenu.index || rowMouse.containsMouse)
                               ? Theme.color.hoverBg : "transparent"

                        Row {
                            id: rowHost
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Text {
                                id: nameText
                                text: "/" + commandRow.modelData.name
                                color: Theme.color.textPrimary   // 输入区保持灰阶，不使用 accent 蓝
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: hintText
                                text: commandRow.modelData.hint   // 命令参数提示
                                visible: text.length > 0
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: commandRow.modelData.description
                                width: Math.max(0, rowHost.width - nameText.width - hintText.width
                                                   - (hintText.visible ? rowHost.spacing * 2 : rowHost.spacing))
                                elide: Text.ElideRight
                                color: Theme.color.textCaption
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: commandMenu.index = commandRow.index
                            onClicked: commandMenu.complete()
                        }
                    }
                }
            }
        }
    }
}
