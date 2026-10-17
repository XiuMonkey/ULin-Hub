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

    // 所在页面若声明了 overlayAnimating（PageOverlay 会把它绑到"卡片正在展开/收起"），
    // 动画展示期间就**静止不转**：那段时间帧预算全给动画，转圈只会抢帧。
    // 沿父链找，所以不用每个页面各自传入（漏一个就白改）。
    readonly property bool pageAnimating: {
        let p = root.parent
        while (p) {
            if (p.overlayAnimating !== undefined)
                return p.overlayAnimating
            p = p.parent
        }
        return false
    }

    // ⚠️ 转的是**这个外层容器**，不是 Canvas 本身：Qt Quick 里对 Canvas 做旋转会让它每帧
    // 重绘/重新上传纹理（实测转圈可见期间持续掉帧，隐藏后立刻恢复流畅）。旋转一个普通 Item
    // 只是变换矩阵，代价几乎为零，外观完全一样。
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
                // 留一段缺口，转起来才看得出方向
                ctx.arc(width / 2, height / 2, radius, 0, Math.PI * 1.45, false)
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
                // 只在**真正可见**时才转：running 默认 true，而这里是一条 Animation.Infinite ——
        // 页面被遮住/收起（visible=false）时动画仍会继续跑，白烧 CPU 并抢帧（插件市场/扩展管理
        // 这类"页 + 转圈"的界面打开时不流畅就是这个原因）。门控在组件里做，比让每个页面自己
        // 声明 overlayAnimating 更不容易漏。
        running: root.running && root.visible && !root.pageAnimating
    }
}
