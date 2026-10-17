// 面板坞里"还没接入"的块占位：说清楚缺什么、下一步接哪个接口，别放哑界面。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string title: ""
    property string detail: ""

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spacing.columnClearance
        spacing: 8

        Text {
            width: parent.width
            text: root.title
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            text: root.detail
            color: Theme.color.textTertiary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            lineHeight: 1.4
        }
    }
}
