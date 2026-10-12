"""读 PNG（纯标准库：zlib + 反滤波），打印外圈与中心的平均颜色，用于判断"黑边"是不是图自带的。

用法： python misc/tools/png-edges.py <png> [环宽像素]
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a png'
    pos = 8
    width = height = None
    bit_depth = color_type = None
    idat = b''
    while pos < len(data):
        (length,) = struct.unpack('>I', data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b'IHDR':
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack('>IIBBBBB', body)
            assert bit_depth == 8 and interlace == 0, f'unsupported png (depth={bit_depth}, interlace={interlace})'
        elif tag == b'IDAT':
            idat += body
        elif tag == b'IEND':
            break

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)

    p = 0
    for y in range(height):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:      # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif f == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:    # Average
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:    # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    return width, height, channels, out


def mean_region(width, height, channels, buf, x0, y0, x1, y1):
    """返回 (r, g, b, a) 均值；color_type 无 alpha 时 a=255。"""
    rs = gs = bs = as_ = 0
    n = 0
    for y in range(y0, y1):
        base = y * width * channels
        for x in range(x0, x1):
            i = base + x * channels
            if channels >= 3:
                r, g, b = buf[i], buf[i + 1], buf[i + 2]
            else:
                r = g = b = buf[i]
            a = buf[i + 3] if channels == 4 else 255
            rs += r; gs += g; bs += b; as_ += a
            n += 1
    return rs / n, gs / n, bs / n, as_ / n


def main():
    path = sys.argv[1]
    band = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    w, h, ch, buf = read_png(path)
    print(f'{path}: {w}x{h} channels={ch}')

    regions = {
        f'外圈 {band}px 环': None,
        '中心 1/3': None,
        '上边中段': (w // 2 - 50, 0, w // 2 + 50, band),
        '下边中段': (w // 2 - 50, h - band, w // 2 + 50, h),
        '左边中段': (0, h // 2 - 50, band, h // 2 + 50),
        '右边中段': (w - band, h // 2 - 50, w, h // 2 + 50),
        '左上角': (0, 0, band * 4, band * 4),
        '右下角': (w - band * 4, h - band * 4, w, h),
    }

    # 外圈环：用"整图 - 内缩图"的互补方式统计
    rs = gs = bs = as_ = 0
    n = 0
    for y in range(h):
        base = y * w * ch
        for x in range(w):
            if band <= x < w - band and band <= y < h - band:
                continue
            i = base + x * ch
            r, g, b = (buf[i], buf[i + 1], buf[i + 2]) if ch >= 3 else (buf[i],) * 3
            a = buf[i + 3] if ch == 4 else 255
            rs += r; gs += g; bs += b; as_ += a
            n += 1
    print(f'  {"外圈 %dpx 环" % band:<16} rgb=({rs/n:6.1f},{gs/n:6.1f},{bs/n:6.1f}) alpha={as_/n:6.1f}')
    print(f'  {"中心 1/3":<16} rgb={mean_region(w, h, ch, buf, w//3, h//3, 2*w//3, 2*h//3)[:3]}')

    for name in ('上边中段', '下边中段', '左边中段', '右边中段', '左上角', '右下角'):
        r, g, b, a = mean_region(w, h, ch, buf, *regions[name])
        print(f'  {name:<16} rgb=({r:6.1f},{g:6.1f},{b:6.1f}) alpha={a:6.1f}')


if __name__ == '__main__':
    main()
