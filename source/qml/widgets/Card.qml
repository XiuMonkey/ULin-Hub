// 通用卡片：圆角面 + 半像素描边 + 可选阴影（MultiEffect 一次 GPU 绘制）。
// 阴影由 MultiEffect 的 autoPadding 自身撑开，调用方不需要预留留白。

import QtQuick
import QtQuick.Effects
import ULinHub 1.0

Item {
    id: root

    default property alias content: contentHolder.data   // 内容默认填进 padding 之内

    property real radius: Theme.radius.card
    property color color: Theme.color.cardBg
    property color borderColor: Theme.color.border
    property bool bordered: true
    property real padding: Theme.spacing.cardPadding

    property bool shadowEnabled: false   // 是否画阴影；下面三项是像素规格（Theme.shadow.*）
    property real shadowBlur: Theme.shadow.cardBlur
    property real shadowDy: Theme.shadow.cardDy
    property color shadowColor: Theme.color.shadow

    Rectangle {   // 有阴影时作为 MultiEffect 的源（自身隐藏），否则直接绘制
        id: surface
        anchors.fill: parent
        visible: !root.shadowEnabled
        radius: root.radius
        color: root.color

        // 浮起面的边不是 border 而是半像素环（官方 --dsw-elevation-stroke: 0 0 0 .5px），
        // 这里同厚：0.5 逻辑px（DPR 2 时正好一个物理像素）。
        border.width: root.bordered ? 0.5 : 0
        border.color: root.borderColor
    }

    MultiEffect {
        id: shadow
        anchors.fill: surface
        visible: root.shadowEnabled
        source: surface
        shadowEnabled: true   // shadowBlur 是 0..1 的归一化值，不是像素：按 32px 作为满值做线性映射（够用且可预测）
        shadowColor: root.shadowColor
        shadowBlur: Math.min(1.0, root.shadowBlur / 32)
        shadowVerticalOffset: root.shadowDy
        shadowOpacity: 1.0
        autoPaddingEnabled: true   // 阴影会画到卡片边界之外、且 MultiEffect 不参与布局：调用方要给 Item 留出四周空间
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
