// Fluent · 窗口背景层（Mica 近似，2026-11 定稿）
// Win11 的 Mica = 桌面壁纸的低饱和着色 + 顶部一层更亮的 rim；长驻表面用它，
// Acrylic 只给浮层（菜单/弹窗，色板里 menuBg 已经是 rgba）。
// 拿不到系统壁纸，用「蓝调渐变 + 左上蓝/右下紫两团薄雾 + 细噪点」逼近 bloom 壁纸的观感。
//
// ⚠️ 两条硬约束（踩过）：
//  1. 画布必须**按圆角路径裁剪**：窗口外轮廓是 surface 的圆角矩形，而 Item.clip 只裁矩形，
//     整块 fillRect 会把四角盖成方的。
//  2. 主题切换（Theme.changed）要 requestPaint：否则 Canvas 只在尺寸变化时重绘，
//     深色模式下会一直留着浅色渐变。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    function roundRectPath(ctx, x, y, w, h, r) {
        ctx.beginPath()
        ctx.moveTo(x + r, y)
        ctx.arcTo(x + w, y, x + w, y + h, r)
        ctx.arcTo(x + w, y + h, x, y + h, r)
        ctx.arcTo(x, y + h, x, y, r)
        ctx.arcTo(x, y, x + w, y, r)
        ctx.closePath()
    }

    Canvas {
        id: mica
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            // 圆角裁剪：非最大化时所有绘制都收进窗口的圆角矩形里
            if (root.edgeToEdge) {
                ctx.beginPath()
                ctx.rect(0, 0, width, height)
            } else {
                root.roundRectPath(ctx, 0, 0, width, height, root.cornerRadius)
            }
            ctx.clip()

            const dark = Theme.isDark

            // 底色：浅 #FAFCFF→#EBF1FB / 深 #1F2026→#16171B（只留平缓渐变，无光晕）
            const base = ctx.createLinearGradient(0, 0, width * 0.3, height)
            base.addColorStop(0, dark ? "#1F2026" : "#FAFCFF")
            base.addColorStop(1, dark ? "#16171B" : "#EBF1FB")
            ctx.fillStyle = base
            ctx.fillRect(0, 0, width, height)

            // 2026-11：光晕完全取消（用户要求），只剩底色渐变 + 噪点

            // 细噪点：压住渐变的色带
            let seed = 71523
            const rand = function () {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff
                return seed / 0x7fffffff
            }
            const ink = Theme.color.textPrimary
            const count = Math.min(900, Math.round(width * height / 2600))
            ctx.fillStyle = Qt.rgba(ink.r, ink.g, ink.b, dark ? 0.040 : 0.022)
            for (let i = 0; i < count; ++i)
                ctx.fillRect(rand() * width, rand() * height, 1, 1)
        }

        // 主题切换 / 强调色变化：重绘（Canvas 默认不会自己跟）
        Connections {
            target: Theme
            function onChanged() { mica.requestPaint() }
        }
    }

    // 顶部 rim：Win11 的 mica rim（1px 更亮的一条；圆角处收进圆角边，避免探出角外）
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.edgeToEdge ? 0 : root.cornerRadius
        anchors.rightMargin: root.edgeToEdge ? 0 : root.cornerRadius
        height: 1
        color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.09 : 0.6)
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
