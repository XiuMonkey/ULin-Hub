// 会话区占位：只在没有选中会话时显示当前会话身份/提示语，消息列表与输入区由
// chat/MessageListView.qml + chat/Composer.qml 承担。

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
            font.family: Theme.font.uiFamily
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
            font.family: Theme.font.monoFamily
            font.pixelSize: Theme.font.caption
        }
    }
}