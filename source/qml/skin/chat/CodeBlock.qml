// 代码块单元：按 DSH 官方版 md code-block 规格复刻
// （`dsh-client-ui-primitives/lib/markdown/CodeBlock.module.css`）：
//   * 整块 = 一个圆角容器 `--dsw-radius-lg`(16)，底色 `--dsw-alias-markdown-code-block`
//     （light #F9FAFB / dark #1B1B1C）；**没有描边** —— 官方层次全靠底色，不靠线；
//   * 顶部 banner：padding 9px 14px、11px/18px 等宽字语言标签 + 右侧复制键
//     （banner 底色 markdown-code-block-banner，比块体只差一档，刻意做得几乎看不见）；
//   * 正文 padding 16px、字号 `--dsw-font-markdown-code-block` = 11px/19px 等宽。
//
// 与官方唯一的刻意差异：官方 `white-space: pre-wrap; word-break: break-all`（长行折行），
// 这里保留**不换行 + 横向滚动**（旧壳行为，也是这个文件原先专门修过的一个点）。
//
// ⚠️ 横向滚动条**不能用 Flickable 的附加属性**（`ScrollBar.horizontal`）：它是覆盖层，
// 画在 Flickable 底边上，而 Flickable 底边 = 文字底部时就会压住最后一行（用户实测：文字结束 y=158、
// 滚动条 y=159 开始，等于 0 间隙）。这里改成**结构上分两行**：
//     [ 代码区（Flickable，高度=内容高，封顶 420） ]
//     [ 6px 间隙 ]
//     [ 滚动条一行（仅超宽时出现） ]

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

    // 官方圆角 16（--dsw-radius-lg）。取 pill（16）而不是 control（12）：control 这一档
    // 现在是官方 md(12) 给按钮/输入框用的，代码块在官方是**大一档**的 lg。
    readonly property int blockRadius: Theme.radius.pill
    readonly property int bannerHeight: 34
    readonly property int bodyPadding: 16

    // 新 token 带兜底：老风格包里没有这两个键时退回近义的既有 token，不至于整块渲染失败
    readonly property color blockBg: Theme.color.codeBlockBg || Theme.color.inputBg
    readonly property color bannerBg: Theme.color.codeBlockBannerBg || Theme.color.panelBg

    implicitHeight: frame.implicitHeight

    Rectangle {
        id: frame
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: banner.height + contentBody.implicitHeight
        radius: root.blockRadius
        color: root.blockBg
        clip: true

        // ---- banner ----
        Rectangle {
            id: banner
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.bannerHeight
            radius: root.blockRadius
            color: root.bannerBg

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: root.language.length > 0 ? root.language : "text"
                color: Theme.color.textPrimary
                font.family: "Consolas"
                font.pixelSize: Theme.font.small      // 11px（官方 banner 11/18）
            }

            IconButton {
                id: copyButton
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                boxSize: 22
                glyphSize: Theme.font.caption
                glyph: "⧉"
                tooltip: qsTrId("common_copy")
                onClicked: {
                    codeView.selectAll()
                    codeView.copy()
                    codeView.deselect()
                }
            }

            // banner 下沿的细分隔线（官方 banner 与块体只差一档底色，靠这条线收口）
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.color.border
            }
        }

        Column {
            id: contentBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: banner.bottom
            anchors.leftMargin: root.bodyPadding
            anchors.rightMargin: root.bodyPadding
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
                    font.pixelSize: Theme.font.small      // 11px（官方 --dsw-font-markdown-code-block）
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

            // 底部留白（官方 pre 的 padding 16px 的下半截；上面由 topMargin 给了一部分）
            Item { width: 1; height: Theme.spacing.labelGap }
        }
    }
}
