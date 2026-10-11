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
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 扩展注入点：扩展用 HostUi.item("composer") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("composer", this)

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

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        // 只有 Shift 例外（Ctrl+Enter 也发送，与原版 eventFilter 一致）
                        if (event.modifiers & Qt.ShiftModifier)
                            return
                        event.accepted = true
                        if (root.streaming)
                            root.stop()
                        else
                            root.submit()
                    }
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
                        // 原版：EnterBtn.png / StopBtn.png（ChatInputWidget.cpp:334/418）
                        iconSource: root.streaming
                            ? "qrc:/DSHHub/StopBtn.png"
                            : "qrc:/DSHHub/EnterBtn.png"
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
}
