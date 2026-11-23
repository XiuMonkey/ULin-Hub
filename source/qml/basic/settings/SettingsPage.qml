// 设置页：按原版 ui/Settings.cpp + ui/PopupWindow.cpp 复刻，卡片 632×432、radius 16，
// 标题 15px 半粗 + 28×28 关闭按钮（tooltip common_close）。
// 左导航宽 120（每项最小高 34，选中 = panelBg、悬停 = activeBg），右内容间距 12。
// 导航顺序：模型列表 / Agent预设 / Server设置 / 外观设置；文案用原版 qsTrId。
// ulincore 模式换一套 6 项导航，标签为字面量。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    Component.onCompleted: HostUi.registerItem("settingsPage", page)   // 登记为扩展注入点：扩展可按名把 UI 挂进这一页

    // Master 后端切到自研内核时，第一页（原“模型列表”）自动换成 ULin Core 设置。
    readonly property bool ulincoreMode: Hub.settings.ulincoreMode

    implicitWidth: 632
    implicitHeight: 432

    Item {   // 遮罩与圆角面由 PageOverlay 绘制（含 t 动画），子项只放内容
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
                font.family: Theme.font.uiFamily
                // 弹窗标题不取 lead(16)：16px 常规字重在 632 宽的卡片里像"放大的正文"，
                // 与下面 14px 的页面标题只差 2px，两级标题会挤在一起；收敛到 15 + 半粗，靠字重立层级。
                font.pixelSize: 15
                font.weight: Theme.font.weightBold
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

            // 导航列整列铺 windowBg 圆角面与右侧分栏；选中项做成白底浮在面上，
            // 避免原版"几行灰字 + 选中与悬停同色"分不清当前页。
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
                        // ulincore 模式下换一套导航：模型列表 / 运行时 / 数据 / Agent / 外观 / 服务
                        model: page.ulincoreMode
                            ? [
                                { label: "模型列表", icon: "model" },
                                { label: "运行时", icon: "theme" },
                                { label: "数据", icon: "server" },
                                { label: "Agent", icon: "bot" },
                                { label: "外观", icon: "theme" },
                                { label: "服务", icon: "server" }
                              ]
                            : [
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
                                    font.family: Theme.font.uiFamily
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

            Item {   // 右内容区：通用 4 页；ulincore 模式下换成 6 页（含 Agent / 外观 / 服务）
                id: content
                width: layout.width - navColumn.width - layout.spacing
                height: layout.height

                // 每个 Loader 对应一页；source 按 ulincoreMode 二选一，4/5 页只在 ulincore 模式激活。
                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 0
                    source: page.ulincoreMode
                        ? Style.url("basic/settings/ulincore/ModelsPage.qml")
                        : Style.url("basic/settings/dsh/ModelListPage.qml")
                }

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 1
                    source: page.ulincoreMode
                        ? Style.url("basic/settings/ulincore/RuntimePage.qml")
                        : Style.url("basic/settings/dsh/AgentPresetPage.qml")
                }

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 2
                    source: page.ulincoreMode
                        ? Style.url("basic/settings/ulincore/DataPage.qml")
                        : Style.url("basic/settings/dsh/ServerPage.qml")
                }

                Loader {
                    anchors.fill: parent
                    visible: Hub.settings.page === 3
                    source: page.ulincoreMode
                        ? Style.url("basic/settings/dsh/AgentPresetPage.qml")
                        : Style.url("basic/settings/AppearancePage.qml")
                }

                Loader {
                    anchors.fill: parent
                    active: page.ulincoreMode && Hub.settings.page === 4
                    visible: page.ulincoreMode && Hub.settings.page === 4
                    source: Style.url("basic/settings/AppearancePage.qml")
                }

                Loader {
                    anchors.fill: parent
                    active: page.ulincoreMode && Hub.settings.page === 5
                    visible: page.ulincoreMode && Hub.settings.page === 5
                    source: Style.url("basic/settings/dsh/ServerPage.qml")
                }
            }
        }
    }
}
