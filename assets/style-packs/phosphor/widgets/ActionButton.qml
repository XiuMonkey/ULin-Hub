// 荧光屏 · 全宽按钮
// 契约与内置 widgets/ActionButton.qml 一致：text / variant / tooltip / clicked() / enabled。
// 造型：直角终端按钮 —— 1px 磷光描边、左沿一道状态条、等宽字；primary 是满格荧光块反白。
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
            return root.pressedState ? Theme.color.activeBg : Theme.color.chipHoverBg
        }
        if (!root.enabled)
            return Theme.color.primaryButtonDisabledBg
        if (root.variant === "danger")
            return root.hovered ? Theme.color.danger : Theme.color.dangerButtonBg
        if (root.variant === "elevated")
            return root.pressedState ? Theme.color.buttonPressed
                : (root.hovered ? Theme.color.buttonHover : "transparent")
        return root.pressedState ? Theme.color.primaryButtonPressed
            : (root.hovered ? Theme.color.primaryButtonHover : Theme.color.primaryButtonBg)
    }

    readonly property color textColor: {
        if (!root.enabled)
            return Theme.color.primaryButtonDisabledText
        if (root.variant === "ghostDanger")
            return root.dangerTone
        if (root.variant === "ghost")
            return root.hovered ? Theme.color.textPrimary : Theme.color.textSecondary
        if (root.variant === "elevated")
            return root.hovered ? Theme.color.accentHover : Theme.color.textSecondary
        if (root.variant === "danger")
            return Theme.color.dangerButtonText || Theme.color.textOnAccent
        return Theme.color.primaryButtonText
    }

    implicitHeight: 32
    opacity: enabled ? 1.0 : 0.85

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.control
        color: root.bgColor
        border.width: root.isGhost ? 0 : 1
        border.color: root.variant === "primary" ? root.bgColor : Theme.color.border
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    // 左沿状态条：像终端里被选中的命令行
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        visible: !root.isGhost
        color: root.variant === "primary" ? Theme.color.primaryButtonText : Theme.color.accent
        opacity: root.variant === "primary" ? 0.25 : 0.9
    }

    Text {
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: "Consolas"
        font.pixelSize: Theme.font.title - 1
        font.weight: Font.Normal
        font.letterSpacing: 1
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
