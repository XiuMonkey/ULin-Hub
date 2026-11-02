"""把六套风格包的预览拼成一张对比图（light / dark 各一张）。"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS = [
    ("papercraft", "素纸 Papercraft", "宣纸 + 稿纸方格 + 硬投影"),
    ("frost", "霜玻 Frost", "环境光斑 + 半透明玻璃"),
    ("phosphor", "荧光屏 Phosphor", "CRT 扫描线 + 直角终端"),
    ("sakura", "樱岚 Sakura", "雾粉渐变 + 飘落花瓣"),
    ("swiss", "苏黎世 Swiss", "纯黑白 + 一点红"),
    ("ios", "iOS 玻璃 iOS Glass", "iOS 系统色 + 毛玻璃材质"),
]

CELL_W, CELL_H = 520, 308
LABEL_H = 44
GAP = 18
COLS = 3
BG = (245, 246, 248)
CARD = (255, 255, 255)


def font(size, bold=False):
    path = r"C:\Windows\Fonts\msyhbd.ttc" if bold else r"C:\Windows\Fonts\msyh.ttc"
    return ImageFont.truetype(path, size)


def build(mode, out_path):
    rows = (len(PACKS) + COLS - 1) // COLS
    width = GAP + COLS * (CELL_W + GAP)
    height = GAP + rows * (CELL_H + LABEL_H + GAP)
    canvas = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(canvas)
    f_title = font(17, True)
    f_sub = font(12)

    for i, (slug, title, sub) in enumerate(PACKS):
        r, c = divmod(i, COLS)
        x = GAP + c * (CELL_W + GAP)
        y = GAP + r * (CELL_H + LABEL_H + GAP)
        shot = Image.open(os.path.join(ROOT, slug, "previews", "%s.png" % mode)).convert("RGB")
        shot = shot.resize((CELL_W, CELL_H), Image.LANCZOS)
        draw.rectangle([x - 1, y - 1, x + CELL_W, y + CELL_H], fill=CARD, outline=(226, 228, 232))
        canvas.paste(shot, (x, y))
        draw.text((x + 2, y + CELL_H + 10), title, font=f_title, fill=(24, 26, 30))
        draw.text((x + 2, y + CELL_H + 30), sub, font=f_sub, fill=(120, 126, 136))

    canvas.save(out_path)
    print("wrote", out_path, canvas.size)


if __name__ == "__main__":
    build("light", os.path.join(ROOT, "overview-light.png"))
    build("dark", os.path.join(ROOT, "overview-dark.png"))
