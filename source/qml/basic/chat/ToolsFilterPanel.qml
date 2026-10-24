// 工具过滤面板（顶栏 ☰ 打开）。只负责画：后台数据与形状全在 C++ 的 common/session/ToolsFilter。
// 行模型角色（ToolsFilter::roleNames）：kind="directory"|"tool"、directory、description、count、
// hiddenCount、toolVisible、hidden、expandedState、toolName、parameters。
// ⚠️ toolVisible/expandedState 不能改名 visible/expanded（QQuickItem 的 final 属性，委托里会报 Cannot override FINAL property）。
// ⚠️ 三条语义陷阱（见 ToolsFilter.h）：隐藏只能靠 IsVisible:"False"；IsExpanded 是**筛选**语义
// （胶囊开关 = 整组隐藏，与卡片展开/收起无关）；HideContexts/DropGuidance 由 C++ 原样带回。
// 布局按设置页同一套分层重排（配色/字阶/圆角全取既有 token，不新增颜色）：标题带图标 + 计数徽章，
// 列表收进 panelBg 卡片，目录头做分区分组头（chevron + 成员数 + 配置隐藏提示 + 胶囊开关），工具行缩进挂在下面。

import QtQuick
import QtQuick.Controls.Basic   // ScrollBar.vertical（列表滚动条；与 MessageListView.qml 同一套导入）
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    readonly property var filter: Hub.toolsFilter   // 模型由宿主单例提供（面板由 Shell 的 Loader 装载，import ULinHub 1.0 就能拿到 Hub）

    signal closeRequested()

    anchors.fill: parent
    focus: true

    implicitWidth: 560   // 窗口最小尺寸远大于这里，取固定值即可
    implicitHeight: 620

    readonly property bool busy: root.filter ? root.filter.busy : false
    readonly property string statusLine: root.filter ? root.filter.status : ""
    readonly property int totalCount: root.filter ? root.filter.totalCount : 0
    readonly property int visibleCount: root.filter ? root.filter.visibleCount : 0

    // 被隐藏的个数 = 总数 − 可见数（C++ 只给这两个数，"已隐藏"由这里算，避免再加一条属性）
    readonly property int hiddenCount: Math.max(0, root.totalCount - root.visibleCount)

    property bool overlayAnimating: false   // 声明了这个，PageOverlay 就会把"卡片正在展开/收回"绑上来 ⇒ 面板自己的转圈先停

    Keys.onEscapePressed: root.closeRequested()   // Esc 关面板（没有焦点的话按键会被宿主那边吃掉）

    // ⚠️ 遮罩与卡片底**由 PageOverlay 统一提供**（与另外三个覆盖页同一套）：
    // 这里曾自带一层整屏遮罩 + 不透明卡片，一缩放就会把遮罩也缩小（"遮罩跟着缩放"就是这么来的）。
    // 现在这里只当内容容器，卡片外观（圆角/阴影/底）交给外壳 ⇒ 展开动画与那三个一致。
    Item {
        id: card

        anchors.fill: parent

        Item {
            id: header

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing.cardPadding
            height: 28

            LineIcon {   // 标题前的图标：与设置页导航项同一个做法（线性图标 + 14~15px），比纯文字多一层"这是哪一页"
                id: headerIcon
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                name: "tools"
                size: 15
                color: Theme.color.textSecondary
            }

            Text {
                id: titleLabel
                anchors.left: headerIcon.right
                anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("toolfilter_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightMedium
            }

            Rectangle {   // 计数徽章：中性面 pill（不抢标题），数字当前唯一的"整页概览"入口
                id: countPill
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
                    text: qsTrId("toolfilter_summary_fmt").arg(root.totalCount).arg(root.hiddenCount)
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.small
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.buttonGap

                ActionButton {   // 刷新：中性"浮起面"按钮（primary 近黑面在这个位置太抢眼，页里主动作是勾选工具）
                    width: 72
                    height: 26
                    text: qsTrId("common_refresh")   // 宿主没有 toolfilter_refresh 这个 id，用既有的通用 id
                    variant: "elevated"
                    enabled: !root.busy
                    onClicked: root.filter.refresh(Hub.currentSessionId)
                }

                IconButton {
                    boxSize: 26
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: root.closeRequested()
                }
            }
        }

        Text {   // 帮助文案：把"勾掉 = 模型看不到、但仍可被调用"说清楚
            id: hint

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.leftMargin: Theme.spacing.cardPadding
            anchors.rightMargin: Theme.spacing.cardPadding
            anchors.topMargin: Theme.spacing.labelGap
            text: qsTrId("toolfilter_help_desc")
            color: Theme.color.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            wrapMode: Text.WordWrap
        }

        Rectangle {
            id: listFrame

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: hint.bottom
            anchors.bottom: footer.top
            anchors.leftMargin: Theme.spacing.cardPadding
            anchors.rightMargin: Theme.spacing.cardPadding
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            radius: Theme.radius.card
            color: Theme.color.panelBg
            border.width: 1

            // 卡片描边走 menuBorder（6%）而不是 border（4%）：与设置页的成员卡同一档，
            // 一屏好几张卡时才分得清"卡"和"卡里的行"。
            border.color: Theme.color.menuBorder

            ListView {
                id: list

                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.filter
                ScrollBar.vertical: AppScrollBar { }
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: row

                    required property string kind
                    required property string directory
                    required property string description
                    required property int count
                    required property int hiddenCount   // 该目录里被隐藏的工具数
                    required property bool toolVisible
                    required property bool hidden
                    required property bool expandedState
                    required property string toolName
                    required property string parameters

                    width: list.width
                    height: row.kind === "directory" ? directoryRow.height : toolRow.height

                    Item {
                        id: directoryRow

                        visible: row.kind === "directory"
                        width: parent.width
                        height: 36

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius.inner
                            color: directoryHover.containsMouse ? Theme.color.hoverBg : "transparent"
                        }

                        LineIcon {
                            id: chevron

                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron-right"
                            size: 11
                            rotation: row.expandedState ? 90 : 0
                            color: row.hidden ? Theme.color.textCaption : Theme.color.textTertiary

                            Behavior on rotation {
                                NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
                            }
                        }

                        Text {
                            id: directoryLabel
                            anchors.left: chevron.right
                            anchors.leftMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: countBadge.left
                            anchors.rightMargin: 8
                            text: row.directory
                            color: row.hidden ? Theme.color.textTertiary : Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                            font.weight: Theme.font.weightMedium
                            elide: Text.ElideRight
                        }

                        Rectangle {   // 成员数：小徽章（设置页的 badge 同一套：buttonBg 中性面）
                            id: countBadge

                            anchors.right: hiddenSuffix.visible ? hiddenSuffix.left : groupToggleItem.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.count > 0
                            implicitWidth: countBadgeLabel.implicitWidth + 12
                            implicitHeight: 16
                            radius: height / 2
                            color: Theme.color.buttonBg

                            Text {
                                id: countBadgeLabel
                                anchors.centerIn: parent
                                text: row.count
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.small
                            }
                        }

                        Text {
                            id: hiddenSuffix

                            anchors.right: groupToggleItem.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.hidden
                            text: qsTrId("toolfilter_group_hidden_suffix")
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                            elide: Text.ElideRight
                        }

                        Item {   // 胶囊开关：**写配置**（打开 = 整组对模型隐藏），不是展开/收起
                            id: groupToggleItem

                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 18

                            Rectangle {
                                anchors.fill: parent
                                radius: height / 2
                                color: row.hidden ? Theme.color.textPrimary : Theme.color.buttonBg
                                border.width: 1
                                border.color: row.hidden ? Theme.color.textPrimary : Theme.color.inputBorder

                                Behavior on color {
                                    ColorAnimation { duration: Theme.motion.fast }
                                }
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                y: 2
                                x: row.hidden ? parent.width - width - 2 : 2
                                color: Theme.color.panelBg
                                border.width: row.hidden ? 0 : 1
                                border.color: Theme.color.inputBorder

                                Behavior on x {
                                    NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
                                }
                            }

                            MouseArea {
                                id: toggleHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.filter.setDirectoryHidden(row.directory, !row.hidden)
                            }

                            AppToolTip {
                                text: qsTrId("toolfilter_group_toggle_tip")
                                visible: toggleHover.containsMouse
                            }
                        }

                        MouseArea {   // 点表头（开关以外的地方）= 展开/收起（纯显示，不写配置）
                            id: directoryHover
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: 46   // 让开胶囊开关
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filter.toggleDirectory(row.directory)
                        }
                    }

                    Item {
                        id: toolRow

                        visible: row.kind === "tool"
                        width: parent.width
                        height: 46

                        // 组内连接线：从目录的 chevron 下方垂下来的一条 2px 竖线（很淡，只做"归属"暗示）。
                        // 放在左侧 13px（= chevron 中心），只覆盖工具行的高度。
                        Rectangle {
                            x: 13
                            y: 0
                            width: 2
                            height: parent.height
                            radius: 1
                            color: Theme.color.border
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 22
                            radius: Theme.radius.inner
                            color: toolHover.containsMouse ? Theme.color.hoverBg : "transparent"
                        }

                        Rectangle {   // 勾选框（中性反相：勾上 = 近黑面 + 白勾；与设置页"支持图片输入"那颗同一套画法）
                            id: marker

                            anchors.left: parent.left
                            anchors.leftMargin: 26
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: Theme.radius.check
                            color: row.toolVisible ? Theme.color.textPrimary : "transparent"
                            border.width: 1
                            border.color: row.toolVisible
                                ? Theme.color.textPrimary
                                : (toolHover.containsMouse ? Theme.color.textSecondary : Theme.color.inputBorder)

                            Text {
                                anchors.centerIn: parent
                                visible: row.toolVisible
                                text: "✓"
                                color: Theme.color.panelBg
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.small
                                font.weight: Theme.font.weightBold
                            }
                        }

                        Column {
                            id: toolText

                            anchors.left: marker.right
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Row {
                                width: parent.width
                                spacing: 8

                                Text {
                                    id: toolNameLabel
                                    width: Math.min(implicitWidth, Math.max(60, toolText.width * 0.45))
                                    text: row.toolName
                                    elide: Text.ElideRight
                                    color: row.toolVisible ? Theme.color.textPrimary : Theme.color.textTertiary
                                    font.family: "Consolas"
                                    font.pixelSize: Theme.font.body
                                }

                                Text {
                                    width: Math.max(0, toolText.width - toolNameLabel.width - 8)
                                    visible: row.parameters.length > 0
                                    text: qsTrId("toolfilter_params_label") + row.parameters
                                    elide: Text.ElideRight
                                    color: Theme.color.textCaption
                                    font.family: "Consolas"
                                    font.pixelSize: Theme.font.small

                                    // Row 里**不能**用 verticalCenter 这类锚点（会打 "Cannot specify ...
                                    // anchors for items inside Row" 并在某些版本里直接不生效），
                                    // 只手动算 y —— Row 只管 x。
                                    y: Math.round((parent.height - height) / 2)
                                }
                            }

                            Text {
                                width: parent.width
                                visible: row.description.length > 0
                                text: row.description
                                color: Theme.color.textSecondary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: toolHover
                            anchors.fill: parent
                            anchors.leftMargin: 22
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filter.setToolVisible(row.toolName, !row.toolVisible)
                        }

                        Rectangle {   // 行分隔线：只从文字左缘起（不通栏），读起来是"组内的行"而不是"网格"
                            anchors.left: parent.left
                            anchors.leftMargin: 52
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.color.border
                        }
                    }
                }
            }

            Column {   // 空态 / 加载中：把"为什么是空的"写清楚（图标 + 一句），别只留一行字
                anchors.centerIn: parent
                width: list.width - 2 * Theme.spacing.cardPadding
                spacing: 8
                visible: list.count === 0

                LineIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "tools"
                    size: 30
                    color: Theme.color.textCaption
                    opacity: 0.5
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.busy
                        ? qsTrId("toolfilter_loading")
                        : (root.statusLine.length > 0 ? root.statusLine : qsTrId("toolfilter_no_tools"))
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body
                }
            }
        }

        Item {
            id: footer

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing.cardPadding
            height: 28

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.buttonGap

                ActionButton {
                    width: 88
                    height: 26
                    text: qsTrId("toolfilter_show_all")
                    variant: "elevated"
                    enabled: !root.busy
                    onClicked: root.filter.setAllVisible(true)
                }

                ActionButton {
                    width: 88
                    height: 26
                    text: qsTrId("toolfilter_hide_all")
                    variant: "elevated"
                    enabled: !root.busy
                    onClicked: root.filter.setAllVisible(false)
                }

                BusySpinner {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    lineWidth: 2
                    running: root.busy
                    visible: root.busy
                }
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, footer.width - 300)
                horizontalAlignment: Text.AlignRight
                text: root.statusLine
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                elide: Text.ElideRight
            }
        }
    }
}
