// 荧光屏 · 通用卡片
// 属性契约与内置 widgets/Card.qml 一致（default content / radius / color / borderColor /
// bordered / padding / shadowEnabled / shadowBlur / shadowDy / shadowColor）。
// 造型：直角终端面板 —— 无阴影，靠 1px 磷光描边 + 四角 L 形角标 立住；标题区不留装饰。
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

    // shadowEnabled 在终端风格里没有意义（磷光屏不发散），保留属性只为契约兼容
    Rectangle {
        id: surface
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.width: root.bordered ? 1 : 0
        border.color: root.borderColor
    }

    // 左上 / 右下 的 L 形角标：靠 4 个 8px 细条拼出来
    Item {
        anchors.fill: parent
        visible: root.bordered
        readonly property color ink: Qt.rgba(root.borderColor.r, root.borderColor.g,
                                             root.borderColor.b, 0.9)
        readonly property int len: 10

        Rectangle { x: 0; y: 0; width: parent.len; height: 1; color: parent.ink }
        Rectangle { x: 0; y: 0; width: 1; height: parent.len; color: parent.ink }
        Rectangle { x: parent.width - parent.len; y: parent.height - 1; width: parent.len; height: 1; color: parent.ink }
        Rectangle { x: parent.width - 1; y: parent.height - parent.len; width: 1; height: parent.len; color: parent.ink }
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
