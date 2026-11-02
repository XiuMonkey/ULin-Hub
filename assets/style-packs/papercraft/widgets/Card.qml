// 素纸 · 通用卡片
// 属性契约与内置 widgets/Card.qml 完全一致（调用方只认这些，不要增删语义）：
//   default content / radius / color / borderColor / bordered / padding /
//   shadowEnabled / shadowBlur / shadowDy / shadowColor
// 造型：直角纸片 + 1px 墨线 + 位移式硬投影（印刷压印感，不糊）。
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

    // 硬投影：整块偏移 dy，不模糊 —— 这是"纸"与"玻璃"最关键的区别
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: root.shadowEnabled ? Math.max(1, root.shadowDy) : 0
        anchors.leftMargin: root.shadowEnabled ? Math.max(1, Math.round(root.shadowDy * 0.5)) : 0
        visible: root.shadowEnabled
        radius: root.radius
        color: root.shadowColor
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.width: root.bordered ? 1 : 0
        border.color: root.borderColor
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
