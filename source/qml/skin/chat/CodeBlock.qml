// 代码块单元（旧壳 CodeBlockView 的既定规则原样保留）：
//   * 代码围栏是**独立单元**、不是正文的一部分；
//   * 上方一行语言标签 + 复制按钮；
//   * **不换行**，横向可滚动；
//   * 语法高亮由 C++ 侧 CodeHighlighter 生成 RichText（D3 的"无头复用"）。
//
// ⚠️ 横向滚动条**不能用 Flickable 的附加属性**（`ScrollBar.horizontal`）：它是覆盖层，
// 画在 Flickable 底边上，而 Flickable 底边 = 文字底部时就会压住最后一行（用户实测：文字结束 y=158、
// 滚动条 y=159 开始，等于 0 间隙）。这里改成**结构上分两行**：
//     [ 代码区（Flickable，高度=内容高，封顶 420） ]
//     [ 6px 间隙 ]
//     [ 滚动条一行（仅超宽时出现） ]
// 这样滚动条与文字不在同一块区域里，**任何情况下都不可能压住文字**。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string language: ""
    property string code: ""
    property string html: ""

    // 代码内容是否比可视宽度宽（决定要不要那一行滚动条）
    readonly property bool overflowX: scroller.contentWidth > scroller.width + 1

    implicitHeight: header.implicitHeight + frame.implicitHeight + Theme.spacing.labelGap

    // 语言标签行
    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: 20

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.language.length > 0 ? root.language : "code"
            color: Theme.color.textCaption
            font.family: "Consolas"
            font.italic: true
            font.pixelSize: Theme.font.small
        }

        IconButton {
            id: copyButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            boxSize: 20
            glyphSize: Theme.font.small
            glyph: "⧉"
            tooltip: qsTrId("common_copy")
            onClicked: {
                codeView.selectAll()
                codeView.copy()
                codeView.deselect()
            }
        }
    }

    // 代码本体：不换行 + 横向滚动
    Rectangle {
        id: frame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacing.labelGap
        // 高度由内部两行（代码区 + 滚动条行）算出来，不再手工加减
        implicitHeight: body.implicitHeight + 2 * Theme.spacing.labelGap
        radius: Theme.radius.control
        // 底色 = 原版 `#codeBlockView { background: {{hoverBg}} }` 的灰面（不是近白的 inputBg）：
        // 灰面自己就有层次，描边只是收个边；换成近白底后描边被迫当"唯一的层次线索"，看着就"太深"。
        color: Theme.color.hoverBg
        border.width: 1
        border.color: Theme.color.border
        clip: true

        Column {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.spacing.labelGap
            anchors.rightMargin: Theme.spacing.labelGap
            anchors.topMargin: Theme.spacing.labelGap
            spacing: 6   // 文字与滚动条之间的呼吸间隙

            Flickable {
                id: scroller
                width: parent.width
                // 代码区高度 = 内容高，封顶 420（超高只裁切，与旧壳一致：纵向没有滚动条）
                height: Math.min(codeView.contentHeight, 420)
                contentWidth: Math.max(width, codeView.implicitWidth)
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                TextEdit {
                    id: codeView
                    width: Math.max(scroller.width, implicitWidth)
                    // 只占代码区，不再往下伸（否则会伸进滚动条那一行）
                    height: scroller.height
                    text: root.html.length > 0 ? root.html : root.code
                    textFormat: TextEdit.RichText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    color: Theme.color.textPrimary
                    selectionColor: Theme.color.selectionBg
                    selectedTextColor: Theme.color.textPrimary
                    font.family: "Consolas"
                    font.pixelSize: Theme.font.caption
                }
            }

            // 滚动条自己占一行（不是附加覆盖层）——文字下方，绝不可能压住文字
            AppScrollBar {
                id: hbar
                orientation: Qt.Horizontal
                width: parent.width
                height: Theme.size.scrollbarThickness
                visible: root.overflowX
                policy: root.overflowX ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                size: scroller.visibleArea.widthRatio
                position: scroller.visibleArea.xPosition
                // 拖动滚动条 -> 移动代码；代码被别的方式滚动时用下面的 Binding 回写位置
                onPositionChanged: scroller.contentX = position * (scroller.contentWidth - scroller.width)
            }

            // 代码区自己滚动（滚轮/拖动）时把位置同步回滚动条；拖动期间不回写，避免打架
            Binding {
                target: hbar
                property: "position"
                value: scroller.visibleArea.xPosition
                when: !hbar.pressed
                restoreMode: Binding.RestoreBindingOrValue
            }
        }
    }
}
