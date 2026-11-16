// 提问 / 工具审批面板（M4b）：排在输入区上方，出现/收起由 Hub.questionActive / Hub.approvalActive 驱动。
// ⚠️ 字段与应答结构沿用既有 InteractionHandler，一个都不能改：
// 提问帧：{ rpcId, payload: { sessionId, questions:[{id, multiSelect, header, question,
// options:[{label, description}]}] } }
// 应答  ：{ sessionId, answer: { answers: [ {id, selected:[label…], custom?} ] } }
// 审批帧：{ rpcId, payload: { sessionId, approvalId, toolName, reason } }
// 应答  ：{ sessionId, approvalId, outcome: "allowed-once" | "rejected" }
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

    // 选择态、默认项、payload 组装都在 ChatController；QML 只负责渲染和转发点击。
    function isSelected(qIndex, label) {
        var item = Hub.answerSelections[qIndex]
        if (!item)
            return false
        return item.selected.indexOf(label) >= 0
    }

    function toggle(qIndex, label) {
        Hub.toggleAnswerOption(qIndex, label)
    }

    function setCustom(qIndex, text) {
        Hub.setAnswerCustom(qIndex, text)
    }

    function submitAnswers() {
        Hub.submitAnswerSelections()
    }

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: column.implicitHeight + 24
        radius: Theme.radius.panel
        color: Theme.color.contentBg || Theme.color.panelBg
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

                    // 原版 `#interactionPanel QFrame#questionCard { background: {{hoverBg}}; border-radius: 10px }`
                    color: Theme.color.hoverBg

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

                        Repeater {   // 选项
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

                                    CheckMark {
                                        anchors.centerIn: parent
                                        visible: optionRow.checked
                                        size: 11
                                        color: Theme.color.textOnAccent
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

                        TextField {   // 没有选项 = 让用户自由填写
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
                                color: Theme.color.contentBg || Theme.color.panelBg
                                border.width: 1

                                // 输入类控件：静息走 inputBorder(10% 黑) = 官方 DSH `--dsw-alias-border-l2`；
                                // 聚焦保留克制可见的中性灰 textSecondary
                                border.color: customEdit.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder
                            }
                        }
                    }
                }
            }

            Item {   // Column 的子项不能用 anchors —— 提交按钮包一层整宽 Item 再右对齐
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

            // ---- 审批：标题行（徽标 + 文案 + 按钮同排）＋ 调用详情卡 ----
            // 详情卡用 tagBg 弱面承载工具名（等宽）与原因，层次靠面色差，不加线不加阴影。
            Item {
                width: column.width
                height: approvalCol.implicitHeight
                visible: Hub.approvalActive

                Column {
                    id: approvalCol
                    width: parent.width
                    spacing: 10

                    Item {   // 标题行
                        width: parent.width
                        height: 28

                        Rectangle {   // 警示徽标：danger 弱底 + 描边
                            id: badge
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            radius: 7
                            color: Qt.rgba(Theme.color.danger.r, Theme.color.danger.g,
                                Theme.color.danger.b, 0.14)
                            border.width: 1
                            border.color: Qt.rgba(Theme.color.danger.r, Theme.color.danger.g,
                                Theme.color.danger.b, 0.35)

                            Text {
                                anchors.centerIn: parent
                                text: "!"
                                color: Theme.color.danger
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                font.weight: Theme.font.weightBold
                            }
                        }

                        Text {
                            anchors.left: badge.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: approvalButtons.left
                            anchors.rightMargin: 8
                            text: qsTrId("ask_tool_approval_title")
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.title
                            font.weight: Theme.font.weightBold
                            elide: Text.ElideRight
                        }

                        Row {
                            id: approvalButtons
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            ActionButton {
                                width: 72
                                height: 28
                                text: qsTrId("common_reject")
                                variant: "ghost"
                                onClicked: Hub.respondApproval(false)
                            }

                            ActionButton {
                                width: 88
                                height: 28
                                text: qsTrId("ask_allow_once")
                                variant: "primary"
                                onClicked: Hub.respondApproval(true)
                            }
                        }
                    }

                    Rectangle {   // 详情卡
                        width: parent.width
                        implicitHeight: detailCol.implicitHeight + 20
                        radius: Theme.radius.card
                        color: Theme.color.tagBg

                        Column {
                            id: detailCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            anchors.topMargin: 10
                            spacing: 4

                            Text {   // 工具名（可能是整条命令，最多三行，超出省略）
                                width: parent.width
                                text: Hub.approvalToolName
                                color: Theme.color.textPrimary
                                font.family: "Consolas"
                                font.pixelSize: Theme.font.caption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }

                            Text {   // 原因（可选）
                                visible: Hub.approvalReason.length > 0
                                width: parent.width
                                text: Hub.approvalReason
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
