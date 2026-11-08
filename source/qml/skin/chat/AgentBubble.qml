// Agent 消息气泡：整条消息套圆角气泡，内部按官方的三层折叠铺开。
// 连续的 thinking/toolCall/toolResult 段归入一个过程组，reply 段是组与组的分界；
// 整轮行只控制过程组显隐，正文始终可见。气泡不画描边，只用极淡的外晕。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"
import "Markdown"

Item {
    id: root

    property var segments: []
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

    // 复制全文优先取 reply 段，没有 reply 时退回所有段；原生拖选无法跨多个 TextEdit。
    readonly property string copyText: {
        var replies = []
        var all = []
        var segs = root.segments || []
        for (var i = 0; i < segs.length; ++i) {
            var text = segs[i] && segs[i].text ? String(segs[i].text) : ""
            if (text.length === 0)
                continue
            all.push(text)
            if (segs[i].type === "reply")
                replies.push(text)
        }
        return (replies.length > 0 ? replies : all).join("\n\n")
    }

    function copyAll() {
        if (root.copyText.length === 0)
            return
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.deselect()
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

    // 把 segments 切成正文块 / 过程块
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

    // 组内是否出现过工具调用 ⇒ 决定组标题/图标（"执行了命令" / "已完成分析"）
    function groupActivity(start, count) {
        for (var i = start; i < start + count; ++i) {
            var type = root.segments[i] ? root.segments[i].type : ""
            if (type === "toolCall" || type === "toolResult")
                return "tool"
        }
        return "think"
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
        color: Theme.color.panelBg
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
        color: Theme.color.panelBg

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
                                    // 没有工具到动作名的映射表，直接用工具名占同一格
                                    title: isThinking
                                        ? qsTrId("chat_thinking_title")
                                        : (seg.type === "toolCall" ? seg.toolName
                                                                   : qsTrId("chat_tool_result_label"))
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
                }
            }

            DeliverablesView {
                width: inner.width
                items: root.deliverables
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
                font.family: "Microsoft YaHei UI"
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
    TextEdit {
        id: clipboardHelper
        visible: false
        readOnly: true
        textFormat: TextEdit.PlainText
        text: root.copyText
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
                color: copyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("context_copy_all")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
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
        onTapped: (eventPoint) => {
            var p = eventPoint.position
            copyMenu.x = Math.max(8, Math.min(root.width - copyMenu.width - 8, p.x))
            copyMenu.y = Math.max(8, Math.min(root.height - copyMenu.height - 8, p.y))
            copyMenu.open()
        }
    }
}
