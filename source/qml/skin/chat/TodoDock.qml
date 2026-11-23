// 会话任务清单（todo 投影）：DSH Web 输入框上方的 TodoPanel 同款卡片。
// 收起时只显示「任务 + 进度」；展开后逐条显示状态：完成、进行中（转圈）、待处理。
// 数据来自 ChatController::applyProjections 的 todos 整表投影，不在本地自己维护任务状态。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 当前状态统计：三组都为零说明列表为空，由外部决定是否创建本组件。
    readonly property var items: Hub.todos || []
    readonly property int doneCount: countStatus("completed")
    readonly property int activeCount: countStatus("in_progress")
    readonly property int pendingCount: items.length - doneCount - activeCount

    property bool expanded: false

    implicitHeight: card.height

    function countStatus(status) {
        var total = 0
        for (var i = 0; i < root.items.length; ++i) {
            if (root.items[i] && root.items[i].status === status)
                total++
        }
        return total
    }

    // 与 DSH Web 的 progressLabel 同口径：零项的状态不显示。
    function progressText() {
        var parts = []
        if (root.doneCount > 0)
            parts.push(qsTrId("chat_todos_done_fmt").arg(root.doneCount))
        if (root.activeCount > 0)
            parts.push(qsTrId("chat_todos_active_fmt").arg(root.activeCount))
        if (root.pendingCount > 0)
            parts.push(qsTrId("chat_todos_pending_fmt").arg(root.pendingCount))
        return parts.join(" · ")
    }

    Rectangle {
        id: card
        width: root.width
        height: contentColumn.height + 12
        radius: Theme.radius.card
        color: Theme.color.composerBg || Theme.color.panelBg
        border.width: 0.5
        border.color: Theme.color.inputBorder

        Column {
            id: contentColumn
            x: 12
            y: 6
            width: Math.max(0, card.width - 17)
            spacing: root.expanded ? 8 : 0

            Item {
                id: header
                width: contentColumn.width
                height: 24

                LineIcon {
                    id: leadIcon
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    name: "checklist"
                    size: 14
                    color: Theme.color.textTertiary
                }

                Text {
                    id: title
                    anchors.left: leadIcon.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("chat_todos_title")
                    color: Theme.color.textPrimary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                }

                Text {
                    id: progress
                    anchors.left: title.right
                    anchors.leftMargin: 8
                    anchors.right: chevron.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.progressText()
                    color: Theme.color.textTertiary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                    elide: Text.ElideRight
                }

                LineIcon {
                    id: chevron
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.expanded ? "chevron-down" : "chevron-right"
                    size: 12
                    color: Theme.color.textTertiary
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.expanded = !root.expanded
                }
            }

            Column {
                id: listColumn
                width: contentColumn.width
                visible: root.expanded
                spacing: 8

                Repeater {
                    model: root.items

                    delegate: Item {
                        required property var modelData
                        width: listColumn.width
                        height: 20

                        BusySpinner {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            height: 14
                            lineWidth: 2
                            visible: modelData.status === "in_progress"
                            running: visible
                        }

                        Text {
                            id: completedGlyph
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            horizontalAlignment: Text.AlignHCenter
                            visible: modelData.status === "completed"
                            text: "✓"
                            color: Theme.color.accent
                            font.pixelSize: 12
                        }

                        Text {
                            id: pendingGlyph
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            horizontalAlignment: Text.AlignHCenter
                            visible: modelData.status !== "completed" && modelData.status !== "in_progress"
                            text: "○"
                            color: Theme.color.textCaption
                            font.pixelSize: 12
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 26
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.content || ""
                            color: Theme.color.textSecondary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.caption
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
