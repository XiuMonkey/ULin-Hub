// 会话区消息列表：一行一条消息，delegate 按 kind 分派到用户气泡 / Agent 气泡 / 系统提示。
// 模型是 Hub.messages（见 include/common/session/MessageListModel.h）。滚动上踩过的坑：
//   1) 不用 positionViewAtEnd()/contentHeight 贴底：vertical ListView 的 contentHeight 是**估值**
//      （Qt 文档：“only calculates (estimates) the contentHeight”）。未实例化的行按已建委托的
//      平均高度补，而本列表行高差异极大（24px 折叠行 ↔ 上千 px 工具卡），实测 28 条只建 17 条时
//      contentHeight=3169、最后一条已建委托的真实底只有 2304 —— 多算 865px。
//      历史上“contentY 被顶到 69412、内容只有 3229”就是这个估值造成的。
//   2) 合法 contentY 区间 = [originY - topMargin, 实测内容底 + bottomMargin - height]。
//      originY 是 ListView 的内容原点（有 header 时 = -headerHeight；实测 StopAtBounds 的
//      下夹取正是 originY - topMargin）。**不能**用 contentHeight-height：它把 header 也算了进去，
//      且含估值（实测多出 header 44 + bottomMargin 8）。
//   3) 贴底跟随不加计时器节流（内容一变就贴）；用户一上手就停跟随，松手按是否在底部决定恢复。
//   4) 委托高度不均 ⇒ cacheBuffer 必须按行数自适应；并且**批量重建后要先把 ≤fullBuildRowLimit
//      的行全部物化一次**，否则 ListView 一直用平均高度补没建的行，contentHeight 与滚动条长度
//      都会长期偏离真值（实测 scrollbar size 0.194 vs 真实 0.219）。
//   5) 不"滚到顶自动翻页"：只显示最近一页，更早的由顶部「加载更多」按钮显式拉。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/chat"

ListView {
    id: list

    // 贴底跟随的"意图位"（旧壳 m_followBottom 同义）
    property bool followBottom: true

    // 列表宽度是否正被外层动画驱动（右栏开合 / 拖右栏宽度时 contentColumn 的右边缘持续在变
    // ⇒ 本列表宽度跟着变；开合按 15px 分步，拖宽 1:1）。true 期间不做"每次变宽就贴底"，并把每个
    // 可见气泡的阴影按停（宽度一变，阴影的离屏纹理就要重做一张）。由 Shell 的 messageLoader 注入
    // （见 Shell.qml），动画落定后补贴底。
    property bool widthAnimating: false

    // 首屏/换会话先隐形，等首次贴底做完再显回来（否则会看到"先在顶部、下一帧跳到底部"）。
    // ⚠️ 用 opacity 而非 visible：visible=false 会让 ListView 停止布局，贴底反而失效。
    property bool awaitingFirstPin: false
    property int lastCount: 0
    opacity: awaitingFirstPin ? 0 : 1

    model: Hub.messages
    spacing: Theme.spacing.sectionGap
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // cacheBuffer 按行数自适应：一页之内全建（contentHeight 才是真值 ⇒ 滚动条不跳、贴底准），超了才虚拟化。
    // ⚠️ 只设 cacheBuffer **并不会真的全建**：实测 28 条贴底时只建了 17 条（ListView 的缓存委托是
    //    异步建的，跳到底部后上方那些不会再补）。所以批量重建后还要 pinStep() 里的“全量物化”配合。
    // 全建的代价线性放大：94 个气泡全建实测 CPU 12.7s / 内存峰值 807MB / 窗口未响应。
    readonly property int fullBuildRowLimit: 40
    cacheBuffer: count <= fullBuildRowLimit ? 100000 : Math.round(Math.max(600, height) * 2)
    // 不要开 reuseItems：委托高度差异大时实测"委托都建出来了、indexAt() 却全返回 -1"，视图停在空白处
    ScrollBar.vertical: AppScrollBar { }

    // 滚轮灵敏度（2026-10-20 用户反馈"有点低"）。
    // 默认行为：Qt 的 Flickable 每格 angleDelta 120 只滚「系统 wheelScrollLines(通常 3) × 行高」≈ 60px，
    // 而 `QStyleHints::wheelScrollLines` 在 Qt6 是**只读属性**（qstylehints.h 只有 READ + NOTIFY），
    // C++ 改不了 ⇒ 只能自己接管滚轮。
    property real wheelSpeedFactor: 2

    // 一格滚轮（angleDelta = ±120）对应的滚动像素：60 × factor。默认 120px（原生 60px）。
    readonly property real wheelStepPx: 60 * wheelSpeedFactor

    // ---- 平滑推进（2026-10-20 第二轮：用户反馈"一段一段的、不平滑"）----
    // 直接 `contentY = next` 是**逐格跳变**，一格 120px 的跳比原生 60px 更扎眼。
    // 改成用一个短动画滑过去；连滚时 `wheelTargetY` 累积（新目标以**上一次的终点**为起点），
    // 所以快速连滚是"一段连续的位移"，不是多次跳。
    property real wheelSmoothMs: 110
    property real wheelTargetY: -1

    NumberAnimation {
        id: wheelAnim
        target: list
        property: "contentY"
        duration: list.wheelSmoothMs
        easing.type: Easing.OutCubic
        // 自然跑完（或被人为 stop）都清目标：下一次滚轮重新以当前 contentY 为起点
        onStopped: list.wheelTargetY = -1
    }

    // 拖动 / 贴底要抢在动画前面：直接掐掉，别让内容被两股力同时推
    function stopWheelAnim() {
        if (wheelAnim.running)
            wheelAnim.stop()
        wheelTargetY = -1
    }

    // 一次滚轮 → 更新 contentY 与 followBottom；返回实际采用的 dy（0 = 没东西可滚）。
    // 正 dy = 内容向上走（看更早的内容）。
    // ⚠️ 抽成函数是为了能在离屏测试台里直接断言数值：wheel 事件在 offscreen 平台压根送不到
    //    （`mouseWheel()` 注入后 handler 一次都不触发，QTest 的 wheel 事件没有窗口接收者），
    //    见 build/temp/wheel-harness。逻辑全在这一个函数里，onWheel 只做参数搬运。
    function wheelScroll(angleY, pixelY) {
        // 只放大 angleDelta（离散格）；pixelDelta 是触控板/高精度滚轮给的真实像素，原样用 —— 放大会飘
        var dy = (pixelY !== 0) ? pixelY : (angleY / 120) * wheelStepPx
        if (dy === 0)
            return 0

        // 连滚累积：起点是"上一次的目标"，不是动画中的当前像素
        var base = wheelTargetY >= 0 ? wheelTargetY : contentY
        // ⚠️ 必须自己夹紧区间：本 ListView 不夹 contentY（见文件头第 1 条），超界会滚进空白区
        var next = Math.max(minContentY, Math.min(maxContentY, base - dy))

        wheelAnim.stop()        // 打断上一段（onStopped 同步把 wheelTargetY 归 -1）
        wheelTargetY = next     // 再写新目标，供下一次连滚累积
        wheelAnim.from = contentY
        wheelAnim.to = next
        wheelAnim.start()

        // 主动滚轮 = 不想继续贴底（否则流式中会被下一个 chunk 拽回底部）；
        // 真正滚回贴底位时才恢复跟随。判据用 pinTargetY（ListView 自己的贴底位置），
        // 不用 maxContentY —— 后者含 contentHeight 的虚高，比真实底部低 ~800px。
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
    // 底部内容边距：原来与上/左右一样取 cardPadding(16) —— 连同 Shell 里两条 anchor 边距，
    // "列表底 → 输入框顶"合计 40px，看着是一块空地。用户要求收到 ~10px：
    // 这里取 labelGap(8)，Shell 那两条取 panelGap(2) ⇒ 合计 12px。
    // ⚠️ maxContentY（下面）用的就是这个值 ⇒ 改它会同步影响"滚到底"的判定，这是预期的。
    bottomMargin: Theme.spacing.labelGap
    leftMargin: Theme.spacing.cardPadding
    rightMargin: Theme.spacing.cardPadding

    // ---- 真实内容几何（**不要**拿 contentHeight 当内容底，见文件头第 1/2 条）----

    // 已建委托里最靠下的底边（内容坐标；-1 = 一条都没建）。
    // 绑定里逐个读 y/height ⇒ Markdown 量完、折叠展开导致的行高变化会自动重算。
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

    // 已建委托里最大的行号：用来判断“最后一条是否已经建出来”。
    // （contentItem 里只有委托带 index；header 没有，按 undefined 跳过。）
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

    // 已建委托里最小的行号：批量重建后判断“是不是已经从头顶建到末尾了”
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

    // 最后一条已实例化 ⇒ builtBottom 才是整表的内容底；否则它只是“缓冲区底”
    readonly property bool lastRowBuilt: count > 0 && lastBuiltIndex === count - 1

    // 合法 contentY 区间。originY 是 ListView 的内容原点（有 header 时通常是 -headerHeight）。
    // ⚠️ originY 在“上方还有未实例化行”时会被 ListView 挪动，实测出现过 +629 的怪值：
    //    直接拿它算下界（顶部边界）会把顶部行锁在视口外。所以取 min()，绝不让它变成正数。
    readonly property real minContentY: Math.min(-topMargin, originY - topMargin)
    readonly property real maxContentY: {
        // ListView 自己那套（含估值）只当上限兜底：它把 header/margin 的账算得不一样，
        // 有 header 时比真实底部多 headerHeight + bottomMargin。
        var flickMax = contentHeight - height + originY + bottomMargin
        var b = builtBottom
        if (b < 0)
            return Math.max(minContentY, flickMax)
        // 实测底：最后一条已建 ⇒ 精确；否则先用已建区域底兜住，绝不滚进未建区域（那片是估值、不是内容）
        return Math.max(minContentY, Math.min(flickMax, b + bottomMargin - height))
    }

    // 上翻一页前记下内容高，重建后补 contentY 差值（否则正在看的那条会被新插入的历史顶走）
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

    Connections {   // 列表内部逻辑，没有管理价值 ⇒ 不登记（登记等于把 id 暴露成可被接管的目标）
        target: Hub
        function onOlderPrepended() {
            Qt.callLater(list.compensateOlder)
        }

        // 换会话必须复位贴底意图，否则新会话整表重建后停在顶部
        function onCurrentSessionChanged() {
            list.followBottom = true
            Qt.callLater(list.pinToBottom)
        }
    }

    // ---- 贴底（2026-10-20 第三次重做）----
    // 第二次重写把落点交给 positionViewAtIndex(count-1, End)，前提是“ListView 用自己的条目几何”。
    // 但这只在**最后一条已实例化**时成立；没建出来时它照样吃 contentHeight 的估值。实测：
    //   · 62 条：第一次 pin 落在 contentY=3076（只建了缓冲区里 16 行），换一次定位后同一批消息的
    //     底边又出现在 6854 —— 落点随“这轮建了哪几行”漂移一屏多；
    //   · 28 条：另一次 pin 偏到 -60（内容顶上方 60px）。
    // 所以现在：
    //   · positionViewAtIndex 只做“把末尾带进视口、逼 ListView 物化”的粗定位；
    //   · 落点由 pinStep() 用**最后一条委托的 y+height**算，最后一条没建出来就下一帧继续（有上限）；
    //   · 落点永远夹在 [minContentY, maxContentY]，不会越出实测内容。
    property int pinAttempts: 0
    // 每帧最多推进 0.8 屏；24 帧 ≈ 400ms，够走完 12000px 左右的内容（正常会话一屏到底，第 2 步就收敛）。
    readonly property int pinRetryLimit: 24
    // 批量重建（换会话 / 首屏整表）后先做一次“全量物化”：见文件头第 4 条。
    property bool bulkBuildPending: false
    // 正停在顶部等 ListView 把这一页的行全部建出来（buffered delegates 是**异步**建的，
    // 文档原话；只等一帧不够：实测立刻跳回底部时只建了 15/28，顶部仍是估值）。
    property bool bulkBuilding: false
    property int bulkWait: 0
    // 贴底过程 / 夹取自身引起的 contentY 变化不再走兜底夹取（否则会把粗定位立刻拉回去）
    property bool pinning: false
    property bool clampingY: false

    function pinToBottom() {
        stopWheelAnim()     // 贴底优先级最高：滚轮动画还在滑就直接掐掉，别两股力打架
        if (widthAnimating) // 右栏开合/拖宽期间宽度每帧在变：不留欠账，落定后 onWidthAnimatingChanged 补
            return
        if (width <= 0 || height <= 0 || count <= 0)
            return
        pinAttempts = 0
        pinning = true
        pinStep()
    }

    function pinStep() {
        if (widthAnimating) {         // 动画期间不每帧贴底（见 widthAnimating 的注）
            pinning = false
            bulkBuilding = false      // 不做“回到顶部补全量物化”：那会在动画落定后当着用户跳一次
            bulkBuildPending = false
            return
        }
        if (!followBottom) {          // 用户中途上滚：停手（与 onMovementStarted 的语义一致）
            pinning = false
            bulkBuilding = false      // 别把“停在顶部等物化”的状态留给下一次
            bulkBuildPending = false
            awaitingFirstPin = false
            return
        }
        if (width <= 0 || height <= 0 || count <= 0) {
            pinning = false
            bulkBuilding = false
            return
        }

        // 1) 批量重建后的“全量物化”：先到顶（顶部这一屏会把后面整段 cacheBuffer 建出来）。
        //    委托是异步建的，所以要在这里**等它建齐**（firstBuiltIndex==0 且 lastBuiltIndex==count-1），
        //    再回到底部；否则立刻跳回去时顶部仍是估值，contentHeight/滚动条长度都会长期偏。
        //    只对 count ≤ fullBuildRowLimit 做；超了按文件头第 4 条就不全建。
        //    这一步发生在 awaitingFirstPin 的 opacity:0 里，用户看不到。
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
                // 再等一帧（ListView 每帧只建一批缓存委托）
                Qt.callLater(pinStep)
                return
            }
            bulkBuilding = false
        }

        // 2) 最后一条还没建：先用 ListView 的定位语义把末尾带进视口（粗定位，可能带估值误差）。
        //    它只是“让 ListView 朝末尾走一趟”，真正的落点下面用实测几何继续修。
        if (!lastRowBuilt && pinAttempts === 0) {
            positionViewAtIndex(count - 1, ListView.End)
            ++pinAttempts
            Qt.callLater(pinStep)
            return
        }

        // 3) 还没建到最后一条：**从“已建区域底”再往下推 0.8 屏**，逼 ListView 把后面的行物化。
        //    这时 maxContentY 只等于“已建缓冲区底”，原地重算同一个值不会前进；每推一次下一帧
        //    builtBottom 会长，直到最后一条建出来（lastRowBuilt），再走第 4 步用实测几何钉死。
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
            // 推不动了（模型/委托异常）：退回“已建区域底”，至少不许有空白
            var settle = Math.max(minContentY, builtBottom + bottomMargin - height)
            if (contentY > settle + 0.5)
                contentY = settle
            pinTargetY = contentY
            pinning = false
            awaitingFirstPin = false
            return
        }

        // 4) 最后一条已建：用实测几何钉死（maxContentY 此时就是真实内容底 + bottomMargin）。
        var target = maxContentY
        if (Math.abs(contentY - target) > 0.5)
            contentY = target
        pinTargetY = contentY

        pinning = false
        awaitingFirstPin = false   // 首屏隐形到此结束（贴好了 / 已尽力）
    }

    // 贴底时 contentY 的目标值（-1 = 还不知道）
    property real pinTargetY: -1

    // 兜底夹取：附加滚动条拖动、ListView“保持首个可见项”的自我调整、外部赋值，都可能把
    // contentY 设到实测区间之外（最典型：把滚动条拖到底 —— ScrollBar 按 contentHeight 的估值
    // 算，比真实内容底多出 header+margin 甚至一整段估值）。这里统一拉回。
    // 贴底过程（pinning）例外：粗定位需要临时越界去逼 ListView 物化，由 pinStep 自己收敛。
    onContentYChanged: {
        if (pinning || clampingY || height <= 0)
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

    // 用户一上手就停止跟随；松手时按"是不是就在贴底位附近"决定要不要恢复。
    // （不用 atYEnd：它按 contentHeight 算，有估值/header 时贴到底反而是 false）
    // 阈值 30px（原 64）：64 太宽 —— 往上翻一点点也会被算成"还在底部"，松手就被拉回去。
    onMovementStarted: {
        list.stopWheelAnim()   // 用户上手拖：滚轮动画立刻让位
        list.followBottom = false
    }
    onMovementEnded: followBottom = pinTargetY >= 0 && Math.abs(contentY - pinTargetY) <= 30

    // 不做"滚到顶部附近就自动拉上一页"：理由见文件头第 5 条 —— 改成顶部那个显式的「加载更多」按钮了。

    // 内容一变就贴底。这里必须是 Qt.callLater，不能直接赋值也不能用计时器节流：
    //   - 直接赋值会被 ListView 在 contentHeight 变化后"保持首个可见项不动"的调整覆盖掉；
    //   - 计时器节流（每次变化都 restart）在流式输出时永远等不到空档，回复会一直涨在视口外。
    // callLater 排在本轮布局结束之后，既贴得准，也不会被推迟。
    //
    // ⚠️ 但"右栏开合动画"这一档要**跳过**：宽度每帧在变，width/contentHeight/height 都会
    //    每帧触发一次，照旧每帧 callLater(pinToBottom) 等于在列表本来就要重排的每一帧上
    //    再叠一趟定位。动画期间只欠着，落定补一次。
    function scheduleFollowPin() {
        // pinning 期间不要重开一轮：pinStep 正在逐帧逼近末尾，重新 pinToBottom() 会把 pinAttempts
        // 归零、从粗定位重来（实测会让长会话永远卡在缓冲区底）。它读的都是活属性，新内容会自己看到。
        if (pinning || !followBottom || widthAnimating)
            return
        Qt.callLater(pinToBottom)
    }
    onContentHeightChanged: scheduleFollowPin()
    // 尺寸变化也要贴：**首帧列表的 width/height 是 0**（QML 还在布局），而 pinToBottom() 开头
    // 就 `if (width <= 0 || height <= 0) return` ⇒ 内容到达时那次 pin 直接空转，之后又没有
    // 内容/行数变化事件可依赖 ⇒ **永远没贴过底**，视图停在顶部。尺寸一可用就补一次。
    onHeightChanged: scheduleFollowPin()
    onWidthChanged: scheduleFollowPin()
    // 动画收尾：把动画期间欠下的那次贴底补上（期间流式追加的内容也一起归位）
    onWidthAnimatingChanged: if (!widthAnimating && followBottom) Qt.callLater(pinToBottom)
    // 行数变化：**从空到有**（换会话整表重建 / 首屏第一页）时先隐形，等这次贴底做完再显回来 ——
    // 否则会看到"首帧在顶部、下一帧跳到底部"（原因见上面 awaitingFirstPin 的注释）。
    // 流式追加（lastCount > 0）不隐形，只贴底。
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
            // 整表重建（ChatController::rebuildFromRecords → clear + 逐行 appendRow）是**同步连发**
            // N 次 countChanged。等这一轮结束再贴，否则每插一行都要定位一次（N 趟布局）。
            Qt.callLater(list.pinToBottom)
        } else {
            scheduleFollowPin()
        }
    }

    // 列表带着已有消息被创建时（Loader 重新激活 / 从轨迹视图切回来）countChanged 不会补发，
    // 这里主动走一次批量贴底。
    Component.onCompleted: {
        if (count > 0 && followBottom) {
            awaitingFirstPin = true
            bulkBuildPending = count <= fullBuildRowLimit
            lastCount = count
            Qt.callLater(pinToBottom)
        }
    }

    // 安全网：贴底过程万一卡在 width/height 未定或 ListView 没物化（awaitingFirstPin 一直为真），
    // 也要把列表显出来 —— 宁可停在中间，也不能一直隐形。
    Timer {
        interval: 700
        running: list.awaitingFirstPin
        onTriggered: {
            list.awaitingFirstPin = false
            list.pinToBottom()
        }
    }

    // ---- 顶部「加载更多」：照原版 ULin Hub 的形态（**不自动翻页**）----
    // 原版只显示最近一页（约 20 个气泡），更早的由用户点这个按钮显式拉。
    // 不做"滚到顶自动拉"的理由见文件头第 5 条：自动翻页会在用户往回看时不断插入历史，
    // 消息数越堆越多、锚点补偿反复触发、滚动条长度跟着变。
    header: Item {
        id: olderHeader
        width: list.width
        // 没有更早的、也没在加载时高度归零（header 高度也算进 contentHeight，贴底要算准）
        height: (Hub.olderAvailable || Hub.loadingOlder) ? 44 : 0

        Rectangle {
            anchors.centerIn: parent
            // ⚠️ 高度为 0（没有更早的页、也没在加载）时必须显式藏起来：
            //    header 的 height 归零**不会**裁剪子项，这颗胶囊照旧渲染，
            //    而 anchors.centerIn 让它的中心落在 content y=0 —— 正好扣在第一条消息头上。
            //    会话短（尤其只有一条系统提示）时就会看到"提示行和「加载更多」挤在同一条基线上"。
            //    离屏 A/B 复现见 build/temp/messagelist-harness/check.py。
            visible: olderHeader.height > 0
            width: 132
            height: 30
            radius: Theme.radius.panel
            color: olderClick.hovered ? Theme.color.hoverBg : Theme.color.panelBg
            // 半像素环（官方 --dsw-elevation-stroke 的厚度）：1 逻辑px 在 200% 下是 2 物理px，偏硬
            border.width: 0.5
            // 静息描边走 inputBorder(10% 黑) 而不是 border(4%)：官方 DSH 里"可点的胶囊/按钮"这一档
            // 用 `--dsw-alias-border-l2`，4% 太浅会看不出这里是个按钮。
            border.color: Theme.color.inputBorder

            Text {
                anchors.centerIn: parent
                // 用原版就有的翻译 id（ulinhub_*.ts 里的 common_load_more / common_loading），
                // 别硬编码中文 —— 否则切语言时这个按钮不会跟着变。
                text: Hub.loadingOlder ? qsTrId("common_loading") : qsTrId("common_load_more")
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

        // 右栏开合动画期间：列表宽度每帧在变，把这件事传给气泡，让它们把"尺寸一变就重做一张
        // 离屏纹理"的 MultiEffect 阴影按停（见 AgentBubble.widthAnimating）。
        // 挂在委托根上、由气泡自己按 parent 读（而不是这里直接写 agentBubble.widthAnimating）：
        // 风格包若覆盖了旧版 AgentBubble（没有这个属性），直接赋值会每行报一次 TypeError；
        // 让它读不到就当 false，旧皮肤照常工作。
        property bool widthAnimating: list.widthAnimating

        // 宽度下限 120：首帧 list.width 可能还是 0（布局未定），此时 Text 会按"一行一个字"排，
        // 高度炸到几千像素 → contentHeight 乱跳 → 滚动条长度乱变。给个下限把它挡掉。
        width: Math.max(120, list.width - list.leftMargin - list.rightMargin)
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
            // 行级 streaming 现在由 ChatController::setStreaming 写入（2026-10-20 修：之前只改控制器标志，
            // 模型里每行永远是 false ⇒ 一有思考段就显示"已完成"）。这里再兜一层"控制器在跑 + 最后一条 agent"，
            // 因为旧 exe 里行级标志可能还没更新 —— 两路任一为真都算在流式。
            streaming: rowRoot.streaming
                || (Hub.streaming && rowRoot.kind === "agent" && rowRoot.index === list.count - 1)
        }
    }
}
