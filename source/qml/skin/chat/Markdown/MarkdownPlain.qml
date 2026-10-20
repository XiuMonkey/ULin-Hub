// 纯 Markdown 段落块：正文里**不属于**表格 / 分界线 / 引用块 / 列表 / 标题的那些行，仍然整段交给
// 一个可选中/复制的 TextEdit 渲染。段落内部的粗体、斜体、链接、行内代码由 MarkdownInline 转成
// QTextDocument 富文本（HTML 子集）—— 这样行内 code 才能用官方 mono 栈，而不是 Qt Markdown
// 导入器在 Windows 上落到的 Courier New。
//
// 官方基准（MarkdownText.module.css）：正文 `font: --dsw-font-markdown-base` 是 14/24，
// 本项目正文 13px ⇒ 行高按官方 delta 同一减法 = 24 + (13 - 14) = 23（:5-10、:45-47）。
//
// 为什么外面要包一个 Item：MarkdownBody 按块切成若干段，每段都要有统一的宽高契约
// （`width` + `implicitHeight`）；而 TextEdit **没有可用的 implicitHeight**（AgentBubble 里
// 旧注释已经记过这个坑），只能由外层 Item 从 contentHeight 转出来。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    objectName: "mdPlain"

    // block = { kind:"plain", text:"..." }
    property var block: null

    // 官方 --dsw-font-markdown-base 的 24px 行高按 13px 正文折算
    readonly property int lineBox: 24 + (Theme.font.body - 14)

    width: parent ? parent.width : 0
    implicitHeight: body.contentHeight
    height: implicitHeight

    MarkdownInline {
        id: inline
    }

    // 正文规格与 AgentBubble 改动前一致（13px / Microsoft YaHei UI / textPrimary）
    // ⚠️ 不能写 lineHeight —— 那是 Text 专有属性，TextEdit 没有；写上会让整个 AgentBubble 变成
    //    "Type AgentBubble unavailable"，外面表现就是**消息列表整片空白**（踩过一次，qmllint 不报）。
    //    行高改由 MarkdownInline 生成的 <p style="line-height:23px"> 控制。
    TextEdit {
        id: body
        objectName: "bubbleText"
        width: parent.width
        height: contentHeight
        text: root.block
            ? inline.renderBlocks(root.block.text || "", {
                  lineHeight: root.lineBox,
                  codePixelSize: Theme.font.body * 0.875
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
        font.pixelSize: Theme.font.body
    }
}
