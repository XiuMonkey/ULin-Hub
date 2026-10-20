// 顶栏入口 chip（子代理 / 后台任务）：状态点 + 标签 + 计数 + ▾，点开一个贴在下方的弹层。
//
// 形状照官方 dsh 的两个 session-header 触发器（`@deepseek-ai/dsh-client-ui-subagent` 的
// 后代数量触发器、`dsh-client-ui-jobs` 的任务列表触发器，两个注册在同一个 slot
// `conversation.session.header.actions`）：
//   · 都是 12px 标签 + 数字 + 12px chevron，running 时前面一颗 ongoing 点，hover 升一档字色；
//   · 官方在数据为空时**整颗不渲染**。本项目保留入口（空态照常显示 0），弹层里给一行说明。
//
// 弹层内容：items = [{ key, title, subtitle, tone }]，点一行发 itemActivated(key)，
// 由顶栏转成"切到那个会话/任务所属会话"（数据来源见 TopBar 的头注释）。
// tone: "running"（进行中，强调色点）/ "error"（失败，危险色点）/ 其他 = 中性点。
//
// 底色/圆角/高度走本项目 chip 体系（透明底 + chipHoverBg、radius chip、selectorChipHeight），
// 不取官方的 28px：顶栏第二行只有 28 高，24 的字块才和左边"对话 / 轨迹"胶囊同一视觉重量。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string text: ""
    property int count: 0
    // 有正在跑的东西时亮一颗点（官方 StateDot ongoing；这里用静态圆点，不做脉冲）
    property bool running: false
    property string tooltip: ""
    // 弹层：标题 + 数据行 + 空态说明
    property string popupTitle: ""
    property string emptyHint: ""
    // [{ key, title, subtitle, tone }]
    property var items: []

    readonly property bool hasItems: items ? items.length > 0 : false

    // 窄栏形态：只留「点 + 计数 + ▾」，标签藏起来（tooltip 里仍带着名字）。
    // 顶栏卡片被右栏压窄时用它换空间 —— **不要**改成整块隐藏，入口一消失用户就找不到了。
    property bool compact: false

    signal clicked()
    // 点中弹层里的一行（key = 会话 id / 任务 id）
    signal itemActivated(string key)

    readonly property bool hovered: mouseArea.containsMouse
    readonly property bool pressed: mouseArea.pressed

    // 两种形态的宽度都摆在明面上：父级（TopBar）要拿它们判断"当前放得下哪一版"。
    // 用 Text.implicitWidth 量，别自己按字数乘字号 —— 中英文混排会算不准。
    readonly property real dotAdvance: root.running ? 6 + row.spacing : 0
    readonly property real labelAdvance: labelText.implicitWidth + row.spacing
    readonly property real tailAdvance: countText.implicitWidth + row.spacing + chevron.implicitWidth
    readonly property real fullWidth: dotAdvance + labelAdvance + tailAdvance
        + 2 * Theme.spacing.labelGap
    readonly property real compactWidth: dotAdvance + tailAdvance + 2 * Theme.spacing.labelGap

    implicitWidth: compact ? compactWidth : fullWidth
    implicitHeight: Theme.size.selectorChipHeight

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.chip
        // 弹层开着时保持底色：它是"开关"，不是一次性按钮
        color: root.pressed
            ? Theme.color.activeBg
            : ((root.hovered || menu.visible) ? Theme.color.chipHoverBg : "transparent")
    }

    Row {
        id: row

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.labelGap
        spacing: 4

        Rectangle {
            width: 6
            height: 6
            radius: 3
            anchors.verticalCenter: parent.verticalCenter
            visible: root.running
            color: Theme.color.accent
        }

        Text {
            id: labelText

            anchors.verticalCenter: parent.verticalCenter
            visible: !root.compact
            text: root.text
            color: root.hovered ? Theme.color.textPrimary : Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            font.weight: Theme.font.weightMedium

            Behavior on color {
                ColorAnimation { duration: Theme.motion.fast }
            }
        }

        Text {
            id: countText

            anchors.verticalCenter: parent.verticalCenter
            text: root.count
            // 0 与"有数"要能一眼分开：0 退到 caption 档、不加粗
            color: root.count > 0 ? Theme.color.textSecondary : Theme.color.textCaption
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            font.weight: root.count > 0 ? Theme.font.weightMedium : Font.Normal
        }

        Text {
            id: chevron

            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: Theme.color.textCaption
            font.pixelSize: Theme.font.small
        }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            // 先让外面接（以后要开真面板就走这条，chip 自己的弹层换成真内容即可）
            root.clicked()
            menu.visible ? menu.close() : menu.open()
        }
    }

    AppToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && root.hovered && !root.pressed && !menu.visible
    }

    Popup {
        id: menu

        // 贴 chip 下方 6px、右缘对齐：chip 就在顶栏最右，往右弹会出窗
        x: root.width - width
        y: root.height + 6
        width: 300
        padding: Theme.spacing.cardPadding
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // QQuickPopup 自带 final 成员 contentWidth，不能同名覆盖；这里用 rowWidth（弹层内容宽）
        readonly property real rowWidth: width - 2 * padding

        background: Rectangle {
            radius: Theme.radius.chip
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            spacing: 6

            Text {
                text: root.popupTitle
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                font.weight: Theme.font.weightMedium
            }

            // 空态：引擎不可用 / 本会话没有子代理或后台任务时给一行说明（不崩、不空白弹层）
            Text {
                visible: !root.hasItems
                // 弹层宽 - 左右 padding；不给 width 会按最长一行撑破弹层
                width: menu.rowWidth
                text: root.emptyHint
                wrapMode: Text.WordWrap
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
                lineHeight: 1.4
            }

            // 数据行（子智能体 / 后台任务）：点一行 = 切到它（由顶栏转发 itemActivated）
            ListView {
                id: rowList

                visible: root.hasItems
                width: menu.rowWidth
                height: Math.min(contentHeight, 300)
                clip: true
                model: root.items
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: itemRow

                    required property var modelData

                    width: rowList.width
                    height: 40

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius.small
                        color: rowMouse.containsMouse ? Theme.color.chipHoverBg : "transparent"
                    }

                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: itemRow.modelData.tone === "running" ? Theme.color.accent
                            : itemRow.modelData.tone === "error" ? Theme.color.danger
                            : Theme.color.textCaption
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            width: parent.width
                            text: itemRow.modelData.title
                            elide: Text.ElideRight
                            color: Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            font.weight: Theme.font.weightMedium
                        }

                        Text {
                            width: parent.width
                            text: itemRow.modelData.subtitle
                            elide: Text.ElideRight
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }

                    MouseArea {
                        id: rowMouse

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.itemActivated(itemRow.modelData.key)
                            menu.close()
                        }
                    }
                }
            }
        }
    }
}
