// 列表（ul / ol）：官方把列表当"有缩进的块"排，样式全在这几条里
// （dsh-client-ui-primitives/lib/markdown/MarkdownText.module.css）：
//   :102  .markdown :where(ul, ol) { margin: 16px 0; padding-left: 18px; }
//   :107  .markdown li:not(:first-child) { margin-top: 6px; }
//   :111  .markdown li > :where(ul, ol) { margin-top: 4px; }
//   :115  .markdown li::marker { line-height: 24px; color: var(--dsw-alias-label-secondary); }
//
// 为什么自绘：Qt 的 MarkdownText 列表是"项目符号 + 24px 缩进 + 0 行距"，标记颜色跟正文一个色；
// 官方是 18px 缩进、项间 6px、标记用 label-secondary（比正文淡一档）。这些从 QML 侧没法改
// QTextListFormat，所以每项自己排：左侧一列标记 + 右边一项富文本（MarkdownInline）。
//
// 取舍：列表项各是一个 TextEdit ⇒ 同一项内可以正常选中/复制，但**跨项拖选**选不到
// （代码块本来就是这样，见 AgentBubble 的 part 分块）。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    objectName: "mdList"

    // block = { kind:"list", items:[{ depth, ordered, marker, text }] }
    property var block: null

    readonly property var items: block ? (block.items || []) : []

    readonly property int indentStep: 18   // :104 padding-left: 18px（每嵌套一层再 +18）
    readonly property int itemGap: 6       // :108 li:not(:first-child) { margin-top: 6px }
    readonly property int nestedGap: 4     // :112 li > ul/ol { margin-top: 4px }
    readonly property int lineBox: 24 + (Theme.font.body - 14)   // 官方 base 24/14，正文 13 ⇒ 23

    // 官方 li::marker 的颜色 = --dsw-alias-label-secondary；Theme.color.textSecondary 在亮/暗两套
    // 主题里正好就是官方这两个值（light #61666B = neutral-bluish-700，dark #CFD3D6 = neutral-bluish-300）。
    readonly property color markerColor: Theme.color.textSecondary

    width: parent ? parent.width : 0
    implicitHeight: column.implicitHeight
    height: implicitHeight

    FontMetrics {
        id: metrics
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
    }

    MarkdownInline {
        id: inline
    }

    // 标记列宽：官方是 18px 的 padding-left，标记（outside）画在这 18px 里、离正文约 6px。
    // 有序列表到两位数（"10."）时 18px 装不下，这里按最长标记放宽 —— 否则标记会被切掉。
    readonly property int markerColumn: {
        var widest = 0
        for (var i = 0; i < root.items.length; ++i) {
            var advance = metrics.advanceWidth(root.items[i].marker || "")
            if (advance > widest)
                widest = advance
        }
        return Math.max(root.indentStep, Math.ceil(widest) + 6)
    }

    function gapFor(index) {
        if (index <= 0)
            return 0
        var current = root.items[index]
        var previous = root.items[index - 1]
        if (!current || !previous)
            return 0
        // 嵌套列表的第一项：官方是 li > ul { margin-top: 4px }
        return (current.depth > previous.depth) ? root.nestedGap : root.itemGap
    }

    Column {
        id: column
        width: parent.width
        spacing: 0

        Repeater {
            model: root.items.length

            delegate: Item {
                id: row

                required property int index

                readonly property var itemData: root.items[row.index] || ({})
                readonly property int depth: Math.min(row.itemData.depth || 0, 3)
                readonly property int indent: row.depth * root.indentStep
                readonly property int rowGap: root.gapFor(row.index)
                // 第一行的行盒高（标记按它垂直居中：Text 的 implicitHeight 比 TextEdit 的行盒高一点）
                readonly property int firstLine: Math.min(body.contentHeight, root.lineBox)

                width: column.width
                height: row.rowGap + body.contentHeight

                Text {
                    id: marker

                    x: row.indent
                    y: row.rowGap + Math.round((row.firstLine - marker.height) / 2)
                    width: Math.max(0, root.markerColumn - 6)
                    text: row.itemData.marker || ""
                    horizontalAlignment: Text.AlignRight
                    color: root.markerColor
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body
                }

                TextEdit {
                    id: body
                    objectName: "bubbleText"
                    x: row.indent + root.markerColumn
                    y: row.rowGap
                    width: Math.max(0, row.width - body.x)
                    height: contentHeight
                    text: inline.render(row.itemData.text || "", {
                        lineHeight: root.lineBox,
                        codePixelSize: Theme.font.body * 0.875
                    })
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
                    font.pixelSize: Theme.font.body
                }
            }
        }
    }
}
