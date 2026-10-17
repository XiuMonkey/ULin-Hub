// 设置 · Server设置（按原版 Settings.cpp 的 serverPanel 复刻）：
//   标签 settings_server_address + 输入框（占位 http://127.0.0.1:3080）+ 说明 settings_server_address_hint
//   + 按钮 settings_save_and_restart（保存后重启服务端生效）。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    // 草稿：打开设置时用服务端当前地址填一次（原版 refreshOnOpen 里做同一件事）
    property string draft: Hub.settings.serverUrl

    Connections {   // 草稿回填，页内逻辑 ⇒ 不登记（登记等于把 id 暴露成可被接管的目标）
        target: Hub.settings
        function onServerUrlChanged() {
            page.draft = Hub.settings.serverUrl
        }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6

        Text {
            text: qsTrId("settings_server_address")
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
        }

        Rectangle {
            id: urlFrame
            width: parent.width
            implicitHeight: 32
            radius: Theme.radius.control
            color: Theme.color.panelBg
            border.width: 1
            // 聚焦描边走中性灰 textSecondary（原来是强调蓝 accent）：与输入卡片（Composer）的聚焦态同款；
            // 静息走 inputBorder(10% 黑) = 官方 DSH `--dsw-alias-border-l2`（输入类控件那一档）
            border.color: urlInput.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder

            TextInput {
                id: urlInput
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing.fieldPadX
                anchors.rightMargin: Theme.spacing.fieldPadX
                verticalAlignment: TextInput.AlignVCenter
                text: page.draft
                selectByMouse: true
                clip: true
                color: Theme.color.textPrimary
                selectionColor: Theme.color.selectionBg
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
                onTextEdited: page.draft = text
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.spacing.fieldPadX
                visible: urlInput.text.length === 0
                text: "http://127.0.0.1:3080"
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
            }
        }

        Text {
            width: parent.width
            text: qsTrId("settings_server_address_hint")
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        IconButton {
            label: qsTrId("settings_save_and_restart")
            onClicked: Hub.settings.saveServerUrl(page.draft)
        }
    }
}
