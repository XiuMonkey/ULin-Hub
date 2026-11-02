// 插件市场（按原版 ui/PluginsManager.cpp + styles/plugin-market.qss 复刻）
// 820×600 弹窗：#popupBody 式圆角卡片 + 标题 plugin_market_title + 右上关闭
// 工具栏：搜索框（plugin_search_placeholder）+ 刷新（common_refresh）+ 重启服务（plugin_restart_server）
// 两个 Tab：市场（plugin_market_title）/ 已安装（plugin_installed）
// 市场页：卡片（名称 14/600 + 分类徽章 tagBg + 描述 12px + 安装按钮 primary 样式）+ 分页（每页 20）
// 已安装页：卡片（名称 + 版本，更新 hoverBg / 卸载 danger）
// 底部状态行（三种加载态 + 操作反馈，全部用既有 id）
// 列表用 ListView（惰性建委托），不是 Flickable+Column+Repeater —— 理由见"列表区"那段注释。
// 整页像一张表格。改版做了四件事（都不改数据与交互语义）：
// ② 搜索框补一颗放大镜（SearchField 新增 leadingIcon，其它调用方默认不受影响）；
// ③ 市场卡片：左侧 32×32 图标底 + 名称/分类/描述 + 右侧**实心**安装键（一页一个的主动作），
// 悬停只把描边提到 inputBorder（不再整圈变蓝）；
// 没有颜色语义的普通按钮）。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    implicitWidth: 820
    implicitHeight: 600

    property double nowMs: Date.now()

    function taskActionLabel(action) {
        if (action === "install") return qsTrId("plugin_install_action")
        if (action === "update") return qsTrId("plugin_update_action")
        if (action === "uninstall") return qsTrId("plugin_uninstall_action")
        if (action === "market") return "插件市场"
        if (action === "approve") return "允许构建"
        if (action === "restart") return "重启服务端"
        return action
    }

    function taskStateLabel(task) {
        if (task.state === "running") return "进行中"
        if (task.state === "blocked") return "等待批准"
        if (task.state === "ok") return "已完成"
        if (task.state === "failed") return "失败"
        return task.state
    }

    function taskElapsed(task) {
        if (task.state !== "running")
            return ""
        return Math.max(0, Math.floor((nowMs - task.startedAt) / 1000)) + "s"
    }

    Timer {
        interval: 1000
        repeat: true
        running: Hub.plugins.activeTaskCount > 0
        onTriggered: page.nowMs = Date.now()
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
                name: "plugin"
                size: 16
                color: Theme.color.textSecondary
            }

            Text {
                anchors.left: titleIcon.right
                anchors.leftMargin: 8
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

                // 理由：转圈是常驻无限动画 ⇒ 每帧整窗重绘 ⇒ 加载期间整窗 ~30ms/帧；按时间门控
                // 又必然要么盖住建卡（变卡）要么延迟（450ms）。静态文字立刻可见且永不重绘。
                // 静态文字两项都满足：**立刻可见**（无延迟）、**永不重绘**（无掉帧）。
                // ⚠️ Row 子项不写 verticalCenter 锚（Qt 会拒绝），按行高现算 y。
                Text {
                    y: Math.round((parent.height - height) / 2)
                    visible: Hub.plugins.busy
                    text: (Hub.plugins.statusText && Hub.plugins.statusText.length > 0) ? Hub.plugins.statusText : "加载中…"
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.small
                }
                IconButton {
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: Hub.plugins.closePage()
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
            height: 32
            spacing: 8

            SearchField {
                id: search
                y: 1
                height: 30
                leadingIcon: "search"
                width: parent.width - refreshButton.width - restartButton.width - 2 * parent.spacing
                placeholderText: qsTrId("plugin_search_placeholder")
                onTextChanged: Hub.plugins.setSearch(text)
            }

            ActionButton {
                id: refreshButton
                width: 72
                height: 30
                y: 1
                text: qsTrId("common_refresh")
                variant: "ghost"
                onClicked: Hub.plugins.refresh()
            }

            ActionButton {
                id: restartButton
                width: 96
                height: 30
                y: 1
                text: qsTrId("plugin_restart_server")
                variant: "ghost"
                onClicked: Hub.plugins.restartServer()
            }
        }

        SegmentedTabs {
            id: tabs
            anchors.left: parent.left
            anchors.top: toolbar.bottom
            anchors.leftMargin: 20
            anchors.topMargin: 12
            tabHeight: 26
            currentKey: Hub.plugins.tab === 0 ? "market" : "installed"
            model: [
                { key: "market", label: qsTrId("plugin_market_title") },
                { key: "installed", label: qsTrId("plugin_installed")   // "已安装"带上数量：两页之间切来切去时，这个数字是最有用的一条信息
                    + (Hub.plugins.installedItems.length > 0 ? " " + Hub.plugins.installedItems.length : "") }
            ]
            onSelected: (key) => Hub.plugins.setTab(key === "market" ? 0 : 1)
        }

        Item {
            id: listArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tabs.bottom
            anchors.bottom: installBox.top
            anchors.margins: 20
            anchors.topMargin: 12

            ListView {
                id: marketList
                anchors.fill: parent
                visible: Hub.plugins.tab === 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }
                spacing: 8
                cacheBuffer: 160   // 只比一屏多留一张卡的余量：再大等于把"以后才要建的"提前到现在建
                model: Hub.plugins.tab === 0 ? Hub.plugins.marketItems : []

                delegate: Rectangle {
                    id: card
                    required property var modelData

                    width: ListView.view.width
                    implicitHeight: Math.max(64, cardBody.implicitHeight + 24)
                    radius: Theme.radius.card
                    color: cardMouse.containsMouse ? Theme.color.hoverBg : Theme.color.panelBg
                    border.width: 1
                    border.color: cardMouse.containsMouse ? Theme.color.inputBorder : Theme.color.menuBorder

                    Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }

                    Rectangle {   // 图标底：32×32 浅面方块（插件二字没有可用的分类图标，统一用 plugin 线性图）
                        id: iconTile
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        radius: Theme.radius.control
                        color: Theme.color.buttonBg

                        LineIcon {
                            anchors.centerIn: parent
                            name: "plugin"
                            size: 16
                            color: cardMouse.containsMouse ? Theme.color.textPrimary : Theme.color.textSecondary
                        }
                    }

                    Column {
                        id: cardBody
                        anchors.left: iconTile.right

                        // 原版 createMarketCard：QHBoxLayout(contentsMargins 14,12,14,12; spacing 12)，
                        // 左侧 infoLayout 是 stretch=1、中间 12px 间隔、右侧安装按钮 stretch=0。
                        // 所以文字栏的右边必须停在按钮左侧，否则描述文字会跑到按钮底下（重合）。
                        anchors.right: installButton.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 14
                        spacing: 3

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                id: nameText
                                width: Math.min(implicitWidth,   // 名称让位给分类徽章后自己省略（Row 不会"吃掉剩余宽度"，必须显式算）
                                                parent.width - (categoryBadge.visible
                                                    ? categoryBadge.implicitWidth + parent.spacing : 0))
                                text: card.modelData.name
                                elide: Text.ElideRight
                                color: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.title
                                font.weight: Theme.font.weightBold
                            }

                            Rectangle {
                                id: categoryBadge
                                visible: card.modelData.category.length > 0
                                implicitWidth: categoryText.implicitWidth + 14
                                implicitHeight: 18
                                radius: height / 2
                                color: Theme.color.buttonBg
                                y: Math.round((parent.height - height) / 2)   // Row 里不写 verticalCenter 锚（会被 Qt 拒绝），按行高现算 y

                                Text {
                                    id: categoryText
                                    anchors.centerIn: parent
                                    text: card.modelData.category
                                    color: Theme.color.textSecondary
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
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            lineHeight: 1.25
                        }
                    }

                    ActionButton {
                        id: installButton
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 64
                        height: 28
                        text: qsTrId("plugin_install_action")
                        variant: "primary"
                        onClicked: Hub.plugins.install(card.modelData.url)
                    }
                }
            }

            ListView {
                id: installedList
                anchors.fill: parent
                visible: Hub.plugins.tab === 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }
                spacing: 8
                cacheBuffer: 160
                model: Hub.plugins.tab === 1 ? Hub.plugins.installedItems : []

                delegate: Rectangle {
                    id: installedCard
                    required property var modelData

                    width: ListView.view.width
                    implicitHeight: 56
                    radius: Theme.radius.card
                    color: installedHover.hovered ? Theme.color.hoverBg : Theme.color.panelBg
                    border.width: 1
                    border.color: installedHover.hovered ? Theme.color.inputBorder : Theme.color.menuBorder

                    HoverHandler { id: installedHover }

                    Rectangle {
                        id: installedIconTile
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 28
                        radius: Theme.radius.control
                        color: Theme.color.buttonBg

                        LineIcon {
                            anchors.centerIn: parent
                            name: "plugin"
                            size: 14
                            color: Theme.color.textSecondary
                        }
                    }

                    Row {
                        anchors.left: installedIconTile.right

                        // 原版 createInstalledCard：名字 label 是 stretch=1，右侧两个按钮 stretch=0。
                        // 所以名字要给按钮让位并省略号收尾，不能盖到按钮上。
                        anchors.right: buttonRow.left
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Text {
                            width: Math.min(implicitWidth,
                                            parent.width - (versionBadge.visible
                                                ? versionBadge.implicitWidth + parent.spacing : 0))
                            text: installedCard.modelData.name
                            elide: Text.ElideRight
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.title
                        }

                        Rectangle {
                            id: versionBadge
                            y: Math.round((parent.height - height) / 2)   // Row 子项不用锚点，y 现算（同市场卡片那颗徽章）
                            visible: installedCard.modelData.version.length > 0
                            implicitWidth: versionText.implicitWidth + 12
                            implicitHeight: 18
                            radius: height / 2
                            color: Theme.color.buttonBg

                            Text {
                                id: versionText
                                anchors.centerIn: parent
                                text: installedCard.modelData.version
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.small
                            }
                        }
                    }

                    Row {
                        id: buttonRow
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        ActionButton {
                            width: 56
                            height: 28
                            text: qsTrId("plugin_update_action")
                            variant: "elevated"
                            onClicked: Hub.plugins.update(installedCard.modelData.name)
                        }

                        ActionButton {   // 卸载：透明面 + 危险色文字（危险动作不该是实心红块，也不该和"更新"长得一样）
                            width: 56
                            height: 28
                            text: qsTrId("plugin_uninstall_action")
                            variant: "ghostDanger"
                            onClicked: Hub.plugins.uninstall(installedCard.modelData.name)
                        }
                    }
                }
            }
        }

        Column {
            id: installBox

            anchors.left: parent.left
            anchors.right: parent.right

            // ⚠️ 不能锚到 installBox 自己（Qt 会丢弃这条锚点）：installBox 塌到 y=0 后列表区高度归零，
            // 市场数据拉到了也一条都显示不出来。贴在底部状态行（statusRow）之上，列表区才正确让位。
            anchors.bottom: statusRow.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 8
            spacing: 6

            readonly property bool installVisible: Hub.plugins.installTasks.length > 0
                || Hub.plugins.installing
                || Hub.plugins.installStatus.length > 0
                || Hub.plugins.blockedBuilds.length > 0
            visible: installVisible
            height: installVisible ? implicitHeight : 0

            Text {
                width: parent.width
                visible: Hub.plugins.installTasks.length > 0
                text: "安装任务" + (Hub.plugins.activeTaskCount > 0 ? " · " + Hub.plugins.activeTaskCount + " 个进行中" : "")
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Repeater {
                model: Hub.plugins.installTasks

                delegate: Rectangle {
                    required property var modelData
                    required property int index

                    visible: index < 5
                    width: parent.width
                    height: taskContent.implicitHeight + 14
                    radius: Theme.radius.control
                    color: Theme.color.inputBg
                    border.width: 1
                    border.color: modelData.state === "failed" ? Theme.color.danger
                        : (modelData.state === "blocked" ? Theme.color.inputBorder : Theme.color.menuBorder)

                    Column {
                        id: taskContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 8
                        spacing: 2

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                id: taskAction
                                text: page.taskActionLabel(modelData.action)
                                color: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                font.weight: Theme.font.weightBold
                            }
                            Text {
                                id: taskState
                                text: page.taskStateLabel(modelData)
                                color: modelData.state === "failed" ? Theme.color.danger
                                    : (modelData.state === "ok" ? Theme.color.textSecondary : Theme.color.accent)
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                font.weight: Theme.font.weightBold
                            }
                            Text {
                                id: taskName
                                width: Math.max(0, parent.width - taskAction.width - taskState.width
                                    - taskElapsed.width - 3 * parent.spacing)
                                text: modelData.name
                                elide: Text.ElideRight
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }
                            Text {
                                id: taskElapsed
                                text: page.taskElapsed(modelData)
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.small
                            }
                        }

                        Text {
                            width: parent.width
                            text: modelData.message
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            wrapMode: Text.NoWrap
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: blockedColumn.implicitHeight + 16
                visible: Hub.plugins.blockedBuilds.length > 0
                radius: Theme.radius.control
                color: Theme.color.inputBg
                border.width: 1
                border.color: Theme.color.inputBorder

                Column {
                    id: blockedColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 8
                    spacing: 6

                    Text {
                        width: parent.width
                        text: qsTrId("plugin_build_scripts_blocked_fmt").arg(Hub.plugins.blockedBuilds.join(", "))
                        wrapMode: Text.WordWrap
                        color: Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                    }

                    ActionButton {
                        width: 150
                        height: 28
                        text: qsTrId("plugin_approve_builds")
                        variant: "primary"
                        onClicked: Hub.plugins.approveBuilds()
                    }
                }
            }

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
                id: pagerRow
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: 28
                spacing: 8
                visible: Hub.plugins.tab === 0

                ActionButton {
                    width: 64
                    height: 28
                    text: qsTrId("common_prev_page")
                    variant: "elevated"
                    enabled: Hub.plugins.canPrev
                    onClicked: Hub.plugins.prevPage()
                }

                Text {
                    y: Math.round((parent.height - height) / 2)   // Row 子项不用锚点：按行高居中的 y（同工具栏那条说明）
                    text: Hub.plugins.pageLabel
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                ActionButton {
                    width: 64
                    height: 28
                    text: qsTrId("common_next_page")
                    variant: "elevated"
                    enabled: Hub.plugins.canNext
                    onClicked: Hub.plugins.nextPage()
                }
            }

            Text {
                id: statusText
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter

                // 限宽：左边界锚到翻页行（pagerRow）右侧 —— 文案再长也不会压住"上一页/下一页"，
                // 右边界到卡片右边，超出部分尾部省略号。
                anchors.left: pagerRow.right
                anchors.leftMargin: 16
                horizontalAlignment: Text.AlignRight
                text: Hub.plugins.status
                elide: Text.ElideRight
                maximumLineCount: 1
                wrapMode: Text.NoWrap
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }
        }
    }
}
