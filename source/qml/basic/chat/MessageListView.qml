// 会话区消息列表：按 kind 分派到用户气泡 / Agent 气泡 / 系统提示。
// 贴底只信已建委托的真实几何：contentHeight 是估值，originY 也可能漂移，直接拿它们定位会跑偏。
// contentY 统一由 minContentY/maxContentY 收口；批量重建后先物化行再贴底。
// 只显示最近一页，更早的由顶部「加载更多」按钮拉取。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/chat"

ListView {
    id: list

    property bool followBottom: true   // 是否跟随最新消息贴底

    // 宽度正被外层动画驱动时，不逐帧贴底，并暂停气泡阴影；由 Shell 的 messageLoader 注入。
    property bool widthAnimating: false

    // 首屏/换会话先隐藏，首次贴底完成后显示；用 opacity 而非 visible，避免 ListView 停止布局。
    property bool awaitingFirstPin: false
    property int lastCount: 0
    opacity: awaitingFirstPin ? 0 : 1

    model: Hub.messages
    spacing: Theme.spacing.sectionGap
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    readonly property int fullBuildRowLimit: 40   // 一页内全建，超出才虚拟化
    cacheBuffer: count <= fullBuildRowLimit ? 100000 : Math.round(Math.max(600, height) * 2)   // 只设它不会真正全建，还要 pinStep() 配合
    ScrollBar.vertical: AppScrollBar { }   // 不要开 reuseItems：高度差异大时 indexAt() 会失效

    property bool bottomSettling: false   // 结束帧解冻期间持续锁底
    Timer {
        id: bottomSettleTimer
        interval: 220
        repeat: false
        onTriggered: list.bottomSettling = false
    }

    // Qt6 的 QStyleHints.wheelScrollLines 只读，默认滚动太慢，只能自己接管滚轮。
    property real wheelSpeedFactor: 2
    readonly property real wheelStepPx: 60 * wheelSpeedFactor   // 一格滚轮对应的像素

    // 用短动画平滑推进；连滚以 wheelTargetY 累积，形成连续位移而不是多次跳变。
    property real wheelSmoothMs: 110
    property real wheelTargetY: -1

    NumberAnimation {
        id: wheelAnim
        target: list
        property: "contentY"
        duration: list.wheelSmoothMs
        easing.type: Easing.OutCubic
        onStopped: list.wheelTargetY = -1   // 下次滚轮重新以当前 contentY 为起点
    }

    function stopWheelAnim() {
        if (wheelAnim.running)
            wheelAnim.stop()
        wheelTargetY = -1
    }

    // 一次滚轮 → 更新 contentY 与 followBottom，返回实际 dy（0 = 没东西可滚）。
    function wheelScroll(angleY, pixelY) {
        // 只放大 angleDelta；pixelDelta 是触控板给出的真实像素，原样使用
        var dy = (pixelY !== 0) ? pixelY : (angleY / 120) * wheelStepPx
        if (dy === 0)
            return 0

        // 连滚以上一次的目标为起点
        var base = wheelTargetY >= 0 ? wheelTargetY : contentY
        // ⚠️ 本 ListView 不自动夹取 contentY，必须自己收口
        var next = Math.max(minContentY, Math.min(maxContentY, base - dy))

        wheelAnim.stop()        // 打断上一段（onStopped 同步把 wheelTargetY 归 -1）
        wheelTargetY = next     // 再写新目标，供下一次连滚累积
        wheelAnim.from = contentY
        wheelAnim.to = next
        wheelAnim.start()

        // 主动滚轮即退出贴底；滚回真实底部附近才恢复。判据用 pinTargetY，不用含估值的 maxContentY。
        var bottom = pinTargetY >= 0 ? pinTargetY : maxContentY
        followBottom = (next >= bottom - 1)
        return dy
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            if (list.wheelScroll(event.angleDelta.y, event.pixelDelta.y) !== 0)
                event.accepted = true
        }
    }

    topMargin: Theme.spacing.cardPadding
    // 连同 Shell 的两条边距，列表底到输入框顶约 12px；maxContentY 会用到这个值。
    bottomMargin: Theme.spacing.labelGap
    leftMargin: Theme.spacing.cardPadding
    rightMargin: Theme.spacing.cardPadding

    // 已建委托里最靠下的底边（-1 = 一条都没建）；读 y/height 以便行高变化时自动重算。
    readonly property real builtBottom: {
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

    // 已建委托里最大的行号；header 没有 index，按 undefined 跳过。
    readonly property int lastBuiltIndex: {
        var idx = -1
        var kids = contentItem ? contentItem.children : []
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (c && c.visible && c.index !== undefined && c.index > idx)
                idx = c.index
        }
        return idx
    }

    // 已建委托里最小的行号，用于判断是否已从顶部建到末尾
    readonly property int firstBuiltIndex: {
        var idx = -1
        var kids = contentItem ? contentItem.children : []
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (c && c.visible && c.index !== undefined && (idx < 0 || c.index < idx))
                idx = c.index
        }
        return idx
    }

    readonly property bool lastRowBuilt: count > 0 && lastBuiltIndex === count - 1

    // originY 在上方仍有未实例化行时会被 ListView 挪动，下界必须取 min()，不能让它变成正数。
    readonly property real minContentY: Math.min(-topMargin, originY - topMargin)
    readonly property real maxContentY: {
        // ListView 的算法含估值，只当上限兜底
        var flickMax = contentHeight - height + originY + bottomMargin
        var b = builtBottom
        if (b < 0)
            return Math.max(minContentY, flickMax)
        // 优先用已建区域底，绝不滚进未建区域
        return Math.max(minContentY, Math.min(flickMax, b + bottomMargin - height))
    }

    // 上翻前记录内容高，重建后补 contentY，避免当前阅读位置被新插入的历史顶走。
    property real olderAnchorHeight: 0

    function requestOlder() {
        if (!Hub.olderAvailable || Hub.loadingOlder)
            return
        olderAnchorHeight = contentHeight
        Hub.loadOlder()
    }

    function compensateOlder() {
        if (olderAnchorHeight <= 0)
            return
        var delta = contentHeight - olderAnchorHeight
        if (delta > 1)
            contentY += delta
        olderAnchorHeight = 0
    }

    Connections {   // 列表内部逻辑，不登记为可被接管的宿主目标
        target: Hub
        function onOlderPrepended() {
            Qt.callLater(list.compensateOlder)
        }
        function onCurrentSessionChanged() {
            list.followBottom = true   // 换会话必须复位贴底意图
            Qt.callLater(list.pinToBottom)
        }
    }

    // 贴底：positionViewAtIndex 只做粗定位，落点用最后一条委托的真实几何计算；
    // 最后一条未建成时逐帧推进（有上限），落点始终夹在 [minContentY, maxContentY] 内。
    property int pinAttempts: 0
    readonly property int pinRetryLimit: 24   // 约 24 帧
    property bool bulkBuildPending: false   // 批量重建后需要先全量物化
    property bool bulkBuilding: false   // 正在顶部等待委托异步建齐
    property int bulkWait: 0
    property bool pinning: false   // 贴底过程中暂不走兜底夹取
    property bool clampingY: false

    function pinToBottom() {
        stopWheelAnim()     // 贴底优先，先打断滚轮动画
        if (widthAnimating) // 宽度动画期间不贴，落定后由 onWidthAnimatingChanged 补
            return
        if (width <= 0 || height <= 0 || count <= 0)
            return
        pinAttempts = 0
        pinning = true
        pinStep()
    }

    function pinStep() {
        if (widthAnimating) {         // 动画期间不贴底
            pinning = false
            bulkBuilding = false
            bulkBuildPending = false
            return
        }
        if (!followBottom) {          // 用户中途上滚，停手
            pinning = false
            bulkBuilding = false
            bulkBuildPending = false
            awaitingFirstPin = false
            return
        }
        if (width <= 0 || height <= 0 || count <= 0) {
            pinning = false
            bulkBuilding = false
            return
        }

        // 1) 批量重建后先到顶物化这一页，等委托建齐再回到底部；仅对不超过上限的行数执行。
        if (bulkBuildPending) {
            bulkBuildPending = false
            if (count <= fullBuildRowLimit && !lastRowBuilt) {
                bulkBuilding = true
                bulkWait = 0
                positionViewAtIndex(0, ListView.Beginning)
                Qt.callLater(pinStep)
                return
            }
        }
        if (bulkBuilding) {
            ++bulkWait
            var allBuilt = (firstBuiltIndex === 0 && lastBuiltIndex === count - 1)
            if (!allBuilt && bulkWait < 12) {
                Qt.callLater(pinStep)   // ListView 每帧只建一批缓存委托
                return
            }
            bulkBuilding = false
        }

        // 2) 先做一次粗定位，把末尾带进视口并促使 ListView 物化。
        if (!lastRowBuilt && pinAttempts === 0) {
            positionViewAtIndex(count - 1, ListView.End)
            ++pinAttempts
            Qt.callLater(pinStep)
            return
        }

        // 3) 最后一条仍未建：从已建区域底继续向下推进，逼后续行物化。
        if (!lastRowBuilt) {
            var ceiling = Math.max(minContentY, contentHeight - height + originY + bottomMargin)
            var advance = builtBottom + Math.round(height * 0.8) + bottomMargin - height
            if (advance > contentY + 0.5)
                contentY = Math.min(advance, ceiling)
            pinTargetY = contentY
            if (pinAttempts < pinRetryLimit) {
                ++pinAttempts
                Qt.callLater(pinStep)
                return
            }
            // 推不动时退回已建区域底，至少不留空白
            var settle = Math.max(minContentY, builtBottom + bottomMargin - height)
            if (contentY > settle + 0.5)
                contentY = settle
            pinTargetY = contentY
            pinning = false
            awaitingFirstPin = false
            return
        }

        // 4) 最后一条已建，用真实几何钉死。
        var target = maxContentY
        if (Math.abs(contentY - target) > 0.5)
            contentY = target
        pinTargetY = contentY

        pinning = false
        awaitingFirstPin = false   // 首屏隐形到此结束（贴好了 / 已尽力）
    }

    property real pinTargetY: -1   // 贴底目标，-1 = 未知

    // 滚动条拖动、ListView 保持首个可见项等操作可能把 contentY 设到合法区间外，这里统一拉回；
    // 贴底过程中的临时越界由 pinStep 自行收敛。
    onContentYChanged: {
        if (pinning || clampingY || bottomAnchorAdjusting || height <= 0)
            return
        var lo = minContentY
        var hi = maxContentY
        if (contentY > hi + 0.5) {
            clampingY = true
            contentY = hi
            clampingY = false
            pinTargetY = contentY
        } else if (contentY < lo - 0.5) {
            clampingY = true
            contentY = lo
            clampingY = false
        }
    }

    // 用户一上手就停止跟随；松手时按是否仍在贴底位附近决定恢复。不用 atYEnd，它含估值。
    onMovementStarted: {
        list.stopWheelAnim()
        list.followBottom = false
    }
    onMovementEnded: followBottom = pinTargetY >= 0 && Math.abs(contentY - pinTargetY) <= 30

    // 内容一变就贴底。必须用 Qt.callLater 排到本轮布局之后：直接赋值会被 ListView 的
    // 保持可见项调整覆盖，计时器节流在流式输出时又等不到空档。
    function scheduleFollowPin() {
        // pinning 期间不重开一轮，否则 pinAttempts 归零，长会话会卡在缓冲区底。
        if (pinning || !followBottom || widthAnimating)
            return
        Qt.callLater(pinToBottom)
    }
    onContentHeightChanged: {
        if (bottomSettling && followBottom)
            bottomSettleTimer.restart()
        if (!followBottom && viewportAnchorIndex >= 0 && (widthAnimating || readingSettling)) {
            // 回收/展开都逐帧按回锚定行，避免只在动画结束时结算一次。
            restoreViewportAnchor(true)
            if (readingSettling)
                readingSettleTimer.restart()
        }
        // 贴底且宽度正在变化时，先用新 contentHeight 同步写一次底部，避免多一帧旧位置。
        if ((widthAnimating || bottomSettling) && followBottom && lastRowBuilt) {
            const bottomTarget = Math.max(minContentY,
                contentHeight - height + originY + bottomMargin)
            if (Math.abs(contentY - bottomTarget) > 0.5) {
                bottomAnchorAdjusting = true
                contentY = bottomTarget
                bottomAnchorAdjusting = false
            }
        }
        scheduleFollowPin()
        if (followBottom) {
            // 内容缩小时 contentY 可能停在旧底部之外，而它本身没变 ⇒ 不会触发 onContentYChanged
            if (!pinning && contentY > maxContentY + 0.5) {
                bottomAnchorAdjusting = true
                contentY = Math.max(minContentY, maxContentY)
                bottomAnchorAdjusting = false
            }
            scheduleBottomViewport()
        }
    }
    // 首帧列表尺寸为 0 时 pinToBottom() 会直接返回，尺寸可用后必须补一次。
    onHeightChanged: {
        scheduleFollowPin()
        if (followBottom)
            scheduleBottomViewport()
    }
    onWidthChanged: {
        scheduleFollowPin()   // 不在这里抢写 contentY，等重排后的 contentHeight 校正
    }

    // 宽度变化会让每条消息重新换行，记下视口顶部所在行与偏移，落定后按同一行恢复。
    property int viewportAnchorIndex: -1
    property real viewportAnchorOffset: 0
    property bool readingSettling: false
    function rememberViewportAnchor() {
        viewportAnchorIndex = -1
        viewportAnchorOffset = 0
        if (followBottom || count <= 0 || height <= 0)
            return
        var top = contentY
        var bestIndex = -1
        var bestOffset = 0
        var bestDistance = Number.MAX_VALUE
        var kids = contentItem ? contentItem.children : []
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (!c || !c.visible || c.index === undefined)
                continue
            if (c.y + c.height <= top)
                continue
            var distance = Math.abs(c.y - top)
            if (distance < bestDistance) {
                bestDistance = distance
                bestIndex = c.index
                bestOffset = c.y - top
            }
        }
        if (bestIndex >= 0) {
            viewportAnchorIndex = bestIndex
            viewportAnchorOffset = bestOffset
        }
    }

    function restoreViewportAnchor(preserveAnchor) {
        if (viewportAnchorIndex < 0 || followBottom)
            return
        var item = itemAtIndex(viewportAnchorIndex)
        if (!item) {
            viewportAnchorIndex = -1
            return
        }
        var target = item.y - viewportAnchorOffset   // offset = item.y - contentY
        target = Math.max(minContentY, Math.min(maxContentY, target))
        if (Math.abs(contentY - target) > 0.5) {
            bottomAnchorAdjusting = true
            contentY = target
            bottomAnchorAdjusting = false
        }
        if (!preserveAnchor)
            viewportAnchorIndex = -1
    }

    Timer {
        id: readingSettleTimer
        interval: 220
        repeat: false
        onTriggered: if (list.readingSettling) {
            list.readingSettling = false
            list.restoreViewportAnchor(false)
        }
    }

    // 等这一轮布局结束后再校底部；同步写时最后一条委托的 y/height 还是旧值，会先 overshoot。
    property bool bottomAnchorScheduled: false
    function scheduleBottomViewport(force) {
        if (!followBottom || (!widthAnimating && !force))
            return
        if (bottomAnchorScheduled)
            return
        bottomAnchorScheduled = true
        Qt.callLater(function () {
            bottomAnchorScheduled = false
            if (!followBottom)
                return
            // 优先用最后一条委托的实际底边，maxContentY 在宽度动画里可能少算一行
            var last = itemAtIndex(count - 1)
            var target = last
                ? last.y + last.height + bottomMargin - height
                : Math.max(minContentY, maxContentY)
            target = Math.max(minContentY, target)
            if (Math.abs(contentY - target) <= 0.5)
                return
            bottomAnchorAdjusting = true   // 防重入：写 contentY 会触发夹取与布局调整
            contentY = target
            bottomAnchorAdjusting = false
        })
    }

    property bool bottomAnchorAdjusting: false

    // 宽度动画或结束解冻时直接把 contentY 锁到底部，避免 contentHeight 更新后多一帧空窗；
    // 正常滚动和不在底部时不生效。
    Binding {
        target: list
        property: "contentY"
        when: (list.widthAnimating || list.bottomSettling) && list.followBottom
        value: Math.max(list.minContentY, list.maxContentY)
        restoreMode: Binding.RestoreNone
    }

    onWidthAnimatingChanged: {
        if (widthAnimating) {
            bottomSettling = false
            readingSettling = false
            if (!followBottom)
                rememberViewportAnchor()
        } else {
            bottomSettling = followBottom
            if (bottomSettling)
                bottomSettleTimer.restart()
            else if (viewportAnchorIndex >= 0) {
                readingSettling = true
                restoreViewportAnchor(true)
                readingSettleTimer.restart()
            }
        }
        if (!widthAnimating && followBottom) {
            if (!pinning && contentY > maxContentY + 0.5) {
                bottomAnchorAdjusting = true
                contentY = Math.max(minContentY, maxContentY)
                bottomAnchorAdjusting = false
            }
            scheduleBottomViewport(true)
            if (!lastRowBuilt)
                Qt.callLater(pinToBottom)
        }
    }
    // 从空到有时先隐藏，等首次贴底完成再显示，避免首帧停在顶部；流式追加不隐藏。
    onCountChanged: {
        var wasEmpty = (lastCount === 0)
        lastCount = count
        if (count <= 0) {
            awaitingFirstPin = false
            pinTargetY = -1
            return
        }
        if (!followBottom) {
            awaitingFirstPin = false
            return
        }
        if (wasEmpty) {
            awaitingFirstPin = true
            bulkBuildPending = true
            // 整表重建会同步连发多次 countChanged，等这一轮结束再贴，避免逐行定位。
            Qt.callLater(list.pinToBottom)
        } else {
            scheduleFollowPin()
        }
    }

    // 列表带着已有消息创建时 countChanged 不会补发，这里主动批量贴底一次。
    Component.onCompleted: {
        if (count > 0 && followBottom) {
            awaitingFirstPin = true
            bulkBuildPending = count <= fullBuildRowLimit
            lastCount = count
            Qt.callLater(pinToBottom)
        }
    }

    // 安全网：贴底卡住时也要把列表显示出来，宁可停在中间也不能一直隐形。
    Timer {
        interval: 700
        running: list.awaitingFirstPin
        onTriggered: {
            list.awaitingFirstPin = false
            list.pinToBottom()
        }
    }

    // 顶部「加载更多」：只显示最近一页，不自动翻页；更早的内容由用户显式拉取。
    header: Item {
        id: olderHeader
        width: list.width
        height: (Hub.olderAvailable || Hub.loadingOlder) ? 44 : 0   // header 高也会计入 contentHeight
        Rectangle {
            anchors.centerIn: parent
            // ⚠️ header 高度归零不会裁剪子项，胶囊中心会落在 content y=0 并压住第一条消息。
            visible: olderHeader.height > 0
            width: 132
            height: 30
            radius: Theme.radius.panel
            color: olderClick.hovered ? Theme.color.hoverBg : Theme.color.panelBg
            border.width: 0.5
            border.color: Theme.color.inputBorder   // 可点胶囊用更深的描边，4% 太浅

            Text {
                anchors.centerIn: parent
                text: Hub.loadingOlder ? qsTrId("common_loading") : qsTrId("common_load_more")   // 不硬编码中文
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            MouseArea {
                id: olderClick
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: !Hub.loadingOlder
                onClicked: list.requestOlder()
            }
        }
    }
    delegate: Item {
        id: rowRoot
        required property string kind
        required property string text
        required property var segments
        required property var images
        required property bool streaming
        required property int index

        // 挂在委托根上供气泡按 parent 读取；旧风格包没有该属性时视为 false，不会报 TypeError。
        property bool widthAnimating: list.widthAnimating
        // 宽度动画期间只冻结视口外的行，动画结束后统一恢复，展开/收回共用。
        property real frozenWidth: 0
        readonly property bool offscreen: rowRoot.y + rowRoot.height <= list.contentY
            || rowRoot.y >= list.contentY + list.height
        onWidthAnimatingChanged: {
            if (widthAnimating)
                frozenWidth = offscreen
                    ? Math.max(120, list.width - list.leftMargin - list.rightMargin)
                    : 0
            else
                frozenWidth = 0
        }
        // 宽度动画期间只冻结视口外的行，动画结束后统一恢复；展开和收回用同一套，
        // 这样滚动条长度与视口锚定在两种动画里表现一致。
        // 首帧 list.width 可能为 0，120 下限避免 Text 逐字换行把行高撑爆。
        width: (widthAnimating && offscreen && frozenWidth > 0)
            ? frozenWidth
            : Math.max(120, list.width - list.leftMargin - list.rightMargin)
        implicitHeight: (rowRoot.kind === "user")
            ? userBubble.implicitHeight
            : ((rowRoot.kind === "system") ? systemNotice.contentHeight : agentBubble.implicitHeight)

        UserBubble {
            id: userBubble
            visible: rowRoot.kind === "user"
            anchors.left: parent.left
            anchors.right: parent.right
            text: rowRoot.text
            images: rowRoot.images
        }
        SystemNotice {
            id: systemNotice
            visible: rowRoot.kind === "system"
            anchors.left: parent.left
            anchors.right: parent.right
            text: rowRoot.text
        }
        AgentBubble {
            id: agentBubble
            visible: rowRoot.kind === "agent"
            anchors.left: parent.left
            anchors.right: parent.right
            segments: rowRoot.segments
            // 行级标志优先；控制器仍在跑且这是最后一条 agent 时也视为流式。
            streaming: rowRoot.streaming
                || (Hub.streaming && rowRoot.kind === "agent" && rowRoot.index === list.count - 1)
        }
    }
}
