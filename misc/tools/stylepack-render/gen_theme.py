"""把 theme-*.json + metrics.json 展开成渲染试验用的 Theme.qml 单例。

真实宿主里 Theme 由 C++ ThemeBridge 提供：metrics.json 只是**覆盖层**（缺的键用 C++ 内置默认值）。
所以这里也必须先以内置 metrics 为基线，再用包里的 metrics 覆盖，否则缺组的键会变成
undefined，QML 里 font.pixelSize 之类的绑定会静默失效（症状是"文字整个不显示"）。
"""
import json
import re
import sys

RGBA = re.compile(r"rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([0-9.]+)\s*)?\)", re.I)


def to_qml_color(v):
    m = RGBA.match(v.strip())
    if not m:
        return v
    r, g, b = int(m.group(1)), int(m.group(2)), int(m.group(3))
    a = m.group(4)
    if a is None:
        return "#%02X%02X%02X" % (r, g, b)
    alpha = float(a)
    if alpha <= 1.0:
        alpha = round(alpha * 255)
    return "#%02X%02X%02X%02X" % (int(alpha), r, g, b)


def qstr(s):
    return '"%s"' % s.replace('"', '\\"')


def merge_metrics(base_path, over_path):
    base = json.load(open(base_path, encoding="utf-8"))
    if over_path and over_path != "none":
        over = json.load(open(over_path, encoding="utf-8"))
        for group, values in over.items():
            if group.startswith("_") or not isinstance(values, dict):
                continue
            merged = base.setdefault(group, {})
            for k, v in values.items():
                merged[k] = v
    return base


def main():
    palette_path, base_metrics, over_metrics, out_path, mode = sys.argv[1:6]
    pal = json.load(open(palette_path, encoding="utf-8"))
    met = merge_metrics(base_metrics, over_metrics)

    lines = ["// 由 gen_theme.py 生成，勿手改。", "pragma Singleton", "import QtQuick", "",
             "QtObject {"]
    lines.append("    readonly property bool isDark: %s" % ("true" if mode == "dark" else "false"))
    lines.append("")
    lines.append("    readonly property QtObject color: QtObject {")
    for k, v in pal.items():
        if k.startswith("_") or not isinstance(v, str):
            continue
        lines.append("        readonly property color %s: %s" % (k, qstr(to_qml_color(v))))
    lines.append("    }")

    for group in ("radius", "font", "spacing", "size", "shadow", "motion"):
        vals = met.get(group)
        if not isinstance(vals, dict):
            continue
        lines.append("")
        lines.append("    readonly property QtObject %s: QtObject {" % group)
        for k, v in vals.items():
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                num = ("%f" % v).rstrip("0").rstrip(".") if isinstance(v, float) else str(v)
                lines.append("        readonly property real %s: %s" % (k, num))
        lines.append("    }")

    lines.append("}")
    open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("wrote", out_path, len(lines), "lines")


if __name__ == "__main__":
    main()
