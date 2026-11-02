// 霜玻 · 全宽按钮
// 契约与内置 widgets/ActionButton.qml 一致：text / variant / tooltip / clicked() / enabled。
// 造型：胶囊 + 竖向渐变（上亮下暗的玻璃片）+ 细高光边，悬停时整体提亮一档。
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
        radius: height / 2
        color: root.bgColor
        border.width: root.isGhost ? 0 : 1
        border.color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.18 : 0.7)

        // 玻璃面的高光：上沿一条白、下沿一条暗
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: !root.isGhost
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.16 : 0.38) }
                GradientStop { position: 0.55; color: Qt.rgba(255, 255, 255, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, root.pressedState ? 0.16 : 0.06) }
            }
        }

        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: root.variant === "primary" || root.variant === "danger"
            ? Theme.font.weightMedium : Font.Normal
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
