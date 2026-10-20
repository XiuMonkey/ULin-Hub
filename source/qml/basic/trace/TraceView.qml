// 轨迹视图（对照官方 @deepseek-ai/dsh-client-ui-trajectory）。
//
// 官方那套是 React 的"记录表 + 可拖拽时间轴 + 14 页检查器"。这里是 QML 的落地版，保留三件最有用的事：
//   1) 按轮次 / 步骤分组的事件记录表（谁、什么工具、多久、多少 token、成功还是失败）；
//   2) 每条记录的耗时 / TTFT / token 用量（数据来自账本事件的 time 与 stream.time0+dt）；
//   3) 点开看这一条的**原文 JSON**（相当于官方的 Inspect，只是把 14 页压成一份原文）。
// 刻意不做的：时间轴拖拽缩放、搜索过滤、分页窗口。记录表本身就在 ListView 里，几千行也只是滚动。
//
// 数据面是 Hub.trace（common/session/TraceModel）：与消息列表吃同一条 session/follow 流，
// 所以"对话"和"轨迹"永远同一份账本，不会两边对不上。
//
// 布局上避开一个坑：记录行里"左侧徽标 + 中间两行文字 + 右侧指标"不要用 Row 硬算宽度
// （Row 的 width 由子项决定，给子项写 parent.width - xxx 会形成绑定环）；这里一律用锚定，
// 中间那列吃掉剩余宽度。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: traceRoot

    // 当前打开检查面板的行（-1 = 收起）
    property int selectedIndex: -1
    // 选中时把这一行的角色值抄一份出来 —— 检查面板不用再按 index 回查模型（省掉角色编号魔法值）
    property string selectedDetail: ""
    property string selectedMeta: ""

    readonly property var summary: Hub.traceSummary

    // 贴底跟随（流式时新记录不断追加）。用户一上手就停，松手时在底部附近才恢复。
    property bool followBottom: true

    // 列表宽度是否正被外层驱动（右栏开合 / 拖右栏宽度；Shell 注入 shell.contentWidthDriven）。
    // 与 MessageListView 同一语义：动画期间宽度持续在变，旧行为是每次变化都重排后立刻 pinToBottom
    // （一次 positionViewAtIndex = 多一整趟布局）；动画期间只欠着，落定补一次。
    property bool widthAnimating: false

    function scheduleFollowPin() {
        if (followBottom && !widthAnimating)
            Qt.callLater(pinToBottom)
    }
    onWidthAnimatingChanged: if (!widthAnimating && followBottom) Qt.callLater(pinToBottom)

    function pinToBottom() {
        if (list.width <= 0 || list.height <= 0 || list.count <= 0)
            return
        list.positionViewAtIndex(list.count - 1, ListView.End)
    }

    function selectRow(index, detail, meta) {
        if (selectedIndex === index) {
            selectedIndex = -1
            return
        }
        selectedDetail = detail
        selectedMeta = meta
        selectedIndex = index
    }

    // ---- 顶部概览：轮次 / 步骤 / 工具 / token / 时长 ----
    Item {
        id: summaryBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.spacing.cardPadding
        anchors.rightMargin: Theme.spacing.cardPadding
        height: 26

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing.sectionGap

            Repeater {
                model: [
                    { label: qsTrId("trace_summary_turns"), value: String(traceRoot.summary.turns || 0) },
                    { label: qsTrId("trace_summary_steps"), value: String(traceRoot.summary.steps || 0) },
                    { label: qsTrId("trace_summary_tools"), value: String(traceRoot.summary.tools || 0) },
                    { label: qsTrId("trace_summary_tokens"), value: "↑" + (traceRoot.summary.inputTokens || 0) + " ↓" + (traceRoot.summary.outputTokens || 0) },
                    { label: qsTrId("trace_summary_time"), value: (traceRoot.summary.durationText || "—") }
                ]

                delegate: Row {
                    required property var modelData
                    spacing: 4

                    Text {
                        text: modelData.label
                        color: Theme.color.textCaption
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }
                    Text {
                        text: modelData.value
                        color: Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        font.weight: Theme.font.weightMedium
                    }
                }
            }
        }

        // 有失败记录时把数字点出来（官方也有这条错误计数）
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: (traceRoot.summary.errors || 0) > 0
            text: qsTrId("trace_summary_errors").arg(traceRoot.summary.errors || 0)
            color: Theme.color.danger
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.small
        }
    }

    // ---- 记录表 ----
    ListView {
        id: list

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: summaryBar.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: Hub.trace
        spacing: 1
        cacheBuffer: 900
        ScrollBar.vertical: AppScrollBar { }
        topMargin: 2
        bottomMargin: Theme.spacing.labelGap
        leftMargin: Theme.spacing.cardPadding
        rightMargin: Theme.spacing.cardPadding

        onMovementStarted: {
            traceRoot.followBottom = false
            // 往回翻时暂停追加：否则每条新事件都整表重建，滚动位置会被打回去
            Hub.setTracePaused(true)
        }
        onMovementEnded: {
            if (list.height <= 0)
                return
            const atBottom = (list.contentY >= list.contentHeight - list.height - 48)
            traceRoot.followBottom = atBottom
            // 回到贴近底部才恢复跟随并补投影（暂停期间的事件已经收进模型，不会丢）
            Hub.setTracePaused(!atBottom)
        }
        onContentHeightChanged: traceRoot.scheduleFollowPin()
        onCountChanged: traceRoot.scheduleFollowPin()
        onHeightChanged: traceRoot.scheduleFollowPin()

        delegate: Item {
            id: rowRoot

            required property int index
            required property int seq
            required property string kind
            required property string timeText
            required property string title
            required property string subtitle
            required property string detail
            required property string durationText
            required property string tokensText
            required property string statusKind
            required property string statusText
            required property bool live

            readonly property bool isTurn: kind === "turn"
            readonly property bool isStep: kind === "step"
            readonly property bool selected: traceRoot.selectedIndex === index

            width: Math.max(160, list.width - list.leftMargin - list.rightMargin)
            height: isTurn ? turnRow.implicitHeight : (isStep ? stepRow.implicitHeight : recordRow.implicitHeight)

            // ---- 轮次分隔 ----
            Rectangle {
                id: turnRow
                visible: rowRoot.isTurn
                width: parent.width
                implicitHeight: turnContent.implicitHeight + 12
                radius: Theme.radius.inner
                color: Theme.color.hoverBg

                Row {
                    id: turnContent
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTrId("trace_turn_n").arg(rowRoot.title)
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        font.weight: Theme.font.weightMedium
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.timeText
                        color: Theme.color.textCaption
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                    Text {
                        visible: rowRoot.durationText.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.durationText
                        color: Theme.color.textSecondary
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                    Text {
                        visible: rowRoot.statusText.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.statusText
                        color: Theme.color.textCaption
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                }
            }

            // ---- 步骤 ----
            Item {
                id: stepRow
                visible: rowRoot.isStep
                width: parent.width
                implicitHeight: 20

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Rectangle {
                        width: 3
                        height: 10
                        radius: 1.5
                        color: Theme.color.border
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTrId("trace_step_n").arg(rowRoot.title)
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.durationText
                        color: Theme.color.textCaption
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                }
            }

            // ---- 普通记录 ----
            Rectangle {
                id: recordRow
                visible: !rowRoot.isTurn && !rowRoot.isStep
                width: parent.width
                implicitHeight: Math.max(kindBadge.height, rightChips.height, textColumn.implicitHeight) + 12
                radius: Theme.radius.inner
                color: rowRoot.selected ? Theme.color.activeBg
                    : (rowMouse.containsMouse ? Theme.color.hoverBg : "transparent")

                Rectangle {
                    id: kindBadge
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    width: kindLabel.implicitWidth + 10
                    height: 16
                    radius: 4
                    color: traceRoot.kindTint(rowRoot.kind, rowRoot.statusKind)

                    Text {
                        id: kindLabel
                        anchors.centerIn: parent
                        text: traceRoot.kindLabel(rowRoot.kind)
                        color: traceRoot.kindTextColor(rowRoot.kind)
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }
                }

                // 右侧指标：从右往左锚，宽度由内容决定；中间那列吃掉剩下的
                Row {
                    id: rightChips
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.top: parent.top
                    anchors.topMargin: 7
                    spacing: 6

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.tokensText
                        visible: rowRoot.tokensText.length > 0
                        color: Theme.color.textCaption
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.durationText
                        visible: rowRoot.durationText.length > 0
                        color: Theme.color.textSecondary
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                    // 状态灯：进行中=强调色，失败/警告=危险色，成功=第三档文字色（不抢眼）
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        visible: rowRoot.statusKind.length > 0
                        color: (rowRoot.statusKind === "error" || rowRoot.statusKind === "warn")
                            ? Theme.color.danger
                            : (rowRoot.statusKind === "running" ? Theme.color.accent : Theme.color.textCaption)
                        opacity: rowRoot.live ? 0.55 : 1
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowRoot.statusKind === "running" ? qsTrId("trace_status_running") : rowRoot.statusText
                        visible: text.length > 0
                        color: (rowRoot.statusKind === "error" || rowRoot.statusKind === "warn")
                            ? Theme.color.danger : Theme.color.textCaption
                        font.family: "Cascadia Mono"
                        font.pixelSize: Theme.font.small
                    }
                }

                Column {
                    id: textColumn
                    anchors.left: kindBadge.right
                    anchors.leftMargin: 8
                    anchors.right: rightChips.left
                    anchors.rightMargin: 8
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    spacing: 2

                    Text {
                        width: parent.width
                        text: rowRoot.title.length > 0 ? rowRoot.title : rowRoot.subtitle
                        color: Theme.color.textPrimary
                        font.family: rowRoot.kind === "tool" ? "Cascadia Mono" : "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                    Text {
                        width: parent.width
                        visible: rowRoot.title.length > 0 && rowRoot.subtitle.length > 0
                        text: rowRoot.subtitle
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: traceRoot.selectRow(
                        rowRoot.index,
                        rowRoot.detail,
                        rowRoot.timeText + "  ·  seq " + rowRoot.seq
                            + (rowRoot.durationText ? "  ·  " + rowRoot.durationText : ""))
                }
            }
        }

        // ---- 空态 ----
        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTrId("trace_empty")
            color: Theme.color.textCaption
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }

    // ---- 检查面板：某条记录的原文 ----
    // 浮在右侧（不挤占记录表宽度 ⇒ 打开/关闭不会让行重排、滚动位置也不会跳）
    Rectangle {
        id: detailPane

        visible: traceRoot.selectedIndex >= 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacing.cardPadding
        anchors.topMargin: Theme.spacing.cardPadding
        anchors.bottomMargin: Theme.spacing.cardPadding
        width: Math.min(460, Math.max(300, traceRoot.width * 0.46))
        radius: Theme.radius.card
        color: Theme.color.cardBg
        border.width: 0.5
        border.color: Theme.color.border

        // 吃掉点击与滚轮：否则会穿透到下面被遮住的那一行（点空白处 ⇒ 选中了看不见的行）
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onClicked: {}
            onWheel: (wheel) => wheel.accepted = true
        }

        Item {
            anchors.fill: parent
            anchors.margins: 12

            Row {
                id: detailHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 22
                spacing: 8

                Text {
                    width: parent.width - closeButton.width - 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("trace_detail_title")
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }

                Item {
                    id: closeButton
                    width: 22
                    height: 22
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: closeMouse.containsMouse ? Theme.color.textPrimary : Theme.color.textTertiary
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: traceRoot.selectedIndex = -1
                    }
                }
            }

            Text {
                id: detailMeta
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: detailHeader.bottom
                anchors.topMargin: 4
                text: traceRoot.selectedMeta
                color: Theme.color.textCaption
                font.family: "Cascadia Mono"
                font.pixelSize: Theme.font.small
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Flickable {
                id: detailScroll

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: detailMeta.bottom
                anchors.topMargin: 8
                anchors.bottom: copyBar.top
                anchors.bottomMargin: 8
                clip: true
                contentWidth: width
                contentHeight: detailText.height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                TextEdit {
                    id: detailText

                    width: detailScroll.width
                    text: traceRoot.selectedDetail
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    color: Theme.color.textSecondary
                    selectionColor: Theme.color.selectionBg
                    selectedTextColor: Theme.color.textPrimary
                    font.family: "Cascadia Mono"
                    font.pixelSize: Theme.font.small
                    // 比视口矮时也要能铺满：contentHeight 取两者的大值由 Flickable 负责
                    height: Math.max(detailScroll.height, contentHeight)
                }
            }

            Item {
                id: copyBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 26

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: copyLabel.implicitWidth + 20
                    height: 26
                    radius: Theme.radius.control
                    color: copyMouse.containsMouse ? Theme.color.hoverBg : "transparent"
                    border.width: 0.5
                    border.color: Theme.color.inputBorder

                    Text {
                        id: copyLabel
                        anchors.centerIn: parent
                        text: qsTrId("trace_copy_json")
                        color: Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }

                    MouseArea {
                        id: copyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            detailText.selectAll()
                            detailText.copy()
                            detailText.deselect()
                        }
                    }
                }
            }
        }
    }

    // ---- 小工具：类型徽标（文案/配色都在 QML，模型只给 kind） ----
    function kindLabel(kind) {
        switch (kind) {
        case "turn": return qsTrId("trace_kind_turn")
        case "step": return qsTrId("trace_kind_step")
        case "user": return qsTrId("trace_kind_user")
        case "assistant": return qsTrId("trace_kind_assistant")
        case "tool": return qsTrId("trace_kind_tool")
        case "context": return qsTrId("trace_kind_context")
        case "system": return qsTrId("trace_kind_system")
        case "compaction": return qsTrId("trace_kind_compaction")
        case "attempt": return qsTrId("trace_kind_attempt")
        case "retry": return qsTrId("trace_kind_retry")
        default: return kind
        }
    }

    function kindTint(kind, statusKind) {
        if (statusKind === "error")
            return Theme.color.dangerBg
        switch (kind) {
        case "user": return Theme.color.userBubbleBg
        case "assistant": return Theme.color.tagBg
        case "tool": return Theme.color.codeBlockBg
        default: return Theme.color.hoverBg
        }
    }

    function kindTextColor(kind) {
        return kind === "user" ? Theme.color.textPrimary : Theme.color.textSecondary
    }
}
