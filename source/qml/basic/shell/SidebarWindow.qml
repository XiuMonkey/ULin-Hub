// 「面板」右栏脱离出来的**独立窗口**（需求 ②）：整个右栏那块卡片拖出去之后住在这里。
//
// 为什么用系统装饰（`flags: Qt.Window`，不加 FramelessWindowHint）：需求要的"随意改变大小（四个边）
// + 最小化/最大化按钮"系统本来就全给了；主窗口那套无边框皮肤（自绘标题栏 + NativeWindowFilter 的
// WM_NCHITTEST 补位 + WindowBridge 的**单窗口**绑定）硬套到多窗口上要先拆它。取向与
// basic/shell/DetachedBlockWindow.qml 一致。
//
// ⚠️ 本文件**不装内容**：右栏那块 RightSidebar 是 **reparent** 进来的**同一份实例**
//    （见 Shell.detachSidebar / attachSidebar）。两个宿主各放一份的话，终端/网页会各跑一个会话
//    —— 那正是需求里明确不要的。
//
// 分工：
//   · Shell 说"要不要显示 / 窗口多大在哪"（sidebarDetached / sidebarWinX…）与"内容归谁"；
//   · 本文件只把窗摆出来：几何由 Shell 主动设（这里**不写 x/y/width/height 绑定** ——
//     绑上去就会被用户的拖动/缩放摘掉，或反过来每帧盖回去）；
//   · 窗口自己的 move/resize（拖系统边框、最大化、贴边）回写 Shell，下次脱离沿用它。
// 抓手（三个点）在右栏自己的标题行里；脱离态由 RightSidebar 直接搬这扇窗（它够得着 Window.window）。

import QtQuick
import QtQuick.Window
import ULinHub 1.0

Window {
    id: win

    // 由 Shell 注入：状态与几何都在壳层（本文件不存状态）
    required property var host

    title: qsTrId("right_sidebar_title")
    flags: Qt.Window                    // 系统装饰：四边缩放 + 最小化/最大化/关闭都由系统提供
    visible: win.host ? win.host.sidebarDetached : false
    // 窗口底色（卡片自己会画背景）：缩放/重绘的那一瞬间不闪白
    color: Theme.color.panelBg
    minimumWidth: 200
    minimumHeight: 160

    // 系统 ✕ / Alt+F4：与"点顶栏 ◫ 关副窗口"**同一语义**（需求 ④）——
    // 右栏回"已附着但隐藏"（内容 reparent 回 Shell、窗口关掉），下一次点 ◫ 默认以**附着**状态打开。
    // close.accepted = true：不让系统直接收掉这扇窗（它还要把内容还回去），隐藏由 sidebarDetached
    // 那条绑定完成。
    onClosing: (close) => {
        close.accepted = true
        console.log("[sidebar-detach] window closing")
        if (win.host)
            win.host.attachSidebar(false)
    }

    // 窗口被搬动/改尺寸：回写 Shell。只在值真的变了时响；Shell 程序化摆窗口期间有自己的
    // applying 门（sidebarWinApplying）⇒ 不会自激（"模型→窗口→模型"来回弹）。
    onXChanged: pushGeometry()
    onYChanged: pushGeometry()
    onWidthChanged: pushGeometry()
    onHeightChanged: pushGeometry()

    function pushGeometry() {
        if (!win.host || !win.host.sidebarDetached)
            return
        win.host.setSidebarWindowGeometry(Math.round(win.x), Math.round(win.y),
                                          Math.round(win.width), Math.round(win.height))
    }

    Component.onCompleted: console.log("[sidebar-detach] window created " + win.width + "x" + win.height
                                       + " at " + win.x + "," + win.y)
}
