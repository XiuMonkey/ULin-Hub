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
            text: Hub.currentSessionId.length > 0
                ? qsTrId("placeholder_session_current")
                : qsTrId("placeholder_session_none")
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.lead
            font.weight: Theme.font.weightBold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Hub.currentSessionId.length > 0
                ? Hub.currentSessionId
                : qsTrId("placeholder_session_hint")
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            color: Theme.color.textSecondary
            font.family: "Consolas"
            font.pixelSize: Theme.font.caption
        }

    }
}