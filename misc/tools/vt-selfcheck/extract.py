"""把引擎抓包（capture.json）转成 selfcheck.cpp 能直接读的定长帧文件 stream.bin。

为什么要有这一步：引擎的抓包是 JSON（"\u001b[93m..." 这种转义），而自检程序不该为了读夹具
带一个 JSON 解析器。这里一次性转成"长度前缀 + 原始字节"，C++ 侧读起来就只有几行。

帧格式（小端）：
  uint8  kind    0 = snapshot（序列化的重放串，喂之前要先硬复位），1 = 实时输出帧
  uint32 length  载荷字节数
  bytes  payload UTF-8 原始字节（转义已解开）

用法：
  python extract.py <capture.json> <stream.bin>
"""

import json
import struct
import sys

KIND_SNAPSHOT = 0
KIND_LIVE = 1


def write_frame(out, kind, text):
    payload = text.encode("utf-8")
    out.write(struct.pack("<BI", kind, len(payload)))
    out.write(payload)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "r", encoding="utf-8") as handle:
        capture = json.load(handle)

    with open(dst, "wb") as out:
        snapshot = capture.get("snapshot") or {}
        write_frame(out, KIND_SNAPSHOT, snapshot.get("screen", ""))
        for frame in capture.get("live", []):
            write_frame(out, KIND_LIVE, frame.get("data", ""))

    print("cols=%s rows=%s snapshot=%d live=%d -> %s" % (
        capture.get("cols"), capture.get("rows"),
        1 if capture.get("snapshot") else 0, len(capture.get("live", [])), dst))


if __name__ == "__main__":
    main()
