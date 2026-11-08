// ULin 品牌标记 · 爬行版：从 assets/storage/ulin-hub-mark-crawl.svg 的 SMIL 关键帧
// 直接搬过来的 QML 实现。QtSvg 不支持 SMIL，直接拿 SVG 当动画只会得到静止首帧；
// 这里保留同一套路径和三组自由度（躯体、后柱、前足），用 Canvas 按原 SVG 的 41 帧关键值采样。
// 不用 QtQuick.Shapes：客户端的 Qt 部署没有带 shapes 插件，Canvas 是 QtQuick 内置的。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    property color color: Theme.color.textPrimary
    property bool running: true

    implicitWidth: 20
    implicitHeight: 16

    property real phase: 0
    readonly property var bodyT: [1.1, 0.8, 0.55, 0.32, 0.14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.06, 0.16, 0.25, 0.29, 0.57, 0.76, 1.1]
    readonly property var bodyR: [1.45, 1.35, 1.12, 0.84, 0.59, 0.45, 0.42, 0.29, 0.1, -0.13, -0.37, -0.59, -0.77, -0.88, -0.9, -0.88, -0.84, -0.79, -0.72, -0.66, -0.6, -0.55, -0.52, -0.5, -0.47, -0.37, -0.24, -0.08, 0.05, 0.13, 0.16, 0.24, 0.37, 0.53, 0.69, 0.8, 0.85, 0.94, 1.15, 1.36, 1.45]
    readonly property var rearT: [-1.55, -1.59, -1.6, -1.56, -1.49, -1.38, -1.23, -1.06, -0.86, -0.64, -0.4, -0.15, 0.1, 0.35, 0.59, 0.81, 1.02, 1.2, 1.35, 1.47, 1.55, 1.59, 1.6, 1.56, 1.49, 1.38, 1.23, 1.06, 0.86, 0.64, 0.4, 0.15, -0.1, -0.35, -0.59, -0.81, -1.02, -1.2, -1.35, -1.47, -1.55]
    readonly property var rearR: [-8.72, -8.96, -8.98, -8.78, -8.37, -7.75, -6.93, -5.95, -4.82, -3.57, -2.24, -0.85, 0.57, 1.96, 3.31, 4.58, 5.74, 6.75, 7.6, 8.26, 8.72, 8.96, 8.98, 8.78, 8.37, 7.75, 6.93, 5.95, 4.82, 3.57, 2.24, 0.85, -0.57, -1.96, -3.31, -4.58, -5.74, -6.75, -7.6, -8.26, -8.72]
    readonly property var headT: [-1.43, -1.48, -1.5, -1.48, -1.43, -1.34, -1.21, -1.06, -0.88, -0.68, -0.46, -0.23, 0, 0.23, 0.46, 0.68, 0.88, 1.06, 1.21, 1.34, 1.43, 1.48, 1.5, 1.48, 1.43, 1.34, 1.21, 1.06, 0.88, 0.68, 0.46, 0.23, 0, -0.23, -0.46, -0.68, -0.88, -1.06, -1.21, -1.34, -1.43]
    readonly property var frontT: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -0.82, -1.25, -1.69, -2.21, -2.4, -2.4, -2.4, -2.4, -2.4, -2.29, -1.89, -1.45, -1.2, -0.81, -0.23, 0]
    readonly property var frontR: [-18, -16.5, -15, -13.5, -12, -10.5, -9, -7.5, -6, -4.5, -3, -1.5, 0, 1.5, 3, 4.5, 6, 7.5, 9, 10.5, 12, 13.5, 15, 16.5, 18, 12.16, 6.95, 0.67, -9.23, -13.18, -17.89, -24.17, -26.07, -26.88, -27.8, -27.65, -25.52, -24.45, -23.33, -22.45, -18]

    readonly property string bodyPath: "M116 63.03 L136 48.73 L136 39 L116 39 L97 39 L78 39 L78 59 L78 97 L58 97 L58 39 L39 39 L39 97 L20 97 L20 91.23 L0 105.53 L0 117 L20 117 L39 117 L58 117 L78 117 L97 117 L97 97 L97 59 L116 59 L116 63.03 Z"
    readonly property var rearPaths: ["M20 73.23 L0 87.53 L0 94.47 L20 80.17 Z", "M20 55.23 L0 69.53 L0 76.47 L20 62.17 Z", "M20 39 L0 39 L0 58.47 L20 44.17 Z"]
    readonly property string headPath: "M78 0 L97 0 L97 20 L78 20 Z"
    readonly property var frontPaths: ["M116 80.03 L136 65.73 L136 59.79 L116 74.09 Z", "M116 96.53 L136 82.23 L136 76.79 L116 91.09 Z", "M116 117 L136 117 L136 93.29 L116 107.59 Z"]

    function sample(values, phase) {
        if (!values || values.length === 0)
            return 0
        var p = Math.max(0, Math.min(1, phase))
        var x = p * (values.length - 1)
        var i = Math.floor(x)
        var f = x - i
        if (i >= values.length - 1)
            return values[values.length - 1]
        return values[i] * (1 - f) + values[i + 1] * f
    }

    function drawPath(ctx, path) {
        var tokens = path.match(/[MLZ]|-?\d+(?:\.\d+)?/g)
        if (!tokens)
            return
        var i = 0
        ctx.beginPath()
        while (i < tokens.length) {
            var cmd = tokens[i++]
            if (cmd === "M")
                ctx.moveTo(Number(tokens[i++]), Number(tokens[i++]))
            else if (cmd === "L")
                ctx.lineTo(Number(tokens[i++]), Number(tokens[i++]))
            else if (cmd === "Z")
                ctx.closePath()
        }
        ctx.fill()
    }

    function drawRotated(ctx, ox, oy, angle, paths) {
        ctx.save()
        ctx.translate(ox, oy)
        ctx.rotate(angle * Math.PI / 180)
        ctx.translate(-ox, -oy)
        for (var i = 0; i < paths.length; ++i)
            drawPath(ctx, paths[i])
        ctx.restore()
    }

    function paint() {
        var ctx = canvas.getContext("2d")
        ctx.clearRect(0, 0, canvas.width, canvas.height)
        ctx.fillStyle = root.color

        // SVG 的 viewBox 是 -18 -11 186 153；把原点平移进 Canvas，给旋转留出边距。
        // 之前 Canvas 只有 0..136 / 0..117，前足和甲片转到边界外就会被裁掉。
        ctx.save()
        ctx.translate(18, 11)

        // 躯体
        ctx.save()
        ctx.translate(0, root.sample(root.bodyT, root.phase))
        drawRotated(ctx, 116, 117, root.sample(root.bodyR, root.phase), [root.bodyPath])
        ctx.restore()

        // 后柱
        ctx.save()
        ctx.translate(0, root.sample(root.rearT, root.phase))
        drawRotated(ctx, 10, 92.85, root.sample(root.rearR, root.phase), root.rearPaths)
        ctx.restore()

        // 头顶方块
        ctx.save()
        ctx.translate(0, root.sample(root.headT, root.phase))
        drawPath(ctx, root.headPath)
        ctx.restore()

        // 前足
        ctx.save()
        ctx.translate(0, root.sample(root.frontT, root.phase))
        drawRotated(ctx, 121, 63, root.sample(root.frontR, root.phase), root.frontPaths)
        ctx.restore()

        ctx.restore()
    }

    NumberAnimation {
        target: root
        property: "phase"
        from: 0
        to: 1
        duration: 1900
        loops: Animation.Infinite
        running: root.running && root.visible
    }

    onPhaseChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()
    Component.onCompleted: canvas.requestPaint()

    Item {
        id: art
        width: 186
        height: 153
        transform: Scale {
            xScale: root.width / 186
            yScale: root.height / 153
        }

        Canvas {
            id: canvas
            anchors.fill: parent
            renderStrategy: Canvas.Immediate
            antialiasing: true
            onPaint: root.paint()
        }
    }
}
