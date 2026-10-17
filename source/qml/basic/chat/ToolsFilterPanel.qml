// 工具过滤面板（顶栏 ☰ 打开）
//
// 旧壳是 Widgets 的 `ToolsFilterPopup` + `ToolsFilterDirectoryEntry`（source/src/ui/TopBar.cpp，已随旧壳删除）。
// QML 侧的分工（2026-10-10 定）：
//   · **后台与形状** 全在 C++ 的 `common/session/ToolsFilter`（数据层 + 摊平的行模型）——本文件只画；
//   · 勾选/开关都直接打到模型上（模型内部即时生效并自动 POST 写回，不需要"保存"按钮）。
//
// 行模型（ToolsFilter::roleNames）：
//   kind          "directory" | "tool"
//   directory     目录头：目录名；工具行：所属目录名
//   description   目录说明 / 工具描述
//   count         目录头：成员数
//   hiddenCount   目录头：该目录里被隐藏的工具数
//   toolVisible   工具行：勾选态（⚠️ 不能叫 visible —— QQuickItem 的 final 属性，委托里会报 Cannot override FINAL property）
//   hidden        目录头：配置里"整组隐藏"（IsExpanded == "False"）
//   expandedState 目录头：界面展开态（同上，别叫 expanded）
//   toolName      工具行：工具名（勾选时回传给模型）
//   parameters    工具行：参数 Schema 紧凑文本（做提示用，不直接铺开）
//
// ⚠️ 三条语义陷阱（见 ToolsFilter.h）：隐藏只能靠 IsVisible:"False"；IsExpanded 是**筛选**语义
//    （胶囊开关 = 整组隐藏），与卡片展开/收起无关；HideContexts/DropGuidance 由 C++ 原样带回。

import QtQuick
import QtQuick.Controls.Basic   // ScrollBar.vertical（列表滚动条；与 MessageListView.qml 同一套导入）
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 模型由宿主单例提供（面板由 Shell 的 Loader 装载，import ULinHub 1.0 就能拿到 Hub）
    readonly property var filter: Hub.toolsFilter

    signal closeRequested()

    anchors.fill: parent
    focus: true

    // 交给 PageOverlay 的**卡片目标框**。原来是 min(窗口-边距, 560/620)，但那种写法依赖 parent.width，
    // 而在 PageOverlay 里 parent 就是卡片框本身 ⇒ 会成环（这正是本面板当初没套 PageOverlay 的原因）。
    // 窗口最小尺寸远大于这里，取固定值即可。
    implicitWidth: 560
    implicitHeight: 620

    readonly property bool busy: root.filter ? root.filter.busy : false
    readonly property string statusLine: root.filter ? root.filter.status : ""
    readonly property int totalCount: root.filter ? root.filter.totalCount : 0
    readonly property int visibleCount: root.filter ? root.filter.visibleCount : 0

    // 声明了这个，PageOverlay 就会把"卡片正在展开/收回"绑上来 ⇒ 面板自己的转圈先停
    property bool overlayAnimating: false

    // Esc 关面板（没有焦点的话按键会被宿主那边吃掉）
    Keys.onEscapePressed: root.closeRequested()

    // ⚠️ 遮罩与卡片底**由 PageOverlay 统一提供**（与另外三个覆盖页同一套）：
    //    这里曾自带一层整屏遮罩 + 不透明卡片，一缩放就会把遮罩也缩小（"遮罩跟着缩放"就是这么来的）。
    //    现在这里只当内容容器，卡片外观（圆角/阴影/底）交给外壳 ⇒ 展开动画与那三个一致。
    Item {
        id: card

        anchors.fill: parent

        // ---------------- 页头：标题 + 刷新 + 关闭 ----------------
        Item {
            id: header

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing.cardPadding
            height: 28

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("toolfilter_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightMedium
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.buttonGap

                ActionButton {
                    width: 72
                    height: 26
                    text: qsTrId("common_refresh")   // 宿主没有 toolfilter_refresh 这个 id，用既有的通用 id
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

        // 帮助文案：把"勾掉 = 模型看不到、但仍可被调用"说清楚（旧壳同一句 id）
        Text {
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

        // ---------------- 列表 ----------------
        ListView {
            id: list

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: hint.bottom
            anchors.bottom: footer.top
            anchors.margins: Theme.spacing.cardPadding
            anchors.topMargin: Theme.spacing.sectionGap
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

                // ---- 目录头：名字 + 成员数 + （配置里）整组隐藏提示 + 胶囊开关 ----
                Rectangle {
                    id: directoryRow

                    visible: row.kind === "directory"
                    width: parent.width
                    height: 38
                    color: directoryHover.containsMouse ? Theme.color.hoverBg : "transparent"

                    // 左：折叠箭头 + 目录名；中：成员数 + （配置里）整组隐藏提示；右：胶囊开关。
                    // ⚠️ 别用一个铺满的 Row 装全部：Row 的隐式宽度只按子项算，配 parent.width 会成循环绑定。
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.right: toggleLeft.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.expandedState ? "▾" : "▸"
                            color: Theme.color.textSecondary
                            font.pixelSize: Theme.font.small
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.directory
                            color: row.hidden ? Theme.color.textTertiary : Theme.color.textPrimary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                            font.weight: Theme.font.weightMedium
                            elide: Text.ElideRight
                            width: Math.max(40, parent.width - 200)   // 让给右边的成员数
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTrId("toolfilter_summary_fmt").arg(row.count).arg(row.hiddenCount)
                            color: Theme.color.textTertiary
                            font.pixelSize: Theme.font.caption
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.hidden
                            text: qsTrId("toolfilter_group_hidden_suffix")
                            color: Theme.color.textTertiary
                            font.pixelSize: Theme.font.caption
                        }
                    }

                    // 胶囊开关的占位（让上面那行的右边界有据可依）
                    Item {
                        id: toggleLeft

                        anchors.right: groupToggleItem.left
                        anchors.rightMargin: 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 0
                        height: 1
                    }

                    // 胶囊开关：**写配置**（勾上 = 整组对模型隐藏），不是展开/收起
                    Item {
                        id: groupToggleItem

                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 34
                        height: 18

                        Rectangle {
                            anchors.fill: parent
                            radius: height / 2
                            color: row.hidden ? Theme.color.accent : Theme.color.buttonBg
                            border.width: 1
                            border.color: Theme.color.border

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                y: 2
                                x: row.hidden ? parent.width - width - 2 : 2
                                color: Theme.color.textOnAccent
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filter.setDirectoryHidden(row.directory, !row.hidden)
                        }

                        AppToolTip {
                            text: qsTrId("toolfilter_group_toggle_tip")
                            visible: toggleHover.containsMouse
                        }

                        MouseArea {
                            id: toggleHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }

                    // 点表头（开关以外的地方）= 展开/收起（纯显示，不写配置）
                    MouseArea {
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

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.color.border
                    }
                }

                // ---- 工具行：勾选框 + 名字 + 描述 ----
                Item {
                    id: toolRow

                    visible: row.kind === "tool"
                    width: parent.width
                    height: 44

                    Rectangle {
                        anchors.fill: parent
                        color: toolHover.containsMouse ? Theme.color.hoverBg : "transparent"
                    }

                    // 勾选框（与 InteractionPanel 的多选方块同一套画法）
                    Rectangle {
                        id: marker

                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        radius: 3
                        color: row.toolVisible ? Theme.color.accent : "transparent"
                        border.width: 1
                        border.color: row.toolVisible ? Theme.color.accent : Theme.color.inputBorder

                        Text {
                            anchors.centerIn: parent
                            visible: row.toolVisible
                            text: "✓"
                            color: Theme.color.textOnAccent
                            font.pixelSize: Theme.font.small
                        }
                    }

                    Column {
                        anchors.left: marker.right
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            text: row.toolName + (row.parameters.length > 0
                                ? "   " + qsTrId("toolfilter_params_label") + row.parameters
                                : "")
                            color: row.toolVisible ? Theme.color.textPrimary : Theme.color.textTertiary
                            font.family: "Consolas"
                            font.pixelSize: Theme.font.small
                            elide: Text.ElideRight
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
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.filter.setToolVisible(row.toolName, !row.toolVisible)
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.color.border
                    }
                }
            }

            // 空态 / 加载中：把"为什么是空的"写清楚
            Text {
                anchors.centerIn: parent
                width: list.width - 2 * Theme.spacing.cardPadding
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: list.count === 0
                text: root.busy
                    ? qsTrId("toolfilter_loading")
                    : (root.statusLine.length > 0 ? root.statusLine : qsTrId("toolfilter_no_tools"))
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
            }
        }

        // ---------------- 页脚：全部显示 / 全部隐藏 / 状态 ----------------
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
                    enabled: !root.busy
                    onClicked: root.filter.setAllVisible(true)
                }

                ActionButton {
                    width: 88
                    height: 26
                    text: qsTrId("toolfilter_hide_all")
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
