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
        width: content.width + 2 * Theme.spacing.cardPadding
        implicitHeight: content.contentHeight + 2 * Theme.spacing.buttonPadY
        radius: Theme.radius.panel
        color: Theme.color.userBubbleBg
        border.width: 1
        border.color: Theme.color.border

        // 量宽度专用：Text 的 implicitWidth 是"不换行时的自然宽度"，
        // 而 TextEdit 在宽度定下来之前**不提供可用的 implicitWidth/Height**（上一版把行高算成 0 就栽在这）。
        Text {
            id: measure
            visible: false
            text: root.text
            textFormat: Text.MarkdownText
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }

        // 只读但可选中/复制（Text 不可选，这里必须是 TextEdit）
        TextEdit {
            id: content
            objectName: "bubbleText"
            anchors.centerIn: parent
            width: Math.min(measure.implicitWidth, Math.max(40, root.width * 0.72 - 2 * Theme.spacing.cardPadding))
            height: contentHeight
            text: root.text
            textFormat: TextEdit.MarkdownText
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            activeFocusOnPress: false
            color: Theme.color.userBubbleText
            selectionColor: Theme.color.selectionBg
            selectedTextColor: content.color
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }
}
