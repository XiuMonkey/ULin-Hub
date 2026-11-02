// iOS 玻璃 · 材质件（包内私有件，不是覆盖件）
// ---------------------------------------------------------------------------
// 这不是"半透明白色方块"，是把玻璃材质拆成四层：
//
//   ① 折射层  可选。在**自己所在的窗口位置**把壁纸重新画一遍，但 soften=1.6（半径放大、
//             峰值压低）+ canvasSize 降采样 —— 得到"背景被磨掉了细节"的样子。
//             之所以要按窗口坐标对齐，是因为玻璃里那团冷色必须正好压在外面那团上，
//             错位一点点，看上去就只是"半透明色块"。
//   ② tint    色板里的面色（带 alpha）。唯一的"填充"。
//   ③ sheen   上亮下暗的竖向渐变。以前给到 0.40，每块面板都像一片厚亚克力 —— 那是拟物。
//             现在压到 0.13，只在最上沿留一点，厚度靠暗示。
//   ④ 边缘    顶部一条只在上沿出现的横向高光 + 一圈几乎看不见的描边 + 底部极淡内阴影。
//
// 整套材质现在只追求一个词：**薄**。层次交给投影（大模糊、低浓度、短位移），
// 不交给材质本身。
//
// 只做材质，不做布局：内容由调用方自己放（anchors.fill 或直接当背景）。
// 需要投影时给 shadowBlur > 0 —— 用 5 层递减的圆角矩形近似高斯，不依赖 shader。
import QtQuick
import QtQuick.Window
import ULinHub 1.0
import "wallpaper.js" as Wallpaper

Item {
    id: glass

    property real radius: Theme.radius.card

    // 面色：直接用色板里的玻璃色（cardBg / panelBg / menuBg…，本身带 alpha）
    property color tint: Theme.color.cardBg
    property real tintAlpha: 1.0          // 再压一层不透明度倍率，1 = 完全按色板
    property color borderColor: Theme.color.border
    property bool bordered: true

    // 折射（只有位置固定的件才开：标题栏。列表里的行要跟着滚动，开了每帧重画）
    property bool refract: false
    property real refractAlpha: 0.96
    property real refractSoften: 1.6

    property real sheen: 1.0              // 竖向渐变强度
    property bool specular: true          // 顶部那条横向高光

    // 自绘柔和投影（近似高斯，不依赖 MultiEffect，软件渲染下也好看）
    property real shadowBlur: 0
    property real shadowDy: 0
    property color shadowColor: Theme.color.shadow
    property int shadowSteps: 8           // 层数越多越接近连续渐变，单层浓度就得越淡
    property real shadowStrength: 0.09    // 单层浓度系数（总浓度 ≈ 系数 × Σp）

    readonly property color faceColor: Qt.rgba(tint.r, tint.g, tint.b, tint.a * tintAlpha)

    // ---------------------------------------------------------------- ① 折射层
    Canvas {
        id: refractLayer
        anchors.fill: parent
        visible: glass.refract
        renderStrategy: Canvas.Cooperative
        canvasSize: Qt.size(Math.max(6, Math.round(width / 8)),
                            Math.max(6, Math.round(height / 8)))

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (width <= 0 || height <= 0)
                return

            ctx.save()
            const r = Math.min(glass.radius, width / 2, height / 2)
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

            // 换算到窗口坐标：玻璃里画的是"我这块位置底下的壁纸"
            const win = Window.window
            const winW = win ? win.width : width
            const winH = win ? win.height : height
            const p = glass.mapToItem(null, 0, 0)

            ctx.translate(-p.x, -p.y)
            Wallpaper.paint(ctx, winW, winH, {
                dark: Theme.isDark,
                base: Theme.color.windowBg,
                soften: glass.refractSoften,
                alpha: glass.refractAlpha,
                glow: false
            })
            ctx.restore()
        }
    }

    // ---------------------------------------------------------------- ② tint
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: glass.faceColor
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    // ---------------------------------------------------------------- ③ sheen
    // 只有最上沿一小段有亮，往下迅速归零；底部留一丁点暗，暗示"这是一片有厚度的平面"。
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        opacity: glass.sheen
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.055 : 0.16)
            }
            GradientStop {
                position: 0.22
                color: Qt.rgba(1, 1, 1, 0.0)
            }
            GradientStop {
                position: 0.80
                color: Qt.rgba(0, 0, 0, 0.0)
            }
            GradientStop {
                position: 1.0
                color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.10 : 0.018)
            }
        }
    }

    // ---------------------------------------------------------------- ④ 边缘
    // 顶部横向高光：两端淡出，就像光从左上斜进来擦过上沿。
    // 值压到 0.5/0.28 —— 以前 0.95 会把整条上沿烧成一道白线。
    Rectangle {
        visible: glass.specular
        x: glass.radius * 0.6
        y: 0
        width: Math.max(0, parent.width - 2 * x)
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.0) }
            GradientStop {
                position: 0.22
                color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.14 : 0.50)
            }
            GradientStop {
                position: 0.70
                color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.06 : 0.26)
            }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: glass.bordered ? 1 : 0
        border.color: glass.borderColor
    }

    // 内侧那一圈更亮的白：外淡内亮两道描边叠起来，才会读成"一片玻璃的断面"
    // 而不是"一个带框的矩形"。真实玻璃的边缘就是这种渐进的折射。
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, glass.radius - 1)
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.075 : 0.42)
        visible: glass.bordered
    }

    // ---------------------------------------------------------------- 投影（可选）
    // 用 n 层递减的圆角矩形近似高斯：外圈最大最淡、内圈最小最浓。
    // 关键是**几何要收住**——阴影只许往下掉一点点、左右最多露一圈窄边，
    // 绝不能四面开花。上一版 5 层 × 1.1 倍宽 + 向上偏移，读起来像一圈厚实的黑边。
    // 现在横向只外扩 0.60、纵向几乎不外扩、上沿不外冒，浓度也砍到原来的三分之一。
    Repeater {
        model: glass.shadowSteps

        delegate: Rectangle {
            required property int index

            z: -1
            visible: glass.shadowBlur > 0

            // p: 1.0 → 0.125；先画最大最淡的一层垫底，最后画最紧最浓的一层压在上面
            readonly property real p: (glass.shadowSteps - index) / glass.shadowSteps
            readonly property real spread: glass.shadowBlur * p

            x: -spread * 0.30
            y: glass.shadowDy + spread * 0.05
            width: glass.width + spread * 0.60
            height: glass.height + spread * 0.60
            radius: glass.radius + spread * 0.30
            color: Qt.rgba(glass.shadowColor.r, glass.shadowColor.g, glass.shadowColor.b,
                           glass.shadowColor.a * glass.shadowStrength * p)
        }
    }
}
