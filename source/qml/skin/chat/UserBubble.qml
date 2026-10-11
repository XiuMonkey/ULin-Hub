// 用户消息气泡：右对齐、圆角、色取自 userBubble* token（与旧壳 #userUnit 同源）。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string text: ""

    // 宽度上限与旧壳 UserMessageUnit 的 MaxWidth 语义一致：长消息不铺满整行
    implicitHeight: bubble.implicitHeight

    Rectangle {
        id: bubble
        anchors.right: parent.right
        width: Math.min(content.implicitWidth + 2 * Theme.spacing.cardPadding, root.width * 0.72)
        implicitHeight: content.implicitHeight + 2 * Theme.spacing.buttonPadY
        radius: Theme.radius.panel
        color: Theme.color.userBubbleBg
        border.width: 1
        border.color: Theme.color.border

        Text {
            id: content
            anchors.centerIn: parent
            width: Math.min(implicitWidth, bubble.width - 2 * Theme.spacing.cardPadding)
            text: root.text
            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            color: Theme.color.userBubbleText
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }
}
