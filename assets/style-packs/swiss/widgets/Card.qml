// 苏黎世 · 通用卡片
// 属性契约与内置 widgets/Card.qml 一致（default content / radius / color / borderColor /
// bordered / padding / shadowEnabled / shadowBlur / shadowDy / shadowColor）。
// 造型：无圆角、2px 硬描边、零阴影 —— 一切层次都靠线与留白，不靠投影。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    default property alias content: contentHolder.data

    property real radius: Theme.radius.card
    property color color: Theme.color.cardBg
    property color borderColor: Theme.color.border
    property bool bordered: true
    property real padding: Theme.spacing.cardPadding

    property bool shadowEnabled: false
    property real shadowBlur: Theme.shadow.cardBlur
    property real shadowDy: Theme.shadow.cardDy
    property color shadowColor: Theme.color.shadow

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.width: root.bordered ? 2 : 0
        border.color: root.borderColor
    }

    // 左上角一个小实心方块：像版面里的角标
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.bordered ? 6 : 0
        height: width
        color: Theme.color.accent
        visible: root.bordered
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
