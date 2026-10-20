// 引用块（blockquote）：官方只做两件事 —— 一条 2px 左边线、正文右移 14px，**不改字色**。
// 出处 dsh-client-ui-primitives/lib/markdown/MarkdownText.module.css:151
//   .markdown blockquote { border-left: 2px solid var(--dsw-alias-label-caption);
//                          margin: 16px 0 0; padding-left: 14px; }
//
// 为什么不能交给 Qt：TextEdit.MarkdownText 的引用块只有"整体缩进"（实测缩进约 84px 且没有色条），
// QTextDocument 的 HTML 子集里 <blockquote> 同样是纯缩进（离屏实测截图见 build/md-probe/out-html.png D 段）。
// 所以左边线必须自己画，引用正文交给 MarkdownInline（行内粗体/链接/代码用官方 mono 栈）。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    objectName: "mdQuote"

    // block = { kind:"quote", text:"..." }
    property var block: null

    // 官方 --dsw-alias-label-caption（dsh-client.css:4485/4486）：
    //   light = 静态 neutral-bluish-400 #ADB2B8 ； dark = neutral-bluish-600 #81858C
    // Theme.color.textCaption 的 dark 值正好是 #81858C，但 light 映射成了 #A2A4A6（neutral-400，
    // 比官方 caption 深一档），所以这里亮暗各写一份字面值，保证和官方数值完全一致。
    readonly property color barColor: Theme.isDark ? "#81858C" : "#ADB2B8"

    readonly property int barWidth: 2      // :152 border-left: 2px
    readonly property int textIndent: 16   // :154 padding-left: 14px + 2px 边线
    readonly property int lineBox: 24 + (Theme.font.body - 14)   // 官方 base 24/14，正文 13 ⇒ 23

    width: parent ? parent.width : 0
    implicitHeight: body.contentHeight
    height: implicitHeight

    Rectangle {
        width: root.barWidth
        height: parent.height
        color: root.barColor
    }

    MarkdownInline {
        id: inline
    }

    TextEdit {
        id: body
        objectName: "bubbleText"
        x: root.textIndent
        width: Math.max(0, root.width - root.textIndent)
        height: contentHeight
        text: root.block
            ? inline.render(root.block.text || "", {
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
