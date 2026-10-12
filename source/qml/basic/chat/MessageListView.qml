// 会话区消息列表：一行一条消息，delegate 按 kind 分派到用户气泡 / Agent 气泡 / 系统提示。
// 模型是 Hub.messages（见 include/common/session/MessageListModel.h）。滚动上踩过的坑：
//   1) 不用 positionViewAtEnd() 贴底：内容高度还在分批测量时它会把 contentY 顶出边界（实测 69412，内容只有 3229）；
//   2) 合法 contentY 区间要算上 ListView 的 margin：[-topMargin, contentH - height + bottomMargin]；
//   3) 贴底跟随不加计时器节流（内容一变就贴）；用户一上手就停跟随，松手按是否在底部决定恢复；
//   4) 委托高度不均 ⇒ cacheBuffer 必须按行数自适应，否则 contentHeight 估值来回修正、滚动条长度一直跳；
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
    // 全建的代价线性放大：94 个气泡全建实测 CPU 12.7s / 内存峰值 807MB / 窗口未响应。
    readonly property int fullBuildRowLimit: 40
    cacheBuffer: count <= fullBuildRowLimit ? 100000 : Math.round(Math.max(600, height) * 2)
    // 不要开 reuseItems：委托高度差异大时实测"委托都建出来了、indexAt() 却全返回 -1"，视图停在空白处
    ScrollBar.vertical: AppScrollBar { }

    topMargin: Theme.spacing.cardPadding
    bottomMargin: Theme.spacing.cardPadding
    leftMargin: Theme.spacing.cardPadding
    rightMargin: Theme.spacing.cardPadding

    readonly property real minContentY: -topMargin
    readonly property real maxContentY: Math.max(minContentY, contentHeight - height + bottomMargin)

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

    // 贴底（2026-10-03 重写）：**交给 ListView 用它自己的条目几何去定位最后一条**，
    // 不再自己算 `contentHeight - height + bottomMargin`。
    //
    // 为什么改：实测（25 条消息、含三条上千像素的工具卡）委托的实际跨度是 y ∈ [-798, 9348]，
    // 而自算的 maxContentY = contentHeight - height + bottomMargin = 9725.9 —— 比最后一条的
    // 底部还低 ~798px（≈ spacing × count）。这个 ListView **不会夹紧 contentY**（本文件开头第 1 条
    // 记的"positionViewAtEnd 跑出 69412"就是同一个毛病），于是 contentY 落到内容之外 ⇒
    // 视口整片空白、indexAt() 全返回 -1。症状正是用户报的"跑到太下面、没有消息"。
    //
    // positionViewAtIndex(count-1, ListView.End) 让**最后一条的底部对齐视口底部**，
    // 用的是 ListView 自己的布局结果，不受 contentHeight 与间距估算差异的影响。
    // （原注释警告的是 `positionViewAtEnd()`；这里是带 index 的定位，语义更明确。）
    function pinToBottom() {
        if (width <= 0 || height <= 0 || count <= 0)
            return
        positionViewAtIndex(count - 1, ListView.End)
        // 记下"贴底时 contentY 应是多少"，给下面 onMovementEnded 判"用户松手时在不在底部"用。
        // 不能用 Qt 的 atYEnd：它按 contentHeight 算，而本列表的 contentHeight 比条目实际跨度
        // 大 ~800px（见上），所以贴到底时 atYEnd 反而是 false。
        pinTargetY = contentY
    }

    // 贴底时 contentY 的目标值（-1 = 还不知道）
    property real pinTargetY: -1

    // 用户一上手就停止跟随；松手时按"是不是就在贴底位附近"决定要不要恢复。
    // （不用 atYEnd：本列表的 contentHeight 比条目实际跨度大 ~800px，贴到底时它反而是 false）
    onMovementStarted: followBottom = false
    onMovementEnded: followBottom = pinTargetY >= 0 && Math.abs(contentY - pinTargetY) <= 64

    // （2026-10-03 删）原先这里有一条"滚到顶部附近就自动拉上一页"：
    //     onContentYChanged: if (contentY <= minContentY + 80) requestOlder()
    // 改法的理由见文件头第 5 条 —— 改成顶部那个显式的「加载更多」按钮了。

    // 内容一变就贴底。这里必须是 Qt.callLater，不能直接赋值也不能用计时器节流：
    //   - 直接赋值会被 ListView 在 contentHeight 变化后"保持首个可见项不动"的调整覆盖掉
    //     （实测停在底部上方 64px，atYEnd=false）；
    //   - 计时器节流（每次变化都 restart）在流式输出时永远等不到空档，回复会一直涨在视口外。
    // callLater 排在本轮布局结束之后，既贴得准，也不会被推迟。
    onContentHeightChanged: if (followBottom) Qt.callLater(pinToBottom)
    // 尺寸变化也要贴：**首帧列表的 width/height 是 0**（QML 还在布局），而 pinToBottom() 开头
    // 就 `if (width <= 0 || height <= 0) return` ⇒ 内容到达时那次 pin 直接空转，之后又没有
    // 内容/行数变化事件可依赖 ⇒ **永远没贴过底**，视图停在顶部（实测症状：「初次加载时啥都没有」
    // —— 内容其实在，只是视口停在顶部那片空白/工具卡片上）。尺寸一可用就补一次。
    onHeightChanged: if (followBottom) Qt.callLater(pinToBottom)
    onWidthChanged: if (followBottom) Qt.callLater(pinToBottom)
    // 行数变化：**从空到有**（换会话整表重建 / 首屏第一页）时先隐形，等这次贴底做完再显回来 ——
    // 否则会看到"首帧在顶部、下一帧跳到底部"（原因见上面 awaitingFirstPin 的注释）。
    // 流式追加（lastCount > 0）不隐形，只贴底。
    onCountChanged: {
        var wasEmpty = (lastCount === 0)
        lastCount = count
        if (count > 0 && followBottom) {
            if (wasEmpty)
                awaitingFirstPin = true
            pinToBottom()
            Qt.callLater(function() {
                list.pinToBottom()
                list.awaitingFirstPin = false
            })
        } else {
            awaitingFirstPin = false
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
            border.width: 1
            border.color: Theme.color.border

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
        required property bool streaming

        // 宽度下限 120：首帧 list.width 可能还是 0（布局未定），此时 Text 会按"一行一个字"排，
        // 高度炸到几千像素 → contentHeight 乱跳 → 滚动条长度乱变。给个下限把它挡掉。
        width: Math.max(120, list.width - list.leftMargin - list.rightMargin)
        implicitHeight: (rowRoot.kind === "user")
            ? userBubble.implicitHeight
            : ((rowRoot.kind === "system") ? systemNotice.implicitHeight : agentBubble.implicitHeight)

        UserBubble {
            id: userBubble
            visible: rowRoot.kind === "user"
            anchors.left: parent.left
            anchors.right: parent.right
            text: rowRoot.text
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
            streaming: rowRoot.streaming
        }
    }
}
