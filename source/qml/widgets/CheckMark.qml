// 统一对钩：两段等斜率直线 + 方头端点 + 尖角转折（同 skin/chat/MenuRow 里那枚）。
// 不用 U+2713 字符：字体里的 ✓ 是流线弧线，粗细不匀、圆头，放大后很脏。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    property color color: Theme.color.textPrimary
    property int size: 18

    implicitWidth: size
    implicitHeight: size

    onColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            ctx.save()
            ctx.scale(width / 18, height / 18)   // 下面的坐标按 18×18 设计栅格写，随 size 等比缩放
            ctx.strokeStyle = root.color
            ctx.lineWidth = 1.9
            ctx.lineCap = "butt"
            ctx.lineJoin = "miter"
            ctx.beginPath()
            ctx.moveTo(2.6, 8.9)
            ctx.lineTo(7.2, 13.5)
            ctx.lineTo(16.4, 4.3)
            ctx.stroke()
            ctx.restore()
        }
        onVisibleChanged: if (visible) requestPaint()
        Component.onCompleted: requestPaint()
    }
}
