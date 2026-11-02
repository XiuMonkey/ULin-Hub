// 素纸 · 窗口背景层
// 契约与内置件一致：根是 Item、锚满父级、不接鼠标事件、描边与叠加绘制由本件负责，
// 最大化/全屏时不留圆角与描边。
// 造型：宣纸底上压一层 32px 方格（像稿纸）+ 极淡的纤维噪点 + 一圈墨线收边。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window
    readonly property int gridStep: 32

    // 稿纸方格 + 纸纤维：只画一次（尺寸变了才重画），避免每帧重绘
    Canvas {
        id: paper
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            const ink = Theme.color.textPrimary
            const step = root.gridStep

            // 方格
            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.rgba(ink.r, ink.g, ink.b, 0.045)
            ctx.beginPath()
            for (let x = step; x < width; x += step) {
                ctx.moveTo(x + 0.5, 0)
                ctx.lineTo(x + 0.5, height)
            }
            for (let y = step; y < height; y += step) {
                ctx.moveTo(0, y + 0.5)
                ctx.lineTo(width, y + 0.5)
            }
            ctx.stroke()

            // 纸纤维：固定种子的伪随机短线，不随窗口抖动
            let seed = 20261030
            const rand = function () {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff
                return seed / 0x7fffffff
            }
            ctx.strokeStyle = Qt.rgba(ink.r, ink.g, ink.b, 0.028)
            ctx.beginPath()
            const count = Math.min(900, Math.round(width * height / 2600))
            for (let i = 0; i < count; ++i) {
                const x = rand() * width
                const y = rand() * height
                const len = 3 + rand() * 7
                ctx.moveTo(x, y)
                ctx.lineTo(x + len, y + (rand() - 0.5) * 2)
            }
            ctx.stroke()
        }
    }

    // 四角墨线：纸页被压在桌面上那种收边
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: root.edgeToEdge ? 0 : 1
        border.color: Theme.color.windowBorder
    }

    // 上沿一道略深的压痕，让标题栏与画布分开
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Theme.size.titleBarHeight
        height: 1
        color: Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g, Theme.color.textPrimary.b, 0.06)
        visible: !root.edgeToEdge
    }
}
