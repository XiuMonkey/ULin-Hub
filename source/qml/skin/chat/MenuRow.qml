// 菜单行（模型 / 思考档位共用；原版 ModelSelectorRow，objectName modelSelectorOption）
// 最小高：带副标题 36、只有名字 28；内边距 8/6。
// 选中时右侧画几何对钩 —— 两段 45° 直线、方头端点、尖角转折。
// 不用 "✓"(U+2713) 字符：字体里那是流线型弧线，笔画粗细不匀、两端圆头，一放大就"随手画"。

import QtQuick
import ULinHub 1.0

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property bool checked: false

    // 次要行（思考档位）降一档色：档位是"当前用多深"，不是"用哪个模型"，
    // 原来两者都是 13px 近黑，一屏 7 行全是同重的黑字，视线没有落点。
    property bool muted: false

    signal chosen()

    // 行高按两行文字的实际占用给余量：13px 标题 + 12px 副标题实测 35，上下各留 3.5；
    // 无副标题的档位行只有单行 13px，28 即可。
    implicitHeight: Math.max(root.subtitle.length > 0 ? 36 : 28, textColumn.implicitHeight + 7)
    radius: 10

    // 选中态用 textPrimary 的 8% 灰，不用 accent 蓝：输入区是灰阶调子，弹层里冒出一块蓝会抢焦点。
    // 取 textPrimary 而非写死灰值，light/dark 共用一份表达式；hover 再深一档（13%），三态才分得开。
    readonly property color selectedBg: Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g,
                                                Theme.color.textPrimary.b, 0.08)
    readonly property color selectedBgHover: Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g,
                                                     Theme.color.textPrimary.b, 0.13)

    color: root.checked
        ? (rowMouse.containsMouse ? root.selectedBgHover : root.selectedBg)
        : ((rowMouse.containsMouse || rowMouse.pressed) ? Theme.color.chipHoverBg : "transparent")

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Column {
            id: textColumn
            width: parent.width - check.width - parent.spacing
            spacing: 0

            Text {
                width: parent.width
                text: root.title
                wrapMode: Text.Wrap
                maximumLineCount: 2
                // 选中时文字仍取 textPrimary：颜色留给底和对钩，文字再上色会有三处强调，
                // 反而看不出重点。次要行正常态淡一档，选中态回到主色。
                color: (root.muted && !root.checked) ? Theme.color.textSecondary
                                                    : Theme.color.textPrimary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.title
                font.weight: (root.muted && !root.checked) ? Font.Normal : Theme.font.weightMedium
            }

            Text {
                width: parent.width
                visible: root.subtitle.length > 0
                text: root.subtitle
                elide: Text.ElideRight
                color: Theme.color.textTertiary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
            }
        }

        Canvas {
            id: check

            width: 18
            height: 18
            visible: root.checked

            // 对钩跟文字同色（textPrimary）：灰底 + 灰勾是"状态标记"，不承担强调职责，
            // 勾形本身（两段 45°、方头）已足够区分。
            property color ink: Theme.color.textPrimary
            onInkChanged: requestPaint()

            anchors.verticalCenter: parent.verticalCenter

            // 两段各 45° 的等斜率直线，夹角 90°，转折在左下、尖角朝右下；
            // lineCap=butt（方头）+ lineJoin=miter（尖角）：圆头/圆角会立刻变回"流线型"。
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = ink
                ctx.lineWidth = 1.9
                ctx.lineCap = "butt"
                ctx.lineJoin = "miter"
                ctx.beginPath()
                ctx.moveTo(2.6, 8.9)
                ctx.lineTo(7.2, 13.5)
                ctx.lineTo(16.4, 4.3)
                ctx.stroke()
            }

            // Canvas 的绘制结果会被缓存；勾首次显示时必须主动重绘一次
            onVisibleChanged: if (visible) requestPaint()
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.chosen()
    }
}
