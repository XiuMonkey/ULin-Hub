// 全宽文本按钮（原版 sidebar.qss 的 #newWorkspaceButton / #clearSessionButton 这类）。
// variant 决定配色（都取 token）：
//   primary     —— primaryButtonBg / Text（官方 brand-primary：light 近黑面 + 白字、dark 近白面 + 近黑字）
//   danger      —— dangerButtonBg / textOnAccent，hover danger
//   elevated    —— 官方 secondary 面：buttonBg/buttonHover/buttonPressed + textPrimary，
//                  无描边（层次靠面色差），用于"新建工作区"这种次要主动作
//   ghost       —— 透明面，文字 textSecondary → 悬停 textPrimary + chipHoverBg（官方 ghost 按钮）
//   ghostDanger —— 透明面，文字 dangerText，悬停 12% 危险色底 —— 危险动作不该用实心红块抢视觉
// radius 12（官方 --dsw-radius-md）、高 32、14px。

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
    // 新 token 带兜底（老风格包没有 dangerText 时退回 danger）
    readonly property color dangerTone: Theme.color.dangerText || Theme.color.danger

    readonly property color bgColor: {
        if (root.isGhost) {
            if (!root.enabled)
                return "transparent"
            if (!root.hovered && !root.pressedState)
                return "transparent"
            return root.variant === "ghostDanger"
                ? Qt.rgba(root.dangerTone.r, root.dangerTone.g, root.dangerTone.b, root.pressedState ? 0.18 : 0.12)
                : (root.pressedState ? Theme.color.activeBg : Theme.color.chipHoverBg)
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
            // 红胶囊的字色不能借用 textOnAccent：那个 token 现在跟着"官方主按钮"走
            // （light 白字 / dark 近黑字），暗色下会把红块上的字压成近黑。
            return Theme.color.dangerButtonText || Theme.color.textOnAccent
        return Theme.color.primaryButtonText
    }

    implicitHeight: 32
    opacity: enabled ? 1.0 : 0.9

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.control      // 12 = 官方 --dsw-radius-md
        color: root.bgColor
        // elevated 的"浮起"在 light 下是白面（官方 button-elevated-fill），需要一道 4% 的收边才立得住；
        // 其余变体的面自己就有对比，不画线（官方 elevated 面的边也是 0.5px ring，不是 border）。
        border.width: root.variant === "elevated" ? 1 : 0
        border.color: Theme.color.border
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        id: labelText
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title   // 14px
        font.weight: root.variant === "elevated" ? Theme.font.weightMedium : Font.Normal
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
