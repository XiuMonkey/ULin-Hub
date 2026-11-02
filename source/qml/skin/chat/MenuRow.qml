// 菜单行（模型 / 思考档位共用；原版 ModelSelectorRow，objectName modelSelectorOption）
// 最小高：带副标题 40、不带副标题 32（档位行只有名字，38 会显得空）；
// 内边距 8/6；选中时右侧画几何对钩 —— 两段 45° 直线、方头端点、尖角转折。
// 不用 "✓"(U+2713) 字符：字体里那是流线型弧线，笔画粗细不匀、两端圆头，
// 一放大就"随手画"，跟这个界面全是直角的调子冲突。

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

    // 行高按"两行文字的实际占用"给余量，不再拍一个 40 的整数：
    // 13px 标题 + 12px 副标题实测 implicitHeight 35，原来 +12 的余量让整行涨到 47，
    // 三行模型叠起来就是"每行之间空一格"。收成 +7（上下各 3.5），整行 42。
    // 无副标题的档位行 28：单行 13px 文字配 32 还是松。
    implicitHeight: Math.max(root.subtitle.length > 0 ? 36 : 28,
                             textColumn.implicitHeight + 7)
    radius: 10

    // 选中态用 textPrimary 的低透明度灰，不用 accent 蓝：
    // 输入区整体是灰阶调子（见 Composer.qml 顶部"视觉约定"），弹层里冒出一块蓝会
    // 抢走输入框的注意力。取 textPrimary 而不是写死一个灰值，是为了两个主题共用一份
    // 表达式 —— light 下浅灰（#0F1115 的 8%），dark 下自动变成淡白（#F9FAFB 的 8%）；
    // 写死 #E9EBF0 在暗色主题里会比菜单底还暗，选中行反而凹进去。
    // 浓度比 chipHoverBg（5% / 8%）高一档，hover 再深一档，三态才分得开。
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
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: (root.muted && !root.checked) ? Font.Normal : Theme.font.weightMedium
            }

            Text {
                width: parent.width
                visible: root.subtitle.length > 0
                text: root.subtitle
                elide: Text.ElideRight
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }
        }

        Canvas {
            id: check

            width: 18
            height: 18
            visible: root.checked

            // 对钩跟文字同色（textPrimary），不另设一色：
            // 灰底 + 灰勾是"状态标记"，不承担强调职责；勾形本身（两段 45°、方头）
            // 已经足够把它和文字区分开。
            property color ink: Theme.color.textPrimary
            onInkChanged: requestPaint()

            anchors.verticalCenter: parent.verticalCenter

            // 两段等斜率（各 45°）的直线，转折在左下、尖角朝右下 —— 这是构图上最"正"的勾形：
            // 短臂与长臂夹角 90°，两条臂的斜率互为相反数，看起来不会一边陡一边缓。
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
