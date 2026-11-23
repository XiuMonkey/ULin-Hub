// 会话目标条（goal 投影）：DSH Web 的 GoalBar 同款位置——输入框上方一条 36px 的紧凑条。
// 只显示持久化目标状态；pause/resume/edit/clear 属于命令/工具侧动作，本卡先做只读展示。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    readonly property var goal: Hub.goal || ({})
    // DSH 的 GoalBar 不渲染 complete：完成后目标条直接消失。
    readonly property bool hasGoal: goal.id !== undefined && goal.phase !== "complete"
    readonly property string phaseText: {
        switch (goal.phase) {
        case "active": return qsTrId("chat_goal_phase_active")
        case "paused": return qsTrId("chat_goal_phase_paused")
        case "blocked": return qsTrId("chat_goal_phase_blocked")
        default: return ""
        }
    }

    implicitHeight: hasGoal ? bar.height : 0

    Rectangle {
        id: bar
        visible: root.hasGoal
        width: root.width
        height: 36
        radius: Theme.radius.card
        color: Theme.color.composerBg || Theme.color.panelBg
        border.width: 0.5
        border.color: Theme.color.inputBorder

        LineIcon {
            id: glyph
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            name: "goal"
            size: 14
            color: Theme.color.textTertiary
        }

        Text {
            id: phase
            anchors.left: glyph.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.phaseText
            color: Theme.color.textPrimary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
            font.weight: Theme.font.weightMedium
        }

        Text {
            id: objective
            anchors.left: phase.right
            anchors.leftMargin: 8
            anchors.right: rounds.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.goal.objective || ""
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
            elide: Text.ElideRight
        }

        Text {
            id: rounds
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrId("chat_goal_rounds_fmt").arg(root.goal.roundsStarted || 0).arg(root.goal.maxGoalRounds || 0)
            color: Theme.color.textTertiary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.small
        }

        ToolTip.visible: hoverArea.containsMouse && root.goal.phase === "blocked"
        ToolTip.text: root.goal.blockedReason || ""

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
    }
}
