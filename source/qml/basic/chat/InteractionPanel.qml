// 提问 / 工具审批面板（M4b）
//
// 旧壳把面板塞进消息列表（MessageHost::handleMuxFrame → InteractionHandler 建 QWidget 插进布局）。
// QML 侧等价物：这个面板排在输入区上方，出现/收起由 Hub.questionActive / Hub.approvalActive 驱动。
//
// 字段与应答结构**照搬旧壳 InteractionHandler**，一个都不能改：
//   提问帧：{ rpcId, payload: { sessionId, questions:[{id, multiSelect, header, question,
//            options:[{label, description}]}] } }
//   应答  ：{ sessionId, answer: { answers: [ {id, selected:[label…], custom?} ] } }
//   审批帧：{ rpcId, payload: { sessionId, approvalId, toolName, reason } }
//   应答  ：{ sessionId, approvalId, outcome: "allowed-once" | "rejected" }
// 文案全部用既有 id（ask_confirm_questions / ask_submit_answer / ask_answer_placeholder /
// ask_tool_approval_title / ask_tool_fmt / ask_reason_fmt / common_reject / ask_allow_once）。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    readonly property bool activePanel: Hub.questionActive || Hub.approvalActive
    visible: activePanel
    implicitHeight: activePanel ? card.implicitHeight : 0

    // 每个问题一项 { id, selected: [label…], custom: "" }
    property var selection: []

    function resetSelection() {
        var built = []
        var qs = Hub.questions
        for (var i = 0; i < qs.length; ++i) {
            var opts = qs[i].options ? qs[i].options : []
            var pre = []
            // 与旧壳一致：单选且只有一个选项时默认选中
            if (opts.length === 1 && !qs[i].multiSelect)
                pre = [opts[0].label]
            built.push({ id: qs[i].id, selected: pre, custom: "" })
        }
        root.selection = built
    }

    function isSelected(qIndex, label) {
        var item = root.selection[qIndex]
        if (!item)
            return false
        return item.selected.indexOf(label) >= 0
    }

    function toggle(qIndex, label, multi) {
        var next = root.selection.slice()
        var item = next[qIndex]
        if (!item)
            return
        var copy = { id: item.id, selected: item.selected.slice(), custom: item.custom }
        if (multi) {
            var at = copy.selected.indexOf(label)
            if (at >= 0)
                copy.selected.splice(at, 1)
            else
                copy.selected.push(label)
        }
        else {
            copy.selected = (copy.selected.length === 1 && copy.selected[0] === label) ? [] : [label]
        }
        next[qIndex] = copy
        root.selection = next
    }

    function setCustom(qIndex, text) {
        var next = root.selection.slice()
        var item = next[qIndex]
        if (!item)
            return
        next[qIndex] = { id: item.id, selected: item.selected.slice(), custom: text }
        root.selection = next
    }

    function submitAnswers() {
        Hub.submitAnswers(root.selection)
    }

    // 登记进统一连接表（选择态复位是面板内部逻辑 ⇒ 不允许接管）
    Connections {   // 页内联动，没有管理价值 ⇒ 不登记（登记等于把 id 暴露成可被接管的目标）
        target: Hub
        function onInteractionChanged() {
            root.resetSelection()
        }
    }

    Component.onCompleted: resetSelection()

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: column.implicitHeight + 24
        radius: Theme.radius.panel
        color: Theme.color.panelBg
        border.width: 1
        border.color: Theme.color.border

        Column {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 14          // #interactionPanel 的内边距 (14,12)
            anchors.rightMargin: 14
            anchors.topMargin: 12
            spacing: 8

            // ================= 提问 =================
            Text {
                visible: Hub.questionActive
                text: qsTrId("ask_confirm_questions")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }

            Repeater {
                model: Hub.questionActive ? Hub.questions : []

                delegate: Rectangle {
                    id: questionCard

                    required property var modelData
                    required property int index

                    width: column.width
                    implicitHeight: qcol.implicitHeight + 20
                    radius: Theme.radius.card
                    color: Theme.color.inputBg
                    border.width: 1
                    border.color: Theme.color.border

                    readonly property var options: modelData.options ? modelData.options : []

                    Column {
                        id: qcol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 12   // #questionCard 的内边距 (12,10)
                        anchors.rightMargin: 12
                        anchors.topMargin: 10
                        spacing: 6

                        Text {
                            visible: questionCard.modelData.header.length > 0
                            width: parent.width
                            text: questionCard.modelData.header
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.title
                            font.weight: Theme.font.weightBold
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: questionCard.modelData.question
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                            wrapMode: Text.WordWrap
                        }

                        // 选项（单选=圆点，多选=方框；与旧壳 QRadioButton/QCheckBox 的语义一致）
                        Repeater {
                            model: questionCard.options

                            delegate: Item {
                                id: optionRow

                                required property var modelData
                                required property int index

                                width: qcol.width
                                implicitHeight: optLabel.implicitHeight + 6

                                readonly property bool checked: root.isSelected(questionCard.index, modelData.label)

                                Rectangle {
                                    id: marker
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.topMargin: 2
                                    width: 14
                                    height: 14
                                    radius: questionCard.modelData.multiSelect ? 3 : 7
                                    color: optionRow.checked ? Theme.color.accent : "transparent"
                                    border.width: 1
                                    border.color: optionRow.checked ? Theme.color.accent : Theme.color.inputBorder

                                    Text {
                                        anchors.centerIn: parent
                                        visible: optionRow.checked
                                        text: "✓"
                                        color: Theme.color.textOnAccent
                                        font.pixelSize: Theme.font.small
                                    }
                                }

                                Text {
                                    id: optLabel
                                    anchors.left: marker.right
                                    anchors.leftMargin: 8
                                    anchors.right: parent.right
                                    text: optionRow.modelData.label
                                    color: Theme.color.textPrimary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.body
                                    wrapMode: Text.WordWrap
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.toggle(questionCard.index, optionRow.modelData.label,
                                        questionCard.modelData.multiSelect)
                                }

                                AppToolTip {
                                    text: optionRow.modelData.description ? optionRow.modelData.description : ""
                                    visible: text.length > 0 && optHover.containsMouse
                                }
                                MouseArea {
                                    id: optHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }
                            }
                        }

                        // 没有选项 = 让用户自由填写（旧壳用 QLineEdit + ask_answer_placeholder）
                        TextField {
                            id: customEdit
                            width: parent.width
                            visible: questionCard.options.length === 0
                            placeholderText: qsTrId("ask_answer_placeholder")
                            color: Theme.color.textPrimary
                            placeholderTextColor: Theme.color.textCaption
                            selectionColor: Theme.color.selectionBg
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                            leftPadding: Theme.spacing.fieldPadX
                            rightPadding: Theme.spacing.fieldPadX
                            topPadding: Theme.spacing.fieldPadY
                            bottomPadding: Theme.spacing.fieldPadY
                            onTextChanged: root.setCustom(questionCard.index, text)

                            background: Rectangle {
                                implicitHeight: 30
                                radius: Theme.radius.input
                                color: Theme.color.panelBg
                                border.width: 1
                                border.color: customEdit.activeFocus ? Theme.color.accent : Theme.color.inputBorder
                            }
                        }
                    }
                }
            }

            // Column 的子项不能用 anchors —— 提交按钮包一层整宽 Item 再右对齐
            Item {
                width: column.width
                height: 34
                visible: Hub.questionActive

                ActionButton {
                    anchors.right: parent.right
                    width: 120
                    height: 34
                    text: qsTrId("ask_submit_answer")
                    variant: "primary"
                    onClicked: root.submitAnswers()
                }
            }

            // ================= 工具审批 =================
            Text {
                visible: Hub.approvalActive
                text: qsTrId("ask_tool_approval_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }

            Text {
                visible: Hub.approvalActive
                width: parent.width
                text: qsTrId("ask_tool_fmt").arg(Hub.approvalToolName)
                color: Theme.color.textPrimary
                font.family: "Consolas"
                font.pixelSize: Theme.font.body
                wrapMode: Text.WordWrap
            }

            Text {
                visible: Hub.approvalActive && Hub.approvalReason.length > 0
                width: parent.width
                text: qsTrId("ask_reason_fmt").arg(Hub.approvalReason)
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                wrapMode: Text.WordWrap
            }

            Item {
                width: column.width
                height: 34
                visible: Hub.approvalActive

                Row {
                    anchors.right: parent.right
                    spacing: 8

                    ActionButton {
                        width: 96
                        height: 34
                        text: qsTrId("common_reject")
                        variant: "danger"
                        onClicked: Hub.respondApproval(false)
                    }

                    ActionButton {
                        width: 96
                        height: 34
                        text: qsTrId("ask_allow_once")
                        variant: "primary"
                        onClicked: Hub.respondApproval(true)
                    }
                }
            }
        }
    }
}
