// 设置（按原版 ui/Settings.cpp + ui/PopupWindow.cpp 复刻）
// 弹窗外壳：可见卡片 632×432（原版窗口 680×480 含 CardShadow level3 的阴影留白
// 24/16/24/32），radius 16，标题 #popupTitle 16px + 28×28 关闭按钮（tooltip common_close）。
// 主体：左导航（宽 120、每项最小高 36、可 check，选中/悬停 = hoverBg）+ 右内容（间距 12）。
// 导航顺序与原版一致：模型列表 / Agent预设 / Server设置 / 外观设置。
// 文案一律用原版既有 id（qsTrId），不写字面量。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    implicitWidth: 632
    implicitHeight: 432

    Item {   // 遮罩与圆角面都由 PageOverlay 提供（它还要按 t 动画），这里只留内容
        id: popup
        anchors.fill: parent

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

            // 导航列：整列铺一块浅面（#F7F8F9）并加圆角，与右侧纯白内容分栏。
            // 原版只有"几行灰字"，选中项又是一块和 hover 同色的灰，看不出"导航 / 内容"的分界，
            // 也分不清当前停在哪一页 —— 这里把选中项反过来做成"白底浮在面上"。
            Item {
                id: navColumn
                width: 120
                height: layout.height

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.card
                    color: Theme.color.windowBg
                    border.width: 1
                    border.color: Theme.color.border
                }

                Column {
                    id: nav
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 2

                    Repeater {
                        model: [   // 图标名 = assets/resources/icons/ui/<name>.svg（model / bot / server / theme）
                            { label: qsTrId("model_list_title"), icon: "model" },
                            { label: qsTrId("settings_agent_preset_tab"), icon: "bot" },
                            { label: qsTrId("settings_server_tab"), icon: "server" },
                            { label: qsTrId("settings_appearance_tab"), icon: "theme" }
                        ]

                        delegate: Item {
                            id: navItem

                            required property var modelData
                            required property int index

                            readonly property bool selected: Hub.settings.page === navItem.index

                            width: nav.width
                            implicitHeight: Math.max(34, navLabel.implicitHeight + 14)

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radius.small
                                color: navItem.selected
                                    ? Theme.color.panelBg
                                    : (navMouse.containsMouse ? Theme.color.activeBg : "transparent")
                                border.width: navItem.selected ? 1 : 0
                                border.color: Theme.color.border
                            }

                            Row {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 9
                                anchors.rightMargin: 9
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                LineIcon {
                                    name: navItem.modelData.icon
                                    size: 14
                                    color: navItem.selected ? Theme.color.textPrimary : Theme.color.textSecondary
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    id: navLabel
                                    width: parent.width - 22   // 14 图标 + 8 间距
                                    text: navItem.modelData.label
                                    elide: Text.ElideRight
                                    color: navItem.selected ? Theme.color.textPrimary : Theme.color.textSecondary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.body
                                    font.weight: navItem.selected ? Theme.font.weightBold : Font.Normal
                                }
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
            }

            Item {   // 右内容区：四页叠放，按 page 显隐（原版也是四块一起建、切换只改可见性）
                id: content
                width: layout.width - navColumn.width - layout.spacing
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
