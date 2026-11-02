// 苏黎世 · 全宽按钮
// 契约与内置 widgets/ActionButton.qml 一致：text / variant / tooltip / clicked() / enabled。
// 造型：方形实块 —— primary 纯黑反白、elevated 白底黑框、ghost 纯字；悬停时黑块换成红。
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
    readonly property bool isGhost: variant === "ghost" || variant === "ghostDanger"
    readonly property color dangerTone: Theme.color.dangerText || Theme.color.danger

    readonly property color bgColor: {
        if (root.isGhost) {
            if (!root.enabled || (!root.hovered && !root.pressedState))
                return "transparent"
            return root.pressedState ? Theme.color.activeBg : Theme.color.hoverBg
        }
        if (!root.enabled)
            return Theme.color.primaryButtonDisabledBg
        if (root.variant === "danger")
            return root.hovered ? Theme.color.danger : Theme.color.dangerButtonBg
        if (root.variant === "elevated")
            return root.pressedState ? Theme.color.buttonPressed
                : (root.hovered ? Theme.color.buttonHover : Theme.color.buttonBg)
        return root.hovered ? Theme.color.accent : Theme.color.primaryButtonBg
    }

    readonly property color textColor: {
        if (!root.enabled)
            return Theme.color.primaryButtonDisabledText
        if (root.variant === "ghostDanger")
            return root.dangerTone
        if (root.variant === "ghost")
            return root.hovered ? Theme.color.accent : Theme.color.textPrimary
        if (root.variant === "elevated")
            return Theme.color.textPrimary
        if (root.variant === "primary")
            return root.hovered ? Theme.color.onAccent : Theme.color.primaryButtonText
        if (root.variant === "danger")
            return Theme.color.dangerButtonText || Theme.color.textOnAccent
        return Theme.color.primaryButtonText
    }

    implicitHeight: 34
    opacity: enabled ? 1.0 : 0.85

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.control
        color: root.bgColor
        border.width: root.variant === "elevated" ? 2 : 0
        border.color: Theme.color.border
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: Theme.font.weightBold
        font.letterSpacing: 0.8
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
