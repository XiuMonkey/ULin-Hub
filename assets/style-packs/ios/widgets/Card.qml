// iOS 玻璃 · 通用卡片（分组列表里的那块"玻璃面"）
// ---------------------------------------------------------------------------
// 属性契约与内置 widgets/Card.qml 完全一致（default content / radius / color /
// borderColor / bordered / padding / shadow*），所以任何调用方都不用改。
//
// 造型：iOS 的 inset grouped 卡片 —— 面色取色板里的半透明玻璃色，上面再叠四层材质
// （见 skin/window/Glass.qml）：上亮下暗的厚度、顶沿受光高光、1px 白描边接力、柔和投影。
// 卡片之间靠 16px 留白分组，而不是靠分割线，这是 iOS 设置页的分组逻辑。
import QtQuick
import ULinHub 1.0
import "../skin/window"

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

    Glass {
        anchors.fill: parent
        radius: root.radius
        tint: root.color
        borderColor: root.bordered ? root.borderColor : "transparent"
        bordered: root.bordered
        // 不要再乘系数：内置调用方（侧栏、面板栏）传进来的已经是"面板档"的模糊半径，
        // 再放大 1.6 倍，边缘会糊成一条黑带。
        shadowBlur: root.shadowEnabled ? root.shadowBlur : 0
        shadowDy: root.shadowDy * 0.7
        shadowColor: root.shadowColor
    }

    // 内容层：留白取色板，和内置件一致
    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
