// 全宽文本按钮（原版 sidebar.qss 的 #newWorkspaceButton / #clearSessionButton 这类）。
// variant 决定配色（都取 token）：
//   primary —— primaryButtonBg / Text，hover primaryButtonHover，pressed primaryButtonPressed
//   danger  —— dangerButtonBg / textOnAccent，hover danger
// radius 8、padding 8px 12px、14px 与原版一致。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    property string variant: "primary"
    property string tooltip: ""

    signal clicked()

    readonly property bool hovered: area.containsMouse
    readonly property bool pressedState: area.pressed
    readonly property color bgColor: {
        if (!root.enabled)
            return Theme.color.primaryButtonDisabledBg
        if (root.variant === "danger")
            return root.hovered ? Theme.color.danger : Theme.color.dangerButtonBg
        if (root.variant === "primary")
            return root.pressedState ? Theme.color.primaryButtonPressed
                : (root.hovered ? Theme.color.primaryButtonHover : Theme.color.primaryButtonBg)
        return "transparent"
    }

    implicitHeight: Math.max(32, labelText.implicitHeight + 16)
    opacity: enabled ? 1.0 : 0.9

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.control      // 8
        color: root.bgColor
    }

    Text {
        id: labelText
        anchors.centerIn: parent
        text: root.text
        color: root.enabled
            ? (root.variant === "danger" ? Theme.color.textOnAccent : Theme.color.primaryButtonText)
            : Theme.color.primaryButtonDisabledText
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title   // 14px
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    AppToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && area.containsMouse
    }
}
