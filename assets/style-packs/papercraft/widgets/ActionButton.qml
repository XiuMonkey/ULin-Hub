// 素纸 · 全宽按钮
// 契约与内置 widgets/ActionButton.qml 一致：text / variant / tooltip / clicked() / enabled。
// 造型：方角、1px 墨线、按下整块位移 1px（像印章压下去），字距略开。
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

    // 位移量：按下时整块（面 + 字）下沉
    readonly property int sink: root.pressedState ? 1 : 0

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: root.sink
        radius: Theme.radius.small * 0.4
        color: root.bgColor

        // 只有 elevated 需要描边立住（其余靠面色）；ghost 永不描边
        border.width: root.variant === "elevated" ? 1 : 0
        border.color: Theme.color.border
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.sink
        text: root.text
        color: root.textColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: root.variant === "primary" || root.variant === "danger"
            ? Theme.font.weightMedium : Font.Normal
        font.letterSpacing: 0.4
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
