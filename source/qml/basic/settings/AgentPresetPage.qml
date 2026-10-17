// 设置 · Agent预设（按原版 Settings.cpp 的 agentPanel 复刻）：
//   标签 settings_default_agent_preset + 预设按钮（显示当前默认名）+ 下拉列表 + 说明 settings_agent_preset_hint。
// 「设为默认」= 一次服务端写入（settings/update，"agent-presets" + {default: id}）；界面乐观更新，失败回退。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    property bool popupOpen: false

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 6

        Text {
            text: qsTrId("settings_default_agent_preset")
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
        }

        Item {
            width: Math.max(280, parent.width)
            implicitHeight: 34

            Rectangle {
                id: presetButton
                width: parent.width
                height: parent.height
                radius: Theme.radius.control
                color: presetMouse.containsMouse ? Theme.color.hoverBg : Theme.color.panelBg
                border.width: 1
                // 这是"下拉触发按钮"：交互控件走官方 DSH 的 `--dsw-alias-border-l2`(inputBorder, 10% 黑)，
                // 和语言下拉同档；卡片/面板那类被动面才用 4% 的 border。
                border.color: Theme.color.inputBorder

                Text {
                    anchors.left: parent.left
                    anchors.right: arrow.left
                    anchors.leftMargin: Theme.spacing.fieldPadX
                    anchors.verticalCenter: parent.verticalCenter
                    text: Hub.settings.defaultPresetName.length > 0
                        ? Hub.settings.defaultPresetName
                        : qsTrId("common_loading")
                    elide: Text.ElideRight
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body
                }

                Text {
                    id: arrow
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing.fieldPadX
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: Theme.color.textCaption
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: presetMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.popupOpen = !page.popupOpen
                }
            }

            // 下拉列表（原版是 QFrame + QListWidget，作为弹窗子控件避免直角背景）
            Rectangle {
                id: popup
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: presetButton.bottom
                anchors.topMargin: 4
                visible: page.popupOpen
                implicitHeight: Math.min(320, list.implicitHeight + 12)
                radius: Theme.radius.control
                color: Theme.color.menuBg
                border.width: 1
                border.color: Theme.color.menuBorder
                clip: true
                z: 10

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 6
                    model: Hub.settings.agentPresets
                    implicitHeight: contentHeight
                    spacing: 0
                    clip: true

                    delegate: Rectangle {
                        required property var modelData
                        width: list.width
                        height: 38
                        radius: Theme.radius.small
                        color: itemMouse.containsMouse ? Theme.color.activeBg : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacing.labelGap
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            elide: Text.ElideRight
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                        }

                        MouseArea {
                            id: itemMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                page.popupOpen = false
                                Hub.settings.selectDefaultPreset(modelData.id)
                            }
                        }
                    }
                }
            }
        }

        Text {
            width: parent.width
            text: qsTrId("settings_agent_preset_hint")
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }

        Text {
            width: parent.width
            visible: Hub.settings.presetFeedback.length > 0
            text: Hub.settings.presetFeedback
            wrapMode: Text.WordWrap
            // 反馈文案：原版走强调色，设置页不出蓝 ⇒ 中性灰（与同页说明文字同档）
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }
    }
}
