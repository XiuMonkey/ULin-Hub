// 顶栏入口 chip（子代理 / 后台任务）：状态点 + 标签 + 计数 + ▾，点开一个贴在下方的弹层。
// 形状照官方 dsh 的两个 session-header 触发器（`@deepseek-ai/dsh-client-ui-subagent` 的
// 后代数量触发器、`dsh-client-ui-jobs` 的任务列表触发器，两个注册在同一个 slot
// `conversation.session.header.actions`）：
// · 都是 12px 标签 + 数字 + 12px chevron，running 时前面一颗 ongoing 点，hover 升一档字色；
// · 官方在数据为空时**整颗不渲染**。本项目保留入口（空态照常显示 0），弹层里给一行说明。
// 弹层内容：items = [{ key, title, subtitle, tone }]，点一行发 itemActivated(key)，
// 由顶栏转成"切到那个会话/任务所属会话"（数据来源见 TopBar 的头注释）。
// tone: "running"（进行中，强调色点）/ "error"（失败，危险色点）/ 其他 = 中性点。
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
    property bool running: false   // 有正在跑的东西时亮一颗点（官方 StateDot ongoing；这里用静态圆点，不做脉冲）
    property string tooltip: ""
    property string popupTitle: ""   // 弹层：标题 + 数据行 + 空态说明
    property string emptyHint: ""
    property var items: []   // [{ key, title, subtitle, tone }]

    readonly property bool hasItems: items ? items.length > 0 : false

    // 窄栏形态：只留「点 + 计数 + ▾」，标签藏起来（tooltip 里仍带着名字）。
    // 顶栏卡片被右栏压窄时用它换空间 —— **不要**改成整块隐藏，入口一消失用户就找不到了。
    property bool compact: false

    signal clicked()
    signal itemActivated(string key)   // 点中弹层里的一行（key = 会话 id / 任务 id）

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
        color: root.pressed   // 弹层开着时保持底色：它是"开关"，不是一次性按钮
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
            color: root.count > 0 ? Theme.color.textSecondary : Theme.color.textCaption   // 0 与"有数"要能一眼分开：0 退到 caption 档、不加粗
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

        x: root.width - width   // 贴 chip 下方 6px、右缘对齐：chip 就在顶栏最右，往右弹会出窗
        y: root.height + 6
        width: 300
        padding: Theme.spacing.cardPadding
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        readonly property real rowWidth: width - 2 * padding   // QQuickPopup 自带 final 成员 contentWidth，不能同名覆盖；这里用 rowWidth（弹层内容宽）

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

            Text {   // 空态：引擎不可用 / 本会话没有子代理或后台任务时给一行说明（不崩、不空白弹层）
                visible: !root.hasItems
                width: menu.rowWidth   // 弹层宽 - 左右 padding；不给 width 会按最长一行撑破弹层
                text: root.emptyHint
                wrapMode: Text.WordWrap
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.small
                lineHeight: 1.4
            }

            ListView {   // 数据行（子智能体 / 后台任务）：点一行 = 切到它（由顶栏转发 itemActivated）
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

                    // 行高随内容：后台任务的 title 是命令全文（引擎侧 `label: args.command`），
                    // 多行脚本本身就带换行符，写死 40 会让 3~4 行文本溢出盒子、跟下一行叠在一起。
                    // title 现在最多两行 + 省略号（见下），所以这里按实际内容算，下限仍是 40。
                    height: Math.max(40, contentColumn.implicitHeight + 6)

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
                        id: contentColumn

                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            width: parent.width
                            text: itemRow.modelData.title

                            // 最多两行，第三行起直接省略号截断。
                            // ⚠️ elide 只处理长度、不处理换行：不写 wrapMode 时默认 NoWrap 虽然禁自动折行，
                            // 但命令里的显式 \n 照样断行 —— 所以必须显式给 wrapMode + maximumLineCount。
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            maximumLineCount: 2
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
