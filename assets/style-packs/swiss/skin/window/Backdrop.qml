// 苏黎世 · 窗口背景层
// 契约与内置件一致：根 Item、锚满父级、不接鼠标、描边自己画、最大化时去圆角。
// 造型：纯版面 —— 48px 版面网格 + 左上/右下两处红色定位标记，除此之外什么都不画。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    Canvas {
        id: layout
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            const ink = Theme.color.textPrimary

            // 版面网格：48px 主格 + 12px 细分
            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.rgba(ink.r, ink.g, ink.b, 0.05)
            ctx.beginPath()
            for (let x = 48; x < width; x += 48) {
                ctx.moveTo(x + 0.5, 0)
                ctx.lineTo(x + 0.5, height)
            }
            for (let y = 48; y < height; y += 48) {
                ctx.moveTo(0, y + 0.5)
                ctx.lineTo(width, y + 0.5)
            }
            ctx.stroke()

            ctx.strokeStyle = Qt.rgba(ink.r, ink.g, ink.b, 0.025)
            ctx.beginPath()
            for (let x = 12; x < width; x += 12) {
                ctx.moveTo(x + 0.5, 0)
                ctx.lineTo(x + 0.5, height)
            }
            for (let y = 12; y < height; y += 12) {
                ctx.moveTo(0, y + 0.5)
                ctx.lineTo(width, y + 0.5)
            }
            ctx.stroke()
        }
    }

    // 版面的两处红色定位标记
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: 10
        height: 10
        color: Theme.color.accent
        visible: !root.edgeToEdge
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: 28
        height: 4
        color: Theme.color.accent
        visible: !root.edgeToEdge
    }

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: root.edgeToEdge ? 0 : 1
        border.color: Theme.color.windowBorder
    }
}
