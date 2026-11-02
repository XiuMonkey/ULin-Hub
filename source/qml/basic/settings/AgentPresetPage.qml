// 设置 · Agent预设（按原版 Settings.cpp 的 agentPanel 复刻）：
// 标签 settings_default_agent_preset + 预设按钮（显示当前默认名）+ 下拉列表 + 说明 settings_agent_preset_hint。

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

        // 页面标题：四页统一到 14 + 半粗，让"设置 / 各页标题 / 字段标签 / 说明"成为四级。
        // 原版这页没有标题，进来只有一行 14px 的字段标签，比导航项还轻，整页没有落点。
        Text {
            text: qsTrId("settings_agent_preset_tab")
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightBold
            bottomPadding: 2
        }

        Text {
            text: qsTrId("settings_default_agent_preset")
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            // 字段标签取 caption(12) + 次级色：它只是"说明下面这个框是什么"，
            // 取 14/13 会和输入框里的内容同级，读起来分不清谁是标签谁是值。
            font.pixelSize: Theme.font.caption
        }

        Item {
            width: Math.max(280, parent.width)
            // 弹层完全落在父 Item 的几何范围内，避免“视觉上展开、命中测试只到 34px”，
            // 下面几项看着能点但鼠标事件进不来。
            implicitHeight: page.popupOpen ? 34 + popup.implicitHeight + 4 : 34
            z: 100

            Rectangle {
                id: presetButton
                width: parent.width
                height: 34
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

            Rectangle {   // 下拉列表（原版是 QFrame + QListWidget，作为弹窗子控件避免直角背景）
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
                z: 100

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
            color: Theme.color.textSecondary   // 反馈文案：原版走强调色，设置页不出蓝 ⇒ 中性灰（与同页说明文字同档）
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
        }
    }
}
