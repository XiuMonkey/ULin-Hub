"""生成 iOS 玻璃包里的颗粒平铺图（96×96 PNG）。

为什么要有它：大面积的柔和渐变在 8bit 色深下会出现色带，真实玻璃的雾面也总带一点
颗粒。这张图在 Backdrop 上以 Image.Tile 平铺、低不透明度叠一层，色带就没了。
颗粒是"白点 + 黑点"两色随机，所以亮色/暗色两套主题共用一个文件。

用法：python make_grain.py
"""
import os
import random

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "ios", "skin", "window", "grain.png")
SIZE = 96
WHITE_RATIO = 0.13
BLACK_RATIO = 0.09


def main():
    rng = random.Random(20261030)   # 固定种子：重跑得到同一张图，预览图才可复现
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()

    for y in range(SIZE):
        for x in range(SIZE):
            r = rng.random()
            if r < WHITE_RATIO:
                px[x, y] = (255, 255, 255, rng.randint(6, 20))
            elif r < WHITE_RATIO + BLACK_RATIO:
                px[x, y] = (0, 0, 0, rng.randint(5, 16))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT, optimize=True)
    print("wrote", OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
