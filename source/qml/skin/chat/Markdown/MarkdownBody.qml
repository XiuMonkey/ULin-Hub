// 气泡正文的 Markdown 排版器：把官方 md 的块级样式补齐，行内语法交给 MarkdownInline。
//
// == 为什么需要它（根因） ==
// 正文原来是一整段 `TextEdit { textFormat: TextEdit.MarkdownText }`，即 Qt 6.11 自带的 md4c 导入器。
// 离屏实测（build/md-probe/，截图 out-md.png）它给出的是：
//   · 表格：**支持**，但画成电子表格 —— 每格四周 1px 深灰实线（官方只有横向发丝线）、表头无字重、
//           cell padding 约 4px（官方 10px 16px）、没有列宽策略（官方首列贴左、末列贴右、宽表横向滚动）；
//   · `---`：**不画线**，只留一个空行（实测 "AAAA\n\n---\n\nBBBB" 的行高 = 3×15px，中间那行是空的）；
//   · 引用块：只有整体缩进（实测缩进 ≈ 84px），没有官方那条 2px 左边线；
//   · 列表：标记与正文同色、缩进约 24px、项间 0 行距（官方 label-secondary 标记、18px、6px）。
// QTextDocument 的 HTML 子集也补不上（out-html.png）：td 的 `style="border-bottom:…"` 被忽略、
// `<blockquote>` 仍是纯缩进、`border-collapse` 不生效。所以这四类块只能按 QML 排版做。
//
// == 做法 ==
// 只在**行首**把正文切成块：表格 / 分界线 / 引用块 / 列表 / 标题 由 QML 自绘，其余连续行原样塞回一个
// 富文本 TextEdit（MarkdownPlain）—— 段落、粗体、链接、行内代码。文本内容不做任何改写（不插标记、
// 不转义），选择/复制仍由各块的 TextEdit 承担。
//
// 标题为什么也要拆：Qt 的 Markdown 导入器把 `## x` 画成“大一点的粗体”，不能设行高、没有块级
// margin；官方 h1–h6 有精确的 font-size / line-height / weight / margin（MarkdownText.module.css:16-39），
// 只能由 MarkdownHeading.qml 逐项照抄。行内 code 的等宽栈同理（MarkdownInline.qml）。
//
// == 块间距 ==
// 官方块间距来自 CSS margin（.markdown p { margin: 16px 0 } 等）。Qt 的 Markdown 导入器**没有**段落
// margin（实测两个段落 = 2×15px，零间距），所以这里按官方数值在块与块之间补：
//   p / table : 16    （table 自身没有 margin —— 官方那 16 来自相邻 <p>，本气泡正文没有 p margin）
//   ul / ol   : 16
//   blockquote: 16 上 / 0 下（:151 margin: 16px 0 0）
//   hr        : 32（:143 margin: 32px 0）
//   h1–h3     : 32 上 / 16 下（:16-29 margin: 32px 0 16px）
//   h4–h6     : 16 上 / 16 下（:31-39 margin: 16px 0）
// 相邻两块取 `max(上一块的下 margin, 这一块的上 margin)`（等价于 CSS 的 margin 折叠）。
// 首块不吃上 margin、末块不吃下 margin（官方 .markdown > *:first-child/:last-child 归零）。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    objectName: "mdBody"

    property string text: ""

    // 官方 --dsw-alias-border-l2（dsh-client.css:4485/4486）：light #0000001A / dark #FFFFFF1F
    readonly property color ruleColor: Theme.isDark ? Qt.rgba(1, 1, 1, 0.122) : Qt.rgba(0, 0, 0, 0.102)

    readonly property var blocks: root.parseBlocks(root.text)

    width: parent ? parent.width : 0
    implicitHeight: column.implicitHeight
    height: implicitHeight

    // =========================================================================
    // 解析：Markdown → 块数组（只在行首切，行内内容原样保留）
    // =========================================================================

    function indentOf(line) {
        var match = /^[ \t]*/.exec(line)
        return match ? match[0].length : 0
    }

    function isThematicBreak(line) {
        return /^ {0,3}((\*[ \t]*){3,}|(-[ \t]*){3,}|(_[ \t]*){3,})$/.test(line)
    }

    // 列表项：`- ` / `* ` / `+ ` / `1. ` / `1) `；返回标记与正文（正文里保留行内 markdown）
    function listItem(line) {
        var match = /^([ \t]*)(?:([-*+])[ \t]+|(\d{1,9})([.)])[ \t]+)(.*)$/.exec(line)
        if (!match)
            return null
        var ordered = match[3] !== undefined
        return {
            indent: match[1].length,
            ordered: ordered,
            marker: ordered ? (match[3] + match[4]) : "•",
            text: match[5] || ""
        }
    }

    // ATX 标题：`# x` … `###### x`（CommonMark 要求 # 后有空格；`##` 单独一行是空标题）
    function atxHeading(line) {
        var match = /^ {0,3}(#{1,6})(?:[ \t]+(.*?))?[ \t]*$/.exec(line)
        if (!match)
            return null
        var text = match[2] || ""
        // 收尾的闭合 # 不算正文（`## 标题 ##` → `标题`）
        text = text.replace(/[ \t]+#+[ \t]*$/, "")
        return { kind: "heading", level: match[1].length, text: text }
    }

    // Setext 标题的下划线：`===` → h1，`---` → h2。
    // 注意 `---` 只有紧跟在段落文字后面才是 h2；空行包围时仍是分界线（见下面的 isThematicBreak 分支）。
    function setextLevel(line) {
        if (/^ {0,3}=+[ \t]*$/.test(line))
            return 1
        if (/^ {0,3}-+[ \t]*$/.test(line))
            return 2
        return 0
    }

    // GFM 行切分：跳过行首/行尾的边框竖线、`\|` 转义、行内代码里的竖线
    function splitRow(line) {
        var source = line.trim()
        var start = (source.charAt(0) === "|") ? 1 : 0
        var end = source.length
        if (end > start && source.charAt(end - 1) === "|") {
            var slashes = 0
            for (var b = end - 2; b >= start && source.charAt(b) === "\\"; --b)
                ++slashes
            if (slashes % 2 === 0)
                --end
        }
        var cells = []
        var current = ""
        var inCode = false
        for (var i = start; i < end; ++i) {
            var ch = source.charAt(i)
            if (ch === "\\" && i + 1 < end) {
                current += source.charAt(i + 1)
                ++i
                continue
            }
            if (ch === "`")
                inCode = !inCode
            if (ch === "|" && !inCode) {
                cells.push(current.trim())
                current = ""
                continue
            }
            current += ch
        }
        cells.push(current.trim())
        return cells
    }

    // 分隔行 `| --- | :-: | ---: |` → 每列的对齐；不是分隔行返回 null
    function delimiterRow(line) {
        if (line.indexOf("-") === -1)
            return null
        var cells = splitRow(line)
        if (cells.length === 0)
            return null
        var aligns = []
        for (var i = 0; i < cells.length; ++i) {
            var cell = cells[i]
            if (!/^:?-{1,}:?$/.test(cell))
                return null
            var left = cell.charAt(0) === ":"
            var right = cell.charAt(cell.length - 1) === ":"
            aligns.push(left && right ? "center" : (right ? "right" : "left"))
        }
        return aligns
    }

    function parseBlocks(markdown) {
        var output = []
        if (!markdown)
            return output

        var lines = markdown.split("\n")
        var total = lines.length
        var pending = []

        function flushPlain() {
            while (pending.length > 0 && pending[0].trim() === "")
                pending.shift()
            while (pending.length > 0 && pending[pending.length - 1].trim() === "")
                pending.pop()
            if (pending.length > 0)
                output.push({ kind: "plain", text: pending.join("\n") })
            pending = []
        }

        var i = 0
        while (i < total) {
            var line = lines[i]

            // ---- ATX 标题：`#{1,6} ` 开头 ----
            var heading = root.atxHeading(line)
            if (heading) {
                flushPlain()
                output.push(heading)
                ++i
                continue
            }

            // ---- Setext 标题：段落文字 + 下一行 `===` / `---` ----
            // 只有“像段落”的行才吃下划线；列表项、引用、表格行各有自己的优先级。
            if (i + 1 < total && line.trim() !== ""
                && !root.isThematicBreak(line)
                && !/^ {0,3}=+[ \t]*$/.test(line)
                && !/^ {0,3}(?:[>|]|[-*+][ \t]|\d{1,9}[.)][ \t])/.test(line)) {
                var setext = root.setextLevel(lines[i + 1])
                if (setext > 0) {
                    flushPlain()
                    output.push({ kind: "heading", level: setext, text: line.trim() })
                    i += 2
                    continue
                }
            }

            // ---- 表格：本行含竖线 + 下一行是分隔行，且列数一致 ----
            if (line.indexOf("|") !== -1 && i + 1 < total) {
                var aligns = root.delimiterRow(lines[i + 1])
                if (aligns) {
                    var header = root.splitRow(line)
                    if (header.length === aligns.length) {
                        flushPlain()
                        var body = []
                        var row = i + 2
                        while (row < total) {
                            var candidate = lines[row]
                            if (candidate.trim() === "" || candidate.indexOf("|") === -1)
                                break
                            var cells = root.splitRow(candidate)
                            while (cells.length < header.length)
                                cells.push("")
                            if (cells.length > header.length)
                                cells = cells.slice(0, header.length)
                            body.push(cells)
                            ++row
                        }
                        output.push({ kind: "table", header: header, rows: body, aligns: aligns })
                        i = row
                        continue
                    }
                }
            }

            // ---- 分界线：3 个以上 - * _。段落行后面的 `---` 已在上面按 setext h2 吃掉 ----
            if (root.isThematicBreak(line)) {
                flushPlain()
                output.push({ kind: "hr" })
                ++i
                continue
            }

            // ---- 引用块：连续以 > 开头的行 ----
            if (/^ {0,3}>/.test(line)) {
                flushPlain()
                var quoted = []
                var quoteLine = i
                while (quoteLine < total && /^ {0,3}>/.test(lines[quoteLine])) {
                    quoted.push(lines[quoteLine].replace(/^ {0,3}> ?/, ""))
                    ++quoteLine
                }
                output.push({ kind: "quote", text: quoted.join("\n") })
                i = quoteLine
                continue
            }

            // ---- 列表 ----
            var first = root.listItem(line)
            if (first) {
                flushPlain()
                var items = []
                var stack = []
                var baseIndent = first.indent
                var last = null
                var cursor = i
                while (cursor < total) {
                    var current = lines[cursor]
                    if (current.trim() === "") {
                        // 空行：只有后面仍是列表项才算松散列表继续，否则列表到此为止
                        var probe = cursor
                        while (probe < total && lines[probe].trim() === "")
                            ++probe
                        var nextItem = (probe < total) ? root.listItem(lines[probe]) : null
                        if (nextItem && nextItem.indent <= baseIndent + 1) {
                            cursor = probe
                            continue
                        }
                        break
                    }
                    var parsed = root.listItem(current)
                    if (parsed) {
                        if (stack.length === 0)
                            stack.push(parsed.indent)
                        else if (parsed.indent > stack[stack.length - 1])
                            stack.push(parsed.indent)
                        else {
                            while (stack.length > 1 && parsed.indent < stack[stack.length - 1])
                                stack.pop()
                        }
                        last = {
                            depth: Math.min(stack.length - 1, 2),
                            indent: parsed.indent,
                            ordered: parsed.ordered,
                            marker: parsed.marker,
                            text: parsed.text
                        }
                        items.push(last)
                        ++cursor
                        continue
                    }
                    // 续行（缩进 >= 项缩进 + 2）并进当前项；Qt 会把软换行渲染成空格，与 CommonMark 一致
                    if (last && root.indentOf(current) >= last.indent + 2) {
                        last.text = last.text + "\n" + current.trim()
                        ++cursor
                        continue
                    }
                    break
                }
                output.push({ kind: "list", items: items })
                i = cursor
                continue
            }

            pending.push(line)
            ++i
        }

        flushPlain()
        return output
    }

    // =========================================================================
    // 块间距（官方 CSS margin，见文件头）
    // =========================================================================

    function marginTop(block) {
        if (!block)
            return 0
        switch (block.kind) {
        case "hr": return 32
        case "list": return 16
        case "quote": return 16
        case "heading": return (block.level || 1) <= 3 ? 32 : 16   // :16-39
        default: return 16      // p / table
        }
    }

    function marginBottom(block) {
        if (!block)
            return 0
        switch (block.kind) {
        case "hr": return 32
        case "list": return 16
        case "quote": return 0  // :151 margin: 16px 0 0
        case "heading": return 16   // h1–h6 下 margin 都是 16（:16-39）
        default: return 16
        }
    }

    function gapFor(index) {
        if (index <= 0)
            return 0
        return Math.max(root.marginBottom(root.blocks[index - 1]), root.marginTop(root.blocks[index]))
    }

    function componentFor(kind) {
        switch (kind) {
        case "table": return tableComponent
        case "quote": return quoteComponent
        case "list": return listComponent
        case "heading": return headingComponent
        case "hr": return ruleComponent
        default: return plainComponent
        }
    }

    // =========================================================================
    // 排版
    // =========================================================================

    Component { id: plainComponent; MarkdownPlain { } }
    Component { id: tableComponent; MarkdownTable { } }
    Component { id: quoteComponent; MarkdownQuote { } }
    Component { id: listComponent; MarkdownList { } }
    Component { id: headingComponent; MarkdownHeading { } }

    // 分界线：官方 .markdown hr { height: 0.5px; margin: 32px 0; background: var(--dsw-alias-border-l2) }
    // （MarkdownText.module.css:143-149）。QML 几何允许小数 ⇒ DPR 2 时正好 1 物理像素。
    Component {
        id: ruleComponent

        Item {
            objectName: "mdRule"
            // 与其它块统一的数据入口（本块没有内容，占位即可 —— Loader 的 onLoaded 会给它赋 block）
            property var block: null
            width: parent ? parent.width : 0
            implicitHeight: 0.5
            height: implicitHeight

            Rectangle {
                width: parent.width
                height: 0.5
                color: root.ruleColor
            }
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: 0

        Repeater {
            // 数字模型：流式续写时块数不变就不重建 delegate（只有内容变），避免整段正文每 chunk 重建
            model: root.blocks.length

            delegate: Item {
                id: slot

                required property int index

                readonly property var blockData: root.blocks[slot.index] || null
                readonly property int gap: root.gapFor(slot.index)

                width: column.width
                // Loader 的 implicitHeight 会自动跟随被加载项的 implicitHeight（未加载时为 0）
                implicitHeight: slot.gap + loader.implicitHeight
                height: implicitHeight

                Loader {
                    id: loader

                    y: slot.gap
                    width: parent.width
                    property var blockData: slot.blockData
                    sourceComponent: root.componentFor(slot.blockData ? slot.blockData.kind : "")

                    // 块数据每 chunk 都是新对象 ⇒ 用 binding 而不是赋值，否则流式时块内容不刷新
                    onLoaded: {
                        if (loader.item)
                            loader.item.block = Qt.binding(function () { return loader.blockData })
                    }
                }
            }
        }
    }
}
