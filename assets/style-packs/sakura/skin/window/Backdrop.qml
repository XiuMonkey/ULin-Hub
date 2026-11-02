// 樱岚 · 窗口背景层
// 契约与内置件一致：根 Item、锚满父级、不接鼠标、描边自己画、最大化时去圆角。
// 造型：晨雾粉的竖向渐变 + 稀疏飘落的花瓣（贝塞尔花瓣，固定种子，不随窗口抖动）。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    Canvas {
        id: sky
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            const dark = Theme.isDark
            const accent = Theme.color.accent

            // 从顶部一层极淡的玫瑰，落到接近无色
            const g = ctx.createLinearGradient(0, 0, 0, height)
            g.addColorStop(0, "rgba(" + Math.round(accent.r * 255) + ","
                           + Math.round(accent.g * 255) + ","
                           + Math.round(accent.b * 255) + ","
                           + (dark ? 0.16 : 0.10) + ")")
            g.addColorStop(0.55, "rgba(255,255,255,0)")
            g.addColorStop(1, "rgba(" + Math.round(accent.r * 255) + ","
                           + Math.round(accent.g * 255) + ","
                           + Math.round(accent.b * 255) + ","
                           + (dark ? 0.10 : 0.06) + ")")
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)

            // 花瓣：五片水滴形（一段二次曲线去、一段回），靠 rotate 绕心排开
            const fill = "rgba(" + Math.round(accent.r * 255) + ","
                + Math.round(accent.g * 255) + "," + Math.round(accent.b * 255) + ","

            let seed = 5150
            const rand = function () {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff
                return seed / 0x7fffffff
            }

            const count = Math.max(10, Math.round(width * height / 62000))
            for (let i = 0; i < count; ++i) {
                const cx = rand() * width
                const cy = rand() * height
                const r = 8 + rand() * 15
                const rot = rand() * Math.PI * 2
                const alpha = (dark ? 0.10 : 0.14) + rand() * 0.12

                ctx.save()
                ctx.translate(cx, cy)
                ctx.rotate(rot)
                ctx.fillStyle = fill + alpha + ")"
                for (let p = 0; p < 5; ++p) {
                    ctx.save()
                    ctx.rotate(p * Math.PI * 2 / 5)
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.quadraticCurveTo(r * 0.52, -r * 0.34, 0, -r)
                    ctx.quadraticCurveTo(-r * 0.52, -r * 0.34, 0, 0)
                    ctx.fill()
                    ctx.restore()
                }
                // 花芯
                ctx.beginPath()
                ctx.arc(0, 0, r * 0.16, 0, Math.PI * 2)
                ctx.fillStyle = fill + Math.min(1, alpha + 0.12) + ")"
                ctx.fill()
                ctx.restore()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: root.edgeToEdge ? 0 : 1
        border.color: Theme.color.windowBorder
    }
}
