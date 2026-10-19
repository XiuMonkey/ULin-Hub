"""生成附件（回形针）图标 —— 48×48，像素级锐利。

视觉语言沿用 icons/ 下既有图标的规范：单一灰 #6B7280、线性描边、圆头、
描边宽 = 画布 1/12（48px 上即 4px）。

前几版"糊"的根因：形状是矢量的，最后一步靠缩小（LANCZOS/BILINEAR）落到小画布，
边界落在半像素上，每条边糊一圈 40~160 的灰边。

这一版改用像素图标的通行做法 —— **中心点采样**：
  对每个输出像素，判断它的中心点是否落在回形针的描边带内部；
  在就填纯色，不在就全透明。alpha 只有 0/255，边缘绝对锐利、没有灰边。
  判断"点是否在描边带内"用的是精确解析式：
    点到中心线的距离 <= 描边宽/2。
  中心线由直线段和 ±180° 圆弧组成，所以点到它们的距离是闭式解，
  不需要采样近似 —— 形状和矢量版完全一致，只是落地方式换成了像素。

输出：48（主档）/ 96 / 144，各自独立采样，大尺寸不会出现"小图放大"的糊边。

用法：python make-attach-icon.py
"""

import math
import os

from PIL import Image, ImageChops

COLOR = (107, 114, 128, 255)

BASE = 48
STROKE = 4.0            # 48px 上的描边宽 = 48/12，与 setting-icon 同比例


class Seg:
    """直线段。"""

    def __init__(self, x0, y0, x1, y1):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1

    def dist(self, px, py):
        dx, dy = self.x1 - self.x0, self.y1 - self.y0
        L2 = dx * dx + dy * dy
        t = 0.0 if L2 == 0 else ((px - self.x0) * dx + (py - self.y0) * dy) / L2
        t = max(0.0, min(1.0, t))
        return math.hypot(px - (self.x0 + t * dx), py - (self.y0 + t * dy))


class Arc:
    """圆弧；角度用数学惯例（0=正右，逆时针为正），y 轴向下故 sin 不取反。"""

    def __init__(self, cx, cy, r, a0, a1):
        self.cx, self.cy, self.r = cx, cy, r
        self.a0, self.a1 = a0, a1

    def dist(self, px, py):
        ang = math.degrees(math.atan2(py - self.cy, px - self.cx))
        lo, hi = self.a0, self.a1
        # 把 ang 归一到 [lo, lo+360)
        while ang < lo:
            ang += 360.0
        while ang >= lo + 360.0:
            ang -= 360.0
        d = math.hypot(px - self.cx, py - self.cy)
        if ang <= hi:                       # 落在弧的角度范围内 → 径向距离
            return abs(d - self.r)
        # 否则最近点在两个端点上
        e = []
        for a in (lo, hi):
            e.append((self.cx + self.r * math.cos(math.radians(a)),
                      self.cy + self.r * math.sin(math.radians(a))))
        return min(math.hypot(px - ex, py - ey) for ex, ey in e)


def _geometry(s):
    """回形针的线段与圆弧（单位：输出像素，s = 相对基准画布的缩放系数）。

    真实回形针的关键在「短芯与内腿平行且紧挨」：
      外腿 ↑ → 顶部 180° 回卷 → 内腿 ↓ → 底部 180° 回卷 → 短芯 ↑
    短芯紧贴内腿、一起被顶部半圆圈住，才看得出是「针套针」。
    短芯顶端要插进顶部半圆圈内部：只有它的圆头越过了 y_top + r_top + 2 这条线，
    顶部圈才会被读成"闭合的扣"，而不是"张开的两根腿"。

    横向刻意收窄（48px 基准、4px 描边）：总墨迹宽 26，占 48 画布的 54%，
    观感是细长的回形针而不是方形；四周留白 11px，与齿轮图标的内边距同量级。
    """
    # ---- 横向 ----
    # 4px 描边 + 腿中心落在**整数** x 上：边界 = 中心 ± 2，正好压在整像素线上，
    # 腿宽严格 4px（中心若落在 x.5，边界就压在像素中心，宽度会在 4/5 之间抖动）。
    # 间距 12 / 10 ⇒ 顶半径 6、底半径 5。半径取整数，圆弧最高/最低点才落在像素正中心，
    # 不会在弧顶留 1px 缺口。
    x_l = 12.0 * s
    x_m = 24.0 * s
    x_r = 34.0 * s
    r_top = (x_m - x_l) / 2               # 6
    r_bot = (x_r - x_m) / 2               # 5

    # ---- 纵向 ----
    # 11..37 是这几版调出来的平衡点：墨迹 3..44（高 41），上下各留 3px；
    # 再拉长会顶到画布边缘（QML 里 Image 一旦被缩放就会糊），再短则显得轻飘。
    # 短芯自由端取半像素（.5）：圆头尖顶落在像素中心，不会因端面压线多出 1px 凸起。
    y_top = 11.0 * s
    y_bot = 37.0 * s
    y_tail = 13.5 * s                     # 短芯顶端：收在顶部半圆圈内

    segs = [
        Seg(x_l, y_bot - 0.5, x_l, y_top),                    # 外腿（上行）
        Seg(x_m, y_top, x_m, y_bot),                          # 内腿（下行）
        Seg(x_r, y_bot, x_r, y_tail),                         # 短芯（上行，贴着内腿）
    ]
    arcs = [
        Arc(x_l + r_top, y_top, r_top, 180, 360),             # 顶部回卷：经上方
        Arc(x_m + r_bot, y_bot, r_bot, 0, 180),               # 底部回卷：经下方
    ]
    return segs, arcs


def build(size):
    unit = size / BASE
    segs, arcs = _geometry(unit)
    # 不做采样补偿：+0.5px 会让腿涨到 5~6px、几根腿糊成一块。
    # 用 1.96 而不是 2.0：中心点采样下，边界恰好落在像素中心（覆盖率 0.5）会被判成
    # "实心"，4px 的腿会胖成 5px；略收 0.04 让边界退进像素内部，腿宽严格 4px。
    half = STROKE * unit / 2 - 0.04

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()

    for y in range(size):
        cy = y + 0.5
        for x in range(size):
            cx = x + 0.5
            inside = any(s.dist(cx, cy) <= half for s in segs) or \
                     any(a.dist(cx, cy) <= half for a in arcs)
            if inside:
                px[x, y] = COLOR

    # 按墨迹 bbox 居中（取整到整像素，缩到其它尺寸也不会引入半像素）
    bb = img.split()[3].getbbox()
    if bb:
        x0, y0, x1, y1 = bb
        dx = round((size - (x1 - x0)) / 2 - x0)
        dy = round((size - (y1 - y0)) / 2 - y0)
        img = ImageChops.offset(img, dx, dy)
    return img


if __name__ == "__main__":
    out_dir = os.path.join(os.path.dirname(__file__), "..", "..",
                           "assets", "resources", "icons")
    out_dir = os.path.abspath(out_dir)
    for s in (48, 96, 144):
        p = os.path.join(out_dir, f"attach-icon-{s}.png")
        build(s).save(p)
        print("wrote", p)
    p = os.path.join(out_dir, "attach-icon.png")
    build(48).save(p)
    print("wrote", p)
