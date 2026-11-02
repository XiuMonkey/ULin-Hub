// 霜玻 · 窗口背景层
// 契约与内置件一致：根 Item、锚满父级、不接鼠标、描边自己画、最大化时去圆角。
// 造型：冷色底 + 三团柔和光斑（Canvas 径向渐变）+ 一层细颗粒，作为玻璃面板后面的"环境光"。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    function rgbaOf(c, a) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
            + Math.round(c.b * 255) + "," + a + ")"
    }

    Canvas {
        id: aura
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            const dark = Theme.isDark
            const accent = Theme.color.accent

            // 三团光：左上一个冷蓝、右上一点洋红、底部一片青
            const blobs = [
                { x: width * 0.16, y: height * 0.10, r: Math.max(width, height) * 0.55 },
                { x: width * 0.92, y: height * 0.18, r: Math.max(width, height) * 0.42 },
                { x: width * 0.62, y: height * 1.02, r: Math.max(width, height) * 0.62 }
            ]
            const tints = dark
                ? [rgbaOf(accent, 0.55), "rgba(110,120,255,0.24)", "rgba(0,190,170,0.20)"]
                : [rgbaOf(accent, 0.42), "rgba(150,195,255,0.32)", "rgba(140,225,215,0.34)"]

            for (let i = 0; i < blobs.length; ++i) {
                const b = blobs[i]
                const g = ctx.createRadialGradient(b.x, b.y, 0, b.x, b.y, b.r)
                g.addColorStop(0, tints[i])
                g.addColorStop(0.55, tints[i].replace(/,[\d.]+\)$/, ",0.10)"))
                g.addColorStop(1, tints[i].replace(/,[\d.]+\)$/, ",0)"))
                ctx.fillStyle = g
                ctx.fillRect(0, 0, width, height)
            }

            // 细颗粒：压住渐变的色带
            let seed = 97631
            const rand = function () {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff
                return seed / 0x7fffffff
            }
            const ink = Theme.color.textPrimary
            const count = Math.min(1400, Math.round(width * height / 1800))
            ctx.fillStyle = Qt.rgba(ink.r, ink.g, ink.b, dark ? 0.05 : 0.03)
            for (let i = 0; i < count; ++i)
                ctx.fillRect(rand() * width, rand() * height, 1, 1)
        }
    }

    // 顶部一道高光，模拟玻璃边缘
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.12 : 0.7)
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
