// 樱岚 · 通用卡片
// 属性契约与内置 widgets/Card.qml 一致（default content / radius / color / borderColor /
// bordered / padding / shadowEnabled / shadowBlur / shadowDy / shadowColor）。
// 造型：大圆角奶白片 + 玫瑰色极淡描边 + 柔和投影（保留内置的 MultiEffect 阴影路径）。
import QtQuick
import QtQuick.Effects
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
        visible: !root.shadowEnabled
        radius: root.radius
        color: root.color
        border.width: root.bordered ? 1 : 0
        border.color: root.borderColor

        // 花瓣边：左上角一小片更亮的粉，让奶白面不死板
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.06 : 0.5) }
                GradientStop { position: 0.38; color: Qt.rgba(255, 255, 255, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.015) }
            }
        }
    }

    MultiEffect {
        id: shadow
        anchors.fill: surface
        visible: root.shadowEnabled
        source: surface
        shadowEnabled: true
        shadowColor: root.shadowColor
        shadowBlur: Math.min(1.0, root.shadowBlur / 32)
        shadowVerticalOffset: root.shadowDy
        shadowOpacity: 1.0
        autoPaddingEnabled: true
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
