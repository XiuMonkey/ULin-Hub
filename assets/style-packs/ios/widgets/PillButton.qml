// iOS 玻璃 · 胶囊标签（chip）
// ---------------------------------------------------------------------------
// 契约与内置 widgets/PillButton.qml 一致（text / selected / pillHeight /
// horizontalPadding / clicked()）。互斥仍由调用方保证。
//
// 造型是 iOS 的 tinted chip：未选中 = systemFill 灰胶囊 + 次级字色；
// 选中 = systemBlue 的淡色胶囊 + systemBlue 字 + 字重 600。
// 内置件故意不做"字色跟着变"，但那套规则是为中性灰皮肤定的；iOS 的选中态本来
// 就是靠 tint 语义表达的（Control 的 .tinted 样式），这里按 iOS 走。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    property bool selected: false
    property int pillHeight: 20
    property int horizontalPadding: Theme.spacing.fieldPadX

    signal clicked()

    readonly property bool hovered: mouseArea.containsMouse

    implicitWidth: label.implicitWidth + 2 * horizontalPadding
    implicitHeight: pillHeight

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.selected
            ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.12)
            : (root.hovered ? Theme.color.buttonHover : Theme.color.tagBg)

        border.width: 1
        border.color: root.selected
            ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.12)
            : (Theme.isDark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.55))

        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.selected ? Theme.color.accent : Theme.color.textSecondary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        font.weight: root.selected ? 600 : 500

        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
