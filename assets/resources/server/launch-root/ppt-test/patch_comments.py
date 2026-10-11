#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 powerpoint.cpp 中基于错误前提的注释，改为实测结论。"""

import os
import sys

CPP = r"C:\Users\Playe\Documents\DSH hub\PPT extension\src\powerpoint.cpp"
FAILED = []

with open(CPP, "rb") as f:
    raw = f.read()
bom = raw.startswith(b"\xef\xbb\xbf")
txt = raw.decode("utf-8-sig")
nl = "\r\n" if "\r\n" in txt else "\n"


def tabs(tpl):
    out = []
    for line in tpl.split("\n"):
        n = 0
        while line[n:n + 4] == "    ":
            n += 4
        out.append("\t" * (n // 4) + line[n:])
    return "\n".join(out)


def patch(label, old_tpl, new_tpl):
    global txt
    old = tabs(old_tpl).replace("\n", nl)
    new = tabs(new_tpl).replace("\n", nl)
    n = txt.count(old)
    if n != 1:
        print("  [FAIL] %-40s hits=%d" % (label, n))
        FAILED.append(label)
        return
    print("  [ ok ] %s" % label)
    txt = txt.replace(old, new)


patch("C1 isAppAlive 注释改为实测结论",
      r'''    // ------------------------------------------------------------------
    // 会话存活判定
    // ------------------------------------------------------------------
    // PowerPoint 与 Word/Excel 不同：由自动化启动的实例在「最后一个演示文稿
    // 被关闭」后会自行退出。此时缓存的 Application 代理进入"已断开"状态
    // （CO_E_OBJNOTCONNECTED = 0x800401FD / RPC_E_DISCONNECTED = 0x80010108），
    // 任何后续成员调用都会失败，且不会自愈。
    //
    // 探活必须走真实 Invoke：自动化代理可能已缓存 ITypeInfo，仅靠
    // GetIDsOfNames 在服务端进程已退出时仍可能返回 DISPID。''',
      r'''    // ------------------------------------------------------------------
    // 会话存活判定
    // ------------------------------------------------------------------
    // 缓存的 Application 代理随时可能失效。一旦失效，任何成员调用都会返回
    // CO_E_OBJNOTCONNECTED(0x800401FD) / RPC_E_DISCONNECTED(0x80010108)，
    // 且永远不会自愈——所以复用前必须探活。
    //
    // 实测结论（PowerPoint 16.0，automation 启动，Visible=0），勿凭直觉推翻：
    //   * 关闭最后一个文稿后 PowerPoint 继续存活：Close 之后 180 秒内不发起
    //     任何 COM 调用，进程依然在。所以「关文稿即退出」是错的，不要据此
    //     推断失效时机。
    //   * 客户端释放最后一个 COM 引用之后，PowerPoint 才退出。
    //   * 传入不可解析的路径等错误不会杀死 PowerPoint。
    // 换言之失效来源不确定（崩溃 / 外部终止 / COM 拆卸 / 宿主回收皆可能），
    // 因此这里只做状态判定，不依赖任何具体的失效触发条件。
    //
    // 探活必须走真实 Invoke：自动化代理可能已缓存 ITypeInfo，仅靠
    // GetIDsOfNames 在服务端进程已退出时仍可能返回 DISPID。''')

patch("C2 ensureApp 注释",
      r'''        // 缓存的 Application 可能已随 PowerPoint 自行退出而失效；先探活再复用，
        // 失效则整体丢弃并在下方重建（自愈）。''',
      r'''        // 缓存的 Application 可能已经失效（服务端退出 / 崩溃 / COM 拆卸）；
        // 先探活再复用，失效则整体丢弃并在下方重建（自愈）。''')

patch("C3 closePresentation 注释",
      r'''        // 关闭最后一个文稿后 PowerPoint 通常会自行退出，Application 代理随即
        // 失效。立即丢弃，避免后续工具命中失效代理（0x800401FD）。''',
      r'''        // Close 之后服务端可能已经消失；探活失败就立即丢弃会话，
        // 避免后续工具命中失效代理（0x800401FD）。''')

patch("C4 shutdown 注释",
      r'''        // Quit 只在代理仍存活时才有意义（服务端可能已自行退出）''',
      r'''        // Quit 只在代理仍存活时才有意义（服务端可能已经消失）''')

if FAILED:
    print("失败: %s" % ", ".join(FAILED))
    sys.exit(1)

out = txt.encode("utf-8")
if bom:
    out = b"\xef\xbb\xbf" + out
with open(CPP, "wb") as f:
    f.write(out)
print("已写回 %s" % CPP)
