// 转圈指示器：Canvas 画一段圆弧 + 旋转动画。
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

    // 沿父链找 overlayAnimating（PageOverlay 把它绑到"卡片正在展开/收起"）：动画期间静止不转，
    // 帧预算全给动画；沿父链找，免得每个页面各自传入而漏掉。
    readonly property bool pageAnimating: {
        let p = root.parent
        while (p) {
            if (p.overlayAnimating !== undefined)
                return p.overlayAnimating
            p = p.parent
        }
        return false
    }

    // 转的是外层容器而不是 Canvas 本身：旋转只走 Item 变换，Canvas 只在尺寸/颜色变化时重绘。
    Item {
        id: rotor
        anchors.fill: parent
        visible: root.running

        Canvas {
            id: canvas
            anchors.fill: parent

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const radius = Math.min(width, height) / 2 - root.lineWidth
                if (radius <= 0)
                    return
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, radius, 0, Math.PI * 1.45, false)   // 留一段缺口，转起来才看得出方向
                ctx.strokeStyle = root.color
                ctx.lineWidth = root.lineWidth
                ctx.lineCap = "round"
                ctx.stroke()
            }
        }
    }

    RotationAnimator {
        target: rotor
        from: 0
        to: 360
        duration: 900
        loops: Animation.Infinite

        // 只在真正可见时才转：running 默认 true 且 loops 无限，页面被遮住时仍会空转，白烧 CPU 抢帧。
        // 门控放在组件里，比让每个页面各自声明 overlayAnimating 更不容易漏。
        running: root.running && root.visible && !root.pageAnimating
    }
}
