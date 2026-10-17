// 转圈指示器：Canvas 画一段圆弧 + 旋转动画。取代旧壳手绘的 SpinnerWidget。
// 不依赖 QtQuick.Shapes（少一个部署模块），Canvas 只画一次、转的是整个 item。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property bool running: true
    property color color: Theme.color.accent
    property int lineWidth: 3

    implicitWidth: 24
    implicitHeight: 24

    onColorChanged: canvas.requestPaint()
    onLineWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: root.running

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const radius = Math.min(width, height) / 2 - root.lineWidth
            if (radius <= 0)
                return
            ctx.beginPath()
            // 留一段缺口，转起来才看得出方向
            ctx.arc(width / 2, height / 2, radius, 0, Math.PI * 1.45, false)
            ctx.strokeStyle = root.color
            ctx.lineWidth = root.lineWidth
            ctx.lineCap = "round"
            ctx.stroke()
        }
    }

    RotationAnimator {
        target: canvas
        from: 0
        to: 360
        duration: 900
        loops: Animation.Infinite
                // 只在**真正可见**时才转：running 默认 true，而这里是一条 Animation.Infinite ——
        // 页面被遮住/收起（visible=false）时动画仍会继续跑，白烧 CPU 并抢帧（插件市场/扩展管理
        // 这类"页 + 转圈"的界面打开时不流畅就是这个原因）。门控在组件里做，比让每个页面自己
        // 声明 overlayAnimating 更不容易漏。
        running: root.running && root.visible
    }
}
