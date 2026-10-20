// 官方表格的 QML 复刻（GFM pipe table）。
//
// 为什么不用 Qt 自带的 Markdown 表格：Qt 6.11 的 TextEdit.MarkdownText **支持** GFM 表格，但它画的是
// 一张"电子表格"——每个单元格四周都有 1px 深灰实线、表头没有字重、单元格 padding 只有 4px 左右。
// 官方（MarkdownText.module.css .tableScroll）是**只有横向发丝线、没有竖线、没有外框**的样式。
// QTextDocument 的 HTML 子集也不支持"只画下边框"（实测：td 上的 `style="border-bottom:…"` 被忽略，
// `border-collapse` 也不生效），所以表格只能按 QML 排版做。
//
// 数值全部来自 `dsh-client-ui-primitives/lib/markdown/MarkdownText.module.css`：
//   :229  .tableScroll table { border-collapse: collapse; width: max-content }   ← 宽表：自然宽 + 横向滚动
//   :239  .tableFill table    { width: 100% }                                     ← 少于 4 列：撑满正文宽
//   :244  th { text-align: start; padding: 10px 16px; border-bottom: .5px solid border-l3;
//              font: --dsw-font-markdown-table-head (500 13/22); max-width: min(30vw,320px); min-width: 100px }
//   :255  td { padding: 10px 16px; border-bottom: .5px solid border-l2;
//              font: --dsw-font-markdown-table (400 13/22); 同 max/min }
//   :264  th:first-child, td:first-child { padding-left: 0 }   ← 首列贴左（表格左缘就是文字左缘）
//   :269  td:last-child { padding-right: 0 }                   ← 末列贴右
//   :229  官方没有表头底色、没有斑马纹、没有外框、没有竖线 —— 只有每行一条下边框。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ULinHub 1.0
import "../../../widgets"

Item {
    id: root

    objectName: "mdTable"

    // block = { kind:"table", header:[...], rows:[[...]], aligns:["left"|"center"|"right"] }
    property var block: null

    property color textColor: Theme.color.textPrimary
    property color selectionColor: Theme.color.selectionBg
    property color selectedTextColor: Theme.color.textPrimary
    property string fontFamily: "Microsoft YaHei UI"
    property int pixelSize: Theme.font.body

    readonly property var header: block ? (block.header || []) : []
    readonly property var rows: block ? (block.rows || []) : []
    readonly property var aligns: block ? (block.aligns || []) : []
    readonly property int columnCount: header.length
    readonly property int rowCount: columnCount > 0 ? 1 + rows.length : 0

    // ---- 官方数值 ----
    readonly property int padX: 16            // :244/:255 padding 10px 16px
    readonly property int padY: 10
    readonly property int lineBox: 22         // --dsw-font-markdown-table-line-height: 22px
    readonly property int minColWidth: 100    // :252/:261 min-width: 100px
    // 官方 :169 行内 code 的 font-size 是 0.875em !important（表格正文 13px ⇒ 11.375px）
    readonly property real inlineCodePixelSize: root.pixelSize * 0.875
    // 官方 max-width: min(30vw, 320px)。桌面窗口下 30vw 恒 ≥ 216，且在常见宽度（≥1067px）下就超过 320，
    // 所以这里直接取上限 320（正文再窄时由下面的"撑满/滚动"逻辑兜住，列宽不会超出气泡）。
    readonly property int maxColWidth: 320

    // 单元格下边框颜色（官方 token --dsw-alias-border-l2 / --dsw-alias-border-l3，见 dsh-client.css:4485/4486）
    //   light: l2 #0000001A(10.2%) / l3 #0000001F(12.2%)
    //   dark : l2 #FFFFFF1F(12.2%) / l3 #FFFFFF29(16.1%)
    // 注意 QML 的 #AARRGGBB 是**透明度在前**，所以这里用 Qt.rgba 按官方 CSS 的 rgba 顺序写。
    readonly property color borderBody: Theme.isDark ? Qt.rgba(1, 1, 1, 0.122) : Qt.rgba(0, 0, 0, 0.102)
    readonly property color borderHead: Theme.isDark ? Qt.rgba(1, 1, 1, 0.161) : Qt.rgba(0, 0, 0, 0.122)

    width: parent ? parent.width : 0
    implicitHeight: layout.implicitHeight
    height: implicitHeight

    // ---- 列宽 ----
    // 官方是浏览器按内容做 auto 布局；QML 没有文字自动排版，这里用 FontMetrics 量"去掉行内标记后的文本"的
    // 单行宽度，再按官方的 min/max 夹住。少于 4 列时把富余宽度按比例摊开（等价于 width:100%）。
    FontMetrics {
        id: metrics
        font.family: root.fontFamily
        font.pixelSize: root.pixelSize
    }

    MarkdownInline {
        id: inline
    }

    function stripInline(markdown) {
        var s = markdown ? String(markdown) : ""
        s = s.replace(/!?\[([^\]]*)\]\([^)]*\)/g, "$1")   // [文字](链接) -> 文字
        s = s.replace(/<br\s*\/?>/gi, " ")
        s = s.replace(/[*_~`]/g, "")                      // 粗体/斜体/删除线/行内代码的标记本身不占宽
        return s
    }

    function cellText(row, column) {
        if (row === 0)
            return root.header[column] || ""
        var body = root.rows[row - 1]
        return body ? (body[column] || "") : ""
    }

    // 单元格内容仍按 GFM 行内语法渲染；行内 code 走官方 mono 栈（MarkdownInline）。
    // 单元格里的换行是硬换行（<br>），行高固定为官方 22px。
    function cellHtml(row, column, alignment) {
        return inline.render(root.cellText(row, column), {
            lineHeight: root.lineBox,
            codePixelSize: root.inlineCodePixelSize,
            hardBreaks: true,
            textAlign: alignment
        })
    }

    // 每列的"自然宽"（只跟单元格文字有关，跟气泡宽无关）—— 单独拎出来，是为了让右栏开合时
    // 每帧只跑下面那段便宜的分配，而不是每帧重新量一遍所有单元格的文字宽度。
    readonly property var naturalWidths: {
        var count = root.columnCount
        if (count === 0)
            return []
        var widths = []
        for (var c = 0; c < count; ++c) {
            var natural = 0
            for (var r = 0; r <= root.rows.length; ++r) {
                var advance = metrics.advanceWidth(root.stripInline(root.cellText(r, c)))
                if (advance > natural)
                    natural = advance
            }
            var left = (c === 0) ? 0 : root.padX
            var right = (c === count - 1) ? 0 : root.padX
            var width = Math.ceil(natural) + left + right
            widths.push(Math.max(root.minColWidth, Math.min(width, root.maxColWidth)))
        }
        return widths
    }

    readonly property var columnWidths: {
        var count = root.columnCount
        if (count === 0)
            return []
        var widths = root.naturalWidths.slice()
        var total = 0
        for (var c = 0; c < count; ++c)
            total += widths[c]

        var available = Math.max(0, root.width)
        // 少于 4 列 ⇒ 官方 .tableFill：width:100%，富余宽度按内容比例分给各列
        if (count < 4 && total < available) {
            var extra = available - total
            var assigned = 0
            for (var k = 0; k < count; ++k) {
                var add = Math.round(extra * (widths[k] / total))
                widths[k] += add
                assigned += add
            }
            // 取整后的零头塞给最后一列：表格右缘必须**正好**落在正文右缘（否则右边露出 1px 锯齿）
            widths[count - 1] += available - total - assigned
        }
        return widths
    }

    readonly property real tableWidth: {
        var sum = 0
        var widths = root.columnWidths
        for (var i = 0; i < widths.length; ++i)
            sum += widths[i]
        return sum
    }

    readonly property bool overflowX: root.columnCount > 0 && root.tableWidth > root.width + 0.5

    Column {
        id: layout
        width: parent.width
        spacing: root.overflowX ? 6 : 0

        Flickable {
            id: scroller
            objectName: "mdTableScroll"
            width: parent.width
            height: table.implicitHeight
            contentWidth: Math.max(width, table.width)
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            // ⚠️ 刻意**不**让 Flickable 接管拖动：一旦 interactive，鼠标在表格里按下拖动就变成滚表，
            //    单元格里的文字再也选不中（"不改变选择/复制行为"）。横向滚动只走下面那条滚动条。
            interactive: false
            clip: true

            Column {
                id: table
                width: root.tableWidth
                spacing: 0

                Repeater {
                    // 数字模型：流式续写时行数不变就不重建 delegate（只有内容变）
                    model: root.rowCount

                    delegate: RowLayout {
                        id: tableRow

                        required property int index

                        readonly property int rowIndex: tableRow.index
                        readonly property bool isHeader: tableRow.rowIndex === 0

                        width: root.tableWidth
                        // Column 按子项 height 下移；Layout 的 height 不会自动跟 implicitHeight，显式绑一下
                        height: implicitHeight
                        spacing: 0

                        Repeater {
                            model: root.columnCount

                            delegate: Item {
                                id: cell

                                required property int index

                                readonly property int columnIndex: cell.index
                                readonly property bool first: cell.columnIndex === 0
                                readonly property bool last: cell.columnIndex === root.columnCount - 1
                                readonly property int padLeft: cell.first ? 0 : root.padX
                                readonly property int padRight: cell.last ? 0 : root.padX
                                // 官方 .tableScroll th 明确写死 text-align: start（:245）——表头不吃列对齐；
                                // td 没有 text-align（:255），由 GFM 分隔行（:---:）决定对齐。
                                readonly property string alignment: tableRow.isHeader
                                    ? "left"
                                    : (root.aligns[cell.columnIndex] || "left")

                                implicitHeight: root.padY * 2
                                                + Math.max(root.lineBox, textItem.contentHeight)
                                Layout.preferredWidth: root.columnWidths[cell.columnIndex] || 0
                                Layout.fillHeight: true

                                TextEdit {
                                    id: textItem

                                    objectName: "bubbleTableCell"
                                    x: cell.padLeft
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.max(0, cell.width - cell.padLeft - cell.padRight)
                                    height: contentHeight
                                    text: root.cellHtml(tableRow.rowIndex, cell.columnIndex, cell.alignment)
                                    textFormat: TextEdit.RichText
                                    wrapMode: TextEdit.Wrap
                                    readOnly: true
                                    selectByMouse: true
                                    persistentSelection: true
                                    activeFocusOnPress: false
                                    color: root.textColor
                                    selectionColor: root.selectionColor
                                    selectedTextColor: root.selectedTextColor
                                    font.family: root.fontFamily
                                    font.pixelSize: root.pixelSize
                                    // 官方 th: 500 13/22（--dsw-font-markdown-table-head）；td: 400 13/22
                                    font.weight: tableRow.isHeader ? Theme.font.weightMedium : Font.Normal
                                    horizontalAlignment: cell.alignment === "right"
                                        ? TextEdit.AlignRight
                                        : (cell.alignment === "center" ? TextEdit.AlignHCenter
                                                                       : TextEdit.AlignLeft)
                                }

                                // 每格一条下边框 = 官方的 border-collapse 单线（相邻格首尾相接 ⇒ 视觉上连续）
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    // 官方 0.5px；QML 几何允许小数，DPR 2 时正好 1 物理像素
                                    height: 0.5
                                    color: tableRow.isHeader ? root.borderHead : root.borderBody
                                }
                            }
                        }
                    }
                }
            }
        }

        // 宽表（≥4 列且自然宽超过正文）才有这一行：与 CodeBlock 的自绘滚动条同一套（结构上占一行，不压字）
        // 高度必须取 AppScrollBar 自己的 implicitHeight（thickness = max(token,10)）；
        // 直接给 Theme.size.scrollbarThickness 会把控件压到 5px，扣掉 padding 后可见把手只剩 1px（实测看不见）。
        //
        // 这里**只用它显示**：position/size 单向绑到 Flickable 的 visibleArea（不写回，避免与拖动互相打架
        // 形成 binding loop —— 实测双向写法会报 "Binding loop detected for property position"）。
        // 拖动/点击由它内部的 MouseArea 负责：按 x 比例映射到 contentX。
        // ⚠️ MouseArea 必须是滚动条的**子项**：放在外面它会变成 Column 的子项，被 Column 重新摆放（实测点不到）。
        AppScrollBar {
            id: hbar
            objectName: "mdTableBar"
            orientation: Qt.Horizontal
            width: parent.width
            height: implicitHeight
            visible: root.overflowX
            policy: root.overflowX ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            size: scroller.visibleArea.widthRatio
            position: scroller.visibleArea.xPosition

            MouseArea {
                id: dragger
                objectName: "mdTableBarDrag"

                anchors.fill: parent
                preventStealing: true

                property real grabOffset: 0

                // 横向可滚距离
                function travel() {
                    return Math.max(0, scroller.contentWidth - scroller.width)
                }

                // 把手长度（框架按 size 摆放；这里只用来算"把左手位置 → contentX"的映射）
                function handleLength() {
                    return Math.max(20, hbar.size * width)
                }

                // 把手的左边缘落在 left 时，内容应该滚到哪里
                function contentForHandleLeft(left) {
                    var span = Math.max(1, width - dragger.handleLength())
                    var ratio = Math.max(0, Math.min(1, left / span))
                    return ratio * dragger.travel()
                }

                function handleLeft() {
                    return hbar.position * (width - dragger.handleLength())
                }

                onPressed: function (mouse) {
                    var length = dragger.handleLength()
                    var left = dragger.handleLeft()
                    // 按在轨道（不在把手上）：先跳到该处；按在把手上：内容不动，只记抓取偏移
                    if (mouse.x < left || mouse.x > left + length)
                        scroller.contentX = dragger.contentForHandleLeft(mouse.x - length / 2)
                    dragger.grabOffset = scroller.contentX - dragger.contentForHandleLeft(mouse.x - length / 2)
                }

                onPositionChanged: function (mouse) {
                    if (!dragger.pressed)
                        return
                    scroller.contentX = Math.max(0, Math.min(dragger.travel(),
                        dragger.grabOffset + dragger.contentForHandleLeft(mouse.x - dragger.handleLength() / 2)))
                }
            }
        }
    }
}
