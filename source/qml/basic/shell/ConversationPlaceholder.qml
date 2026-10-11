// 会话区占位：M3 只显示当前会话身份与服务端状态，消息列表/输入区在 M4–M5 迁入。
// 本文件将被 chat/MessageListView.qml + chat/Composer.qml 取代。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: placeholder

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 4 * Theme.spacing.windowMargin, 520)
        spacing: Theme.spacing.sectionGap

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Hub.currentSessionId.length > 0 ? "当前会话" : "还没有选中会话"
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.lead
            font.weight: Theme.font.weightBold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Hub.currentSessionId.length > 0 ? Hub.currentSessionId : "在左侧列表里选一个，或点 ＋ 新建"
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            color: Theme.color.textSecondary
            font.family: "Consolas"
            font.pixelSize: Theme.font.caption
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "消息列表与输入区在 M4–M5 迁入。服务端状态：" + Hub.serverState
            wrapMode: Text.WordWrap
            color: Theme.color.textCaption
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

    }
}