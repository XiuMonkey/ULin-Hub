// iOS 玻璃 · 壁纸的唯一真源
// ---------------------------------------------------------------------------
// Backdrop（当壁纸画，清晰）和 Glass（当"透过磨砂玻璃看到的壁纸"画，柔化）都调这里，
// 所以玻璃里的颜色永远和它背后那张壁纸对得上 —— 这不是贴一张预制图，是同一套底色
// 用不同的 soften 重新算一遍。坐标全部归一化到窗口尺寸，两个调用方给同一个 (w, h) 就对齐。
//
// 设计取向改了：第一版是「斜向彩色主渐变 + 高饱和色斑」，压在白玻璃下整屏都被染色，
// 读起来像 2013 年的拟物皮肤。现在的做法是**中性底 + 极淡的冷色雾**：
// 底色是近乎无彩的冷灰白，色相只在很大半径、很低峰值的光团里出现（峰值 ≤ 0.30）。
// 这样白玻璃压上去是「白底上有一点冷色倾向」，而不是「一块糖」。玻璃的层次改由
// 边缘高光和柔阴影承担，不再指望背景色有多浓 —— 现代玻璃本来就是"薄"的。
//
// 为什么用 Canvas 而不是 ShaderEffect：宿主允许软件渲染（QT_QUICK_BACKEND=software），
// 那里没有 shader；Canvas 的 canvasSize 降采样 + 双线性上采样本身就是一次廉价模糊，
// 在任何后端都成立。
.pragma library

// 底色。亮色不是纯白 —— 纯白会让上面的白玻璃完全消失；留一档灰才分得出层。
var LIGHT_BASE = [245, 246, 249]
var DARK_BASE = [18, 18, 21]

// 亮：**只在上半部有一片彩色云**，其余是大面积中性灰白。
// 这是这套图的构图核心 —— 满屏渐变正是"难看"的来源。把颜色集中到右上角一片，
// 界面大部分保持干净（现代），而那一片又给了玻璃可"透"的东西（磨砂）。
// 峰值控制在 0.5 以内：落在 #EDEFF4 上出来是 (199,215,247) 这个级别的淡蓝，不是蓝。
var LIGHT_BLOBS = [
    { x: 0.50, y: 0.01, r: 0.26, c: [136, 178, 252], a: 0.52 },    // 上部：标题栏与主区顶部
    { x: 0.80, y: 0.18, r: 0.22, c: [190, 170, 248], a: 0.40 },    // 右上（被打折到淡紫）
    { x: 0.36, y: 0.92, r: 0.24, c: [138, 210, 244], a: 0.34 },    // 下部：输入卡背后
    { x: 0.04, y: 0.52, r: 0.26, c: [174, 204, 242], a: 0.26 }     // 左缘：侧栏背后
]

// 暗：同一构图，云换成深靛与紫，底是近黑（纯黑会把玻璃的层次吃掉）
var DARK_BLOBS = [
    { x: 0.50, y: 0.01, r: 0.28, c: [54, 76, 158], a: 0.58 },
    { x: 0.80, y: 0.18, r: 0.24, c: [90, 58, 146], a: 0.44 },
    { x: 0.36, y: 0.92, r: 0.26, c: [28, 94, 122], a: 0.38 },
    { x: 0.04, y: 0.52, r: 0.28, c: [30, 44, 92], a: 0.34 }
]

// 顶部一道极淡的白光：只给壁纸本体（玻璃不画），作用是给所有浮起的件一个统一的受光方向
var LIGHT_TOP_GLOW = 0.34
var DARK_TOP_GLOW = 0.06

function rgba(c, a) {
    return "rgba(" + c[0] + "," + c[1] + "," + c[2] + "," + a + ")"
}

// QML 的 color 对象 -> Canvas 吃得下的字符串。别直接 String(color)：
// 那是 #AARRGGBB，而 Canvas 的 fillStyle 按 CSS 解析（#RRGGBBAA），alpha 会跑错位置。
function css(c, a) {
    if (!c || c.r === undefined)
        return c
    return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
        + Math.round(c.b * 255) + "," + (a === undefined ? 1 : a) + ")"
}

// o = {
//   dark:   true/false
//   soften: 1 = 壁纸本体；>1 = 被玻璃磨过的样子（半径放大、峰值压低）
//   alpha:  整体不透明度（玻璃里压低一档，好让面上的 tint 说话）
//   base:   底色（QML color 或字符串）铺在光团之下；null = 用内置中性底
//   glow:   是否画顶部那道白光（玻璃里不画，它自己已经有 sheen 了）
// }
function paint(ctx, w, h, o) {
    var dark = o.dark === true
    var soften = o.soften === undefined ? 1 : o.soften
    var alpha = o.alpha === undefined ? 1 : o.alpha
    var base = o.base === undefined ? null : o.base
    var glow = o.glow === undefined ? (soften <= 1) : o.glow

    // ① 底色：整张图的"无彩色锚点"。没有它，光团之间的空隙会露出窗口色，出现色块感。
    ctx.fillStyle = base ? css(base, alpha) : rgba(dark ? DARK_BASE : LIGHT_BASE, alpha)
    ctx.fillRect(0, 0, w, h)

    // ② 冷色雾：大半径、低峰值、软衰减
    var blobs = dark ? DARK_BLOBS : LIGHT_BLOBS
    var ref = Math.max(w, h)
    for (var k = 0; k < blobs.length; ++k) {
        var b = blobs[k]
        var cx = b.x * w
        var cy = b.y * h
        var rad = ref * b.r * soften
        var peak = b.a * alpha * (soften > 1 ? 0.85 : 1)
        var rg = ctx.createRadialGradient(cx, cy, 0, cx, cy, rad)
        // 四段衰减：中心满、30% 处还留七成、80% 几乎没了 —— 这样过渡没有"硬边"
        rg.addColorStop(0.00, rgba(b.c, peak))
        rg.addColorStop(0.30, rgba(b.c, peak * 0.70))
        rg.addColorStop(0.62, rgba(b.c, peak * 0.30))
        rg.addColorStop(1.00, rgba(b.c, 0))
        ctx.fillStyle = rg
        ctx.fillRect(0, 0, w, h)
    }

    // ③ 顶部白光：一条从窗口上沿往下淡出的柔光
    if (glow) {
        var tg = ctx.createLinearGradient(0, 0, 0, h * 0.55)
        tg.addColorStop(0.0, rgba([255, 255, 255], dark ? DARK_TOP_GLOW : LIGHT_TOP_GLOW))
        tg.addColorStop(1.0, rgba([255, 255, 255], 0))
        ctx.fillStyle = tg
        ctx.fillRect(0, 0, w, h)
    }
}

// 暗角：只给壁纸本体用（玻璃不画，否则每个玻璃片自带一圈黑边）。
// 强度压得很低 —— 以前 0.38/0.10 会在四角压出一圈明显的脏灰。
function vignette(ctx, w, h, dark) {
    var g = ctx.createRadialGradient(w * 0.5, h * 0.42, Math.min(w, h) * 0.34,
                                     w * 0.5, h * 0.5, Math.max(w, h) * 0.92)
    g.addColorStop(0, "rgba(0,0,0,0)")
    g.addColorStop(1, dark ? "rgba(0,0,0,0.16)" : "rgba(24,32,64,0.035)")
    ctx.fillStyle = g
    ctx.fillRect(0, 0, w, h)
}
