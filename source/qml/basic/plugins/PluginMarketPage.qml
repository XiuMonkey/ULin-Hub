// 插件市场（按原版 ui/PluginsManager.cpp + styles/plugin-market.qss 复刻）
//
//   820×600 弹窗：#popupBody 式圆角卡片 + 标题 plugin_market_title + 右上关闭
//   工具栏：搜索框（plugin_search_placeholder）+ 刷新（common_refresh）+ 重启服务（plugin_restart_server）
//   两个 Tab：市场（plugin_market_title）/ 已安装（plugin_installed）
//   市场页：卡片（名称 14/600 + 分类徽章 tagBg + 描述 12px + 安装按钮 primary 样式）+ 分页（每页 20）
//   已安装页：卡片（名称 + 版本，更新 hoverBg / 卸载 danger）
//   底部状态行（三种加载态 + 操作反馈，全部用既有 id）

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    Rectangle {
        anchors.fill: parent
        color: Theme.color.overlayBg
        MouseArea { anchors.fill: parent }
    }

    Card {
        id: popup
        anchors.centerIn: parent
        width: 820
        height: 600
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
                text: qsTrId("plugin_market_title")
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
                    running: Hub.plugins.busy
                    visible: Hub.plugins.busy
                    anchors.verticalCenter: parent.verticalCenter
                }

                IconButton {
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: Hub.plugins.closePage()
                }
            }
        }

        // ---- 工具栏 ----
        Row {
            id: toolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            spacing: 8

            SearchField {
                id: search
                width: parent.width - refreshButton.width - restartButton.width - 2 * parent.spacing
                placeholderText: qsTrId("plugin_search_placeholder")
                onTextChanged: Hub.plugins.setSearch(text)
            }

            IconButton {
                id: refreshButton
                label: qsTrId("common_refresh")
                onClicked: Hub.plugins.refresh()
            }

            IconButton {
                id: restartButton
                label: qsTrId("plugin_restart_server")
                onClicked: Hub.plugins.restartServer()
            }
        }

        // ---- Tab ----
        Row {
            id: tabs
            anchors.left: parent.left
            anchors.top: toolbar.bottom
            anchors.leftMargin: 20
            anchors.topMargin: 12
            spacing: 8

            Repeater {
                model: [qsTrId("plugin_market_title"), qsTrId("plugin_installed")]

                delegate: Rectangle {
                    id: tabItem

                    required property string modelData
                    required property int index

                    implicitWidth: tabLabel.implicitWidth + 36
                    implicitHeight: 32
                    radius: 10
                    color: Hub.plugins.tab === index
                        ? Theme.color.activeBg
                        : (tabMouse.containsMouse ? Theme.color.hoverBg : "transparent")
                    border.width: 1
                    border.color: Theme.color.border

                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: tabItem.modelData
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.title
                        font.weight: Hub.plugins.tab === tabItem.index ? Theme.font.weightBold : Font.Normal
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Hub.plugins.setTab(tabItem.index)
                    }
                }
            }
        }

        // ---- 列表区 ----
        Flickable {
            id: listArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tabs.bottom
            anchors.bottom: installBox.top
            anchors.margins: 20
            anchors.topMargin: 12
            contentHeight: contentColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: contentColumn
                width: listArea.width
                spacing: 8

                // ===== 市场页 =====
                Repeater {
                    model: Hub.plugins.tab === 0 ? Hub.plugins.marketItems : []

                    delegate: Rectangle {
                        id: card
                        required property var modelData

                        width: contentColumn.width
                        implicitHeight: cardBody.implicitHeight + 24
                        radius: Theme.radius.card
                        color: Theme.color.panelBg
                        border.width: 1
                        border.color: cardMouse.containsMouse ? Theme.color.accent : Theme.color.border

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }

                        Column {
                            id: cardBody
                            anchors.left: parent.left
                            // 原版 createMarketCard：QHBoxLayout(contentsMargins 14,12,14,12; spacing 12)，
                            // 左侧 infoLayout 是 stretch=1、中间 12px 间隔、右侧安装按钮 stretch=0。
                            // 所以文字栏的右边必须停在按钮左侧，否则描述文字会跑到按钮底下（重合）。
                            anchors.right: installButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 14
                            anchors.rightMargin: 12
                            spacing: 4

                            Row {
                                width: parent.width
                                spacing: 8

                                Text {
                                    id: nameText
                                    text: card.modelData.name
                                    color: Theme.color.textPrimary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.title
                                    font.weight: Theme.font.weightBold
                                }

                                Rectangle {
                                    visible: card.modelData.category.length > 0
                                    implicitWidth: categoryText.implicitWidth + 16
                                    implicitHeight: categoryText.implicitHeight + 4
                                    radius: 6
                                    color: Theme.color.tagBg
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        id: categoryText
                                        anchors.centerIn: parent
                                        text: card.modelData.category
                                        color: Theme.color.accent
                                        font.family: "Microsoft YaHei UI"
                                        font.pixelSize: Theme.font.small
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                visible: card.modelData.description.length > 0
                                text: card.modelData.description
                                wrapMode: Text.WordWrap
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }
                        }

                        IconButton {
                            id: installButton
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            label: qsTrId("plugin_install_action")
                            onClicked: Hub.plugins.install(card.modelData.url)
                        }
                    }
                }

                // ===== 已安装页 =====
                Repeater {
                    model: Hub.plugins.tab === 1 ? Hub.plugins.installedItems : []

                    delegate: Rectangle {
                        id: installedCard
                        required property var modelData

                        width: contentColumn.width
                        implicitHeight: 56
                        radius: Theme.radius.card
                        color: Theme.color.panelBg
                        border.width: 1
                        border.color: Theme.color.border

                        Text {
                            anchors.left: parent.left
                            // 原版 createInstalledCard：名字 label 是 stretch=1，右侧两个按钮 stretch=0。
                            // 所以名字要给按钮让位并省略号收尾，不能盖到按钮上。
                            anchors.right: buttonRow.left
                            anchors.leftMargin: 14
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: installedCard.modelData.version.length > 0
                                ? installedCard.modelData.name + "  " + installedCard.modelData.version
                                : installedCard.modelData.name
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.title
                        }

                        Row {
                            id: buttonRow
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12

                            IconButton {
                                label: qsTrId("plugin_update_action")
                                onClicked: Hub.plugins.update(installedCard.modelData.name)
                            }

                            IconButton {
                                label: qsTrId("plugin_uninstall_action")
                                // 色板里没有 dangerText 这个键（叫 danger）——写错的键名只会打 warning、
                                // 颜色静默取默认值，所以这里按色板实际键名取。
                                glyphColor: Theme.color.danger
                                glyphColorHover: Theme.color.danger
                                onClicked: Hub.plugins.uninstall(installedCard.modelData.name)
                            }
                        }
                    }
                }
            }
        }

        // ---- 市场包自动安装（M6）：/dsh-market/* 回 404 时自动 pnpm add dshmarket ----
        // 原版是 4px 进度条 + 状态行；这里同尺寸，安装中用往返动画表示"进行中"。
        Column {
            id: installBox

            anchors.left: parent.left
            anchors.right: parent.right
            // 原来是 anchors.bottom: installBox.top —— 锚到自己，Qt 会丢弃这条锚点，
            // installBox 于是塌到 y=0，连带 listArea.bottom(=installBox.top) 也变成 0，
            // 列表区高度归零 ⇒ 市场数据拉到了也一条都显示不出来。
            // 它应该贴在底部状态行（statusRow）之上，列表区随之正好让位。
            anchors.bottom: statusRow.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 8
            spacing: 6

            readonly property bool installVisible: Hub.plugins.installing || Hub.plugins.installStatus.length > 0
            visible: installVisible
            height: installVisible ? implicitHeight : 0

            Rectangle {
                width: parent.width
                height: 4
                radius: 2
                color: Theme.color.inputBg
                visible: Hub.plugins.installing

                Rectangle {
                    id: installBar
                    width: Math.max(24, parent.width * 0.3)
                    height: parent.height
                    radius: 2
                    color: Theme.color.accent

                    SequentialAnimation on x {
                        running: Hub.plugins.installing
                        loops: Animation.Infinite
                        NumberAnimation { from: 0; to: installBar.parent.width - installBar.width; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: installBar.parent.width - installBar.width; to: 0; duration: 900; easing.type: Easing.InOutSine }
                    }
                }
            }

            Text {
                width: parent.width
                text: Hub.plugins.installStatus
                elide: Text.ElideRight
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Text {
                width: parent.width
                visible: Hub.plugins.installLog.length > 0
                text: Hub.plugins.installLog.slice(Math.max(0, Hub.plugins.installLog.length - 3)).join("\n")
                color: Theme.color.textCaption
                font.family: "Consolas"
                font.pixelSize: Theme.font.small
                elide: Text.ElideRight
                maximumLineCount: 3
                wrapMode: Text.NoWrap
            }
        }

        // ---- 分页 + 状态行 ----
        Item {
            id: statusRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 16
            implicitHeight: 40

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                visible: Hub.plugins.tab === 0

                IconButton {
                    label: qsTrId("common_prev_page")
                    enabled: Hub.plugins.canPrev
                    onClicked: Hub.plugins.prevPage()
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Hub.plugins.pageLabel
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                IconButton {
                    label: qsTrId("common_next_page")
                    enabled: Hub.plugins.canNext
                    onClicked: Hub.plugins.nextPage()
                }
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 0
                horizontalAlignment: Text.AlignRight
                text: Hub.plugins.status
                elide: Text.ElideRight
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }
        }
    }
}
