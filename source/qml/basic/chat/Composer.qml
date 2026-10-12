// 输入区（按原版 ui/ChatInputWidget.cpp + chat.qss 复刻）
//
// 结构（自上而下，与原版一一对应）：
//   ① 带阴影的卡片（原版 ShadowPanel{blur 12, dy 3, radius 22} + 整体下移 6）
//   ② #inputCapsule（panelBg + 1px composerBorder + radius 22；内部上内边距 10、行间距 12）
//        ├ 输入框（16px、内边距 4/12/0/16、高度 40–336、超出靠内部滚动、无可见滚动条）
//        └ #inputControlRow（内边距 8/2/8/6、spacing 12）：左侧模型/思考 chip，中间 stretch，
//          右侧发送键（32×32，永远可用，streaming 时变中止）
//   ③ sessionStatsLine：固定 14px、**始终显示**（空串不画字）；整体比卡片底边再低 6px
//
// 与原版一致的键位：Enter/Return 无 Shift 则发送（输出中则中止）——**Ctrl+Enter 也发送**；
// Shift+Enter 换行。输入区**从不禁用**；空文本静默丢弃（拦截在宿主）。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 扩展注入点：扩展用 HostUi.item("composer") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("composer", this)

    // 斜杠命令菜单句柄：菜单是 Popup，可视项挂在 Overlay 下、不在本项的 children 里，
    // 外部（离屏试装台、扩展）要读它的状态只能走这个转发。
    readonly property var commandMenuRef: commandMenu

    property bool streaming: Hub.streaming

    implicitHeight: shadowHost.height + Theme.size.statsLineHeight

    function submit() {
        const text = editor.text
        if (text.trim().length === 0)
            return      // 空文本静默丢弃（原版由宿主拦，这里同一语义）
        Hub.sendPrompt(text)
        editor.clear()
        editor.forceActiveFocus()
    }

    function stop() {
        Hub.cancelPrompt()
    }

    // 按键路由（单独一个函数：规则集中一处，也便于离屏测）。
    // 返回 true = 已消费（调用方把 event.accepted 置真）。
    function handleKey(event) {
        // 斜杠命令菜单优先吃键：↑↓ 选、Tab 补全、Esc 收起
        if (commandMenu.visible) {
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
            // 只有 Shift 例外（Ctrl+Enter 也发送，与原版 eventFilter 一致）
            if (event.modifiers & Qt.ShiftModifier)
                return false
            // 候选还没敲全 ⇒ 这一下是"补全"，不是提交
            if (commandMenu.visible && !commandMenu.isExact()) {
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

    // ---- ① 阴影卡片 ----
    Item {
        id: shadowHost
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: capsule.implicitHeight

        // 原版阴影：blur 12 / dy 3 / radius 22（token 里叫 inputBlur/inputDy/inputRadius）
        MultiEffect {
            anchors.fill: capsule
            anchors.topMargin: Theme.shadow.inputDy
            source: capsule
            shadowEnabled: true
            shadowBlur: Theme.shadow.inputBlur
            shadowColor: Theme.color.shadowSubtle
            shadowVerticalOffset: Theme.shadow.inputDy
            shadowHorizontalOffset: 0
            autoPaddingEnabled: true
        }

        // ---- ② 输入卡片 ----
        Rectangle {
            id: capsule
            visible: false                 // 只作为上面 MultiEffect 的 source 绘制
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            implicitHeight: content.implicitHeight + 10
            radius: Theme.radius.composer
            // composerBg：可选 token。壁纸类皮肤需要单独把输入卡做成半透明，
            // 但输入卡与 Agent 气泡共用 panelBg（AgentBubble.qml），靠 panelBg 区分不开，
            // 所以给输入卡留一个专属键；皮肤不定义它时回退 panelBg（行为与原来完全一致）。
            color: Theme.color.composerBg || Theme.color.panelBg
            border.width: 1
            border.color: Theme.color.composerBorder
        }

        // 真实内容（与 capsule 同尺寸；卡片本体不参与绘制）
        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 10           // kCardTopPadding
            spacing: 12                     // kCardGap

            TextEdit {
                id: editor
                objectName: "composerEditor"   // 供离屏试装台定位（见 misc/tools/qml-harness.py）
                width: parent.width
                // 内边距：上 4 / 右 12 / 左 16（原版 chat.qss:125-134）
                leftPadding: 16
                rightPadding: 12
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
                font.pixelSize: Theme.font.lead          // 16px
                // 高度自适应：40–336（原版 kEditorMinHeight / kEditorMaxHeight）。
                // ⚠️ TextEdit 的 implicitHeight 是只读的（原版也是 setFixedHeight 而非设 implicit），
                // 所以这里给 height 赋值。
                height: Math.min(Math.max(contentHeight + topPadding, Theme.size.editorMinHeight),
                                 Theme.size.editorMaxHeight)
                // 无可见滚动条：超出靠编辑器内部滚动
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacing.editorPadTop
                    visible: editor.text.length === 0
                    text: qsTrId("chat_input_placeholder")
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.lead
                }

                onTextChanged: {
                    commandMenu.dismissed = false     // 一敲字就重新参与候选（Esc/点外面收起后不要赖着不走）
                    commandMenu.syncVisibility()      // Popup 只能显式开关（见 commandMenu 上的说明）
                    // 刚打出 `/` 时懒拉一次命令表：会话切换时宿主已拉过，这里兜"开着会话时新注册了命令"
                    if (text === "/")
                        Hub.commands.refresh(Hub.currentSessionId)
                }

                Keys.onPressed: (event) => {
                    if (root.handleKey(event))
                        event.accepted = true
                }
            }

            // ---- #inputControlRow：左 chip / 中 stretch / 右发送键 ----
            // 控制行：原版 #inputControlRow 的 margins 是 (8,2,8,6)，而 QHBoxLayout 是**按内容框**居中。
            // 之前直接在带非对称 padding 的 Row 里用 anchors.verticalCenter，等于按"整行高"居中，
            // 比内容框中心低 2px —— 这就是发送键看起来歪的原因。现在外层 Item 只负责 margin，
            // 内层 Row 铺满内容框，子项在 Row 内居中。
            Item {
                id: controlRow
                width: parent.width
                implicitHeight: inner.height + 2 + 6

                Row {
                    id: inner
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 8                  // kRowSidePadding
                    anchors.rightMargin: 8
                    anchors.topMargin: 2                   // kRowTopPadding
                    height: Math.max(chip.implicitHeight, sendButton.implicitHeight)
                    spacing: 12

                    // 模型 / 思考档位 chip（原版 ModelSelector：28px 高，无模型时隐藏）
                    ModelSelectorChip {
                        id: chip
                        anchors.verticalCenter: parent.verticalCenter
                        onSelectModelRequested: (provider, model) => Hub.selectModel(provider, model)
                        onSelectLevelRequested: (levelId) => Hub.selectLevel(levelId)
                    }

                    Item {
                        width: parent.width - chip.width - sendButton.width - 2 * parent.spacing
                        height: 1
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    IconButton {
                        id: sendButton
                        anchors.verticalCenter: parent.verticalCenter
                        boxSize: Theme.size.sendButton   // 32×32
                        // 原版：enter-btn.png / stop-btn.png（ChatInputWidget.cpp:334/418）
                        iconSource: root.streaming
                            ? "qrc:/ULinHub/stop-btn.png"
                            : "qrc:/ULinHub/enter-btn.png"
                        iconSize: Theme.size.sendButton
                        // 原版悬停：叠在图标之上的圆形白色蒙版（hover 35% / pressed 55%）
                        circleHoverOverlay: true
                        tooltip: root.streaming ? qsTrId("chat_stop_output") : qsTrId("common_send")
                        // 原版：按钮**永远可用**（没有 disabled 判定）
                        onClicked: root.streaming ? root.stop() : root.submit()
                    }
                }
            }
        }
    }

    // ---- ③ 小灰字统计行：固定 14px、始终显示、空串不画字 ----
    Text {
        id: statsLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // 比卡片底边再低 14px：原先与卡片底边贴齐，被输入卡的阴影（inputDy 3 / inputBlur 12）顶着。
        // 只挪文字、**不动 root.implicitHeight** —— 调用方（Shell 的 composerLoader）是底部对齐的，
        // 加高度会把输入卡连同消息列表一起顶上移。
        // 余量：composerLoader 的 bottomMargin = Theme.spacing.columnClearance（16px），
        // 减去这 14px 后距容器底边还剩 2px，不会被裁切。上限是 -16。
        anchors.bottomMargin: -14
        height: Theme.size.statsLineHeight
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        text: Hub.statsLine
        elide: Text.ElideRight
        color: Theme.color.textTertiary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption

        // 仅在被省略号截断时给 tooltip（原版 syncToolTip 同规则）
        AppToolTip {
            visible: statsLine.truncated && statsLineMouse.containsMouse
            text: Hub.statsLine
        }

        MouseArea {
            id: statsLineMouse
            anchors.fill: parent
            hoverEnabled: true
        }
    }

    // ---- ④ 斜杠命令菜单 ----
    // 用 Popup（= 画在窗口 Overlay 层）而不是内联 Rectangle，原因是实测踩到过一次：
    //   · Composer 的兄弟节点 messageLoader 是**后声明**的（见 basic/shell/Shell.qml），
    //     后声明的兄弟画在上面 ⇒ 菜单被消息列表压住，列表里那颗「加载更多」直接盖在菜单上；
    //   · 命中测试跟绘制同序 ⇒ 菜单行根本点不到，点了是列表/按钮收到事件。
    //   `z` 只在同一个父节点内排序，加多大都没用；Popup 走 Overlay，天然在所有普通项之上。
    // `focus: false` 沿用 ModelSelectorChip 的做法：不抢输入框焦点，用户可以一边看候选一边继续敲。
    // 候选来自引擎（Hub.commands.items），这里只做前缀过滤 —— 本地不维护第二份命令表，"认不认识"由引擎裁定。
    Popup {
        id: commandMenu
        objectName: "composerCommandMenu"   // 供离屏试装台定位（Popup 不在 children 里，试装台走 Overlay）

        // 只在"还在敲命令名"时有候选：`/com` 会过滤，`/compact ` 之后（名字已敲定）自动收起
        readonly property var candidates: Hub.commands.matching(editor.text)
        property int index: 0
        property bool dismissed: false
        // 补全时会把文本改掉，那次改动不该被当成"用户又在敲"而把菜单重新弹出来
        property bool suppressReopen: false

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
        // "选中的正是我敲完的那个名字" ⇒ 这一下 Enter 不再是补全，而是提交（= 执行命令）
        function isExact() {
            const item = current()
            return item !== null && editor.text === "/" + item.name
        }
        function complete() {
            const item = current()
            if (item === null)
                return
            suppressReopen = true
            // 有 input hint 的命令补一个空格（用户接着敲参数）；没有的就把名字放上，再按 Enter 即执行
            editor.text = "/" + item.name + (item.hint ? " " : "")
            editor.cursorPosition = editor.text.length
            suppressReopen = false
            dismissed = true          // 补完就收，别继续挡着内容
            close()
            editor.forceActiveFocus()
        }
        // Popup 的 visible 不能直接绑（closePolicy 从内部关掉它会**拆掉绑定**，之后再也弹不出来），
        // 所以只在这里显式开关；所有会影响"该不该显示"的地方都调它。
        function syncVisibility() {
            if (suppressReopen || dismissed || candidates.length === 0)
                close()
            else
                open()
        }

        onCandidatesChanged: syncVisibility()
        // 被 Esc / 点到外面关掉时记下来：等用户再敲字（onTextChanged 会清掉 dismissed）才重新弹
        onClosed: dismissed = true

        parent: root
        x: 16
        y: shadowHost.y - height - 8            // 贴输入卡上沿 8px
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
            // 命令多时封顶（与 ModelSelectorChip 的菜单同一手法），否则会把整个菜单顶出屏幕
            implicitHeight: Math.min(contentHeight, 320)
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
                                color: Theme.color.accent
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: hintText
                                // input hint = 这个命令吃什么参数（引擎给的 `input.hint`）
                                text: commandRow.modelData.hint
                                visible: text.length > 0
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                // 宽度手工算：Row 里若用 `parent.width - x` 会和 Row 的定位互相触发
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
