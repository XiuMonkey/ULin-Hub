// 扩展管理（按原版 src/ui/ExtensionManagerPopup.cpp + styles/extension-manager.qss 复刻）
//
//   圆角卡片弹窗 + 标题 ext_manager_title + 右上关闭
//   工具栏：安装扩展（ext_install）/ 移除选中（ext_remove_selected，无选中时禁用）/ 刷新（common_refresh）
//           / 重启服务端（plugin_restart_server，工具扩展增删后生效用）
//   列表（panelBg + 1px border + radius 12，行 hover/selected 背景与旧 QSS 同款）：
//     两种来源 —— 工具扩展（profiles/web/extensions.json）、客户端扩展（<exe>/clientExtensions）。
//     未装载的客户端扩展按原版在行尾标「·未装载」。
//   移除要确认：AppPopup + ext_remove_confirm_title / ext_remove_confirm_fmt（确认按钮用 common_delete）
//   底部状态行：ext_empty_hint / ext_installed_count_fmt / ext_installing / ext_install_*_fmt …
//
// 与 C++ 的分工：本页只负责画与点；列表来源、安装解压、移除、清理残留全在
// ExtensionManagerController（Hub.extensions），文案也由它给（同一个 qtTrId id）。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    // 背景遮罩：吃掉点击，别让底下的壳还能点到
    Rectangle {
        anchors.fill: parent
        color: Theme.color.overlayBg
        MouseArea { anchors.fill: parent }
    }

    Card {
        id: popup
        anchors.centerIn: parent
        width: 760
        height: 540
        radius: Theme.radius.popup
        padding: 0
        shadowEnabled: true

        // ---- 标题行 ----
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
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("ext_manager_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.lead
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.buttonGap

                BusySpinner {
                    width: 16
                    height: 16
                    lineWidth: 2
                    running: Hub.extensions.busy
                    visible: Hub.extensions.busy
                    anchors.verticalCenter: parent.verticalCenter
                }

                IconButton {
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: Hub.extensions.closePage()
                }
            }
        }

        // ---- 工具栏（与原版三个按钮一一对应 + 重启服务端）----
        Row {
            id: toolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            spacing: 8

            IconButton {
                id: installButton
                label: qsTrId("ext_install")
                enabled: !Hub.extensions.busy
                onClicked: Hub.extensions.installFromFile()
            }

            IconButton {
                id: removeButton
                label: qsTrId("ext_remove_selected")
                enabled: Hub.extensions.canRemove && !Hub.extensions.busy
                onClicked: confirmPopup.open()
            }

            IconButton {
                id: refreshButton
                label: qsTrId("common_refresh")
                enabled: !Hub.extensions.busy
                onClicked: Hub.extensions.refresh()
            }

            IconButton {
                id: restartButton
                label: qsTrId("plugin_restart_server")
                enabled: !Hub.extensions.busy
                onClicked: Hub.extensions.restartServer()
            }
        }

        // ---- 列表 ----
        Rectangle {
            id: listFrame
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: toolbar.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            anchors.bottom: statusRow.top
            anchors.bottomMargin: 12
            color: Theme.color.panelBg
            border.width: 1
            border.color: Theme.color.border
            radius: 12

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                model: Hub.extensions.items
                spacing: 2

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index

                    width: list.width
                    height: 40
                    radius: 8
                    // 选中/悬停用旧 QSS 的 item:selected / item:hover 两个色
                    color: Hub.extensions.selectedIndex === row.index ? Theme.color.activeBg
                        : (rowHover.hovered ? Theme.color.hoverBg : "transparent")

                    HoverHandler { id: rowHover }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                        elide: Text.ElideRight
                        width: parent.width - kindBadge.width - stateDot.width - 46
                    }

                    // 种类徽章：工具扩展在原版里没有后缀，所以 kindLabel 为空时不画
                    Rectangle {
                        id: kindBadge
                        anchors.right: stateDot.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.modelData.kindLabel !== ""
                        width: visible ? kindText.width + 14 : 0
                        height: 20
                        radius: 6
                        color: Theme.color.tagBg

                        Text {
                            id: kindText
                            anchors.centerIn: parent
                            text: row.modelData.kindLabel
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }

                    // 装载状态点：绿=已装载/生效，灰=已安装未装载
                    Rectangle {
                        id: stateDot
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: row.modelData.loaded ? Theme.color.textPrimary : Theme.color.border
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: Hub.extensions.selectedIndex = row.index
                    }
                }
            }

            // 空列表提示（原版 ext_empty_hint）
            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: Hub.extensions.items.length === 0
                text: qsTrId("ext_empty_hint")
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        // ---- 底部：重启提示 + 状态行 ----
        Item {
            id: statusRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 16
            implicitHeight: 40

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                visible: Hub.extensions.restartHint !== ""
                text: Hub.extensions.restartHint
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                elide: Text.ElideRight
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                text: Hub.extensions.status
                elide: Text.ElideRight
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }
        }
    }

    // ---- 移除确认（原版 QMessageBox::question 的 QML 版）----
    AppPopup {
        id: confirmPopup
        title: qsTrId("ext_remove_confirm_title")
        bodyWidth: 420
        bodyHeight: 120
        property string pendingName: ""

        // 打开时把当前选中项的名字抓下来（关掉选中后还要显示它）
        onOpened: {
            const rows = Hub.extensions.items
            const i = Hub.extensions.selectedIndex
            pendingName = (i >= 0 && i < rows.length) ? rows[i].name : ""
        }

        Text {
            width: parent.width
            text: qsTrId("ext_remove_confirm_fmt").arg(confirmPopup.pendingName)
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
            wrapMode: Text.WordWrap
        }

        Row {
            spacing: Theme.spacing.buttonGap

            IconButton {
                label: qsTrId("common_cancel")
                onClicked: confirmPopup.close()
            }

            IconButton {
                label: qsTrId("common_delete")
                onClicked: {
                    Hub.extensions.removeSelected()
                    confirmPopup.close()
                }
            }
        }
    }
}
