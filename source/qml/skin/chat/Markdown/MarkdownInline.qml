// 行内 Markdown → QTextDocument 富文本（HTML 子集）。
//
// 为什么需要它：Qt 6.11 的 `TextEdit.MarkdownText` 把行内代码交给
// `QFontDatabase::systemFont(QFontDatabase::FixedFont)`，Windows 上落到 Courier New ——
// 字重偏细、字形与官方 mono 栈不一致（用户截图里表格中的 `Program.cs / .csproj`）。
// 官方 CSS（dsh-client-ui-primitives/lib/markdown/MarkdownText.module.css:163-174）给行内 code 的是：
//   font: var(--dsw-font-markdown-code);                 // 12px/19px（以 14px 正文为基准）
//   font-family: var(--ds-font-family-code);             // 见 dsh-client.css:4476 的官方 mono 栈
//   font-size: 0.875em !important;                       // 相对正文
//   background-color: var(--dsw-alias-markdown-inline-code);
//   border: .5px solid var(--dsw-alias-border-l1);
//   border-radius: var(--dsw-radius-sm);                 // 8px
//   padding: 0 5px;
// QTextDocument 的 HTML 子集支持 font-family / font-size / background-color / padding，
// 但不支持 border-radius（Qt 的富文本没有内联圆角），所以圆角仍由“背景贴字”近似 ——
// 这与改动前的 MarkdownText 观感一致，没有额外退化。
//
// 其余行内语法（粗体 / 斜体 / 删除线 / 链接）也在这里转成 HTML，避免整段退回 Qt 的
// Markdown 导入器；换行按 CommonMark：软换行变空格，行尾两个空格或反斜杠是硬换行。

import QtQuick
import ULinHub 1.0

QtObject {
    id: root

    // 官方 mono 栈（dsh-client.css:4476）：
    //   "SF Mono", "JetBrains Mono", "Fira Code", Consolas, "Liberation Mono",
    //   Menlo, Courier, "PingFang SC", "Microsoft YaHei"
    // 取**第一个本机已安装**的族，Windows 上实际落到 Consolas（与官方 Chromium 一致），
    // 而不是 Qt Markdown 导入器的 Courier New。
    readonly property string monoFamily: {
        var stack = ["SF Mono", "JetBrains Mono", "Fira Code", "Consolas",
                     "Liberation Mono", "Menlo", "Courier", "PingFang SC", "Microsoft YaHei"]
        var available = Qt.fontFamilies()
        for (var i = 0; i < stack.length; ++i) {
            if (available.indexOf(stack[i]) !== -1)
                return stack[i]
        }
        return "Consolas"
    }

    // 官方 --dsw-font-markdown-base 在 14px 正文下是 14/24；本项目正文是 13px，
    // 按官方 `--dsh-content-font-delta` 的同一减法，行高 = 24 + (13 - 14) = 23。
    readonly property int basePixelSize: Theme.font.body
    readonly property int baseLineHeight: 24 + (Theme.font.body - 14)

    readonly property color codeBackground: Theme.color.inlineCodeBg
    readonly property color linkColor: Theme.color.linkText
    readonly property color textColor: Theme.color.textPrimary

    function cssColor(value) {
        var text = String(value)
        // QML color.toString() 是 #AARRGGBB；QTextDocument 的 CSS 颜色用 #RRGGBB。
        if (text.length === 9 && text.charAt(0) === "#")
            return "#" + text.substring(3)
        return text
    }

    function escapeHtml(value) {
        return String(value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
    }

    function findCodeClose(text, start, runLength) {
        var i = start
        while (i < text.length) {
            if (text.charAt(i) !== "`") {
                ++i
                continue
            }
            var run = 0
            while (i + run < text.length && text.charAt(i + run) === "`")
                ++run
            if (run === runLength)
                return i
            i += run
        }
        return -1
    }

    // 行内 code → <span>；字号按官方 0.875em（正文 13px ⇒ 11.375px），字号/字重可覆盖：
    // 标题里的 code 按官方 `font: inherit` 用标题字号与字重。
    function codeSpan(content, options) {
        var size = options.codePixelSize !== undefined
            ? options.codePixelSize
            : root.basePixelSize * 0.875
        var style = "font-family:" + root.monoFamily
            + ";font-size:" + size + "px"
            + ";color:" + root.cssColor(root.textColor)
            + ";background-color:" + root.cssColor(root.codeBackground)
            + ";padding:0 5px;border-radius:8px;"
        if (options.inheritCodeWeight !== true)
            style += "font-weight:400;"
        return "<span style=\"" + style + "\">" + root.escapeHtml(content) + "</span>"
    }

    // 普通文本片段：先转义，再处理链接 / 粗体 / 斜体 / 删除线。
    function formatText(value, options) {
        var html = root.escapeHtml(value)
        var link = root.cssColor(root.linkColor)
        var strongStyle = options.inheritStrong === true ? "" : "font-weight:600"

        // 图片先降级成 alt 文本（QML 气泡没有图片加载器）
        html = html.replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
        // 链接：官方 --dsw-alias-link。href 先存成占位符，避免后面的强调规则改写 URL 里的 _ / *。
        var urls = []
        html = html.replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+&quot;[^&]*&quot;)?\)/g,
                            function (_, label, href) {
            urls.push(href)
            return "<a href=\"\u0001U" + (urls.length - 1) + "\u0002\" style=\"color:" + link
                + ";text-decoration:none\">" + label + "</a>"
        })
        if (strongStyle !== "") {
            html = html.replace(/\*\*([^*]+)\*\*/g,
                                "<span style=\"" + strongStyle + "\">$1</span>")
            html = html.replace(/__([^_]+)__/g,
                                "<span style=\"" + strongStyle + "\">$1</span>")
        }
        else {
            html = html.replace(/\*\*([^*]+)\*\*/g, "$1")
            html = html.replace(/__([^_]+)__/g, "$1")
        }
        html = html.replace(/(^|[^*])\*([^*\n]+)\*/g, "$1<i>$2</i>")
        html = html.replace(/(^|[^_])_([^_\n]+)_/g, "$1<i>$2</i>")
        html = html.replace(/~~([^~]+)~~/g, "<s>$1</s>")
        html = html.replace(/\u0001U(\d+)\u0002/g, function (_, index) {
            return urls[Number(index)]
        })
        return html
    }

    function wrap(inner, options) {
        var lineHeight = options.lineHeight !== undefined
            ? options.lineHeight
            : root.baseLineHeight
        var style = "line-height:" + lineHeight + "px;color:" + root.cssColor(root.textColor)
        if (options.textAlign !== undefined)
            style += ";text-align:" + options.textAlign
        return "<div style=\"" + style + "\">" + inner + "</div>"
    }

    // 单段行内渲染（表格单元格 / 标题 / 列表项 / 引用）。
    function render(markdown, options) {
        options = options || {}
        var text = String(markdown === undefined || markdown === null ? "" : markdown)
        // 原文里的 <br> 先变成硬换行哨兵，避免被 escapeHtml 转义成可见文本
        text = text.replace(/<br\s*\/?>/gi, "\u000B")

        var output = ""
        var buffer = ""

        function flush() {
            if (buffer.length > 0) {
                output += root.formatText(buffer, options)
                buffer = ""
            }
        }

        var i = 0
        while (i < text.length) {
            var ch = text.charAt(i)

            if (ch === "`") {
                var run = 0
                while (i + run < text.length && text.charAt(i + run) === "`")
                    ++run
                var close = root.findCodeClose(text, i + run, run)
                if (close !== -1) {
                    flush()
                    var content = text.substring(i + run, close)
                    // CommonMark：code span 内的换行变空格；首尾各一个空格且内容非空时去掉
                    content = content.replace(/[ \t]*\n[ \t]*/g, " ")
                    if (content.length > 1 && content.charAt(0) === " "
                        && content.charAt(content.length - 1) === " "
                        && content.trim() !== "")
                        content = content.substring(1, content.length - 1)
                    output += root.codeSpan(content, options)
                    i = close + run
                    continue
                }
            }

            if (ch === "\u000B") {
                flush()
                output += "<br>"
                ++i
                continue
            }

            if (ch === "\n") {
                var hard = / {2,}$/.test(buffer) || /\\$/.test(buffer)
                if (/ {2,}$/.test(buffer))
                    buffer = buffer.replace(/[ \t]+$/, "")
                else if (/\\$/.test(buffer))
                    buffer = buffer.substring(0, buffer.length - 1)
                flush()
                if (options.hardBreaks === true || hard)
                    output += "<br>"
                else
                    output += " "
                ++i
                continue
            }

            buffer += ch
            ++i
        }
        flush()
        return options.noWrap === true ? output : root.wrap(output, options)
    }

    // 多段落正文：官方 .markdown p { margin: 16px 0 }（MarkdownText.module.css:45-47）。
    // 首段不吃上 margin、末段不吃下 margin，等价于官方的
    // `.markdown > *:first-child { margin-top: 0 }` / `:last-child { margin-bottom: 0 }`。
    function renderBlocks(markdown, options) {
        options = options || {}
        var text = String(markdown === undefined || markdown === null ? "" : markdown)
        var paragraphs = text.replace(/\r\n/g, "\n").split(/\n[ \t]*\n+/)
        var output = ""
        for (var i = 0; i < paragraphs.length; ++i) {
            if (paragraphs[i].trim() === "")
                continue
            var last = true
            for (var j = i + 1; j < paragraphs.length; ++j) {
                if (paragraphs[j].trim() !== "") {
                    last = false
                    break
                }
            }
            var margin = last ? "margin:0" : "margin:0 0 16px 0"
            var lineHeight = options.lineHeight !== undefined
                ? options.lineHeight
                : root.baseLineHeight
            var paragraphOptions = {}
            for (var key in options)
                paragraphOptions[key] = options[key]
            paragraphOptions.noWrap = true
            output += "<p style=\"" + margin + ";line-height:" + lineHeight
                + "px;color:" + root.cssColor(root.textColor) + "\">"
                + root.render(paragraphs[i], paragraphOptions) + "</p>"
        }
        return output
    }
}
