// Fluent · 全宽按钮
// 契约与内置 widgets/ActionButton.qml 一致：text / variant / tooltip / clicked() / enabled。
// 造型：Win11 按钮 —— 4px 圆角；次要按钮是半透明面 + 1px 描边；主按钮是强调色填充；
// 悬停提亮一档、按下回落到按下色，全部走色板令牌。
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
    readonly property bool isAccent: variant === "primary" || variant === "danger"
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
                : (root.hovered ? Theme.color.buttonHover : Theme.color.buttonBg)
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
            return Theme.color.textPrimary
        if (root.variant === "danger")
            return Theme.color.dangerButtonText || Theme.color.textOnAccent
        return Theme.color.primaryButtonText
    }

    implicitHeight: 32
    opacity: enabled ? 1.0 : 0.9

    Rectangle {
        id: face
        anchors.fill: parent
        radius: Theme.radius.control          // Win11 控件圆角 4px
        color: root.bgColor
        // Win11：次要按钮亮面上有一道 1px 描边；强调色按钮不描边
        border.width: (root.isGhost || root.isAccent) ? 0 : 1
        border.color: Theme.color.border

        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: root.isAccent ? Theme.font.weightMedium : Font.Normal
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
