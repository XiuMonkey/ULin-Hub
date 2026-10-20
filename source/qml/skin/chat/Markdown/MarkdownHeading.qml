// Markdown 标题块（h1–h6）：官方 `MarkdownText.module.css:16-43` 的排版规格。
//
// 为什么必须自绘：Qt 的 `TextEdit.MarkdownText` 只把 `## x` 变成“字号大一点的粗体”，
// 既不能设行高，也没有块级 margin。用户看到的“2. 等待期间没有阻塞”就是 h2：比正文略大、
// 很黑、行距紧、上下贴住。官方是 700 字重 + 固定行高 + 32/16px margin。
//
// 官方 token（dsh-client-ui-theme/lib/client.js:1160，以 14px 正文为基准）：
//   h1: 700 21/30   h2: 700 19/28   h3: 700 18/26
//   h4: 600 14/24   h5/h6: 600 14/24（base-strong）
//   margin: h1–h3 32px 0 16px；h4–h6 16px 0（MarkdownText.module.css:16-39）
// 本项目正文是 13px（Theme.font.body），按官方 `--dsh-content-font-delta` 的同一减法整体 -1：
//   h1 20/29 · h2 18/27 · h3 17/25 · h4 13/23 · h5/h6 13/23。
//
// 标题里的行内 code 按官方 `:176-179 { font: inherit; font-family: mono }` 用标题字号/字重，
// 不再缩到 0.875em。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    objectName: "mdHeading"

    // block = { kind:"heading", level:1..6, text:"..." }
    property var block: null

    readonly property int level: {
        if (!root.block)
            return 1
        return Math.max(1, Math.min(6, root.block.level || 1))
    }

    // 与官方 token 的 calc(21px + delta) 等逐项对应；delta = Theme.font.body - 14。
    readonly property int pixelSize: {
        var delta = Theme.font.body - 14
        switch (root.level) {
        case 1: return 21 + delta
        case 2: return 19 + delta
        case 3: return 18 + delta
        default: return 14 + delta
        }
    }

    readonly property int lineBox: {
        var delta = Theme.font.body - 14
        switch (root.level) {
        case 1: return 30 + delta
        case 2: return 28 + delta
        case 3: return 26 + delta
        default: return 24 + delta
        }
    }

    // 官方 h1–h3 是 700，h4–h6 是 600
    readonly property int fontWeight: root.level <= 3 ? 700 : 600

    width: parent ? parent.width : 0
    implicitHeight: body.contentHeight
    height: implicitHeight

    MarkdownInline {
        id: inline
    }

    TextEdit {
        id: body

        objectName: "bubbleHeading"
        width: parent.width
        height: contentHeight
        text: root.block
            ? inline.render(root.block.text || "", {
                  lineHeight: root.lineBox,
                  codePixelSize: root.pixelSize,
                  inheritCodeWeight: true,
                  inheritStrong: true
              })
            : ""
        textFormat: TextEdit.RichText
        wrapMode: TextEdit.Wrap
        readOnly: true
        selectByMouse: true
        persistentSelection: true
        activeFocusOnPress: false
        color: Theme.color.textPrimary
        selectionColor: Theme.color.selectionBg
        selectedTextColor: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: root.pixelSize
        font.weight: root.fontWeight
    }
}
