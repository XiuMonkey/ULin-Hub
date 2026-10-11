// 设置（按原版 ui/Settings.cpp + ui/PopupWindow.cpp 复刻）
//
//   弹窗外壳：可见卡片 632×432（原版窗口 680×480 含 CardShadow level3 的阴影留白
//             24/16/24/32），radius 16，标题 #popupTitle 16px + 28×28 关闭按钮（tooltip common_close）。
//   主体：左导航（宽 120、每项最小高 36、可 check，选中/悬停 = hoverBg）+ 右内容（间距 12）。
//   导航顺序与原版一致：模型列表 / Agent预设 / Server设置 / 外观设置。
//
// 文案一律用原版既有 id（qsTrId），不写字面量。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    // 遮罩（原版 #windowScrim = overlayBg）
    Rectangle {
        anchors.fill: parent
        color: Theme.color.overlayBg
        MouseArea { anchors.fill: parent }
    }

    Card {
        id: popup

        anchors.centerIn: parent
        width: 632
        height: 432
        radius: Theme.radius.popup
        padding: 0
        shadowEnabled: true

        // ---- 标题行（原版 #popupBody 的标题行：spacing 8、关闭按钮 28×28）----
        Item {
            id: titleRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 16
            implicitHeight: 32

            Text {
                id: popupTitle
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("settings_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.lead
            }

            IconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                boxSize: 28
                glyph: "✕"
                tooltip: qsTrId("common_close")
                onClicked: Hub.settings.closePage()
            }
        }

        // ---- 主体：左导航 + 右内容（原版主布局边距 20,16,20,20、spacing 12）----
        Row {
            id: layout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            anchors.bottomMargin: 20
            spacing: 12

            Column {
                id: nav
                width: 120
                spacing: 4

                Repeater {
                    model: [
                        qsTrId("model_list_title"),
                        qsTrId("settings_agent_preset_tab"),
                        qsTrId("settings_server_tab"),
                        qsTrId("settings_appearance_tab")
                    ]

                    delegate: Item {
                        id: navItem

                        required property string modelData
                        required property int index

                        width: nav.width
                        implicitHeight: Math.max(36, navLabel.implicitHeight + 16)

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius.control
                            color: (Hub.settings.page === navItem.index || navMouse.containsMouse)
                                ? Theme.color.hoverBg
                                : "transparent"
                        }

                        Text {
                            id: navLabel
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: navItem.modelData
                            wrapMode: Text.WordWrap
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.title
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Hub.settings.setPage(navItem.index)
                        }
                    }
                }
            }

            // 右内容区：四页叠放，按 page 显隐（原版也是四块一起建、切换只改可见性）
            Item {
                id: content
                width: layout.width - nav.width - layout.spacing
                height: layout.height

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 0
                    source: Style.url("basic/settings/ModelListPage.qml")
                }

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 1
                    source: Style.url("basic/settings/AgentPresetPage.qml")
                }

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 2
                    source: Style.url("basic/settings/ServerPage.qml")
                }

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 3
                    source: Style.url("basic/settings/AppearancePage.qml")
                }
            }
        }
    }
}
