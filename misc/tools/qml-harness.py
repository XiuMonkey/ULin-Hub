"""QML 试装台：用桩件实现 ULinHub 的四个单例，把任意页面单独跑起来并截图。

为什么需要它：QML 有一类失效**不报错**（自定义 contentItem 后不渲染、自我锚定压成 0 高、
坏 token 让颜色取默认值），静态检查（qmllint / token 守卫）看不见，只能靠"真的渲染出来看一眼"。

用法:
  python misc/tools/qml-harness.py --page qml/basic/settings/AppearancePage.qml
  python misc/tools/qml-harness.py --page qml/basic/settings/AppearancePage.qml --shot
  python misc/tools/qml-harness.py --page qml/basic/plugins/PluginMarketPage.qml --theme dark --shot

产物: build/temp/qml-harness/{ULinHub/, Main.qml}
  ULinHub/Theme.qml 由**真实色板与 metrics.json** 生成（不是手写近似值），
  所以颜色/字号/圆角与运行时一致。
"""
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HARNESS = os.path.join(REPO, "build", "temp", "qml-harness")
PALETTE_DIR = os.path.join(REPO, "assets", "resources", "styles")
METRICS = os.path.join(REPO, "source", "qml", "metrics.json")
QML_EXE = r"D:\Qt\6.11.2\msvc2022_64\bin\qml.exe"
QT_QML = r"D:\Qt\6.11.2\msvc2022_64\qml"
SHOT = os.path.join(REPO, "misc", "tools", "window-shot.py")


def qml_value(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    s = str(v)
    # 颜色（#RGB / #RRGGBB / rgba(...)）用 color 字面量，其余当字符串
    if s.startswith("#") or s.startswith("rgba(") or s.startswith("rgb("):
        return json.dumps(s)
    return json.dumps(s)


def qt_object(name, props):
    lines = [f"        property QtObject {name}: QtObject {{"]
    for k, v in props.items():
        lines.append(f"            property var {k}: {qml_value(v)}")
    lines.append("        }")
    return "\n".join(lines)


def build_theme(theme):
    palette = json.load(io.open(os.path.join(PALETTE_DIR, f"theme-{theme}.json"), encoding="utf-8"))
    metrics = json.load(io.open(METRICS, encoding="utf-8"))

    out = ["pragma Singleton", "import QtQuick", "", "// 由 misc/tools/qml-harness.py 从真实色板/metrics.json 生成，勿手改。", "QtObject {"]
    out.append(qt_object("color", palette))
    for group, values in metrics.items():
        if isinstance(values, dict):
            out.append(qt_object(group, values))
    out.append("}")
    return "\n".join(out) + "\n"


HUB = '''pragma Singleton
import QtQuick

// 试装台的 Hub 桩件：只覆盖被测页面用到的接口。
QtObject {
    property QtObject settings: QtObject {
        signal languageChanged()
        // 与 SettingsController::languages 同构：第 0 项是"跟随系统"（code 为空串）
        property var languages: [
            { name: "跟随系统", code: "" },
            { name: "简体中文", code: "zh-CN" },
            { name: "English", code: "en" }
        ]
        property string languageCode: ""
        property string appearanceFeedback: ""
        function setLanguage(code) { languageCode = code; languageChanged(); appearanceFeedback = "界面语言已切换（试装台）" }
        function resetStyles() { appearanceFeedback = "已重置为默认（试装台）" }
    }

    // 交互面板（basic/chat/InteractionPanel.qml）用到的接口
    signal interactionChanged()
    property bool questionActive: false
    property bool approvalActive: false
    property string approvalToolName: ""
    property string approvalReason: ""
    property var questions: []
    function submitAnswers(selection) { }
    function respondApproval(allow) { }

    property QtObject plugins: QtObject {
        property bool busy: false
        property int tab: 0
        property var marketItems: [
            { name: "dsh-tools-filter", category: "工具", url: "https://example.invalid/a",
              description: "按关键词过滤工具调用，减少无关工具进入上下文。这条描述故意写长一点，用来看它是否会被安装按钮压住。" },
            { name: "dsh-quick-orbit", category: "界面", url: "https://example.invalid/b",
              description: "侧栏快捷轨道。" }
        ]
        property var installedItems: [ { name: "dshmarket", version: "1.9.0" } ]
        property bool installing: false
        property string installStatus: ""
        property var installLog: []
        property string status: "已加载 2 个插件（试装台）"
        property string pageLabel: "第 1 / 1 页"
        property bool canPrev: false
        property bool canNext: false
        function openPage() {} function closePage() {} function refresh() {}
        function setTab(t) { tab = t } function setSearch(s) {}
        function install(u) {} function update(n) {} function uninstall(n) {}
        function restartServer() {} function prevPage() {} function nextPage() {}
    }

    // 扩展管理桩件：与 C++ 的 ExtensionManagerController 同名同形（三来源 + 选中 + 状态 + 重启提示）
    property QtObject extensions: QtObject {
        property bool open: true
        property bool busy: false
        property int selectedIndex: 2
        readonly property bool canRemove: selectedIndex >= 0
        property string status: "已安装 4 个扩展。（试装台）"
        property string restartHint: "工具扩展已装好 —— 重启内置服务端后才会加载"
        property var items: [
            { kind: "tool",    name: "dsh-tools-filter", kindLabel: "",                 label: "dsh-tools-filter",                          loaded: true },
            { kind: "tool",    name: "dshmarket",        kindLabel: "",                 label: "dshmarket",                                 loaded: true },
            { kind: "client",  name: "QuickOrbit",       kindLabel: "客户端扩展",       label: "QuickOrbit  [客户端扩展]",                  loaded: true },
            { kind: "client",  name: "OrbitLite",        kindLabel: "客户端扩展",       label: "OrbitLite  [客户端扩展·未装载]",            loaded: false }
        ]
        function openPage() { open = true }
        function closePage() { open = false }
        function refresh() {}
        function installFromFile() {}
        function removeSelected() { return false }
        function cleanupResiduals() {}
        function restartServer() {}
    }
}
'''

STYLE = '''pragma Singleton
import QtQuick

// 试装台的 Style 桩件：直接按仓库内的相对路径解析（不经 <exe>/styles 的遮蔽逻辑）。
QtObject {
    property string rootUrl: "%s"
    function url(logical) { return rootUrl + "/" + logical }
}
'''

HOST_UI = '''pragma Singleton
import QtQuick

// 试装台的 HostUi 桩件：与宿主的 HostUi 同名同形（registerItem / item / mountInto / unmountOwner）。
// 宿主 QML 根里有 Component.onCompleted: HostUi.registerItem("topBar", this) —— 没有这个桩件，
// 那些页面在试装台里会因 HostUi 未定义而整个装载失败。
QtObject {
    property var hostItems: ({})
    property var mounts: []
    function registerItem(name, item) { hostItems[name] = item; return true }
    function item(name) { return hostItems[name] || null }
    function mountInto(owner, hostItem, url, context) { mounts.push({ owner: owner, host: hostItem, url: url }) ; return owner + "#stub" }
    function unmountOwner(owner) { const n = mounts.length; mounts = mounts.filter(function (m) { return m.owner !== owner }); return n - mounts.length }
    function unmountAll() { const n = mounts.length; mounts = []; return n }
    function mounted() { return mounts }
}
'''

WINDOW_BRIDGE = '''pragma Singleton
import QtQuick

QtObject {
    property bool maximized: false
    property bool fullScreen: false

    // 与宿主 WindowBridge::recentPointerPos 同形：试装台里没有真鼠标事件，返回无效值，
    // 于是弹窗退回"从居中长出来"那条路（widgets/AppPopup.qml 会调它）。
    function recentPointerPos(maxAgeMs) { return undefined }
}
'''

QMLDIR = '''module ULinHub
singleton Theme 1.0 Theme.qml
singleton HostUi 1.0 HostUi.qml
singleton Hub 1.0 Hub.qml
singleton Style 1.0 Style.qml
singleton WindowBridge 1.0 WindowBridge.qml
'''

MAIN = '''import QtQuick
import QtQuick.Window
import ULinHub 1.0

Window {
    id: win
    width: 900
    height: 700
    visible: true
    title: "QML 试装台 - %(page)s"
    color: Theme.color.windowBg

    Rectangle {
        anchors.fill: parent
        anchors.margins: 20
        color: Theme.color.panelBg
        radius: Theme.radius.card
        border.width: 1
        border.color: Theme.color.border

        Loader {
            id: host
            anchors.fill: parent
            anchors.margins: 16
            source: Style.url("%(page)s")
        }
    }

    // 装载失败要看得见（否则只会是一片空白，和"渲染不出来"分不清）
    Connections {
        target: host
        function onStatusChanged() {
            if (host.status === Loader.Error)
                console.error("[harness] 页面装载失败: " + Style.url("%(page)s"))
        }
    }

}
'''


def say(msg):
    """控制台是 GBK：非 ASCII 直接 print 会抛 UnicodeEncodeError，统一走这里（写 UTF-8 日志 + 保底打印）。"""
    line = str(msg).rstrip()
    with io.open(os.path.join(HARNESS, "harness.log") if os.path.isdir(HARNESS) else os.devnull,
                 "a", encoding="utf-8", newline="\n") as fh:
        fh.write(line + "\n")
    try:
        print(line)
    except UnicodeEncodeError:
        print(line.encode("ascii", "replace").decode("ascii"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--page", required=True, help="相对 source/qml 的页面路径，例如 basic/settings/AppearancePage.qml")
    ap.add_argument("--theme", default="light", choices=["light", "dark"])
    ap.add_argument("--shot", action="store_true", help="启动 qml.exe 并截图")
    ap.add_argument("--out", default=os.path.join(HARNESS, "shot.png"))
    ap.add_argument("--keep", action="store_true", help="截图后不关闭进程")
    args = ap.parse_args()

    page = args.page[4:] if args.page.startswith("qml/") else args.page
    src_page = os.path.join(REPO, "source", "qml", page)
    if not os.path.exists(src_page):
        say(f"!! 页面不存在: {src_page}")
        return 2

    shutil.rmtree(HARNESS, ignore_errors=True)
    os.makedirs(os.path.join(HARNESS, "ULinHub"))
    root_url = "file:///" + os.path.join(REPO, "source", "qml").replace("\\", "/")

    io.open(os.path.join(HARNESS, "ULinHub", "qmldir"), "w", encoding="utf-8", newline="\n").write(QMLDIR)
    io.open(os.path.join(HARNESS, "ULinHub", "Theme.qml"), "w", encoding="utf-8", newline="\n").write(build_theme(args.theme))
    io.open(os.path.join(HARNESS, "ULinHub", "Hub.qml"), "w", encoding="utf-8", newline="\n").write(HUB)
    io.open(os.path.join(HARNESS, "ULinHub", "Style.qml"), "w", encoding="utf-8", newline="\n").write(STYLE % root_url)
    io.open(os.path.join(HARNESS, "ULinHub", "WindowBridge.qml"), "w", encoding="utf-8", newline="\n").write(WINDOW_BRIDGE)
    io.open(os.path.join(HARNESS, "ULinHub", "HostUi.qml"), "w", encoding="utf-8", newline="\n").write(HOST_UI)
    io.open(os.path.join(HARNESS, "Main.qml"), "w", encoding="utf-8", newline="\n").write(MAIN % {"page": page})

    say(f"试装台: {HARNESS}")
    say(f"  页面: {page}   主题: {args.theme}")
    say(f"  手动运行: \"{QML_EXE}\" -I \"{HARNESS}\" -I \"{QT_QML}\" \"{os.path.join(HARNESS, 'Main.qml')}\"")

    if not args.shot:
        return 0

    env = dict(os.environ)
    env["QML2_IMPORT_PATH"] = HARNESS + os.pathsep + QT_QML
    env["QML_IMPORT_PATH"] = env["QML2_IMPORT_PATH"]
    proc = subprocess.Popen([QML_EXE, "-I", HARNESS, "-I", QT_QML, os.path.join(HARNESS, "Main.qml")],
                            cwd=HARNESS, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    time.sleep(6)
    if proc.poll() is not None:
        say("!! qml.exe 已退出，输出如下：")
        say(proc.stdout.read())
        return 3

    shot = subprocess.run([sys.executable, SHOT, "--pid", str(proc.pid), "--out", args.out],
                          capture_output=True, text=True, encoding="utf-8", errors="replace")
    say("截图: " + ((shot.stdout or "").strip() or (shot.stderr or "").strip()))
    if not args.keep:
        proc.terminate()
        time.sleep(1)
    say("qml.exe 输出:")
    if args.keep:
        say("(keep，进程仍在跑)")
    else:
        try:
            say(proc.stdout.read())
        except Exception as exc:
            say(f"(读取输出失败: {exc})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
