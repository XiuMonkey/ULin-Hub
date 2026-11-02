// 霜玻 · 通用卡片
// 属性契约与内置 widgets/Card.qml 一致（default content / radius / color / borderColor /
// bordered / padding / shadowEnabled / shadowBlur / shadowDy / shadowColor）。
// 造型：大圆角玻璃面 —— 半透明底（色板里带 alpha）+ 冷白高光边 + 顶部一层自上而下的柔光。
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

        // 玻璃的"上亮下暗"：一层同圆角的竖向渐变盖在面上
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.10 : 0.55) }
                GradientStop { position: 0.42; color: Qt.rgba(255, 255, 255, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.12 : 0.02) }
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
