// iOS 玻璃 · 全宽按钮
// ---------------------------------------------------------------------------
// 契约与内置 widgets/ActionButton.qml 一致（text / variant / tooltip / clicked / enabled）。
//
// 造型改过一轮。第一版是全胶囊 + 上下渐变 + 一层同色投影，那是"糖果按钮"：
// 渐变让它像塑料，同色投影让它飘起来。现在三档都回到平面：
//   primary  = 实心 systemBlue，radius 10，白字，无边框、无渐变、无投影
//   elevated = 极淡的灰填充（不是白玻璃），文字用主色 —— 这是现代次要按钮的做法
//   ghost    = 纯文字，systemBlue
// 圆角 10 而不是 height/2：胶囊在桌面尺寸（34px 高）下会显得胖，10 更接近
// iOS 里 inset grouped 列表内的按钮形状。
// 按压反馈只保留 0.98 的微缩 + 一档变深，不再有额外图层。
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
    readonly property bool isFilled: variant === "primary" || variant === "danger"
    readonly property color dangerTone: Theme.color.dangerText || Theme.color.danger

    readonly property real corner: 10

    readonly property color bgColor: {
        if (root.isGhost) {
            if (!root.enabled || (!root.hovered && !root.pressedState))
                return "transparent"
            return root.pressedState ? Theme.color.activeBg : Theme.color.chipHoverBg
        }
        if (!root.enabled)
            return Theme.color.primaryButtonDisabledBg
        if (root.variant === "danger")
            return root.hovered ? Qt.darker(Theme.color.danger, 1.06) : Theme.color.dangerButtonBg
        if (root.variant === "elevated") {
            // iOS 的 .tinted 次要按钮：底是主色的淡色，文字同色。灰底会读成"禁用"。
            // 暗色下同一个 alpha 会糊进背景（蓝字也发暗），所以要单独提一档。
            const a = Theme.color.accent
            const k = Theme.isDark
                ? (root.pressedState ? 0.30 : (root.hovered ? 0.24 : 0.17))
                : (root.pressedState ? 0.20 : (root.hovered ? 0.15 : 0.10))
            return Qt.rgba(a.r, a.g, a.b, k)
        }
        return root.pressedState ? Theme.color.primaryButtonPressed
            : (root.hovered ? Theme.color.primaryButtonHover : Theme.color.primaryButtonBg)
    }

    readonly property color textColor: {
        if (!root.enabled)
            return root.isFilled ? Theme.color.primaryButtonDisabledText : Theme.color.textTertiary
        if (root.variant === "ghostDanger")
            return root.dangerTone
        if (root.variant === "ghost")
            return root.hovered ? Theme.color.accentHover : Theme.color.accent
        if (root.variant === "elevated")
            return Theme.color.accent
        if (root.variant === "danger")
            return Theme.color.dangerButtonText || Theme.color.textOnAccent
        return Theme.color.primaryButtonText
    }

    implicitHeight: 34
    opacity: enabled ? 1.0 : 0.85

    scale: (pressedState && !isGhost) ? 0.985 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.corner
        color: root.bgColor
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body + 1
        font.weight: root.isFilled ? 600 : 500
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
