// 扩展管理（按原版 src/ui/ExtensionManagerPopup.cpp + styles/extension-manager.qss 复刻）
// 圆角卡片弹窗 + 标题 ext_manager_title + 右上关闭
// 工具栏：安装扩展（ext_install）/ 移除选中（ext_remove_selected，无选中时禁用）/ 刷新（common_refresh）
// / 重启服务端（plugin_restart_server，工具扩展增删后生效用）
// 列表（panelBg + 1px border + radius 12，行 hover/selected 背景与旧 QSS 同款）：
// 两种来源 —— 工具扩展（profiles/web/extensions.json）、客户端扩展（<exe>/clientExtensions）。
// 未装载的客户端扩展按原版在行尾标「·未装载」。
// 移除要确认：AppPopup + ext_remove_confirm_title / ext_remove_confirm_fmt（确认按钮用 common_delete）
// 底部状态行：ext_empty_hint / ext_installed_count_fmt / ext_installing / ext_install_*_fmt …
// 与 C++ 的分工：本页只负责画与点；列表来源、安装解压、移除、清理残留全在
// ExtensionManagerController（Hub.extensions），文案也由它给（同一个 qtTrId id）。
// 分不出"这是工具扩展还是客户端扩展"，也看不出选中了哪一行。改版照设置页的做法：
// ① 标题带图标 + 已安装数量徽章（中性 pill）；
// ② 主动作「安装扩展」提到工具行最左、用 primary 实心面 —— 一页只该有一个实心按钮；
// 其余（移除/刷新/重启）退成中性浮起面或图标，视觉主次清楚；
// ③ 列表仍是卡片，但行里补了 26×26 图标底（工具/客户端两种图标）+ 种类徽章 + 装载状态，
// 选中行 = 浅面 + 描边 + 左侧 2px 竖条（与设置页展开的成员卡同一个选中标记）；
// ④ 空态给图标 + 一句，别只留一行干字。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    implicitWidth: 760
    implicitHeight: 540

    property bool overlayAnimating: false   // 由 PageOverlay 绑过来：卡片几何正在动（展开/收回）。页里那些"一直在跑"的东西看它按停

    // 列表里的"工具扩展"在 C++ 侧 kindLabel 为空串（原版就没有种类后缀）⇒ 这里用它区分两种来源，
    // 图标 / 徽章 / 分组说明都靠它，不需要 C++ 再补字段。
    function isClientKind(kindLabel) {
        return kindLabel !== undefined && kindLabel !== null && kindLabel.length > 0
    }

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

            LineIcon {
                id: titleIcon
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                name: "extensions"
                size: 16
                color: Theme.color.textSecondary
            }

            Text {
                id: titleLabel
                anchors.left: titleIcon.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("ext_manager_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.lead
            }

            Rectangle {   // 已安装数量：中性 pill（与工具过滤页标题旁那颗同一规格）
                anchors.left: titleLabel.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: countLabel.implicitWidth + 14
                implicitHeight: 19
                radius: height / 2
                color: Theme.color.buttonBg
                border.width: 1
                border.color: Theme.color.border

                Text {
                    id: countLabel
                    anchors.centerIn: parent
                    text: Hub.extensions.items.length
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.small
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.buttonGap

                // 这里原有一颗 BusySpinner（running: Hub.extensions.busy），但 Hub.extensions.busy
                // 从来没有被置真过 ⇒ 它永远不会显示，是个死控件（用户："扩展管理页哪有转圈？"）。
                // 需要"加载中"提示时再加静态文字（理由见 PluginMarketPage：转圈会拖整窗重绘）。
                IconButton {
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: Hub.extensions.closePage()
                }
            }
        }

        Row {
            id: toolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            height: 30   // ⚠️ Row 的子项不用 verticalCenter 锚（Qt 会拒绝）：固定行高 30 + 自身 30 高即可对齐
            spacing: 8

            ActionButton {
                id: installButton
                width: 96
                height: 30
                text: qsTrId("ext_install")
                variant: "primary"
                enabled: !Hub.extensions.busy
                onClicked: Hub.extensions.installFromFile()
            }

            ActionButton {
                id: removeButton
                width: 88
                height: 30
                text: qsTrId("ext_remove_selected")
                variant: "elevated"
                enabled: Hub.extensions.canRemove && !Hub.extensions.busy
                onClicked: confirmPopup.open()
            }

            Item { width: 4; height: 1 }   // 主/次动作之间的呼吸（不是分隔符，只是一个空隙）

            ActionButton {
                id: refreshButton
                width: 72
                height: 30
                text: qsTrId("common_refresh")
                variant: "ghost"
                enabled: !Hub.extensions.busy
                onClicked: Hub.extensions.refresh()
            }

            ActionButton {
                id: restartButton
                width: 96
                height: 30
                text: qsTrId("plugin_restart_server")
                variant: "ghost"
                enabled: !Hub.extensions.busy
                onClicked: Hub.extensions.restartServer()
            }
        }

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
            border.color: Theme.color.menuBorder
            radius: Theme.radius.card

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: Hub.extensions.items
                spacing: 2
                ScrollBar.vertical: AppScrollBar { }
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index

                    readonly property bool selected: Hub.extensions.selectedIndex === row.index
                    readonly property bool clientKind: page.isClientKind(row.modelData.kindLabel)

                    width: list.width
                    height: 46
                    radius: Theme.radius.inner
                    color: row.selected
                        ? Theme.color.hoverBg
                        : (rowHover.hovered ? Theme.color.hoverBg : "transparent")
                    border.width: row.selected ? 1 : 0
                    border.color: Theme.color.inputBorder

                    HoverHandler { id: rowHover }

                    Rectangle {   // 选中标记：左侧 2px 竖条（设置页里"已展开的成员卡"用的是同一个记号）
                        visible: row.selected
                        x: 1
                        y: 9
                        width: 2
                        height: parent.height - 18
                        radius: 1
                        color: Theme.color.textPrimary
                    }

                    Rectangle {   // 图标底：26×26 浅面方块，工具扩展 / 客户端扩展两种图标（kindLabel 空 = 工具）
                        id: iconTile
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 26
                        radius: Theme.radius.control
                        color: Theme.color.buttonBg

                        LineIcon {
                            anchors.centerIn: parent
                            name: row.clientKind ? "plugin" : "extensions"
                            size: 14
                            color: row.selected ? Theme.color.textPrimary : Theme.color.textSecondary
                        }
                    }

                    Text {
                        id: rowLabel
                        anchors.left: iconTile.right
                        anchors.leftMargin: 10
                        anchors.right: kindBadge.visible ? kindBadge.left : stateHint.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                        font.weight: row.selected ? Theme.font.weightMedium : Font.Normal
                        elide: Text.ElideRight
                    }

                    Rectangle {   // 种类徽章：工具扩展在原版里没有后缀，所以 kindLabel 为空时不画
                        id: kindBadge
                        anchors.right: stateHint.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.modelData.kindLabel !== ""
                        implicitWidth: kindText.implicitWidth + 14
                        implicitHeight: 19
                        radius: height / 2
                        color: Theme.color.buttonBg

                        Text {
                            id: kindText
                            anchors.centerIn: parent
                            text: row.modelData.kindLabel
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }

                    // 装载状态：点 + 文案（点只保留"已装载/未装载"这一个含义，文字把它说清）
                    // ⚠️ Row 里不写 verticalCenter 锚（Qt 会打 "Cannot specify ... anchors for items
                    // inside Row"），两者都用 y 现算 —— Row 只管 x。
                    Row {
                        id: stateHint
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Text {
                            id: stateText
                            y: Math.round((parent.height - height) / 2)
                            visible: !row.modelData.loaded
                            text: qsTrId("ext_client_not_loaded")
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }

                        Rectangle {
                            y: Math.round((parent.height - height) / 2)
                            width: 7
                            height: 7
                            radius: 3.5

                            // 已装载 = 近黑实心，未装载 = textCaption（最淡的文字档）。
                            color: row.modelData.loaded ? Theme.color.textPrimary : Theme.color.textCaption
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: false
                        onClicked: Hub.extensions.selectedIndex = row.index
                    }
                }
            }

            Column {   // 空列表提示（原版 ext_empty_hint）
                anchors.centerIn: parent
                width: parent.width - 80
                spacing: 10
                visible: Hub.extensions.items.length === 0

                LineIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "extensions"
                    size: 34
                    color: Theme.color.textCaption
                    opacity: 0.45
                }

                Text {
                    width: parent.width
                    text: qsTrId("ext_empty_hint")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }

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

    AppPopup {
        id: confirmPopup
        title: qsTrId("ext_remove_confirm_title")
        bodyWidth: 420
        bodyHeight: 120
        property string pendingName: ""

        onOpened: {   // 打开时把当前选中项的名字抓下来（关掉选中后还要显示它）
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
                glyphColor: Theme.color.danger
                glyphColorHover: Theme.color.danger
                onClicked: {
                    Hub.extensions.removeSelected()
                    confirmPopup.close()
                }
            }
        }
    }
}
