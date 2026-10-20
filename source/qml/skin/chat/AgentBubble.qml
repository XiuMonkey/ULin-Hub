// Agent 消息气泡：整条消息套一层圆角气泡，内部按**官方的三层折叠**铺开（2026-10-20 重做）。
//
// 官方一条助手消息从上到下（用户截图 折叠逻辑/*.png 为准）：
//   「已完成，用时 1 分 21 秒 ⌃」             ← 整轮行（turn）：只管过程组的显隐，**正文始终可见**
//   「⌃/⊠ 已完成分析」「⌃/›_ 执行了命令」     ← 过程组（ChatGroupSeat）：一段连续的 thinking/toolCall
//        「⊠ 思考 · 摘要…」                   ← 成员行（ReasoningRow）
//        「›_ 运行命令 · 摘要…」              ← 成员行（ToolRow）
//   「正文」                                  ← 最终回复（reply 段，按代码围栏切成 正文/代码块）
//   「⌃ 执行了命令」                          ← 又一组（正文是组与组之间的分界）
//
// 也就是说：连续的 thinking/toolCall/toolResult 段 = 一个过程组，reply 段是分界线。
// 折叠行的外观规格见 widgets/Collapsible.qml 的头注（24px 行高、图标/hover 换箭头、圆点摘要…）。
//
// 注：旧壳的 Agent 消息是**没有气泡**的裸文本；这里按用户要求保留圆角气泡（有意偏离原版），
// 但"气泡怎么立住"仍按原版：不画描边，用 shadowSubtle 那层极淡的外晕。

import QtQuick
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"
import "Markdown"

Item {
    id: root

    property var segments: []
    property bool streaming: false

    // 本气泡的宽度是否正被外层动画驱动（右栏开合 / 拖右栏宽度时会话区宽度持续在变 ⇒ 列表与
    // 气泡跟着变宽；开合按 15px 分步）。宿主（MessageListView 的委托根）提供同名属性就跟着它；
    // 没有（独立使用 / 旧风格包）就当 false。true 期间把下面的 MultiEffect 阴影按停：源面尺寸一变
    // ⇒ 阴影离屏纹理就要重做一张，这是开合动画里按"每个可见气泡"累计的一笔固定开销
    // （机制与实测见 widgets/PageOverlay.qml）。
    property bool widthAnimating: parent ? parent.widthAnimating === true : false

    implicitHeight: bubble.implicitHeight

    // ---- 展开态：全部外提到气泡这一层 ----
    // 官方每条 disclosure 的展开态由 `useDisclosure(version)` 持有：version 不变（同一轮流式/重放）就保留读者
    // 已展开的那一栏。本项目对应物是段级 delegate 由 Repeater 承载，而 Repeater 在 segments 每次变化时会
    // **销毁并重建全部 delegate**（流式每个 chunk 一次）⇒ 状态存在 Collapsible 自己身上会被下一个 chunk 冲掉。
    // 所以 key → bool 统一记在这里，Collapsible 走受控模式（`controlled: true`，只发 toggled）。
    // key 约定：成员 `type:toolName:index`、过程组 `group:start`、整轮 `turn`。
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

    // 所有展开态都存在 expandedKeys 里（整体换对象才会让 binding 失效）
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

    // ---- 分块：把 segments 切成「正文块 / 过程块」----
    readonly property var blocks: {
        var out = []
        var segs = root.segments || []
        var i = 0
        while (i < segs.length) {
            if (segs[i].type === "reply") {
                out.push({ kind: "reply", part: segs[i], start: i, count: 1, members: [] })
                i += 1
                continue
            }
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

    // 有没有过程块：纯文本回复不该出现整轮折叠行
    readonly property bool hasProcessBlocks: {
        var bs = root.blocks
        for (var i = 0; i < bs.length; ++i) {
            if (bs[i].kind === "process")
                return true
        }
        return false
    }

    // ---- 最外层：整轮行 ----
    // 官方文案 = message.turnProcess.worked「已完成」，有时长时是「已完成，用时 1 分 21 秒」。
    // 本项目 C++ 侧还没有 turn 起止时间（引擎 turn/end 只带 {turn, reason}，事件里没有 time），
    // 所以只显示"已完成"；补时长要改 ChatController（见交付说明）。
    readonly property bool turnExpanded: root.expandedKeys["turn"] === true
    // 流式过程中不出现（官方 turnProcessAlwaysOpen：turn 未结束 / aborted / error 时整轮不折叠）
    readonly property bool turnRowVisible: root.hasProcessBlocks && !root.streaming

    // 本轮刚跑完（streaming true→false）时把整轮行展开：官方里"正在跑的 turn"始终展开，只有回看历史才折叠。
    // 不这么做的话，流式一结束、过程内容就会当着读者的面消失。
    // （历史消息加载时 streaming 一直是 false，这个回调不触发 ⇒ 那些仍是默认折叠。）
    onStreamingChanged: {
        if (!root.streaming && root.hasProcessBlocks)
            root.expandKey("turn")
    }

    // 组内是否出现过工具调用 ⇒ 决定组标题/图标（"执行了命令" / "已完成分析"）
    function groupActivity(start, count) {
        for (var i = start; i < start + count; ++i) {
            var type = root.segments[i] ? root.segments[i].type : ""
            if (type === "toolCall" || type === "toolResult")
                return "tool"
        }
        return "think"
    }

    // ---- 摘要算法：与官方 ReasoningRow 一致 ----
    // 折叠摘要 = 正文**第一行**；流式中改成"最近一个已完成段落的第一行"（段内出现过换行才算写完），
    // 并去掉 Markdown 的 ** 强调符号（官方 replaceAll("**","")）。
    function firstLine(text) {
        if (!text)
            return ""
        var newline = text.indexOf("\n")
        return newline === -1 ? text : text.substring(0, newline)
    }

    function latestParagraphFirstLine(text) {
        if (!text)
            return ""
        // 官方 separator = /\r?\n(?:[\t ]*\r?\n)+/：空行分段
        var separator = /\r?\n(?:[\t ]*\r?\n)+/g
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

    // 工具行的折叠摘要：官方 ToolRow 的 collapsedContent 是"一句能看懂调用了什么"的单行。
    // 本项目 toolCall 段 = prettyArguments + "\n\n--- 结果 ---\n" + 结果 ⇒ 取分隔符之前压成单行、剥最外层花括号。
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

    // 柔和外阴影：原版 Agent 消息单元画的不是描边，而是 `shadowSubtle`（AgentMessageUnit.cpp:1011）
    // 那层几乎看不见的晕 —— 真机像素实测原版气泡左缘是 #FAFBFD→#F8F9FB→#F4F5F7→#ECEDEF 的四段渐隐。
    // 做法：拿一块同形状的实心矩形当 MultiEffect 的源，气泡本体（下面那层、不透明）正好盖住它，
    // 于是只露出晕开的一圈，不会多画任何颜色；参数与 Card 同一档（= 原版 CardShadow{8,2,*12}）。
    Rectangle {
        id: shadowShape

        anchors.left: parent.left
        width: bubble.width
        height: bubble.implicitHeight
        radius: bubble.radius
        color: Theme.color.panelBg
        visible: false   // 只做 MultiEffect 的源（与 widgets/Card.qml 的 surface 同一套写法）
    }

    MultiEffect {
        anchors.fill: shadowShape
        source: shadowShape
        shadowEnabled: true
        // 宽度动画期间整层关掉：只留下面那颗不透明的 bubble 本体（阴影本来就极淡，落定后恢复）。
        // ⚠️ 不能只改 shadowEnabled=false：那样 MultiEffect 会把 source（同形状、下移 cardDy 的
        //    实心块）原样画出来，气泡底缘会多出 2px 的色块。
        visible: !root.widthAnimating
        shadowColor: Theme.color.shadowSubtle
        shadowBlur: Math.min(1.0, Theme.shadow.cardBlur / 32)   // shadowBlur 是 0..1，按 32px 满值线性映射
        shadowVerticalOffset: Theme.shadow.cardDy
        autoPaddingEnabled: true
    }

    Rectangle {
        id: bubble

        anchors.left: parent.left
        // 限到 82%：左侧留出气泡感，右侧留白；用户气泡是从右侧 72% 收进来的
        width: Math.round(root.width * 0.82)
        implicitHeight: inner.implicitHeight + 2 * Theme.spacing.buttonPadY
        radius: Theme.radius.card
        color: Theme.color.panelBg
        // 无描边：原版 `#agentBubble` / `#agentUnit` 只有 `background: {{panelBg}} + border-radius: 12px`
        // （chat.qss），一条 border 都没有 —— 层次由上面那层柔和阴影给，不靠线。

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

            // ⚠️ model 必须是**数字**：Repeater 只要 model 被赋一个新数组就会销毁并重建全部 delegate，
            //    而 blocks 每个 chunk 都会重算出一个新数组 ⇒ 流式时整块内容每秒重建几十次，
            //    肉眼就是"这一直在闪"、点击也常落在重建的间隙里（"点不动"）。
            //    数字在结构不变时保持同值，Repeater 就不重建；内容变化由 delegate 内 `root.blocks[index]` 的绑定刷新。
            Repeater {
                model: root.blocks.length

                delegate: Column {
                    id: block

                    required property int index
                    readonly property var blockData: root.blocks[index]

                    width: inner.width
                    spacing: Theme.spacing.labelGap

                    // ---- 过程组（官方 ChatGroupSeat）：一组连续的思考/工具，收起只剩一行 ----
                    // 截图形态：收起是「⊠ 已完成分析」/「›_ 执行了命令」，展开变「⌃ …」+ 下面挂成员行。
                    // 一行"图标 + 动作短语"，没有摘要、没有右侧 meta。
                    Collapsible {
                        id: group
                        objectName: "processGroup" + block.blockData.start
                        width: parent.width
                        // 官方 turnProcessAlwaysOpen：turn 还没结束时过程组**始终可见**，不受整轮行控制。
                        // （之前只写 turnExpanded，结果流式时 turnExpanded 还是 false ⇒ 思考和工具整段消失。）
                        visible: block.blockData.kind === "process" && (root.turnExpanded || root.streaming)

                        readonly property bool isTail: root.streaming
                            && (block.blockData.start + block.blockData.count) === root.segments.length
                        readonly property string activity: root.groupActivity(block.blockData.start,
                                                                             block.blockData.count)

                        controlled: true
                        expanded: root.isGroupExpanded(block.blockData.start)
                        onToggled: root.toggleGroup(block.blockData.start)

                        iconKind: activity === "tool" ? "tool" : "think"
                        // 官方文案：进行中用 stepProcess.*（"正在分析请求"/"正在运行命令"），
                        // 结束后用 stepProcess.done.*（"已完成分析"/"执行了命令"）。
                        title: isTail
                            ? (activity === "tool"
                                ? qsTrId("chat_process_tools_running")
                                : qsTrId("chat_process_think_running"))
                            : (activity === "tool"
                                ? qsTrId("chat_process_tools_done")
                                : qsTrId("chat_process_think_done"))
                        summary: ""          // 分组行没有摘要（成员行才有）
                        running: isTail
                        // 过程组只是**排版容器**，自己不当滚动区（2026-10-20 修"滚动区把工具调用行吞了"）：
                        // 组正文一旦限高内滚，同组后面的成员行（`>_ read …` / `✧ 思考 …`）就被卷进
                        // 滚动内容里 —— 要滚很久才见得到，滚到底还会被裁在边上；而组的 400px 视口
                        // 也只够装推理正文，视觉上就是"后面那些行被正文滚没了"。
                        // 内滚下沉到每个成员自己的正文（思考 400 / 工具 260），成员行仍按 Column 顺序
                        // 排在正文滚动区**下面**，不再属于任何滚动内容。
                        maxBodyHeight: 0

                        Column {
                            width: parent.width
                            spacing: Theme.spacing.buttonGap   // 官方组内 --dsh-chat-flow-gap: 6px

                            Repeater {
                                // 同样是数字模型：组内成员数不变时（流式续写）不重建 delegate
                                model: block.blockData.count

                                delegate: Collapsible {
                                    id: member
                                    objectName: "segmentCard" + member.abs   // 供离屏试装台按序取段
                                    width: parent.width

                                    required property int index
                                    readonly property int abs: block.blockData.start + member.index
                                    readonly property var seg: root.segments[member.abs]
                                    readonly property bool isThinking: seg.type === "thinking"
                                    // 官方 running = "这一段是流式尾巴"（历史段早已定稿，不该再闪）
                                    readonly property bool isTail: root.streaming
                                        && abs === root.segments.length - 1

                                    // 展开态外提（见 root.expandedKeys 的注）：受控模式下这里只读，
                                    // 点击由 onToggled 回调翻 root.expandedKeys，绑定不会被打断。
                                    controlled: true
                                    expanded: root.isSegmentExpanded(seg, abs)
                                    onToggled: root.setSegmentExpanded(seg, abs)

                                    iconKind: isThinking ? "think" : "tool"
                                    running: isTail
                                    // 官方 ReasoningRow 的标题就是"思考"；ToolRow 是动作短语（"运行命令"）。
                                    // 本项目没有工具→动作名的映射表，用工具名占同一格：比"工具调用：xxx"短，也不随语言变长。
                                    title: isThinking
                                        ? qsTrId("chat_thinking_title")
                                        : (seg.type === "toolCall" ? seg.toolName
                                                                   : qsTrId("chat_tool_result_label"))
                                    // 官方摘要：思考=首行（流式=最近完成段落首行、去掉 **）；工具=参数压平的单行
                                    summary: isThinking
                                        ? root.thinkingSummary(seg.text, isTail)
                                        : root.toolArgsSummary(seg.text)
                                    // 官方 ToolRow 传 keepContentWhenOpen（展开后摘要仍在）；ReasoningRow 不传
                                    keepSummaryWhenExpanded: !isThinking
                                    // 官方工具卡正文 .bodyScroll { max-height: 260px }；思考正文 400。
                                    // 思考的限高必须落在**成员自己**身上：过程组不再限高（见上面 group 的注），
                                    // 否则 60 行推理会把同组的工具调用行顶到滚动内容深处，就是用户截图里的“被吞”。
                                    maxBodyHeight: isThinking ? 400 : 260

                                    // 只读但可选中/复制；高度必须显式给 contentHeight（TextEdit 没有可用的 implicit*）。
                                    // ⚠️ 折叠时不塞正文（展开才塞）：QQuickTextEdit 即便 visible:false，宽度一变也会
                                    //    重排整篇 MarkdownText —— 本会话 52 条折叠正文实测 52ms/帧；折叠时 text 为空
                                    //    则 17ms/帧（108 帧只掉 1）。已展开的行**照旧**（内容/选择/复制/内滚完全不变），
                                    //    只有"折叠→展开"那一瞬间才做一次排版（最大一条 ~6KB，估 1–3ms，无感）。
                                    TextEdit {
                                        objectName: "bubbleText"
                                        width: parent.width
                                        height: contentHeight
                                        text: member.expanded
                                            ? (member.seg ? member.seg.text : "")
                                            : ""
                                        textFormat: TextEdit.MarkdownText
                                        wrapMode: TextEdit.Wrap
                                        readOnly: true
                                        selectByMouse: true
                                        persistentSelection: true
                                        activeFocusOnPress: false
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

                    // ---- 正文段：正文 / 代码块按出现顺序 ----
                    // 同样用数字模型（parts 的数量在流式续写时不变，只有文本变），避免每 chunk 重建正文。
                    readonly property var replyParts: (blockData.kind === "reply" && blockData.part.parts)
                        ? blockData.part.parts
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

                            // 正文：交给 MarkdownBody 按官方 md 的**块级**规格排版
                            // （表格 / 分界线 / 引用块 / 列表 / 标题 用 QML 自绘，段落交给 MarkdownInline）。
                            // 为什么不能继续用一整段 TextEdit：见 MarkdownBody.qml 文件头的根因实测。
                            // 字号/字色/选择行为与改动前一致（13px、textPrimary、可选中复制）。
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
                }
            }
        }
    }
}
