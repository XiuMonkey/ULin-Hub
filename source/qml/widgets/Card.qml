// 通用卡片：圆角面 + 1px 描边 + 可选阴影（MultiEffect，GPU 一次绘制）。
// 对应旧壳的 CardShadow/ShadowPanel 那一套"手绘阴影 + 留白反推"，但不再需要调用方算留白 ——
// 阴影由 MultiEffect 的 autoPadding 自己撑开。

import QtQuick
import QtQuick.Effects
import ULinHub 1.0

Item {
    id: root

    // 内容默认填进 padding 之内
    default property alias content: contentHolder.data

    property real radius: Theme.radius.card
    property color color: Theme.color.cardBg
    property color borderColor: Theme.color.border
    property bool bordered: true
    property real padding: Theme.spacing.cardPadding

    // 阴影规格（像素），取值来自 Theme.shadow.*
    property bool shadowEnabled: false
    property real shadowBlur: Theme.shadow.cardBlur
    property real shadowDy: Theme.shadow.cardDy
    property color shadowColor: Theme.color.shadow

    // 有阴影时由 MultiEffect 负责绘制（源面隐藏），否则直接画面
    Rectangle {
        id: surface
        anchors.fill: parent
        visible: !root.shadowEnabled
        radius: root.radius
        color: root.color
        // 官方所有"浮起面"的边不是 border，而是 `--dsw-elevation-stroke: 0 0 0 .5px` 的**半像素环**
        // （见 dsh-client-ui-theme 的 gradient_shadow_text.css）。这里跟它同厚：0.5 逻辑px，
        // 在 200% 缩放下正好 1 物理px —— 原来是 1 逻辑px = 2 物理px，卡片边缘看着比官方"硬一倍"。
        border.width: root.bordered ? 0.5 : 0
        border.color: root.borderColor
    }

    MultiEffect {
        id: shadow
        anchors.fill: surface
        visible: root.shadowEnabled
        source: surface
        // shadowBlur 是 0..1 的归一化值，不是像素：按 32px 作为满值做线性映射（够用且可预测）
        shadowEnabled: true
        shadowColor: root.shadowColor
        shadowBlur: Math.min(1.0, root.shadowBlur / 32)
        shadowVerticalOffset: root.shadowDy
        shadowOpacity: 1.0
        // 阴影会画到卡片边界之外、且 MultiEffect 不参与布局：调用方要给 Item 留出四周空间
        autoPaddingEnabled: true
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
