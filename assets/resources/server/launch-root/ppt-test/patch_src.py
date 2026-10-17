#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PPT extension 源码修复脚本。

修复项：
  1. powerpoint.cpp  —— PowerPoint 关闭最后一个文稿后自行退出，导致缓存的
                        Application 代理失效（0x800401FD）且无法自愈。
  2. powerpoint.cpp  —— COM 路径对正斜杠支持不稳定，统一归一化为反斜杠。
  3. make_ext.ps1    —— settings.example.json 未随包分发（错误信息却指向它）。
  4. install.ps1     —— 部署目录名 PPTExt 与实际 PPTExtPlugin 不一致。

源文件使用 TAB 缩进；本脚本模板用 4 空格书写，插入前统一转为 TAB，
避免手工对齐出错。每个补丁都断言「恰好命中一次」，命中数异常即整体失败。
"""

import os
import sys

SRC = r"C:\Users\Playe\Documents\DSH hub\PPT extension"

FAILED = []


def read_text(path):
    with open(path, "rb") as f:
        raw = f.read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    txt = raw.decode("utf-8-sig")
    nl = "\r\n" if "\r\n" in txt else "\n"
    return txt, nl, bom


def write_text(path, txt, bom):
    data = txt.encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    bak = path + ".bak"
    if not os.path.exists(bak):
        with open(bak, "wb") as f:
            with open(path, "rb") as g:
                f.write(g.read())
    with open(path, "wb") as f:
        f.write(data)


def spaces_to_tabs(tpl):
    """把模板中每 4 个前导空格转为 1 个 TAB，匹配 .cpp 的缩进风格。"""
    out = []
    for line in tpl.split("\n"):
        n = 0
        while line[n:n + 4] == "    ":
            n += 4
        out.append("\t" * (n // 4) + line[n:])
    return "\n".join(out)


def patch(label, txt, nl, old_tpl, new_tpl, tabs=True):
    conv = spaces_to_tabs if tabs else (lambda s: s)
    old = conv(old_tpl).replace("\n", nl)
    new = conv(new_tpl).replace("\n", nl)
    n = txt.count(old)
    if n != 1:
        print("  [FAIL] %-46s hits=%d" % (label, n))
        FAILED.append(label)
        return txt
    print("  [ ok ] %s" % label)
    return txt.replace(old, new)


# ======================================================================
# 1 + 2. powerpoint.cpp
# ======================================================================
CPP = os.path.join(SRC, "src", "powerpoint.cpp")
print("[1/4] %s" % CPP)
txt, nl, bom = read_text(CPP)
print("      newline=%s bom=%s bytes=%d" % ("CRLF" if nl == "\r\n" else "LF", bom, len(txt)))

ENSURE_COM = r'''    void ensureCom()
    {
        if (!g_comInitialized) {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            g_comInitialized = true;
        }
    }'''

HELPERS = r'''    // ------------------------------------------------------------------
    // 会话存活判定
    // ------------------------------------------------------------------
    // PowerPoint 与 Word/Excel 不同：由自动化启动的实例在「最后一个演示文稿
    // 被关闭」后会自行退出。此时缓存的 Application 代理进入"已断开"状态
    // （CO_E_OBJNOTCONNECTED = 0x800401FD / RPC_E_DISCONNECTED = 0x80010108），
    // 任何后续成员调用都会失败，且不会自愈。
    //
    // 探活必须走真实 Invoke：自动化代理可能已缓存 ITypeInfo，仅靠
    // GetIDsOfNames 在服务端进程已退出时仍可能返回 DISPID。
    bool isAppAlive()
    {
        if (!g_session.app)
            return false;

        std::wstring probeErr;
        com::DispatchPtr presentations =
            com::getDispatchProperty(g_session.app.get(), L"Presentations", &probeErr);
        if (!presentations)
            return false;

        // Count 是数值属性，必须真正跨进程往返才能取到
        VARIANT count;
        VariantInit(&count);
        bool ok = com::getProperty(presentations.get(), L"Count", &count, &probeErr);
        VariantClear(&count);
        return ok;
    }

    // 丢弃整个会话（Application + 当前文稿），下次调用会重新创建。
    void dropSession()
    {
        g_session.pres = com::DispatchPtr();
        g_session.app = com::DispatchPtr();
        g_session.launchedByUs = false;
    }

    // PowerPoint 的 COM 接口对正斜杠路径支持不稳定：SaveAs / Export /
    // AddPicture / UserPicture 传入含正斜杠的路径时会返回 0x80020009，
    // 反斜杠形式则正常。统一在进入 COM 前归一化为反斜杠。
    std::wstring normalizePath(const std::wstring& in)
    {
        std::wstring out = in;
        for (wchar_t& c : out) {
            if (c == L'/')
                c = L'\\';
        }
        return out;
    }'''

txt = patch("01 插入 isAppAlive / dropSession / normalizePath",
            txt, nl, ENSURE_COM, ENSURE_COM + "\n\n" + HELPERS)

txt = patch("02 ensureApp 复用前探活并自愈", txt, nl,
            r'''    bool ensureApp(std::wstring* error)
    {
        ensureCom();
        if (g_session.app)
            return true;
''',
            r'''    bool ensureApp(std::wstring* error)
    {
        ensureCom();

        // 缓存的 Application 可能已随 PowerPoint 自行退出而失效；先探活再复用，
        // 失效则整体丢弃并在下方重建（自愈）。
        if (g_session.app && !isAppAlive())
            dropSession();

        if (g_session.app)
            return true;
''')

txt = patch("03 closePresentation 检测 PowerPoint 已退出", txt, nl,
            r'''        bool ok = com::callMethod(g_session.pres.get(), L"Close", {}, nullptr, error);
        g_session.pres = com::DispatchPtr();
        return ok;
    }''',
            r'''        bool ok = com::callMethod(g_session.pres.get(), L"Close", {}, nullptr, error);
        g_session.pres = com::DispatchPtr();

        // 关闭最后一个文稿后 PowerPoint 通常会自行退出，Application 代理随即
        // 失效。立即丢弃，避免后续工具命中失效代理（0x800401FD）。
        if (!isAppAlive())
            dropSession();
        return ok;
    }''')

txt = patch("04 openPresentation 路径归一化", txt, nl,
            r'''    bool openPresentation(const std::wstring& path, std::wstring* error)
    {
        if (!ensureApp(error))
            return false;

        // 若当前已打开同一路径则直接复用
        if (g_session.pres) {
            std::wstring cur = com::getStringProperty(g_session.pres.get(), L"FullName", nullptr);
            if (!cur.empty() && _wcsicmp(cur.c_str(), path.c_str()) == 0)
                return true;
        }''',
            r'''    bool openPresentation(const std::wstring& path, std::wstring* error)
    {
        if (!ensureApp(error))
            return false;

        const std::wstring filePath = normalizePath(path);

        // 若当前已打开同一路径则直接复用
        if (g_session.pres) {
            std::wstring cur = com::getStringProperty(g_session.pres.get(), L"FullName", nullptr);
            if (!cur.empty() && _wcsicmp(cur.c_str(), filePath.c_str()) == 0)
                return true;
        }''')

txt = patch("05 openPresentation Open() 传归一化路径", txt, nl,
            r'''        std::vector<VARIANT> args = {
            com::vtString(path),
            com::vtMissing(), // ReadOnly''',
            r'''        std::vector<VARIANT> args = {
            com::vtString(filePath),
            com::vtMissing(), // ReadOnly''')

txt = patch("06 savePresentation 路径归一化", txt, nl,
            r'''            long format = ppSaveAsOpenXMLPresentation;
            std::wstring lower = *path;''',
            r'''            const std::wstring target = normalizePath(*path);
            long format = ppSaveAsOpenXMLPresentation;
            std::wstring lower = target;''')

txt = patch("07 savePresentation SaveAs() 传归一化路径", txt, nl,
            r'''            std::vector<VARIANT> args = {
                com::vtString(*path),
                com::vtInt(format)
            };''',
            r'''            std::vector<VARIANT> args = {
                com::vtString(target),
                com::vtInt(format)
            };''')

txt = patch("08 insertPicture 路径归一化", txt, nl,
            r'''        // 图片尺寸
        unsigned iw = 0, ih = 0;
        std::wstring iErr;
        if (!img::readPixelSize(imagePath, iw, ih, &iErr)) {''',
            r'''        const std::wstring imgPath = normalizePath(imagePath);

        // 图片尺寸
        unsigned iw = 0, ih = 0;
        std::wstring iErr;
        if (!img::readPixelSize(imgPath, iw, ih, &iErr)) {''')

txt = patch("09 insertPicture AddPicture() 传归一化路径", txt, nl,
            r'''        std::vector<VARIANT> args = {
            com::vtString(imagePath),
            com::vtInt(msoFalse),
            com::vtInt(msoTrue),''',
            r'''        std::vector<VARIANT> args = {
            com::vtString(imgPath),
            com::vtInt(msoFalse),
            com::vtInt(msoTrue),''')

txt = patch("10 exportPdf 路径归一化", txt, nl,
            r'''        std::vector<VARIANT> args = {
            com::vtString(path),
            com::vtInt(ppSaveAsPDF)
        };''',
            r'''        std::vector<VARIANT> args = {
            com::vtString(normalizePath(path)),
            com::vtInt(ppSaveAsPDF)
        };''')

txt = patch("11 exportSlidePng 路径归一化", txt, nl,
            r'''        std::vector<VARIANT> args = {
            com::vtString(path),
            com::vtString(L"PNG"),''',
            r'''        std::vector<VARIANT> args = {
            com::vtString(normalizePath(path)),
            com::vtString(L"PNG"),''')

txt = patch("12 setSlideBackground 路径归一化", txt, nl,
            r'''        if (!imagePath.empty()) {
            std::vector<VARIANT> args = { com::vtString(imagePath) };''',
            r'''        if (!imagePath.empty()) {
            std::vector<VARIANT> args = { com::vtString(normalizePath(imagePath)) };''')

txt = patch("13 shutdown 同样丢弃失效会话", txt, nl,
            r'''    void shutdown(std::wstring* error)
    {
        if (g_session.pres) {
            com::putProperty(g_session.pres.get(), L"Saved", com::vtBool(true), nullptr);
            com::callMethod(g_session.pres.get(), L"Close", {}, nullptr, nullptr);
            g_session.pres = com::DispatchPtr();
        }
        if (g_session.app && g_session.launchedByUs) {
            com::callMethod(g_session.app.get(), L"Quit", {}, nullptr, nullptr);
        }
        g_session.app = com::DispatchPtr();
    }''',
            r'''    void shutdown(std::wstring* error)
    {
        if (g_session.pres) {
            com::putProperty(g_session.pres.get(), L"Saved", com::vtBool(true), nullptr);
            com::callMethod(g_session.pres.get(), L"Close", {}, nullptr, nullptr);
            g_session.pres = com::DispatchPtr();
        }
        // Quit 只在代理仍存活时才有意义（服务端可能已自行退出）
        if (g_session.app && g_session.launchedByUs && isAppAlive()) {
            com::callMethod(g_session.app.get(), L"Quit", {}, nullptr, nullptr);
        }
        dropSession();
    }''')

if not FAILED:
    write_text(CPP, txt, bom)
    print("      -> written")

# ======================================================================
# 3. make_ext.ps1 —— 打包 settings.example.json
# ======================================================================
MK = os.path.join(SRC, "make_ext.ps1")
print("[2/4] %s" % MK)
txt, nl, bom = read_text(MK)
txt = patch("14 make_ext 打包 settings.example.json", txt, nl,
            r'''    @{ src = (Join-Path $Root "regulation.json5"); entry = "regulation.json5" },''',
            r'''    @{ src = (Join-Path $Root "regulation.json5"); entry = "regulation.json5" },
    # 运行时的报错信息会引导用户"参考 settings.example.json"，
    # 缺了它用户会被指到一个不存在的文件（曾经的实际故障）。
    @{ src = (Join-Path $Root "settings.example.json"); entry = "settings.example.json" },''',
            tabs=False)
if "14 " not in " ".join(FAILED):
    write_text(MK, txt, bom)
    print("      -> written")

# ======================================================================
# 4. install.ps1 —— 目录名对齐 PPTExtPlugin
# ======================================================================
INS = os.path.join(SRC, "install.ps1")
print("[3/4] %s" % INS)
txt, nl, bom = read_text(INS)
txt = patch("15 install 注释目录名对齐", txt, nl,
            r'''#     harness\profiles\web\extensions\PPTExt\              (DLL + regulation.json5 + settings)''',
            r'''#     harness\profiles\web\extensions\PPTExtPlugin\        (DLL + regulation.json5 + settings)''',
            tabs=False)
txt = patch("16 install extRoot 目录名对齐", txt, nl,
            r'''    $extRoot = Join-Path $profile "extensions\PPTExt"''',
            r'''    $extRoot = Join-Path $profile "extensions\PPTExtPlugin"''',
            tabs=False)
txt = patch("17 install 注册表名对齐", txt, nl,
            r'''    if ($names -notcontains "PPTExt") {
        $names += "PPTExt"''',
            r'''    if ($names -notcontains "PPTExtPlugin") {
        $names += "PPTExtPlugin"''',
            tabs=False)
if not FAILED:
    write_text(INS, txt, bom)
    print("      -> written")

# ======================================================================
print("[4/4] 结果")
if FAILED:
    print("  失败补丁: %s" % ", ".join(FAILED))
    sys.exit(1)
print("  全部补丁已应用（原文件备份为 *.bak）")
