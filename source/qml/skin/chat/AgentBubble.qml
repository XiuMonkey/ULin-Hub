// Agent 消息气泡：整条消息套圆角气泡，内部按官方的三层折叠铺开。
// 层级（官方原版）：
//   「已完成」            ← 整轮行：本轮除最终输出外的一切都收在里面
//     「已完成分析」      ← 过程组：连续的 thinking/toolCall/toolResult（思考与工具同组）
//     「中间输出文字」    ← 非最终 reply：也在完成行里，隔开前后两个过程组
//     「执行了命令」      ← 只含工具、没有思考的组
//   「最终输出」          ← 最后一段 reply，始终可见
// 所以：reply 段是过程组的分界；最后一段 reply 是唯一留在完成行外面的正文。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"
import "Markdown"

Item {
    id: root

    property var segments: []
    // 最后一段 reply 是最终输出；流式期间它的正文走 replyTail，提交后回落到 segments。
    readonly property int finalReplyIndex: {
        var index = -1
        var segs = root.segments || []
        for (var i = 0; i < segs.length; ++i) {
            if (segs[i].type === "reply")
                index = i
        }
        return index
    }
    // 仅最终 reply 的流式尾文本；thinking/tool 不走这里。
    property string replyTail: ""
    // 流式结尾的 Markdown part 切分；有它时代码块也按 part 实时渲染。
    property var replyTailParts: []
    property bool streaming: false
    // 只在“研究”阶段（最后一块是 thinking/tool 组成的过程块）显示爬行提示；
    // 一旦开始输出正文（最后一块是 reply），提示直接收起，不做动画。
    readonly property bool showStreamingHint: {
        if (!root.streaming)
            return false
        var bs = root.blocks
        return bs.length > 0 && bs[bs.length - 1].kind === "process"
    }
    property var deliverables: []

    // 复制全文优先取 reply 段，没有 reply 时退回所有段。
    // 按需拼一次，不做成绑定：流式期间 replyTail 每 33ms 变一次，绑定会把整段文本反复重建。
    function collectCopyText() {
        var replies = []
        var all = []
        var segs = root.segments || []
        for (var i = 0; i < segs.length; ++i) {
            var text = (i === root.finalReplyIndex && root.replyTail.length > 0)
                ? root.replyTail
                : (segs[i] && segs[i].text ? String(segs[i].text) : "")
            if (text.length === 0)
                continue
            all.push(text)
            if (segs[i].type === "reply")
                replies.push(text)
        }
        return (replies.length > 0 ? replies : all).join("\n\n")
    }

    function copyAll() {
        var text = root.collectCopyText()
        if (text.length === 0)
            return
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.deselect()
    }

    // 右键菜单「复制」：把拖选中的文字写进剪贴板（与 Ctrl+C 同一份内容）。
    // 选区在右键按下 / 弹窗抢焦点后可能被清掉，所以 press 阶段先抓成字符串；
    // 一条消息拆成多个 TextEdit（prose / 代码块 / 过程段），这里取第一个有选区的，
    // 原生拖选不能跨控件的代价照旧接受。
    property string capturedSelection: ""

    function findSelectedText(item) {
        if (!item)
            return ""
        if (item.selectedText !== undefined && item.selectedText.length > 0)
            return item.selectedText
        var kids = item.children || []
        for (var i = 0; i < kids.length; ++i) {
            var t = root.findSelectedText(kids[i])
            if (t.length > 0)
                return t
        }
        // Flickable 等容器把子项挂在 contentItem 上，不在 children 里
        var ci = item.contentItem
        if (ci && ci !== item) {
            var ciKids = ci.children || []
            for (var j = 0; j < ciKids.length; ++j) {
                var t2 = root.findSelectedText(ciKids[j])
                if (t2.length > 0)
                    return t2
            }
        }
        return ""
    }

    function copySelection() {
        if (root.capturedSelection.length === 0)
            return
        selectionHelper.text = root.capturedSelection
        selectionHelper.selectAll()
        selectionHelper.copy()
        selectionHelper.deselect()
    }

    // 宽度正被外层动画驱动时按停阴影，避免源面尺寸变化反复重做离屏纹理；
    // 宿主没有该属性（独立使用或旧风格包）时视为 false。
    property bool widthAnimating: parent ? parent.widthAnimating === true : false

    implicitHeight: bubble.implicitHeight + (root.showStreamingHint ? 29 : 0)

    // 展开态必须外提：Repeater 在 segments 变化时会重建全部 delegate，存在 Collapsible 内部会被
    // 下一个流式 chunk 冲掉。Collapsible 走受控模式；key 约定为成员 `type:toolName:index`、
    // 过程组 `group:start`、整轮 `turn`。
    property var expandedKeys: ({})

    function segmentKey(segment, index) {
        return (segment.type || "") + ":" + (segment.toolName || "") + ":" + index
    }

    function isSegmentExpanded(segment, index) {
        return root.expandedKeys[root.segmentKey(segment, index)] === true
    }

    function setSegmentExpanded(segment, index) {
        root.toggleKey(root.segmentKey(segment, index))
    }

    function isGroupExpanded(start) {
        return root.expandedKeys["group:" + start] === true
    }

    function toggleGroup(start) {
        root.toggleKey("group:" + start)
    }

    function toggleKey(key) {
        var next = {}
        for (var k in root.expandedKeys)
            next[k] = root.expandedKeys[k]
        if (next[key] === true)
            delete next[key]
        else
            next[key] = true
        root.expandedKeys = next
    }

    function expandKey(key) {
        if (root.expandedKeys[key] === true)
            return
        var next = {}
        for (var k in root.expandedKeys)
            next[k] = root.expandedKeys[k]
        next[key] = true
        root.expandedKeys = next
    }

    // 把 segments 切成正文块 / 过程块，顺序严格保持 segments 的原始顺序
    readonly property var blocks: {
        var out = []
        var segs = root.segments || []
        var lastReply = root.finalReplyIndex
        var i = 0
        while (i < segs.length) {
            if (segs[i].type === "reply") {
                // 按真实 segment 顺序排列；最终 reply 不压到底部，否则“正文 → 思考/工具”
                // 会被重排成“思考/工具 → 正文”，与流式输出顺序不一致。
                out.push({ kind: "reply", part: segs[i], start: i, count: 1, members: [],
                           final: i === lastReply })
                i += 1
                continue
            }
            // 过程组 = 一段连续的非正文段（思考与工具同组，reply 是组与组的分界）。
            var start = i
            var members = []
            while (i < segs.length && segs[i].type !== "reply") {
                members.push({ seg: segs[i], abs: i })
                i += 1
            }
            out.push({ kind: "process", part: null, start: start, count: members.length, members: members })
        }
        return out
    }

    readonly property bool hasProcessBlocks: {
        var bs = root.blocks
        for (var i = 0; i < bs.length; ++i) {
            if (bs[i].kind === "process")
                return true
        }
        return false
    }

    // 整轮行：C++ 侧暂无 turn 时长，只显示“已完成”；流式期间不出现。
    readonly property bool turnExpanded: root.expandedKeys["turn"] === true
    readonly property bool turnRowVisible: root.hasProcessBlocks && !root.streaming

    // 本轮结束时展开整轮行，避免过程内容在流式结束后突然消失；历史消息不触发此回调。
    onStreamingChanged: {
        if (!root.streaming && root.hasProcessBlocks)
            root.expandKey("turn")
    }

    // 组内出现过思考 ⇒ 「已完成分析」（思考与工具同组，标题按分析算）；
    // 只有工具的组才叫「执行了命令」。
    function groupActivity(start, count) {
        var hasTool = false
        for (var i = start; i < start + count; ++i) {
            var type = root.segments[i] ? root.segments[i].type : ""
            if (type === "thinking")
                return "think"
            if (type === "toolCall" || type === "toolResult")
                hasTool = true
        }
        return hasTool ? "tool" : "think"
    }

    // 折叠摘要取正文第一行；流式中改取最近一个已完成段落的首行，并去掉 Markdown 强调符号。
    function firstLine(text) {
        if (!text)
            return ""
        var newline = text.indexOf("\n")
        return newline === -1 ? text : text.substring(0, newline)
    }

    function latestParagraphFirstLine(text) {
        if (!text)
            return ""
        var separator = /\r?\n(?:[\t ]*\r?\n)+/g   // 空行分段
        var summary = ""
        var paragraphStart = 0
        while (true) {
            var match = separator.exec(text)
            var paragraphEnd = match ? match.index : text.length
            var chunk = text.substring(paragraphStart, paragraphEnd)
            var newline = chunk.indexOf("\n")
            if (newline !== -1) {   // 段内已有换行 ⇒ 该段首行算"写完"
                var candidate = chunk.substring(0, newline).trim()
                if (candidate !== "")
                    summary = candidate
            }
            if (!match)
                return summary
            paragraphStart = match.index + match[0].length
        }
        return summary
    }

    function thinkingSummary(text, running) {
        var line = running ? root.latestParagraphFirstLine(text) : root.firstLine(text)
        return line.replace(/\*\*/g, "")
    }

    // 工具调用摘要：取结果分隔符之前的参数，压成单行并去掉最外层花括号。
    function toolArgsSummary(text) {
        if (!text)
            return ""
        var marker = "\n\n--- 结果 ---\n"
        var cut = text.indexOf(marker)
        var args = (cut === -1 ? text : text.substring(0, cut)).trim()
        if (args.charAt(0) === "{")
            args = args.substring(1)
        if (args.length > 0 && args.charAt(args.length - 1) === "}")
            args = args.substring(0, args.length - 1)
        return args.replace(/\s+/g, " ").trim()
    }

    // 柔和外阴影：同形状实心矩形作为 MultiEffect 的源，气泡本体盖住它，只露出晕开的一圈；
    // 参数与 Card 同档。
    Rectangle {
        id: shadowShape
        anchors.left: parent.left
        width: bubble.width
        height: bubble.implicitHeight
        radius: bubble.radius
        // 皮肤可用 agentBubbleBg 让气泡完全融进背景（阴影源同色，阴影一起消失）
        color: Theme.color.agentBubbleBg || Theme.color.panelBg
        visible: false   // 只做 MultiEffect 的源（与 widgets/Card.qml 的 surface 同一套写法）
    }

    MultiEffect {
        anchors.fill: shadowShape
        source: shadowShape
        shadowEnabled: true
        // 宽度动画期间整层关掉；不能只设 shadowEnabled=false，否则源块会原样画出，底缘多出 2px 色块。
        visible: !root.widthAnimating
        shadowColor: Theme.color.shadowSubtle
        shadowBlur: Math.min(1.0, Theme.shadow.cardBlur / 32)   // shadowBlur 是 0..1，按 32px 满值线性映射
        shadowVerticalOffset: Theme.shadow.cardDy
        autoPaddingEnabled: true
    }

    Rectangle {
        id: bubble
        anchors.left: parent.left
        width: Math.min(Math.round(root.width * 0.82), 748)   // Web 聊天正文列默认 748px，窗口再宽也不拉满
        implicitHeight: inner.implicitHeight + 2 * Theme.spacing.buttonPadY
        radius: Theme.radius.card
        color: Theme.color.agentBubbleBg || Theme.color.panelBg

        Column {
            id: inner
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.spacing.cardPadding
            anchors.rightMargin: Theme.spacing.cardPadding
            anchors.topMargin: Theme.spacing.buttonPadY
            spacing: Theme.spacing.labelGap

            // ---- 整轮行（官方 turn 行）：只管过程组的显隐，正文始终可见 ----
            Collapsible {
                objectName: "turnRow"
                width: parent.width
                visible: root.turnRowVisible
                controlled: true
                expanded: root.turnExpanded
                onToggled: root.toggleKey("turn")
                chevronTrailing: true          // 官方：文字在前、箭头紧跟文字右侧，且没有 leading 图标
                title: qsTrId("chat_turn_done")
                summary: ""
            }

            // ⚠️ model 必须是数字：数组每次变化都会让 Repeater 重建全部 delegate，流式时会持续闪烁；
            // 数字不变则不重建，内容变化交给 delegate 内的绑定刷新。
            Repeater {
                model: root.blocks.length
                delegate: Column {
                    id: block
                    required property int index
                    readonly property var blockData: root.blocks[index]

                    width: inner.width
                    spacing: Theme.spacing.labelGap

                    // 中间输出（非最终 reply）收进「已完成」：只有最后一段输出常显，
                    // 其余跟随整轮行展开/折叠；过程组有自己的显隐绑定，不受这里影响。
                    // 最终正文无条件常显；只有中间 reply 收束在「已完成」里，不单独露到过程组外。
                    visible: blockData.kind !== "reply" || blockData.final

                    // 过程组：一组连续的思考/工具，收起只剩一行“图标 + 动作短语”。
                    Collapsible {
                        id: group
                        objectName: "processGroup" + block.blockData.start
                        width: parent.width
                        // 流式期间过程组始终可见，不受整轮行控制。
                        visible: block.blockData.kind === "process" && (root.turnExpanded || root.streaming)

                        readonly property bool isTail: root.streaming
                            && (block.blockData.start + block.blockData.count) === root.segments.length
                        readonly property string activity: root.groupActivity(block.blockData.start,
                                                                             block.blockData.count)
                        controlled: true
                        expanded: root.isGroupExpanded(block.blockData.start)
                        onToggled: root.toggleGroup(block.blockData.start)

                        iconKind: activity === "tool" ? "tool" : "think"
                        title: isTail   // 进行中与结束后的标题分属两套文案
                            ? (activity === "tool"
                                ? qsTrId("chat_process_tools_running")
                                : qsTrId("chat_process_think_running"))
                            : (activity === "tool"
                                ? qsTrId("chat_process_tools_done")
                                : qsTrId("chat_process_think_done"))
                        summary: ""          // 分组行没有摘要
                        running: isTail
                        // 过程组只做排版；内滚下沉到成员正文，否则后面的成员行会被卷进滚动区。
                        maxBodyHeight: 0

                        Column {
                            width: parent.width
                            spacing: Theme.spacing.buttonGap

                            Repeater {
                                model: block.blockData.count   // 数字模型，流式续写时不重建 delegate

                                delegate: Collapsible {
                                    id: member
                                    objectName: "segmentCard" + member.abs
                                    width: parent.width
                                    required property int index
                                    readonly property int abs: block.blockData.start + member.index
                                    readonly property var seg: root.segments[member.abs]
                                    readonly property bool isThinking: seg.type === "thinking"
                                    readonly property bool isTail: root.streaming   // 只有流式尾巴算进行中
                                        && abs === root.segments.length - 1
                                    controlled: true   // 展开态由 root.expandedKeys 控制
                                    expanded: root.isSegmentExpanded(seg, abs)
                                    onToggled: root.setSegmentExpanded(seg, abs)

                                    iconKind: isThinking ? "think" : "tool"
                                    running: isTail
                                    // 没有工具到动作名的映射表，直接用工具名占同一格。
                                    // ACP 的 tool title 有时是一整条命令（账本里确实存在
                                    // name="`pwd && ls ...`" 这种），折叠行只有一行高，
                                    // 不截断就会横向溢出盖住相邻行；正文有内滚，不受影响。
                                    readonly property string rawTitle: isThinking
                                        ? qsTrId("chat_thinking_title")
                                        : (seg.type === "toolCall" ? seg.toolName
                                                                   : qsTrId("chat_tool_result_label"))
                                    title: rawTitle.length > 64 ? rawTitle.slice(0, 64) + "…" : rawTitle
                                    summary: isThinking
                                        ? root.thinkingSummary(seg.text, isTail)
                                        : root.toolArgsSummary(seg.text)
                                    keepSummaryWhenExpanded: !isThinking   // 工具行展开后仍保留摘要
                                    maxBodyHeight: isThinking ? 400 : 260   // 限高落在成员正文，过程组不限高

                                    // 折叠时不塞正文：TextEdit 即使不可见，宽度变化仍会重排 MarkdownText。
                                    TextEdit {
                                        objectName: "bubbleText"
                                        width: parent.width
                                        height: Math.ceil(contentHeight)
                                        // TextEdit 不自动裁切：流式追加时 contentHeight 可能晚一帧，
                                        // 不裁会把命令正文画到下一段"思考"/工具行上。
                                        clip: true
                                        text: member.expanded
                                            ? (member.seg ? member.seg.text : "")
                                            : ""
                                        textFormat: TextEdit.MarkdownText
                                        wrapMode: TextEdit.Wrap
                                        readOnly: true
                                        selectByMouse: true
                                        persistentSelection: true
                                        activeFocusOnPress: true
                                        color: Theme.color.textSecondary
                                        selectionColor: Theme.color.selectionBg
                                        selectedTextColor: Theme.color.textPrimary
                                        font.family: member.isThinking ? "Microsoft YaHei UI" : "Consolas"
                                        font.pixelSize: Theme.font.caption
                                    }
                                }
                            }
                        }
                    }

                    // 正文段：正文 / 代码块按出现顺序；用数字模型避免每个 chunk 重建。
                    readonly property bool replyTailActive: !!blockData.final
                        && root.replyTail.length > 0
                    readonly property var replyParts: (blockData.kind === "reply")
                        ? (replyTailActive
                            ? root.replyTailParts
                            : (blockData.part.parts || []))
                        : []

                    Repeater {
                        model: block.replyParts.length
                        delegate: Item {
                            id: part
                            required property int index
                            readonly property var partData: block.replyParts[index]
                            width: block.width
                            implicitHeight: (partData.kind === "code")
                                ? codeUnit.implicitHeight
                                : proseText.implicitHeight

                            // 正文交给 MarkdownBody 做块级排版；整段 TextEdit 无法正确呈现表格、列表等结构。
                            MarkdownBody {
                                id: proseText
                                visible: part.partData.kind !== "code"
                                width: parent.width
                                text: visible ? part.partData.text : ""
                            }

                            CodeBlock {
                                id: codeUnit
                                visible: part.partData.kind === "code"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                language: visible ? part.partData.language : ""
                                code: visible ? part.partData.text : ""
                                html: visible ? part.partData.html : ""
                            }
                        }
                    }
                    // 最终 reply 的流式尾：只更新这一块，稳定 segments/Repeater 不重建。
                    MarkdownBody {
                        // 有 part 切分时正文/代码块都走上面的 Repeater；这里只做旧路径兜底。
                        visible: block.replyTailActive && block.replyParts.length === 0
                        width: block.width
                        text: block.replyTailActive ? root.replyTail : ""
                    }
                }
            }

            DeliverablesView {
                width: inner.width
                items: root.deliverables
                // 产物只在整轮输出结束后出现：present 是在回合中间调用的，事件会先到；
                // 流式期间先压住，避免卡片抢在正文/流式尾巴前面。
                visible: !root.streaming && root.deliverables.length > 0
            }
        }
    }

    // 流式提示：气泡下方一条极淡的分隔线 + 爬行标记 + 文案；
    // 只在最后一条 agent 消息的 streaming 期间显示，结束后高度收起。
    Rectangle {
        id: streamDivider
        visible: root.showStreamingHint
        anchors.top: bubble.bottom
        anchors.left: bubble.left
        width: bubble.width
        height: 1
        color: Theme.color.border
        opacity: 0.7
    }

    Item {
        id: streamingHint
        visible: root.showStreamingHint
        anchors.top: streamDivider.bottom
        anchors.left: bubble.left
        anchors.leftMargin: Theme.spacing.cardPadding
        width: Math.max(0, bubble.width - 2 * Theme.spacing.cardPadding)
        height: 24

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            CrawlMark {
                width: 20
                height: 16
                running: streamingHint.visible
            }

            Text {
                id: streamingText
                text: qsTrId("chat_streaming_hint")
                color: Theme.color.textPrimary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
            }
        }

        SequentialAnimation {
            running: streamingHint.visible
            loops: Animation.Infinite
            NumberAnimation {
                target: streamingText
                property: "opacity"
                from: 0.45
                to: 0.9
                duration: 900
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                target: streamingText
                property: "opacity"
                from: 0.9
                to: 0.45
                duration: 900
                easing.type: Easing.InOutSine
            }
        }
    }

    // QML 没有独立 Clipboard 类型，借只读 TextEdit 复制全文。
    // text 只在 copyAll() 时写入，保持为空避免流式期间绑定重建全文。
    TextEdit {
        id: clipboardHelper
        visible: false
        readOnly: true
        textFormat: TextEdit.PlainText
        width: 1
        height: 1
    }

    // 复制选区用：TextEdit.copy() 只认自己的选区，借一个无绑定的隐藏 TextEdit 写字符串。
    TextEdit {
        id: selectionHelper
        visible: false
        readOnly: true
        textFormat: TextEdit.PlainText
        width: 1
        height: 1
    }

    Popup {
        id: copyMenu
        parent: root
        width: 156
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 8
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: copyMenu.width - 12
            spacing: 2

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                visible: root.capturedSelection.length > 0
                color: selCopyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("common_copy")
                    color: Theme.color.textSecondary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: selCopyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.copySelection()
                        copyMenu.close()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                visible: root.capturedSelection.length > 0
                color: quoteMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: "引用文本"
                    color: Theme.color.textSecondary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: quoteMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Hub.quoteMessageText(root.capturedSelection)
                        copyMenu.close()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                color: copyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("context_copy_all")
                    color: Theme.color.textSecondary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.copyAll()
                        copyMenu.close()
                    }
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        // 右键按下时选区还在，先抓一份；Popup 打开会抢焦点，之后不再依赖原选区。
        onPressedChanged: {
            if (pressed)
                root.capturedSelection = root.findSelectedText(root)
        }
        onTapped: (eventPoint) => {
            var p = eventPoint.position
            if (root.capturedSelection.length === 0)
                root.capturedSelection = root.findSelectedText(root)
            copyMenu.x = Math.max(8, Math.min(root.width - copyMenu.width - 8, p.x))
            copyMenu.y = Math.max(8, Math.min(root.height - copyMenu.height - 8, p.y))
            copyMenu.open()
        }
    }
}
