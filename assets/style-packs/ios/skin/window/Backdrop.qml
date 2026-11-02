// iOS 玻璃 · 窗口背景层
// ---------------------------------------------------------------------------
// 契约与内置件一致：根 Item、锚满父级、不接鼠标、描边自己画、最大化时去圆角。
//
// 造型 = iOS 26 的"极光"壁纸：四团低饱和色斑压在白灰（亮）/ 近黑（暗）底上，
// 外加一层暗角与一层细颗粒。它存在的意义不只是背景好看 —— 上面的玻璃片要靠它
// 才有东西可"透"：玻璃里的颜色就是从这张图上同一位置重新采样出来的（见 Glass.qml）。
import QtQuick
import ULinHub 1.0
import "wallpaper.js" as Wallpaper

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    // 底色：窗口圆角之外的地方靠它兜底，Canvas 只负责色斑
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.color.windowBg
    }

    Canvas {
        id: aurora
        anchors.fill: parent

        // 降采样再上采样 = 一层廉价的模糊，顺带把渐变的色带抹掉（软件渲染下也成立）
        canvasSize: Qt.size(Math.max(8, Math.round(width / 3)),
                            Math.max(8, Math.round(height / 3)))

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (width <= 0 || height <= 0)
                return

            ctx.save()
            // 圆角裁切：窗口圆角之外不许有壁纸，否则四角会露出方角
            const r = Math.min(root.cornerRadius, Math.min(width, height) / 2)
            ctx.beginPath()
            ctx.moveTo(r, 0)
            ctx.lineTo(width - r, 0)
            ctx.quadraticCurveTo(width, 0, width, r)
            ctx.lineTo(width, height - r)
            ctx.quadraticCurveTo(width, height, width - r, height)
            ctx.lineTo(r, height)
            ctx.quadraticCurveTo(0, height, 0, height - r)
            ctx.lineTo(0, r)
            ctx.quadraticCurveTo(0, 0, r, 0)
            ctx.closePath()
            ctx.clip()

            Wallpaper.paint(ctx, width, height, {
                dark: Theme.isDark,
                base: Theme.color.windowBg,
                soften: 1
            })
            Wallpaper.vignette(ctx, width, height, Theme.isDark)
            ctx.restore()
        }
    }

    // 细颗粒（比 1px 噪音更像"雾面"）：跟着包一起走的 96×96 平铺图，缺文件也不报错
    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl("grain.png")
        fillMode: Image.Tile
        opacity: Theme.isDark ? 0.26 : 0.34
        smooth: false
        visible: status === Image.Ready
    }

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: root.edgeToEdge ? 0 : 1
        border.color: Theme.color.windowBorder
    }
}
