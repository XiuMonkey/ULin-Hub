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

    // “助手”/“完全权限”这类中性 chip 的底色：比 5% hover 灰深一点，比 activeBg 浅一点。
    readonly property color neutralChipBg: Theme.color.tagBg || (Theme.isDark ? "#333437" : "#F2F4F6")

    // 工作区 / 模式 / 模型这一行的上下文 chip
    component ContextChip: Rectangle {
        id: ctx
        property string iconName: ""     // 左侧 LineIcon 名；空串时整颗图标不显示
        property string label: ""        // chip 文本，超长按 maxLabelWidth 省略
        property bool accent: false      // accent 淡底 + accent 字，标记当前生效项
        // 中性高亮：浅灰底 + 灰字，用于“助手”这类需要强调但不抢 accent 的上下文 chip。
        property bool neutral: false
        property bool clickable: false
        property int maxLabelWidth: 120
        signal clicked()

        implicitWidth: ctxRow.implicitWidth + 12   // 文字宽度 + 左右各 6px
        implicitHeight: 22
        radius: 5
        // 状态级联：neutral（中性强调）> accent（当前生效）> hover > 透明。
        color: ctx.neutral
            ? root.neutralChipBg
            : (ctx.accent
                ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.10)
                : (ctx.clickable && ctxMouse.containsMouse ? Theme.color.hoverBg : "transparent"))
        border.width: 1
        border.color: ctx.neutral
            ? Theme.color.border
            : (ctx.accent
                ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.22)
                : Theme.color.border)

        Row {
            id: ctxRow
            anchors.centerIn: parent
            spacing: 4
            LineIcon {
                name: ctx.iconName
                size: 11
                color: ctx.neutral ? Theme.color.textSecondary
                    : (ctx.accent ? Theme.color.accent : Theme.color.textTertiary)
                visible: ctx.iconName.length > 0
            }
            Text {
                text: ctx.label
                width: Math.min(implicitWidth, ctx.maxLabelWidth)   // 超长 label 省略，避免把控制行挤出可视区
                color: ctx.neutral ? Theme.color.textSecondary
                    : (ctx.accent ? Theme.color.accent : Theme.color.textTertiary)
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
                elide: Text.ElideRight
            }
            Text {
                visible: ctx.clickable
                text: "▾"
                color: ctx.neutral ? Theme.color.textTertiary
                    : (ctx.accent ? Theme.color.accent : Theme.color.textTertiary)
                font.pixelSize: 9   // 下拉箭头比正文小一档
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
        property string prefix: ""   // 图标位：用等宽字体渲染 "/"、"@" 等符号
        property string label: ""
        property string tip: ""
        signal clicked()

        implicitWidth: toolContent.implicitWidth + 12   // 左右各 6px
        implicitHeight: 26   // 与权限按钮同高，控制行内对齐
        radius: 6
        color: toolMouse.containsMouse ? Theme.color.iconButtonHoverBg : "transparent"

        Row {
            id: toolContent
            anchors.centerIn: parent
            spacing: 4
            Text {
                text: toolButton.prefix
                color: Theme.color.textTertiary
                font.family: Theme.font.monoFamily
                font.pixelSize: Theme.font.small
            }
            Text {
                text: toolButton.label
                color: Theme.color.textTertiary
                font.family: Theme.font.uiFamily
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

    // 扩展 UI 插槽：命令按钮区 / 命令面板区。
    // 扩展通过 HostUi.item("composer.commandButton") 取得 Row 后追加自己的按钮；
    // 通过 HostUi.item("composer.commandPanel") 取得浮层容器后注入自己的 Popup/面板。
    property alias commandButtonSlotItem: commandButtonSlot
    property alias commandPanelSlotItem: commandPanelSlot

    // 向扩展系统登记入口槽位，插件通过 HostUi.item 取用。
    Component.onCompleted: {
        HostUi.registerItem("composer", this)
        HostUi.registerItem("composer.commandButton", commandButtonSlot)
        HostUi.registerItem("composer.commandPanel", commandPanelSlot)
    }

    // 命令面板扩展位：不画任何东西，只提供坐标/父对象；扩展注入 Popup 或 Item。
    Item {
        id: commandPanelSlot
        anchors.fill: parent
        z: 100   // 高于输入卡内容，扩展面板可覆盖其上
    }

    // 拖入图片附件；addImageFiles 会按扩展名筛选
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]   // 只接受文件管理器拖入的 URI 列表

        onDropped: (drop) => {
            if (!drop.hasUrls)
                return
            var urls = []   // 收集全部 URL 后一次性交给 Hub，由它按扩展名筛选
            for (var i = 0; i < drop.urls.length; ++i)
                urls.push(drop.urls[i])
            Hub.addAttachments(urls)
        }
    }

    readonly property var commandMenuRef: commandMenu   // 供外部读取 Popup 状态

    property bool streaming: Hub.streaming                     // 流式中：发送键切换为停止键
    readonly property bool cancelling: Hub.cancelling === true   // 已停止，等待收尾

    // 宽度正被外层动画驱动时按停阴影，避免反复重做离屏纹理；由 Shell 注入。
    property bool widthAnimating: false

    // 内边距与行距集中定义，保证文字、chip、发送键对齐。
    readonly property real cardPadH: Theme.spacing.cardPadding      // 左右内边距：编辑文字与发送键共用，保证右缘对齐
    readonly property real cardPadTop: Theme.spacing.sectionGap
    readonly property real cardPadBottom: Theme.spacing.editorPadTop
    readonly property real cardGap: Theme.spacing.panelGap          // 上下文行 / 附件 / 编辑区 / 控制行之间的纵向间距

    // 纯视觉可用状态：按钮仍可点，空文本由宿主静默丢弃。
    readonly property bool canSend: streaming || editor.text.trim().length > 0

    // 权限状态与选项语义都由 Hub 提供；QML 不解释预设字符串。
    readonly property string permissionLabel: Hub.permissionLabel      // 当前预设的展示名
    readonly property bool permissionIsFull: Hub.permissionIsFull     // 完全权限：按钮常驻高亮
    readonly property var permissionOptions: Hub.permissionOptions    // [{ id, label, description }]

    implicitHeight: shadowHost.height + Theme.size.statsLineHeight   // 输入卡 + 固定高度的统计行
    // 提交：空文本静默丢弃；发出后清空并保持焦点，方便连续输入。
    function submit() {
        const text = editor.text
        if (text.trim().length === 0)
            return      // 空文本静默丢弃
        Hub.sendPrompt(text)
        editor.clear()
        editor.forceActiveFocus()
    }

    // 停止当前流式输出；真正的收尾进度由 Hub.cancelling 反映。
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
        // Enter 分支按优先级处理：Shift 让给编辑器换行 → 候选未补全先补全 → 流式中停止 → 否则发送。
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
        implicitHeight: capsule.implicitHeight   // 只占输入卡高度，阴影靠 MultiEffect 外扩

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
            autoPaddingEnabled: true   // 预留阴影扩散边距，避免被 Item 边界裁掉
        }

        // 输入卡片
        Rectangle {
            id: capsule
            // 静止时只作为阴影源；宽度动画期间直接绘制。
            visible: root.widthAnimating
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            implicitHeight: content.implicitHeight + root.cardPadTop + root.cardPadBottom   // 高度由内容决定，上下内边距另加
            radius: Theme.radius.composer
            // 皮肤可用 composerBg 单独覆盖输入卡底色，未定义时回退 panelBg。
            color: Theme.color.composerBg || Theme.color.panelBg
            // 浅色下输入卡边框只做很轻的分隔，别比卡片阴影还抢眼；聚焦也只加深描边。
            readonly property color restingBorder: Theme.color.composerBorder || Theme.color.border || (Theme.isDark ? "#2E3034" : "#ECEEF1")
            readonly property color activeBorder: Theme.color.composerBorderActive
                || Theme.color.composerBorder
                || (Theme.isDark ? "#4A4D53" : "#DDE1E6")
            border.color: (editor.activeFocus || dropArea.containsDrag) ? activeBorder : restingBorder   // 聚焦或拖入文件时加深
            border.width: 1
        }

        // 真实内容，与 capsule 同尺寸；自上而下：上下文 chip → 待发附件 → 编辑区 → 控制行。
        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: root.cardPadTop   // 内容整体下移一个上内边距
            spacing: root.cardGap

            Row {
                id: contextRow
                x: root.cardPadH
                width: Math.max(0, parent.width - 2 * root.cardPadH)
                height: 24
                spacing: 6
                clip: true   // 窄宽度下裁掉溢出的 chip，不换行

                // 四个 chip 依次为工作区、助手、模型、UlinCore；整行 clip，窄宽度下末尾被裁掉。
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
                    label: "助手"   // 固定文案；子智能体切换在 agentMenu 里
                    neutral: true
                    onClicked: agentMenu.open()
                }
                // 模型与推理等级选择由 ModelSelectorChip 自身完成，变更回调直接交给 Hub。
                ModelSelectorChip {
                    id: chip
                    onSelectModelRequested: (provider, model) => Hub.selectModel(provider, model)
                    onSelectLevelRequested: (levelId) => Hub.selectLevel(levelId)
                }
                UlinCoreModelChip { }
            }

            // 待发附件行：图片缩略图 / 普通文件卡 / 浏览器上下文卡 / 消息引用卡。
            Item {
                id: pendingRow
                width: parent.width
                visible: Hub.pendingAttachments.length > 0
                height: visible ? 64 : 0   // 无附件时高度归零，不额外占行距
                clip: true   // 固定 64 高度，超出裁切

                Row {
                    x: root.cardPadH
                    width: Math.max(0, parent.width - 2 * root.cardPadH)
                    spacing: 10

                Repeater {
                    model: Hub.pendingAttachments

                    Rectangle {
                        id: attachmentTile
                        // 待发附件模型字段：kind / name / suffix / sizeLabel / icon / preview /
                        // title / url / selector / prompt / text，由 Hub.pendingAttachments 提供。
                        required property var modelData
                        required property int index

                        // kind 缺失时按 preview 是否存在回退判定为图片。
                        readonly property bool isImage: modelData.kind === "image"
                            || (modelData.kind === undefined
                                && modelData.preview !== undefined && modelData.preview.length > 0)
                        readonly property bool isBrowser: modelData.kind === "browser"   // 网页上下文卡
                        readonly property bool isQuote: modelData.kind === "quote"      // 消息引用卡

                        // 与 DSH FileCard 同构：240×64、极细描边、输入区同色底；浏览器卡片宽一点放下位置。
                        width: (isBrowser || isQuote) ? 300 : (isImage ? 64 : 240)
                        height: 64
                        radius: Theme.radius.panel
                        color: isImage ? (Theme.isDark ? "#232427" : "#F2F3F5")
                            : (Theme.color.composerBg || Theme.color.panelBg)
                        border.width: 0.5   // 极细描边，卡片之间不互相抢眼
                        border.color: Theme.color.inputBorder
                        clip: isImage   // 图片靠遮罩裁圆角，普通卡保持描边完整

                        // 圆角遮罩源：只作为 MultiEffect.maskSource 使用，本身不参与绘制。
                        Rectangle {
                            id: thumbMask
                            anchors.fill: parent
                            anchors.margins: 0.5
                            radius: Theme.radius.panel
                            visible: false
                            layer.enabled: attachmentTile.isImage
                        }

                        Image {
                            id: thumb
                            anchors.fill: parent
                            anchors.margins: 0.5
                            visible: attachmentTile.isImage
                            fillMode: Image.PreserveAspectCrop   // 裁切填满，缩略图不拉伸
                            source: modelData.preview !== undefined ? modelData.preview : ""
                            layer.enabled: attachmentTile.isImage
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: thumbMask
                            }

                            // 加载失败最多重试 3 次；追加 r= 参数用于绕开图片缓存。
                            property int retry: 0

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

                        HoverHandler {
                            id: chipHover
                        }

                        // 普通文件卡：类型图标 + 文件名（中略）+ “后缀 · 大小”。
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 22
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 10
                            visible: !attachmentTile.isImage && !attachmentTile.isBrowser && !attachmentTile.isQuote   // 三种专用卡互斥，普通文件卡兜底

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 28
                                height: 28
                                source: "qrc:/ULinHub/icons/ui/"
                                    + (modelData.icon !== undefined && modelData.icon.length > 0
                                        ? modelData.icon : "file-generic") + ".svg"
                                sourceSize.width: 64
                                sourceSize.height: 64
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, parent.width - 38)
                                spacing: 2

                                Text {
                                    width: parent.width
                                    text: modelData.name !== undefined ? modelData.name : ""
                                    elide: Text.ElideMiddle   // 文件名中略，保留扩展名
                                    color: Theme.color.textPrimary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.body
                                    font.weight: Theme.font.weightMedium
                                }

                                Text {
                                    width: parent.width
                                    text: {
                                        const suffix = modelData.suffix !== undefined && modelData.suffix.length > 0
                                            ? modelData.suffix.toUpperCase() : "FILE"
                                        const size = modelData.sizeLabel !== undefined ? modelData.sizeLabel : ""
                                        return size.length > 0 ? suffix + " · " + size : suffix
                                    }
                                    elide: Text.ElideRight
                                    color: Theme.color.textTertiary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.caption
                                }
                            }
                        }

                        // 消息引用卡：与浏览器引用卡同尺寸/同结构，只是内容来自选中的消息文字。
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 24
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 8
                            visible: attachmentTile.isQuote

                            Rectangle {
                                width: 30
                                height: 30
                                radius: Theme.radius.small
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.isDark ? "#2A2C30" : "#EEF1F5"
                                Text {
                                    anchors.centerIn: parent
                                    text: "\u275D"
                                    color: Theme.color.accent
                                    font.pixelSize: 16
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, parent.width - 38)
                                spacing: 2

                                Text {
                                    width: parent.width
                                    text: "引用文本"
                                    color: Theme.color.textPrimary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.body
                                    font.weight: Theme.font.weightMedium
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.text ? String(modelData.text) : ""
                                    elide: Text.ElideRight
                                    maximumLineCount: 2   // 引用最多两行，超出省略
                                    wrapMode: Text.Wrap
                                    color: Theme.color.textSecondary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.caption
                                }

                                Text {
                                    width: parent.width
                                    text: "选中文本 · 发送时写为 Markdown 引用"
                                    elide: Text.ElideRight
                                    color: Theme.color.textTertiary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.small
                                }
                            }
                        }

                        // 浏览器上下文卡片：标题 + 元素位置 + 「读取浏览器内容」短提示。
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 24
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 8
                            visible: attachmentTile.isBrowser

                            Rectangle {
                                width: 30
                                height: 30
                                radius: Theme.radius.small
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.isDark ? "#2A2C30" : "#EEF1F5"

                                LineIcon {
                                    anchors.centerIn: parent
                                    name: "browser"
                                    size: 17
                                    color: Theme.color.accent
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, parent.width - 38)
                                spacing: 2

                                Text {
                                    width: parent.width
                                    text: (modelData.title !== undefined && modelData.title.length > 0)
                                        ? modelData.title
                                        : (modelData.url !== undefined ? modelData.url : "")
                                    elide: Text.ElideRight
                                    color: Theme.color.textPrimary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.body
                                    font.weight: Theme.font.weightMedium
                                }

                                Text {
                                    width: parent.width
                                    text: (modelData.selector !== undefined && modelData.selector.length > 0)
                                        ? modelData.selector
                                        : (modelData.url !== undefined ? modelData.url : "")
                                    elide: Text.ElideMiddle
                                    color: Theme.color.textTertiary
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.caption
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.prompt !== undefined ? modelData.prompt : "读取浏览器内容"   // 点击后实际动作的提示语
                                    elide: Text.ElideRight
                                    color: Theme.color.accent
                                    font.family: Theme.font.uiFamily
                                    font.pixelSize: Theme.font.small
                                }
                            }
                        }

                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 3
                            // 默认完全透明：只画一颗深灰叉；悬浮时铺一层浅灰半透明底，无边框。
                            color: removeHover.hovered
                                ? (Theme.isDark ? Qt.rgba(0.80, 0.82, 0.85, 0.20)
                                                : Qt.rgba(0.55, 0.57, 0.60, 0.25))
                                : "transparent"
                            border.width: 0

                            HoverHandler {
                                id: removeHover
                            }

                            Text {
                                anchors.centerIn: parent
                                text: "\u2715"
                                color: Theme.color.textSecondary
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
                                 Theme.size.editorMaxHeight)   // 内容高度夹在单行下限与多行上限之间
                contentWidth: width
                contentHeight: editor.height
                clip: true
                interactive: false   // 按住鼠标用于选文字，不让 Flickable 抢走
                boundsBehavior: Flickable.StopAtBounds   // 不做过冲回弹

                // 只有内容超高时才给滚动条让出宽度。
                readonly property bool needsScroll: contentHeight > height + 0.5

                ScrollBar.vertical: AppScrollBar {
                    id: editorScrollBar
                    objectName: "composerScrollBar"   // 供 UI 测试/自动化定位
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

                // 显式处理滚轮，兼容 interactive=false，且不影响拖选文字；
                // angleDelta 以 1/8 度为单位，除以 2 得到较缓的滚动步长。
                WheelHandler {
                    onWheel: (event) => {
                        const maxY = Math.max(0, editorView.contentHeight - editorView.height)
                        if (maxY <= 0)
                            return
                        editorView.contentY = Math.min(maxY, Math.max(0, editorView.contentY - event.angleDelta.y / 2))
                    }
                }

                TextEdit {
                    id: editor
                    objectName: "composerEditor"   // 供 UI 测试/自动化定位
                    // 固定顶对齐：Flickable 的 view 比单行 editor 高时，别让文字在
                    // 附件缩略图和输入行之间被居中/下沉，造成一块空档。
                    anchors.top: parent.top
                    anchors.left: parent.left
                    // 超高时为滚动条让出宽度
                    width: editorView.width - (editorView.needsScroll
                                               ? Theme.size.scrollbarThickness : 0)
                    leftPadding: root.cardPadH    // 与卡片内边距一致，文字与上方 chip 左对齐
                    rightPadding: root.cardPadH
                    topPadding: Theme.spacing.editorPadTop
                    bottomPadding: 0   // 底部留白由 editorView 的夹取负责
                    textFormat: TextEdit.PlainText   // 不做富文本解析，输入即纯文本
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere   // 长单词/URL 也强制换行，避免横向溢出
                    selectByMouse: true
                    persistentSelection: true   // 失焦后保留选区：先选文字再点工具按钮
                    color: Theme.color.textPrimary
                    selectionColor: Theme.color.selectionBg
                    selectedTextColor: Theme.color.textPrimary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.body   // 与 SearchField 输入文字同档
                    // 高度等于全文高度，夹取与滚动交给外层 editorView。
                    height: contentHeight + topPadding + bottomPadding
                    onCursorRectangleChanged: editorView.ensureCursorVisible()   // 键盘/鼠标移动光标都跟随

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

                    // 输入变化驱动斜杠菜单；首次敲 "/" 时懒拉一次命令表。
                    onTextChanged: {
                        commandMenu.dismissed = false     // 敲字后重新参与候选
                        commandMenu.syncVisibility()
                        if (text === "/")   // 懒拉一次命令表，兼容运行中注册的命令
                            Hub.commands.refresh(Hub.currentSessionId)
                    }

                    // 所有按键先交给 root.handleKey 统一路由；消费后不再冒泡。
                    Keys.onPressed: (event) => {
                        if (root.handleKey(event))
                            event.accepted = true
                    }
                }
            }

            // 控制行：左侧工具、右侧发送键；行高跟随发送键，工具按钮垂直居中。
            Item {
                id: controlRow
                width: parent.width
                implicitHeight: sendButton.implicitHeight   // 行高由发送键决定

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

                // 命令按钮扩展位：扩展按钮追加在这里，保持和内置按钮同高同间距。
                Row {
                    id: commandButtonSlot
                    anchors.left: commandButton.right
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                }

                // 上下文：插入 “@” 供后续引用
                CompactToolButton {
                    id: contextButton
                    anchors.left: commandButtonSlot.right
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

                // 截图：框选整个屏幕，截完作为图片附件进输入区。
                Rectangle {
                    id: screenshotButton
                    anchors.left: contextButton.right
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    radius: Theme.radius.small
                    color: screenshotMouse.containsMouse ? Theme.color.iconButtonHoverBg : "transparent"

                    LineIcon {
                        anchors.centerIn: parent
                        name: "camera"
                        size: 17
                        // 相机的线条密度比 + 高，hover 跟着 textPrimary 会整块发黑；
                        // 只做一档轻微加深，保持和同级工具按钮同一观感。
                        color: screenshotMouse.containsMouse ? Theme.color.textSecondary : Theme.color.textTertiary
                    }

                    // 左键直接框选；右键打开“隐藏/保留主窗口”的截图选项。
                    MouseArea {
                        id: screenshotMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                shotOptionsMenu.open()
                                return
                            }
                            screenshotWindow.beginShot()
                        }
                    }
                }

                // 权限预设直接显示在按钮上，点开切换
                Rectangle {
                    id: permissionButton
                    anchors.left: screenshotButton.right
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: permissionRow.implicitWidth + 14   // 左右各 7px
                    height: 26
                    radius: 6
                    // 完全权限常驻中性高亮；其余预设只做 hover 反馈。
                    color: root.permissionIsFull
                        ? root.neutralChipBg
                        : (permissionMouse.containsMouse ? root.neutralChipBg : "transparent")
                    border.width: 1
                    border.color: root.permissionIsFull ? Theme.color.border : "transparent"

                    // 图标 + 文案 + 下拉箭头；宽度由文案决定。
                    Row {
                        id: permissionRow
                        anchors.centerIn: parent
                        spacing: 4

                        LineIcon {
                            name: "tools"
                            size: 13
                            color: root.permissionIsFull ? Theme.color.textSecondary : Theme.color.textTertiary
                        }
                        Text {
                            text: root.permissionLabel
                            color: root.permissionIsFull ? Theme.color.textSecondary : Theme.color.textTertiary
                            font.family: Theme.font.uiFamily
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
                    opacity: root.canSend && !root.cancelling ? 1.0 : 0.45   // 不可发送/收尾中只降低存在感，仍可点
                    circleHoverOverlay: root.canSend && !root.cancelling   // 收尾中不再给 hover 反馈
                    // 暗色主题圆底是白色，悬停蒙版必须用压暗色。
                    circleHoverTint: Theme.isDark ? "#000000" : "#FFFFFF"
                    // 文案随流式/收尾状态切换。
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
        width: 264   // 容得下工作区名 + 会话数 + 勾选
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // 下方空间不足时改为从 chip 上方向上展开；否则底部菜单会被窗口裁掉。
        function updateMenuPosition() {
            const chipPos = workspaceChip.mapToItem(root, 0, 0)
            const win = root.Window.window
            const winH = win ? win.height : root.height
            const rootScene = root.mapToItem(null, 0, 0)
            const chipTopScene = rootScene.y + chipPos.y
            const chipBottomScene = chipTopScene + workspaceChip.height
            const menuH = height > 0 ? height : contentItem.implicitHeight + 2 * padding
            const spaceBelow = winH - chipBottomScene - 8
            const spaceAbove = chipTopScene - 8
            const below = chipPos.y + workspaceChip.height + 6
            x = Math.max(8, Math.min(root.width - width - 8, chipPos.x))
            // 上下都按窗口（不是 Composer 自身）计算；Composer 本身只有输入卡那么高。
            y = (spaceBelow >= menuH || spaceAbove < menuH) ? below : chipPos.y - menuH - 6
        }
        onOpened: updateMenuPosition()
        onHeightChanged: if (visible) updateMenuPosition()

        // 工作区条目：workspaceId / title / count / ungrouped；顺序由 Hub.sessions 统一给出。
        readonly property var items: Hub.sessions ? Hub.sessions.workspaceItems() : []

        // 切换工作区由 Hub 负责恢复最近的会话；QML 只关闭菜单。
        function activate(item) {
            Hub.sessions.activateWorkspace(item.workspaceId)
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
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
            }

            Repeater {
                model: workspaceMenu.items

                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 30
                    radius: 5
                    // 当前工作区（含“未分组”映射）以 activeBg 标记；hover 优先于选中态。
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
                            font.family: Theme.font.uiFamily
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
                        CheckMark {
                            visible: modelData.workspaceId === Hub.currentWorkspaceId
                                || (modelData.ungrouped && Hub.currentWorkspaceId.length === 0)
                            size: 12
                            color: Theme.color.accent
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
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
            }

            Rectangle { width: parent.width; height: 1; color: Theme.color.border }

            // 底部固定动作：新建会话、新建工作区。
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
                    font.family: Theme.font.uiFamily
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
                    font.family: Theme.font.uiFamily
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
        width: 280   // 容得下子智能体名 + 状态点
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // 下方空间不足时改为从 chip 上方向上展开；否则子代理列表会被窗口裁掉。
        function updateMenuPosition() {
            const chipPos = agentChip.mapToItem(root, 0, 0)
            const win = root.Window.window
            const winH = win ? win.height : root.height
            const rootScene = root.mapToItem(null, 0, 0)
            const chipTopScene = rootScene.y + chipPos.y
            const chipBottomScene = chipTopScene + agentChip.height
            const menuH = height > 0 ? height : contentItem.implicitHeight + 2 * padding
            const spaceBelow = winH - chipBottomScene - 8
            const spaceAbove = chipTopScene - 8
            const below = chipPos.y + agentChip.height + 6
            x = Math.max(8, Math.min(root.width - width - 8, chipPos.x))
            // 以窗口可用高度判断上拉/下拉。
            y = (spaceBelow >= menuH || spaceAbove < menuH) ? below : chipPos.y - menuH - 6
        }
        onOpened: updateMenuPosition()
        onHeightChanged: if (visible) updateMenuPosition()

        // 子智能体（peer）列表：id / label / running；running 决定左侧状态点的颜色。
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
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
            }
            Text {
                width: parent.width
                height: 22
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: qsTrId("composer_agent_desc")
                color: Theme.color.textTertiary
                font.family: Theme.font.uiFamily
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
                            color: modelData.running ? Theme.color.accent : Theme.color.textCaption   // 运行中的 peer 标 accent
                        }
                        Text {
                            text: modelData.label
                            color: Theme.color.textSecondary
                            font.family: Theme.font.uiFamily
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
                        // 切到该 peer 对应的会话，菜单随之关闭。
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
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
            }

            Rectangle { width: parent.width; height: 1; color: Theme.color.border }

            // 工具权限入口：打开工具筛选面板。
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
                    font.family: Theme.font.uiFamily
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

    // 截图：全屏置顶窗口。仿微信/QQ 截图：选区外变暗、选区内保持原色，
    // 拖完不立刻截，先出现工具栏，可继续移动/改大小，点“完成”才抓屏。
    Window {
        id: screenshotWindow
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool   // 无边框、置顶、不占任务栏
        visibility: Window.Hidden
        color: "transparent"   // 覆盖层本身透明，压暗与高亮都由子矩形绘制

        // 覆盖层状态机：drawing（拖出选区）→ selectionReady（可移动/缩放并显示工具栏）
        // → commitSelection 抓屏，或 cancelShot 直接退出。
        property bool selectionReady: false   // 首帧框选完成，选区可再次编辑
        property bool hideWindowOnShot: true   // 右键菜单可切换：截桌面时是否隐藏 ULin Hub
        property bool drawing: false           // 正在拖出选区，此时不显示工具栏
        property rect resizeStart: Qt.rect(0, 0, 0, 0)   // 缩放开始时的选区快照
        property point resizePress: Qt.point(0, 0)       // 缩放按下的局部坐标，用于计算增量
        property string resizeMode: ""         // 正在拖拽的角："tl" / "tr" / "bl" / "br"
        property int minShotSize: 8            // 有效选区的最小边长，更小视为误触
        property bool appHiddenForShot: false  // 主窗口已为本次截图隐藏，收尾时需还原
        property var hiddenAppWindow: null     // 被隐藏的主窗口
        property int appRestoreVisibility: Window.Windowed   // 还原时恢复的窗口状态

        // 恢复被截图隐藏的主窗口，并还原其原来的可见状态。
        function restoreAppWindow() {
            if (!appHiddenForShot || !hiddenAppWindow)
                return
            const w = hiddenAppWindow
            const vis = appRestoreVisibility
            appHiddenForShot = false
            hiddenAppWindow = null
            w.show()
            w.visibility = vis
            w.requestActivate()
        }

        function beginShot() {
            resetSelection()
            // 窗口上次关闭时可能缓存了旧内容；先透明显示，等第一帧新 QML
            // 绘制完成后再显形，避免复用上一次选区纹理造成“闪旧框”。
            opacity = 0
            const w = root.Window.window
            // 尚未挂到 Window 时无法确定目标屏幕，直接放弃。
            if (!w)
                return
            screen = w.screen
            if (hideWindowOnShot) {
                // 先让主窗口真正隐藏，再显示全屏选区层；反过来会在桌面/窗口
                // 还没重绘时截到冻结的旧画面。
                appHiddenForShot = true
                hiddenAppWindow = w
                appRestoreVisibility = w.visibility
                w.hide()
                shotShowTimer.restart()   // 等主窗口隐藏、桌面重绘后再显示覆盖层
            } else {
                showFullScreen()
                shotRevealTimer.restart()   // 覆盖层先透明显示，60ms 后淡入
            }
        }

        // 关闭覆盖层并还原此前隐藏的主窗口。
        function cancelShot() {
            hide()
            restoreAppWindow()
        }

        // 复位到“未选区”初始态，供显示前与取消时复用。
        function resetSelection() {
            selectionReady = false
            drawing = false
            shotSelection.x = 0
            shotSelection.y = 0
            shotSelection.width = 0
            shotSelection.height = 0
        }

        // 记录选区后先隐藏覆盖层，等下一拍由 shotTimer 抓屏，避免把工具栏拍进去。
        function commitSelection() {
            if (shotSelection.width < minShotSize || shotSelection.height < minShotSize)
                return
            const r = Qt.rect(shotSelection.x, shotSelection.y, shotSelection.width, shotSelection.height)
            const willRestore = appHiddenForShot   // 先缓存还原所需状态，再隐藏覆盖层并交给 shotTimer
            const appWindow = hiddenAppWindow
            const restoreVisibility = appRestoreVisibility
            hide()
            shotTimer.captureRect = r
            shotTimer.hideApp = willRestore
            shotTimer.appWindow = appWindow
            shotTimer.restoreVisibility = restoreVisibility
            shotTimer.restart()
        }

        onVisibleChanged: {
            // 显示前由按钮先 reset；这里只在关闭后清理，避免显示第一帧闪旧选区。
            if (!visible) {
                opacity = 0
                resetSelection()
            }
        }

        Item {
            id: shotUi
            anchors.fill: parent
            focus: true   // 接管 Esc；覆盖层内的交互全部挂在这棵子树下
            Keys.onEscapePressed: screenshotWindow.cancelShot()

            // 未开始框选：整屏压暗。
            Rectangle {
                anchors.fill: parent
                color: "#66000000"   // 约 40% 黑遮罩
                visible: !screenshotWindow.selectionReady && !screenshotWindow.drawing
            }

            // 选区外四块压暗，选区内保持原色（真正的“变亮”）。
            Rectangle { x: 0; y: 0; width: parent.width
                height: Math.max(0, shotSelection.y)
                color: "#66000000"; visible: screenshotWindow.selectionReady || screenshotWindow.drawing }
            Rectangle { x: 0; y: shotSelection.y + shotSelection.height
                width: parent.width; height: Math.max(0, parent.height - shotSelection.y - shotSelection.height)
                color: "#66000000"; visible: screenshotWindow.selectionReady || screenshotWindow.drawing }
            Rectangle { x: 0; y: shotSelection.y
                width: Math.max(0, shotSelection.x); height: shotSelection.height
                color: "#66000000"; visible: screenshotWindow.selectionReady || screenshotWindow.drawing }
            Rectangle { x: shotSelection.x + shotSelection.width; y: shotSelection.y
                width: Math.max(0, parent.width - shotSelection.x - shotSelection.width); height: shotSelection.height
                color: "#66000000"; visible: screenshotWindow.selectionReady || screenshotWindow.drawing }

            Rectangle {
                id: shotSelection
                visible: screenshotWindow.selectionReady || screenshotWindow.drawing
                color: "transparent"
                border.width: 1.5
                border.color: Theme.color.accent
                radius: 2
                z: 2

                // 选区本体只做平移；改大小交给四角手柄，避免两套拖拽逻辑互抢事件。
                MouseArea {
                    anchors.fill: parent
                    enabled: screenshotWindow.selectionReady
                    cursorShape: Qt.SizeAllCursor
                    property point press: Qt.point(0, 0)      // 按下时的局部坐标
                    property rect start: Qt.rect(0, 0, 0, 0)   // 按下时的选区快照
                    onPressed: (mouse) => {
                        press = Qt.point(mouse.x, mouse.y)
                        start = Qt.rect(shotSelection.x, shotSelection.y, shotSelection.width, shotSelection.height)
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const dx = mouse.x - press.x
                        const dy = mouse.y - press.y
                        shotSelection.x = Math.max(0, Math.min(shotUi.width - start.width, start.x + dx))
                        shotSelection.y = Math.max(0, Math.min(shotUi.height - start.height, start.y + dy))
                    }
                }
            }

            // 四角拖拽改大小；手柄坐标按选区边界各外扩 5px 摆放。
            Repeater {
                model: ["tl", "tr", "bl", "br"]
                delegate: Rectangle {
                    required property string modelData
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.color.accent
                    visible: screenshotWindow.selectionReady
                    z: 3
                    x: (modelData === "tl" || modelData === "bl")
                        ? shotSelection.x - 5 : shotSelection.x + shotSelection.width - 5
                    y: (modelData === "tl" || modelData === "tr")
                        ? shotSelection.y - 5 : shotSelection.y + shotSelection.height - 5

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: modelData === "tl" || modelData === "br"
                            ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        onPressed: (mouse) => {
                            screenshotWindow.resizeMode = modelData
                            screenshotWindow.resizeStart = Qt.rect(shotSelection.x, shotSelection.y, shotSelection.width, shotSelection.height)
                            screenshotWindow.resizePress = Qt.point(mouse.x, mouse.y)
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const dx = mouse.x - screenshotWindow.resizePress.x
                            const dy = mouse.y - screenshotWindow.resizePress.y
                            const r = screenshotWindow.resizeStart
                            let x = r.x, y = r.y, w = r.width, h = r.height
                            if (screenshotWindow.resizeMode === "tl") {
                                x = r.x + dx; y = r.y + dy; w = r.width - dx; h = r.height - dy
                            } else if (screenshotWindow.resizeMode === "tr") {
                                y = r.y + dy; w = r.width + dx; h = r.height - dy
                            } else if (screenshotWindow.resizeMode === "bl") {
                                x = r.x + dx; w = r.width - dx; h = r.height + dy
                            } else {
                                w = r.width + dx; h = r.height + dy
                            }
                            if (w < screenshotWindow.minShotSize || h < screenshotWindow.minShotSize)
                                return
                            shotSelection.x = Math.max(0, Math.min(shotUi.width - w, x))
                            shotSelection.y = Math.max(0, Math.min(shotUi.height - h, y))
                            shotSelection.width = w
                            shotSelection.height = h
                        }
                    }
                }
            }

            // 初始框选区域。
            MouseArea {
                id: shotDrawArea
                z: 1
                anchors.fill: parent
                enabled: !screenshotWindow.selectionReady
                cursorShape: Qt.CrossCursor
                property point startPoint
                onPressed: (mouse) => {
                    screenshotWindow.drawing = true
                    startPoint = Qt.point(mouse.x, mouse.y)
                    shotSelection.x = mouse.x
                    shotSelection.y = mouse.y
                    shotSelection.width = 0
                    shotSelection.height = 0
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    shotSelection.x = Math.min(startPoint.x, mouse.x)
                    shotSelection.y = Math.min(startPoint.y, mouse.y)
                    shotSelection.width = Math.abs(mouse.x - startPoint.x)
                    shotSelection.height = Math.abs(mouse.y - startPoint.y)
                }
                onReleased: {
                    screenshotWindow.drawing = false
                    if (shotSelection.width < screenshotWindow.minShotSize
                            || shotSelection.height < screenshotWindow.minShotSize) {
                        screenshotWindow.resetSelection()
                    } else {
                        screenshotWindow.selectionReady = true   // 松手即进入可编辑状态，不自动抓屏
                    }
                }
            }

            // 已有选区时点空白处 = 取消当前选区，重新框。
            MouseArea {
                z: 1
                anchors.fill: parent
                enabled: screenshotWindow.selectionReady
                onPressed: screenshotWindow.resetSelection()
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 18
                // 顶部提示随阶段切换：框选中 → 选区编辑中。
                text: screenshotWindow.selectionReady
                    ? "拖动选区移动 · 拖四角调整 · 点完成截图"
                    : "拖动选择截图区域 · Esc 取消"
                color: "white"
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
                z: 20
            }

            // QQ/微信式操作栏：选区右下（空间不够就移到上方）。
            Rectangle {
                id: shotToolbar
                visible: screenshotWindow.selectionReady
                z: 30
                width: 148   // 两个 64px 按钮 + 4px 间距 + 两侧各 8px 留白
                height: 38
                radius: 8
                color: Theme.color.menuBg
                border.width: 0.5
                border.color: Theme.color.menuBorder
                // 默认贴选区右下角，水平留 8px；底部放不下时改到选区上方。
                x: Math.max(8, Math.min(shotUi.width - width - 8, shotSelection.x + shotSelection.width - width))
                y: shotSelection.y + shotSelection.height + 10 + height > shotUi.height - 8
                    ? Math.max(8, shotSelection.y - height - 10)
                    : shotSelection.y + shotSelection.height + 10

                Row {
                    anchors.centerIn: parent
                    spacing: 4

                    Rectangle {
                        width: 64
                        height: 28
                        radius: 6
                        color: cancelMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            color: Theme.color.textSecondary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                        }
                        MouseArea {
                            id: cancelMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: screenshotWindow.cancelShot()
                        }
                    }

                    Rectangle {
                        width: 64
                        height: 28
                        radius: 6
                        color: confirmMouse.containsMouse ? Theme.color.accent : Theme.color.activeBg
                        Text {
                            anchors.centerIn: parent
                            text: "完成"
                            color: confirmMouse.containsMouse ? "white" : Theme.color.textPrimary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                            font.weight: Theme.font.weightBold
                        }
                        MouseArea {
                            id: confirmMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: screenshotWindow.commitSelection()
                        }
                    }
                }
            }
        }
    }

    // 覆盖层淡入：等第一帧新 QML 画完再显形，避免复用上次选区纹理。
    Timer {
        id: shotRevealTimer
        interval: 60
        repeat: false
        onTriggered: screenshotWindow.opacity = 1
    }

    // 主窗口隐藏与覆盖层显示之间留出的重绘时间。
    Timer {
        id: shotShowTimer
        interval: 120
        repeat: false
        onTriggered: {
            screenshotWindow.showFullScreen()
            shotRevealTimer.restart()
        }
    }

    // 抓屏与主窗口还原在同一拍完成：抓的是已重绘的桌面，不会带上覆盖层。
    Timer {
        id: shotTimer
        interval: 140
        repeat: false
        property rect captureRect: Qt.rect(0, 0, 0, 0)   // 待抓取的屏幕区域
        property bool hideApp: false                      // 抓完是否需要还原主窗口
        property var appWindow: null                      // 本次被隐藏的主窗口
        property int restoreVisibility: Window.Windowed   // 还原时的目标可见状态
        onTriggered: {
            // 兜底跳过退化选区，避免抓到空图。
            if (captureRect.width >= 2 && captureRect.height >= 2)
                Hub.captureScreenArea(captureRect.x, captureRect.y, captureRect.width, captureRect.height)
            if (hideApp && appWindow) {
                appWindow.show()
                appWindow.visibility = restoreVisibility
                appWindow.requestActivate()
                screenshotWindow.appHiddenForShot = false
                screenshotWindow.hiddenAppWindow = null
            }
        }
    }

    // 右键截图按钮：选择截图时隐藏还是保留 ULin Hub 窗口。
    Popup {
        id: shotOptionsMenu
        parent: screenshotButton
        width: 226
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // 相对截图按钮定位：右缘对齐、向上展开，避免被窗口底部裁掉。
        x: -width + screenshotButton.width
        y: -height - 8

        background: Rectangle {
            radius: 10
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: shotOptionsMenu.width - 12
            spacing: 2

            Text {
                width: parent.width
                height: 24
                verticalAlignment: Text.AlignVCenter
                leftPadding: 6
                text: "截图选项"
                color: Theme.color.textCaption
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
            }

            // 两个互斥选项：“隐藏主窗口后截图”与“保留窗口截图”，勾选态由 hideWindowOnShot 决定。
            Rectangle {
                width: parent.width
                height: 32
                radius: 5
                color: hideOptionMouse.containsMouse ? Theme.color.hoverBg
                    : (screenshotWindow.hideWindowOnShot ? Theme.color.activeBg : "transparent")

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7
                    CheckMark {
                        width: 12
                        height: 12
                        visible: screenshotWindow.hideWindowOnShot
                        size: 12
                        color: Theme.color.accent
                    }
                    Text {
                        text: "隐藏 ULin Hub 后截图"
                        color: Theme.color.textSecondary
                        font.family: Theme.font.uiFamily
                        font.pixelSize: Theme.font.caption
                    }
                }

                MouseArea {
                    id: hideOptionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        screenshotWindow.hideWindowOnShot = true
                        shotOptionsMenu.close()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 32
                radius: 5
                color: showOptionMouse.containsMouse ? Theme.color.hoverBg
                    : (!screenshotWindow.hideWindowOnShot ? Theme.color.activeBg : "transparent")

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7
                    CheckMark {
                        width: 12
                        height: 12
                        visible: !screenshotWindow.hideWindowOnShot
                        size: 12
                        color: Theme.color.accent
                    }
                    Text {
                        text: "显示窗口截图"
                        color: Theme.color.textSecondary
                        font.family: Theme.font.uiFamily
                        font.pixelSize: Theme.font.caption
                    }
                }

                MouseArea {
                    id: showOptionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        screenshotWindow.hideWindowOnShot = false
                        shotOptionsMenu.close()
                    }
                }
            }
        }
    }

    // 权限菜单：会话级权限预设，完全权限需二次确认
    Popup {
        id: permissionMenu
        // 直接以入口按钮为 parent：Popup 的 x/y 就相对按钮，不再做跨层 mapToItem。
        // 这样菜单始终贴着按钮上沿，并且水平居中在按钮正上方。
        parent: permissionButton
        width: 292   // 两行说明（预设名 + 描述）的宽度
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((permissionButton.width - width) / 2)   // 水平居中在入口按钮上方
        y: -height - 8

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
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.small
            }

            // 选项字段：id / label / description；是否完全权限由 Hub.permissionOptionIsFull 判定。
            Repeater {
                model: root.permissionOptions

                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: 46   // 两行：预设名 + 说明
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
                            // 完全权限用 danger 色提示风险，其余预设用次级文字色。
                            color: Hub.permissionOptionIsFull(modelData.id)
                                ? Theme.color.danger : Theme.color.textSecondary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                        }
                        Text {
                            text: modelData.description
                            width: parent.width
                            color: Theme.color.textCaption
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small
                            elide: Text.ElideRight
                        }
                    }

                    CheckMark {
                        id: optionCheck
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Hub.permissionOptionActive(modelData.id)
                        size: 12
                        color: Theme.color.accent
                    }

                    MouseArea {
                        id: optionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            permissionMenu.close()
                            if (Hub.permissionOptionIsFull(modelData.id))
                                permissionConfirm.open()
                            else
                                Hub.setPermissionPreset(modelData.id)
                        }
                    }
                }
            }
        }
    }

    // 完全权限二次确认：模态遮罩、只允许 Esc 关闭，确认后才真正写入预设。
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
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightBold
                }
            }

            Text {
                x: 20
                width: parent.width - 40
                text: qsTrId("permission_confirm_desc")
                color: Theme.color.textSecondary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
                wrapMode: Text.WordWrap
                lineHeight: 1.4
            }

            // 取消 / 确认：只有确认才写入完全权限预设。
            Row {
                width: parent.width - 40
                x: 20
                height: 32
                spacing: 8

                Item { width: parent.width - 84 - 118 - 16; height: 1 }   // 弹性占位，把按钮推到右侧

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
                        Hub.setFullAccessPermission()
                        permissionConfirm.close()
                    }
                }
            }

            Item { width: 1; height: 14 }   // 底部留白
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

        // 状态圆点 + 统计文本；文本为空时整行隐藏，但外层 Item 仍占位。
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
                    color: Hub.streaming ? Theme.color.accent : Theme.color.textCaption   // 流式中点亮
                    opacity: Hub.streaming ? 1 : 0.55
                }
            }

            Text {
                id: statsText
                width: Math.min(implicitWidth, statsLine.width - 24)   // 两侧各留 12px，避免贴边
                height: statsLine.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: Hub.statsLine
                elide: Text.ElideRight
                color: Theme.color.textTertiary
                font.family: Theme.font.uiFamily
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

        readonly property var candidates: Hub.commands.matching(editor.text)   // 前缀过滤结果，随输入自动重算
        property int index: 0            // 键盘 ↑↓ 高亮的候选下标
        property bool dismissed: false   // Esc/点击外部关闭后，等下次输入才重新弹出
        property bool suppressReopen: false   // 补全引起的文本变化不重新弹菜单

        // 当前高亮项；index 越界时夹回有效范围。
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
        // 文本已恰好等于 "/name"：Enter 直接发送，不再补全。
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

        onCandidatesChanged: syncVisibility()   // 候选集合变化即重算显隐
        onClosed: dismissed = true   // 等用户再次输入才重新弹出

        parent: root
        x: 16
        y: shadowHost.y - height - 8   // 贴输入卡上沿 8px
        width: Math.min(560, root.width - 32)   // 窄窗口下左右各留 16px
        padding: 6
        modal: false
        focus: false   // 不抢输入框焦点，按键由 editor 的 Keys 转发
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

                // 候选字段：name（不含 "/"）、hint（参数提示，可为空）、description（一句话说明）。
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
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.body
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: hintText
                                text: commandRow.modelData.hint   // 命令参数提示
                                visible: text.length > 0
                                color: Theme.color.textTertiary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: commandRow.modelData.description
                                width: Math.max(0, rowHost.width - nameText.width - hintText.width
                                                   - (hintText.visible ? rowHost.spacing * 2 : rowHost.spacing))
                                elide: Text.ElideRight
                                color: Theme.color.textCaption
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: commandMenu.index = commandRow.index   // hover 与键盘共用同一个高亮下标
                            onClicked: commandMenu.complete()
                        }
                    }
                }
            }
        }
    }
}
