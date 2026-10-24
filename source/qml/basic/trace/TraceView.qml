// 轨迹视图（对照官方 @deepseek-ai/dsh-client-ui-trajectory）：按轮次/步骤分组的记录表 +
// 每条的耗时/TTFT/token 用量 + 点开看原文 JSON（相当于官方 Inspect 压成一份原文）。
// 刻意不做：时间轴拖拽缩放、搜索过滤、分页窗口 —— 记录表本身在 ListView 里，几千行也只是滚动。
// 数据面是 Hub.trace（common/session/TraceModel）：与消息列表吃同一条 session/follow 流，
// 所以"对话"和"轨迹"永远同一份账本，不会两边对不上。
// ⚠️ 记录行"左侧徽标 + 中间两行文字 + 右侧指标"不要用 Row 硬算宽度（Row 的 width 由子项决定，
// 给子项写 parent.width - xxx 会形成绑定环）；一律用锚定，中间那列吃掉剩余宽度。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: traceRoot

    property int selectedIndex: -1   // 当前打开检查面板的行（-1 = 收起）
    property string selectedDetail: ""   // 选中时把这一行的角色值抄一份出来 —— 检查面板不用再按 index 回查模型（省掉角色编号魔法值）
    property string selectedMeta: ""

    readonly property var summary: Hub.traceSummary

    property bool followBottom: true

    // 列表宽度是否正被外层驱动（右栏开合 / 拖右栏宽度；Shell 注入 shell.contentWidthDriven）。
    // 与 MessageListView 同一语义：动画期间宽度持续在变，旧行为是每次变化都重排后立刻 pinToBottom
    // （一次定位 = 多一整趟布局）；动画期间只欠着，落定补一次。
    property bool widthAnimating: false

    // 跟随状态只有一个入口：跟随 ⇄ 不跟随的同时把"暂停投影"一起切。
    // 不跟随时新事件只记账不建行（Hub.setTracePaused）—— 否则表还在长、行号还在变，
    // 用户正在检查的那一条会被挤走。
    function setFollow(on) {
        if (followBottom === on)
            return
        followBottom = on
        Hub.setTracePaused(!on)
    }

    function scheduleFollowPin() {
        if (followBottom && !widthAnimating)
            Qt.callLater(list.pinBottom)
    }
    onWidthAnimatingChanged: if (!widthAnimating) scheduleFollowPin()

    function pinToBottom() {
        list.pinBottom()
    }

    Connections {   // 换会话：账本整份换掉，跟随意图与贴底目标都得作废（否则新会话停在上一份的坐标上）
        target: Hub
        function onCurrentSessionChanged() {
            traceRoot.followBottom = true
            Hub.setTracePaused(false)
            list.pinAttempts = 0
            list.pinTargetY = -1
            Qt.callLater(traceRoot.pinToBottom)
        }
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

        Text {   // 有失败记录时把数字点出来（官方也有这条错误计数）
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: (traceRoot.summary.errors || 0) > 0
            text: qsTrId("trace_summary_errors").arg(traceRoot.summary.errors || 0)
            color: Theme.color.danger
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.small
        }
    }

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
        readonly property int fullBuildRowLimit: 40   // 与消息列表同一取舍：一屏之内全建（contentHeight 才是真值 ⇒ 滚动条长度不跳），超了才虚拟化
        cacheBuffer: count <= fullBuildRowLimit ? 100000 : Math.round(Math.max(600, height) * 2)
        ScrollBar.vertical: AppScrollBar { }
        topMargin: 2
        bottomMargin: Theme.spacing.labelGap
        leftMargin: Theme.spacing.cardPadding
        rightMargin: Theme.spacing.cardPadding

        readonly property real builtBottom: {   // 已建委托里最靠下的底边（内容坐标；-1 = 一条都没建）
            var b = -1
            var kids = contentItem ? contentItem.children : []
            for (var i = 0; i < kids.length; ++i) {
                var c = kids[i]
                if (!c || !c.visible)
                    continue
                if (c.y + c.height > b)
                    b = c.y + c.height
            }
            return b
        }

        readonly property int lastBuiltIndex: {   // 已建委托里最大的行号（contentItem 里只有委托带 index）
            var idx = -1
            var kids = contentItem ? contentItem.children : []
            for (var i = 0; i < kids.length; ++i) {
                var c = kids[i]
                if (c && c.visible && c.index !== undefined && c.index > idx)
                    idx = c.index
            }
            return idx
        }

        readonly property bool lastRowBuilt: count > 0 && lastBuiltIndex === count - 1   // 最后一条已实例化 ⇒ builtBottom 才是整表内容底；否则只是"缓冲区底"

        // ListView 自己的合法下界；originY 在"上方还有未建行"时会被挪动，取 min() 不让它变正数
        readonly property real minContentY: Math.min(-topMargin, originY - topMargin)

        readonly property real maxContentY: {   // 合法上界：最后一条已建 ⇒ 精确；否则先用"已建区域底"兜住，绝不滚进未建区域（那片是估值）
            var flickMax = contentHeight - height + originY + bottomMargin
            var b = builtBottom
            if (b < 0)
                return Math.max(minContentY, flickMax)
            return Math.max(minContentY, Math.min(flickMax, b + bottomMargin - height))
        }

        property real pinTargetY: -1         // 上一次贴底落点（真实底部；-1 = 还不知道）
        property bool pinning: false         // 贴底过程中：此时 contentY 的变化不算"用户滚动"
        property bool clampingY: false
        property int pinAttempts: 0
        readonly property int pinRetryLimit: 12

        function pinBottom() {
            if (widthAnimating)          // 动画期间不每帧贴底，落定后 onWidthAnimatingChanged 补
                return
            if (width <= 0 || height <= 0 || count <= 0)
                return
            if (!traceRoot.followBottom)
                return
            stopWheelAnim()              // 贴底优先级最高
            pinAttempts = 0
            pinning = true
            advancePin()
        }

        function advancePin() {   // 逐帧逼近末尾：末尾还没实例化时先请 ListView 朝末尾走一趟，再按"已建底"往下推
            if (!traceRoot.followBottom || widthAnimating) {
                pinning = false
                return
            }
            if (lastRowBuilt) {
                var target = maxContentY
                if (Math.abs(contentY - target) > 0.5)
                    contentY = target
                pinTargetY = contentY
                pinning = false
                return
            }
            if (pinAttempts === 0) {
                positionViewAtIndex(count - 1, ListView.End)
            } else {
                var ceiling = Math.max(minContentY, contentHeight - height + originY + bottomMargin)
                var advance = builtBottom + Math.round(height * 0.8) + bottomMargin - height
                if (advance > contentY + 0.5)
                    contentY = Math.min(advance, ceiling)
            }
            pinTargetY = contentY
            if (++pinAttempts <= pinRetryLimit) {
                Qt.callLater(advancePin)
                return
            }
            var settle = Math.max(minContentY, builtBottom + bottomMargin - height)   // 推不动了（委托异常）：退回已建区域底，至少不许留在空白里
            if (contentY > settle + 0.5)
                contentY = settle
            pinTargetY = contentY
            pinning = false
        }

        readonly property real wheelStepPx: 120
        property real wheelTargetY: -1

        NumberAnimation {
            id: wheelAnim
            target: list
            property: "contentY"
            duration: 110
            easing.type: Easing.OutCubic
            onStopped: list.wheelTargetY = -1
        }

        function stopWheelAnim() {
            if (wheelAnim.running)
                wheelAnim.stop()
            wheelTargetY = -1
        }

        function wheelScroll(angleY, pixelY) {   // 一次滚轮 → 返回实际采用的 dy（0 = 没东西可滚）
            var dy = (pixelY !== 0) ? pixelY : (angleY / 120) * wheelStepPx   // 只放大 angleDelta；pixelDelta 是触控板的真实像素，放大反而会飘
            if (dy === 0)
                return 0

            var base = wheelTargetY >= 0 ? wheelTargetY : contentY
            var lo = minContentY
            var hi = Math.max(maxContentY, contentY)   // 不许把自己往回拉（滚动途中上界会滞后）
            var next = Math.max(lo, Math.min(hi, base - dy))

            wheelAnim.stop()        // 打断上一段（onStopped 会把 wheelTargetY 归 -1）
            wheelTargetY = next
            wheelAnim.from = contentY
            wheelAnim.to = next
            wheelAnim.start()

            var bottom = pinTargetY >= 0 ? pinTargetY : (contentHeight - height + originY + bottomMargin)
            traceRoot.setFollow(next >= bottom - 24)
            return dy
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function (event) {
                if (list.wheelScroll(event.angleDelta.y, event.pixelDelta.y) !== 0)
                    event.accepted = true
            }
        }

        onMovementStarted: {   // 用户一上手就停跟随。拖滚动条不发这个信号，但拖拽本身是 movement ⇒ 两路都覆盖了
            stopWheelAnim()          // 拖动优先于滚轮动画，别两股力打架
            traceRoot.setFollow(false)
        }

        onMovementEnded: {
            var bottom = pinTargetY >= 0 ? pinTargetY : (contentHeight - height + originY + bottomMargin)
            if (height > 0 && contentY >= bottom - 24)
                traceRoot.setFollow(true)
        }

        // 所有"用户滚动"（滚轮 / 触摸拖动 / 拖滚动条把手）都会走到这里 —— contentY 变了。
        // 自己贴底（pinning）与夹取（clampingY）引起的变化要跳过，否则会把跟随状态判反。
        onContentYChanged: {
            if (pinning || clampingY || widthAnimating || height <= 0 || count <= 0)
                return

            var flickMax = contentHeight - height + originY + bottomMargin
            if (lastRowBuilt && contentY > maxContentY + 0.5) {
                clampingY = true
                contentY = maxContentY
                clampingY = false
                pinTargetY = contentY
            }
            if (contentY < minContentY - 0.5) {
                clampingY = true
                contentY = minContentY
                clampingY = false
            }

            var bottom = pinTargetY >= 0 ? pinTargetY : flickMax   // 拖到底时 contentY 会受估值影响略过头，所以判据带 24px 容差；真的到底了就恢复跟随
            traceRoot.setFollow(contentY >= bottom - 24)
        }

        onContentHeightChanged: {
            traceRoot.scheduleFollowPin()

            // 内容变矮时 contentY 可能停在旧底部之外（contentY 本身没变 ⇒ onContentYChanged 不会兜底）
            if (traceRoot.followBottom && !pinning && contentY > maxContentY + 0.5) {
                clampingY = true
                contentY = Math.max(minContentY, maxContentY)
                clampingY = false
            }
        }
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

                Row {   // 右侧指标：从右往左锚，宽度由内容决定；中间那列吃掉剩下的
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
                    Rectangle {   // 状态灯：进行中=强调色，失败/警告=危险色，成功=第三档文字色（不抢眼）
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

        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTrId("trace_empty")
            color: Theme.color.textCaption
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }

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

        MouseArea {   // 吃掉点击与滚轮：否则会穿透到下面被遮住的那一行（点空白处 ⇒ 选中了看不见的行）
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
                    height: Math.max(detailScroll.height, contentHeight)   // 比视口矮时也要能铺满：contentHeight 取两者的大值由 Flickable 负责
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
