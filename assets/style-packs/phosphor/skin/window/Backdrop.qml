// 荧光屏 · 窗口背景层
// 契约与内置件一致：根 Item、锚满父级、不接鼠标、描边自己画、最大化时去圆角。
// 造型：CRT —— 近黑底 + 每 3px 一条扫描线 + 屏幕中心一圈微光 + 四角暗角。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window
    readonly property color glow: Theme.color.accent

    Canvas {
        id: screen
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            // 扫描线：3px 一行，1px 亮线（磷光余晖）
            ctx.fillStyle = Qt.rgba(root.glow.r, root.glow.g, root.glow.b,
                                    Theme.isDark ? 0.045 : 0.03)
            for (let y = 0; y < height; y += 3)
                ctx.fillRect(0, y, width, 1)

            // 中心微光
            const g = ctx.createRadialGradient(width / 2, height * 0.42, 0,
                                               width / 2, height * 0.42,
                                               Math.max(width, height) * 0.62)
            g.addColorStop(0, "rgba(" + Math.round(root.glow.r * 255) + ","
                           + Math.round(root.glow.g * 255) + ","
                           + Math.round(root.glow.b * 255) + ","
                           + (Theme.isDark ? 0.10 : 0.07) + ")")
            g.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)

            // 暗角
            const v = ctx.createRadialGradient(width / 2, height / 2, Math.min(width, height) * 0.32,
                                               width / 2, height / 2, Math.max(width, height) * 0.78)
            v.addColorStop(0, "rgba(0,0,0,0)")
            v.addColorStop(1, Theme.isDark ? "rgba(0,0,0,0.55)" : "rgba(20,40,30,0.10)")
            ctx.fillStyle = v
            ctx.fillRect(0, 0, width, height)
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
