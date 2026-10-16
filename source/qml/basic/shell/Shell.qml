// 壳层布局：卡片式（与旧壳一致）——
//   左边一张侧栏卡片，右边一列：顶栏卡片 + 会话卡片；卡片之间留 panelGap、与窗口之间留 windowMargin。
// 旧壳的对应关系：`#sidebar`/`#topBar` 的 radius 12 + `ShadowPanel` 的 CardShadow{8,2,12}；
// 这里由 components/Card.qml 的 MultiEffect 阴影承担，尺寸/颜色全取 token。
// 这一层只做装配与遮罩/Toast 宿主，具体内容都在被 Style.url() 装载的单元里（可被风格包覆盖）。

import QtQuick
import QtQuick.Window
import ULinHub 1.0
import "../../widgets"
// ui/shell 与 ui/sidebar、ui/topbar 是并列目录：QML 只按同目录/已 import 的目录解析组件名，
// 所以跨目录用裸组件名（Sidebar / TopBar）必须显式 import
import "../sidebar"
import "../topbar"
import "../browser"

Item {
    id: shell

    // 扩展注入点：扩展用 HostUi.item("shell") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: {
        HostUi.registerItem("shell", this)
        evStart()
    }

    function evSay(msg) {
        console.log("[EV] " + msg)
    }
    function evStart() {
        evSay("BOOT")
        evRzHandoff = Qt.application.arguments.indexOf("--ev-handoff") >= 0
        evSay("BOOT handoff=" + evRzHandoff + " args=" + Qt.application.arguments)
        evTimer.restart()
    }
    Timer {
        id: evTimer
        interval: 3000
        repeat: true
        onTriggered: shell.evStep()
    }
    // 缩放取证节拍：走**真**入口（SidebarWindow.resizeBegin/resizeApply，与八条热区同一个）
    Timer {
        id: evResizeTimer
        interval: 10
        repeat: true
        onTriggered: shell.evResizeTick()
    }
    property int evRzN: 0
    property int evRzTarget: 80
    property bool evRzHandoff: false
    property real evRzBaseW: 0
    property real evRzBaseH: 0
    function evResizeStart() {
        evRzN = 0
        evRzBaseW = sidebarWindow.width
        evRzBaseH = sidebarWindow.height
        evSay("RZ start base=" + evRzBaseW + "x" + evRzBaseH + " @" + sidebarWindow.x + ","
              + sidebarWindow.y + " dpr=" + sidebarWindow.devicePixelRatio)
        evResizeTimer.restart()
    }
    function evResizeTick() {
        if (evRzN > evRzTarget) {
            evResizeTimer.stop()
            evSay("RZ done " + sidebarWindow.width + "x" + sidebarWindow.height)
            return
        }
        evRzN++
        // 与八条热区完全同一条路：先 resizeBegin（按下点 = 基准屏幕坐标），再 resizeApply。
        // 每拍都 resizeBegin 是因为 resizeApply 的"交给系统"那一档有一次手势只交接一次的门
        // —— 代码驱动没有真实手势，只能按拍重开。
        // ⚠️ 上限钳在屏幕内：窗口一旦超出屏幕，屏幕抓图会把"屏幕外的部分"读成黑，污染黑块统计
        const maxW = Screen.desktopAvailableWidth - sidebarWindow.x - 40
        const maxH = Screen.desktopAvailableHeight - sidebarWindow.y - 40
        const w = Math.min(evRzBaseW + evRzN * 3, maxW)
        const h = Math.min(evRzBaseH + evRzN * 2, maxH)
        sidebarWindow.resizeBegin(0, 0)
        sidebarWindow.resizeApply(Qt.RightEdge, w - evRzBaseW, h - evRzBaseH, evRzHandoff)
        if (evRzN % 8 === 0 || evRzN === 1)
            evSay("RZ n=" + evRzN + " win=" + sidebarWindow.width + "x" + sidebarWindow.height
                  + " @" + sidebarWindow.x + "," + sidebarWindow.y)
    }
    property int evN: 0
    property real evGripX: 0
    property real evGripY: 0
    function evStep() {
        evN++
        if (evN === 1) {
            // ① 先打开右栏（脱离需要右栏在槽位里）
            Hub.openRightSidebar()
            evSay("S0 open rightSidebar=" + Hub.rightSidebarOpen + " dock=" + dockScreenRect())
            return
        }
        if (evN === 2) {
            // ② 脱离成独立窗口（代码驱动，不合成鼠标）
            liftSidebarForDrag()
            finishSidebarDrag(20, 20)
            sidebarWindow.moveTo(400, 200)
            evSay("S1 detached=" + sidebarDetached
                  + " win=" + sidebarWindow.x + "," + sidebarWindow.y + " "
                  + sidebarWindow.width + "x" + sidebarWindow.height)
            return
        }
        if (evN === 3) {
            evSay("DONE-detach win=" + sidebarWindow.x + "," + sidebarWindow.y + " "
                  + sidebarWindow.width + "x" + sidebarWindow.height)
            evResizePass()
            return
        }
        if (evN === 4) {
            // ② 三点抓手在独立态下搬窗：复现 RightSidebar 的 owner 查找（findShell 未定义）
            const grip = evFindGrip(rightSidebar)
            if (!grip) {
                evSay("S2 grip NOT FOUND")
                return
            }
            evGripX = grip.mapToGlobal(grip.width / 2, grip.height / 2).x
            evGripY = grip.mapToGlobal(grip.width / 2, grip.height / 2).y
            evSay("S2 grip found " + grip + " detached=" + sidebarDetached
                  + " gripScreen=" + evGripX + "," + evGripY
                  + " win=" + sidebarWindow.x + "," + sidebarWindow.y)
            // 走的就是 RightSidebar 里那段：owner = findShell() —— 若未定义会抛 ReferenceError
            let found = null
            try {
                found = findShell()
            } catch (e) {
                evSay("S2 findShell() THREW: " + e)
            }
            evSay("S2 findShell() -> " + found)
            return
        }
        if (evN >= 5 && evN <= 7) {
            // ③ 喂屏幕坐标（1:1 跟手判据），再跑一次缩放取证（多跑几轮好让外部采样窗口对齐）
            const dx = 40 + (evN - 5) * 40
            const dy = 25 + (evN - 5) * 25
            // 先按一次（真抓手的按下路径：记 pressWinX/Y）
            gripPress()
            const before = sidebarWindow.x + "," + sidebarWindow.y
            gripDragTo(dx, dy)
            evSay("S3 feed d=" + dx + "," + dy + " pressWin=" + gripPressWin()
                  + " owner=" + evFindOwner()
                  + " before=" + before + " after=" + sidebarWindow.x + "," + sidebarWindow.y)
            evSay("S3 chain:" + evChain())
            if (evN === 7) {
                evSay("S4 done win=" + sidebarWindow.x + "," + sidebarWindow.y)
                evResizePass()
            }
            return
        }
        if (evN === 8) {
            evSay("S5 all done")
            evTimer.stop()
        }
    }
    // 跑一轮缩放取证（3s 后开始，每 10ms 一拍）
    function evResizePass() {
        evRzDelay.restart()
    }
    // 启动时就按命令行开关选档：环境变量由取证脚本给（无参数 = 只跑 resizeApply 那一档）
    function evProbeMode() {
        return Qt.application.arguments.indexOf("--ev-qml-resize") >= 0
    }
    Timer {
        id: evRzDelay
        interval: 3000
        repeat: false
        onTriggered: shell.evResizeStart()
    }
    // 找到抓手（RightSidebar 头部那三个点）
    function evFindGrip(item) {
        if (!item) return null
        if (item.objectName === "panelGrip") return item
        const kids = item.children
        for (let i = 0; i < kids.length; i++) {
            const f = evFindGrip(kids[i])
            if (f) return f
        }
        return null
    }
    // 用**真**抓手发拖拽信号：与 RightSidebar 的 onDragMoved 接线同一条路
    function gripDragTo(dx, dy) {
        const grip = evFindGrip(rightSidebar)
        if (!grip) return
        grip.dragging = true   // 真机上"越过 6px 手感阈值"置的就是它（RightSidebar 会拿它门控）
        grip.dragMoved(dx, dy, Qt.point(evGripX + dx, evGripY + dy), Qt.point(0, 0))
    }
    // 找壳层（RightSidebar 里那段要复现的"父链查找"）
    function evFindOwner() {
        for (let p = rightSidebar.parent; p; p = p.parent) {
            if (p.moveSidebarWindowTo)
                return p
        }
        return null
    }
    // 打印右栏的 parent 链，看清楚 reparent 之后到底挂在谁下面
    function evChain() {
        let out = ""
        let p = rightSidebar.parent
        let guard = 0
        while (p && guard < 12) {
            const has = p.moveSidebarWindowTo ? "HAS" : "no"
            out += " <- " + p + "[" + has + "]"
            p = p.parent
            guard++
        }
        return out
    }
    function gripPress() {
        const grip = evFindGrip(rightSidebar)
        if (grip) grip.pressStarted()
    }
    function gripPressWin() {
        const grip = evFindGrip(rightSidebar)
        return grip ? (grip.pressWinX + "," + grip.pressWinY) : "?"
    }

    // 供子界面报一句轻提示（未落地的入口用它，别放哑按钮）
    function notify(text) {
        toast.show(text)
    }

    // ---------------- 块脱离 / 回附（独立窗口）----------------
    // 状态在 common/shell/PanelDockModel（QML 面是单例 Dock），窗口对象在这里按 Dock.detachedBlocks
    // 一扇一块地建；本层只做"把壳层的坐标与右栏几何喂给它"这种接线。
    //
    // 落点提示的矩形与开关：矩形在**拖动开始时**算一次（拖动期间右栏可能已经收起，每帧重算会指错地方）；
    // 开关由正在拖的那个独立窗口推过来（拖动中主窗口收不到 hover，只能这样推）。
    property rect dropZone: Qt.rect(0, 0, 0, 0)
    property bool dropHintActive: false

    // ---------------- 右栏整体：拿起来跟手 → 回附 / 脱离（需求 ①②③④）----------------
    // 内容**不是副本**：拿起来/脱离时把 RightSidebar 那个 Item reparent 到独立窗口的内容槽，
    // 回附时 reparent 回附着槽位 sidebarSlot —— 终端/网页只有一份会话（各放一份会各跑一个渲染进程）。
    // 状态就下面这几条（窗口对象是文件末尾的 SidebarWindow 实例）。
    //
    // 时序（本次改动的核心，别退回"到阈值就在松手时出窗"那版）：
    //   ① 三点越过手感阈值 ⇒ liftSidebarForDrag：右栏进**无边框临时窗口**，从这一刻起 1:1 跟着光标；
    //      ⚠️ 这时**不动**主窗口布局（不调 Hub.closeRightSidebar）⇒ 顶栏/会话区尺寸一动不动；
    //   ② 松手 ⇒ finishSidebarDrag：落回右栏原区域（±24）就 reparent 回槽位；
    //      落在区域外才**确认脱离**（sidebarDetached=true）+ 这时才腾出主窗口的空间。
    property bool sidebarDetached: false
    // 独立窗口几何：跟手/自己摆的时候都走 applySidebarWindowGeometry 这一条路（见下）。
    property real sidebarWinX: 0
    property real sidebarWinY: 0
    property real sidebarWinW: 0
    property real sidebarWinH: 0
    // 正在程序化摆窗口：这期间窗口的几何变化不回写状态（防回环：摆 → 通知 → 又摆）
    property bool sidebarWinApplying: false

    // ---- 需求 ①②：按下三点越过手感阈值就"拿起来"，跟着光标 1:1 走，松手才定结局 ----
    // 与"确认脱离"（sidebarDetached）分开：拿起来只是**临时窗口**，结局未定。
    property bool sidebarFloating: false
    // 跟手基准：拿起来那一刻的光标屏幕坐标 + 窗口位置（1:1：窗口位置 = 基准 + 屏幕位移）
    property real floatOriginX: 0
    property real floatOriginY: 0
    property real floatWinX: 0
    property real floatWinY: 0
    // 松手落点容差（与独立窗口往回拖用同一个 slack，手感一致）
    readonly property int dropSlack: 24
    // "右栏此刻在独立窗口里"（跟手 or 已确认脱离）：独立窗口的显示与右栏的可见性都看它
    readonly property bool sidebarInWindow: sidebarDetached || sidebarFloating

    // 右栏（面板坞）在**本窗口场景坐标**里的矩形：落点提示要画的矩形（脱离方向的判定在抓手自己那里，
    // 它按屏幕坐标算；回附方向由独立窗口里的抓手按屏幕坐标算）。
    // 用**开着**的几何现算，不读 rightSidebar 的当前坐标：右栏收起时它在窗口右缘之外，
    // 脱离时它已经不在本窗口里 —— 两种情况下都读不到"它该在的位置"。
    function dockSceneRect() {
        return Qt.rect(shell.width - Theme.spacing.windowMargin - shell.rightSidebarEffW,
                       sidebarSlot.y, shell.rightSidebarEffW, sidebarSlot.height)
    }
    // 同一块矩形在**屏幕坐标**里：跨窗口判断（脱离方向够不够、独立窗口拖回来落没落进来）只能用屏幕坐标
    // —— 各自窗口的场景原点不是一个。抓手每次需要时**现算**（主窗口被搬动后属性绑定不会重算）。
    function dockScreenRect() {
        const xOpen = shell.width - Theme.spacing.windowMargin - shell.rightSidebarEffW
        const p = shell.mapToGlobal(xOpen, sidebarSlot.y)
        return Qt.rect(p.x, p.y, shell.rightSidebarEffW, sidebarSlot.height)
    }

    // 松手落点判定（屏幕坐标 + ±dropSlack 容差）：跟手期间右栏没在槽位里，只能按"光标回没回到
    // 那块矩形"来判 —— 与独立窗口往回拖用的是同一条规则。
    function insideDockZone(p) {
        const r = dockScreenRect()
        return p.x >= r.x - dropSlack && p.x <= r.x + r.width + dropSlack
               && p.y >= r.y - dropSlack && p.y <= r.y + r.height + dropSlack
    }

    // 程序化摆独立窗口（只走这一条路；窗口自己改了几何走 setSidebarWindowGeometry 回写）。
    // ⚠️ **不要**在这里直接写 `sidebarWindow.x/y/width/height`：那会绕过窗口里那条"每帧最多下发一次"
    //    的合并（SidebarWindow.applyGeometry / Qt.callLater），把 125Hz 的跟手节拍原样变成
    //    125 次/秒的原生窗口尺寸变更 —— 正是"拖右下角缩放时出黑块"的成因（见 SidebarWindow 文件头）。
    //    所以这里只更新壳层记着的目标值，真正下发交给窗口那一个入口。
    function applySidebarWindowGeometry(x, y, w, h) {
        sidebarWinApplying = true
        sidebarWinX = Math.round(x)
        sidebarWinY = Math.round(y)
        sidebarWinW = Math.round(w)
        sidebarWinH = Math.round(h)
        sidebarWindow.applyGeometry(sidebarWinX, sidebarWinY, sidebarWinW, sidebarWinH)
        sidebarWinApplying = false
    }
    // 独立窗口 move/resize 的回写口（SidebarWindow 调）
    function setSidebarWindowGeometry(x, y, w, h) {
        if (sidebarWinApplying)
            return
        sidebarWinX = x
        sidebarWinY = y
        sidebarWinW = w
        sidebarWinH = h
    }

    // 「拿起来」（需求 ①）：三点越过手感阈值那一刻调。把右栏搬进**无边框临时窗口**，
    // 从此这扇窗跟着光标 1:1 走 —— 不再是"到阈值时松手才突然出窗"。
    // ⚠️ 跟手期间**绝不**碰主窗口布局（不调 Hub.closeRightSidebar）：顶栏/会话区尺寸一动不动。
    function liftSidebarForDrag() {
        if (sidebarInWindow)
            return
        // 窗口几何 = 右栏那块矩形**本身**（位置/尺寸逐像素一致）：不再为自绘标题栏多留一条高度、
        // 也不再上移 —— 副窗口的"标题栏"就是面板自己的头部行（见 SidebarWindow.qml 的说明），
        // 于是右栏内容零跳变地落进窗口，而"这已经是一扇窗"由头部里那两颗窗口按钮看得出来。
        const r = dockScreenRect()
        const w = Math.max(Math.round(r.width), Theme.size.rightSidebarMinWidth)
        const h = Math.max(Math.round(r.height), 240)
        const x = Math.round(r.x)
        const y = Math.round(r.y)

        sidebarWindow.prepareForLift(x, y, w, h)   // 记目标几何（窗口自己要用，见 normalizeForLift）
        // ⚠️ 必须在 reparent **之前**置位：reparent 换宿主会让抓手的 MouseArea 立刻丢掉抓取
        //    （Qt 在 item 换窗口时会 un-grab），那次 cancel 不是松手（RightSidebar 里靠 handedOff 丢掉）。
        sidebarFloating = true
        sidebarWindow.normalizeForLift()           // 上一次可能留了最大化/最小化：显示后拉回普通态
        applySidebarWindowGeometry(x, y, w, h)

        // 跟手基准：光标**此刻**在哪（用屏幕坐标差 1:1，见下方 Timer 的说明）
        const p = WindowBridge.cursorScreenPos()
        floatOriginX = p.x
        floatOriginY = p.y
        floatWinX = x
        floatWinY = y
        // 落点提示：跟手全程都画着（画的就是"松手回哪儿"），光标落进去时更实一点
        beginDetachPreview(false)
        // reparent：同一份实例搬过去（终端/网页不会重开会话）
        rightSidebar.parent = sidebarWindow.panelSlot
        console.log("[sidebar-drag] lift win " + w + "x" + h + " @" + x + "," + y
                    + " cursor " + Math.round(p.x) + "," + Math.round(p.y))
    }

    // 跟手到某个光标**屏幕**位置（1:1）：窗口位置 = 拿起来那一刻的窗口位置 + 光标屏幕位移。
    // 单独抽成函数是因为"跟手"就是这件事本身 —— 下面的 Timer 只是按光标喂它。
    function sidebarFollowTo(screenX, screenY) {
        if (!sidebarFloating)
            return
        applySidebarWindowGeometry(floatWinX + (screenX - floatOriginX),
                                   floatWinY + (screenY - floatOriginY),
                                   sidebarWinW, sidebarWinH)
        // 落点提示：跟手全程都画着（画的就是"松手回哪儿"）。矩形只在空掉时重算一次
        // —— 跟手期间布局是冻的，每帧重算没必要（抓手那次 cancel 会把矩形清掉，这里自愈）。
        if (dropZone.width <= 0 || dropZone.height <= 0)
            dropZone = dockSceneRect()
        dropHintActive = insideDockZone(Qt.point(screenX, screenY))
    }

    // 跟手一帧：按光标跟一次，并按**物理**左键状态收尾（松手就定结局）。
    // 为什么松手只能这样判 —— 见 WindowBridge.leftButtonDown 的说明（跟手期间鼠标消息投给握着
    // 捕获的**主窗口**，搬出去的那扇窗一个鼠标事件都收不到）。
    function sidebarFollowTick() {
        if (!sidebarFloating)
            return
        const p = WindowBridge.cursorScreenPos()
        sidebarFollowTo(p.x, p.y)
        if (!WindowBridge.leftButtonDown())
            finishSidebarDrag(p.x, p.y)
    }

    // 松手：按落点分两个分支（需求 ①②）。重复调用是安全的（跟手的 Timer 与抓手的 finished 都汇到这儿）。
    function finishSidebarDrag(atScreenX, atScreenY) {
        if (!sidebarFloating)
            return
        const over = insideDockZone(Qt.point(atScreenX, atScreenY))
        if (over) {
            // ① 落回右栏原区域 ⇒ 回附：内容 reparent 回槽位。
            //    跟手期间没动过 rightInset ⇒ 布局从头到尾没变，这里只是把卡片放回原位。
            rightSidebar.parent = sidebarSlot
            sidebarFloating = false
            console.log("[sidebar-drag] drop in zone → 回附（布局未动过）")
        }
        else {
            // ② 落在区域外 ⇒ **此刻**才确认脱离：窗口留在光标处、接上自绘标题栏（它本来就在），
            //    主窗口这时才腾出空间（需求 ② 要的时序：跟手期间不许重排）。
            sidebarDetached = true
            sidebarFloating = false
            Hub.closeRightSidebar()
            console.log("[sidebar-drag] drop outside → 独立窗口 @" + sidebarWindow.x + "," + sidebarWindow.y
                        + " " + sidebarWindow.width + "x" + sidebarWindow.height)
        }
        endDetachPreview()
    }

    // 回附（需求 ③⑤）：内容 reparent 回附着槽位、关掉独立窗口。
    //   · 从独立窗口把三点拖回右栏位置松手 ⇒ open=true（用户就是想看它，落回来就展开）；
    //   · 点顶栏 ◫ / 点独立窗口自绘的 ✕ / 系统 ✕ ⇒ open=false（"已附着但隐藏"，下次点 ◫ 默认以附着状态打开）。
    function attachSidebar(open) {
        if (!sidebarDetached)
            return
        // 副窗口被最小化时先还原再隐藏：否则下次"拿起来"show 出来的还是一扇最小化的窗
        // （showNormal 是 QWindow 的公有槽，QML 侧可以直接调）
        if (sidebarWindow.visibility === Window.Minimized)
            sidebarWindow.showNormal()
        sidebarDetached = false        // 独立窗口随之隐藏；自绘 ✕ / 系统 ✕ 那条路都汇到这里
        rightSidebar.parent = sidebarSlot
        if (open)
            Hub.openRightSidebar()
        else
            Hub.closeRightSidebar()
        console.log("[sidebar-detach] attach open=" + open)
    }

    // 把副窗口**移动**到某处（面板头部那条拖动热区、独立态下三点抓手都调它；只改 x/y、不动尺寸）。
    // 为什么不让它们直接写 win.x/win.y（原来就是那样）：几何必须**只有一个出口**
    // （SidebarWindow.applyGeometry 的"每帧最多下发一次"），多一个直接写窗口的口子就等于绕开合并 ——
    // 拖动是 125Hz 的，绕开之后又变成每帧多次原生窗口变更，缩放那边刚修好的东西会被它带回来。
    // 尺寸取壳层记着的值（拖动不改宽高），位置按"按下时位置 + 屏幕位移"给（1:1 跟手）。
    function moveSidebarWindowTo(x, y) {
        evSay("MSW to " + x + "," + y + " detached=" + sidebarDetached + " floating=" + sidebarFloating
              + " w=" + sidebarWinW + " h=" + sidebarWinH + " applying=" + sidebarWinApplying)
        if (!sidebarDetached && !sidebarFloating)
            return
        applySidebarWindowGeometry(x, y, sidebarWinW, sidebarWinH)
    }

    // 面板头部那两颗窗口按钮（− 最小化 / □❐ 最大化-还原）的转发口：按钮只在副窗口态可见
    // （见 RightSidebar.qml），点它时副窗口必然在。实现**只有一份**（SidebarWindow 里的
    // minimizeWindow / toggleMaximize），这里只转发 —— 面板够不着那扇窗（窗口对象归壳层所有），
    // 千万别在 RightSidebar 里再抄一份窗口操作。
    function minimizeSidebarWindow() {
        if (sidebarDetached)
            sidebarWindow.minimizeWindow()
    }
    function toggleMaximizeSidebarWindow() {
        if (sidebarDetached)
            sidebarWindow.toggleMaximize()
    }

    // 抓手正在拖（此时落点就是右栏自己）：提示跟着亮起来当"松手就不会跑"的反馈。
    // 由右栏标题行那只抓手的 `sidebarPreviewChanged` 推过来（拖动中主窗口收不到 hover，只能这样推）；
    // 落点矩形按右栏**附着时**的几何现算（收起/脱离时读不到它的坐标，见 dockSceneRect）。
    function beginDetachPreview(over) {
        dropZone = dockSceneRect()
        dropHintActive = over
    }
    function endDetachPreview() {
        dropHintActive = false
        dropZone = Qt.rect(0, 0, 0, 0)
    }
    // ---- 跟手驱动（需求 ①）----
    // 为什么必须"问"而不是"听事件"：右栏一进临时窗口，鼠标消息就投给握着捕获的**主窗口**了
    // （Windows 的鼠标捕获是线程级、消息只给捕获窗口），搬出去的那扇窗收不到任何鼠标事件 ⇒
    // 位置与"松没松手"只能按 ~120Hz 直接问系统（WindowBridge.cursorScreenPos / leftButtonDown）。
    // running 直接绑 sidebarFloating：不跟手时它不跑，代价为 0；停下时也不需要谁去 stop。
    Timer {
        id: sidebarFollowTimer
        interval: 8
        repeat: true
        running: shell.sidebarFloating
        onTriggered: shell.sidebarFollowTick()
    }

    // 右栏标题行那三个点抓手把**某个块**拖出去了（上一轮的"按块脱离"路径）：在光标处建独立窗口。
    // ⚠️ 现在没有调用方（抓手改拖整个右栏了），保留是因为 DetachedBlockWindow 那条链仍在，
    //    删掉会连带 Dock.detach/setDetachedGeometry 变成孤儿。
    // ⚠️ atScreenX/Y 是**屏幕坐标**（抓手在 RightSidebar 里算的：跨窗口判断只能用屏幕坐标）。
    //    独立窗口的 x/y 本来就是屏幕坐标，所以这里不再做 scene/global 的来回换算 —— 搬家前那版
    //    把屏幕坐标先 mapFromItem 成场景坐标再当窗口位置用，只在"窗口锚在场景原点"这一条上碰巧等价。
    function detachActiveBlock(blockId, atScreenX, atScreenY) {
        const idx = Dock.activeIndex
        if (idx < 0)
            return
        // 拖动的这段时间里活动块可能已经换了（或块被关了）⇒ 只认抓的那个块
        if (Dock.activeBlockId !== blockId)
            return
        const r = dockSceneRect()
        const w = Math.max(r.width, 480)
        const h = Math.max(r.height, 240)
        Dock.detach(idx)
        Dock.setDetachedGeometry(blockId, Math.round(atScreenX - w / 2 + Theme.spacing.windowMargin),
                                 Math.round(atScreenY - 20), Math.round(w), Math.round(h))
        // 脱离 = 这个块有自己的窗口了：右栏收起（留着的话同一块在两处画两份）。
        // 块本身仍是"已附着 + 活动"（Dock 里没动它），所以拖回去就回到原位。
        Hub.closeRightSidebar()
        console.log("[detach] detach block idx=" + idx + " id=" + blockId + " at "
                    + Math.round(atScreenX) + "," + Math.round(atScreenY) + " " + w + "x" + h)
    }

    // ---- 右侧栏：宽度可拖（拖左边缘），压窄量 = 宽度 + 间距 ----
    // 这是本文件里唯一被动画驱动的量：它一变，顶栏卡片与会话卡片**同时**变窄，
    // 消息列表也跟着重新换行 —— 也就是"顶栏和聊天区被压缩"的那段动态。
    // 宽度归壳层所有（面板只管画卡片）：拖柄改的是 rightSidebarW，默认值来自 token。
    // 正在拖：拖动期间必须**关掉**压窄动画，否则布局会追着光标慢慢爬
    property bool resizing: false
    // 钳制策略（会话区保底 / 上限 / 夹取 / 拖动换算）全在 C++：公式在
    // source/src/common/layout/SidebarMetrics.cpp（自由函数、可脱离 QML 单测），
    // QML 面是单例 `SidebarLayout`（source/src/qmlbridge/SidebarLayout.cpp，只转发）。
    // 这里只负责把 token 与窗口尺寸喂进去，不再自己算公式
    // （那几条都是"纯数值规则"，放 C++ 才能脱离窗口验证；拖动仍是 1:1，公式逐位等价）。
    readonly property real rightSidebarMinW: Theme.size.rightSidebarMinWidth
    // 会话区保底宽度：**按窗口比例兜底**（取 token 与 25% 窗口宽里更小的那个）。
    // 为什么不能死守 token 的 320：窗口不大时，固定 240 侧栏 + 320 之后留给右侧栏的额度
    // 可能比默认宽度 360 还小 ⇒ 面板一开局就顶在上限、只能往右（变窄）拖，往左没反应。
    readonly property real rightSidebarConvMin: SidebarLayout.conversationMinWidth(
        width, Theme.size.minConversationWidth)
    readonly property real rightSidebarMaxW: SidebarLayout.maxWidth(width, rightSidebarMinW, rightSidebarConvMin,
        Theme.spacing.windowMargin, Theme.size.sidebarWidth, Theme.spacing.panelGap)
    // 用户宽度（默认值也不能超过上限，否则同上）
    property real rightSidebarW: Math.min(Theme.size.rightSidebarWidth, rightSidebarMaxW)
    // 生效宽度：用户值先按上下限夹一次（窗口变小、或被拖到极端都不会越界）
    readonly property real rightSidebarEffW: SidebarLayout.clampWidth(
        rightSidebarW, rightSidebarMinW, rightSidebarMaxW)
    readonly property real rightSidebarFull: rightSidebarEffW + Theme.spacing.panelGap
    property real rightInset: Hub.rightSidebarOpen ? rightSidebarFull : 0
    // 缓动取 OutCubic（不做过冲）：压窄是"腾地方"，回弹会让人以为界面抖了一下。
    // ⚠️ 拖拽时 enabled=false：宽度是用户手指的位置，不能再去补间。
    Behavior on rightInset {
        enabled: !shell.resizing
        NumberAnimation {
            duration: Theme.motion.normal
            easing.type: Easing.OutCubic
        }
    }
    // 归一化进度（0 = 收起，1 = 完全展开）：面板的位移/淡入跟着它走
    readonly property real rightSidebarT: rightSidebarFull > 0
        ? Math.min(1, rightInset / rightSidebarFull) : 0

    // 拖拽换算：按下时的光标窗口 x / 当时宽度 + 当前光标窗口 x ⇒ 新宽度。
    // 公式在 C++（SidebarLayout.widthFromDrag），这里只把壳层的上下限喂进去；保留这层包装是因为
    // 上下限是壳层的属性，调用点（拖柄）不该自己知道它们。
    // 往左拖 ⇒ cursorX < pressX ⇒ 宽度变大。
    function widthFromDrag(pressX, pressWidth, cursorX) {
        return SidebarLayout.widthFromDrag(pressX, pressWidth, cursorX, rightSidebarMinW, rightSidebarMaxW)
    }
    // 夹取入口（拖柄调它；QML 外部/扩展要设宽度也走它）
    function setRightSidebarWidth(w) {
        rightSidebarW = SidebarLayout.clampWidth(w, rightSidebarMinW, rightSidebarMaxW)
    }

    // ---- 左侧：侧栏卡片 ----
    Card {
        id: sidebarCard
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.windowMargin
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset
        width: Theme.size.sidebarWidth
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true
        // 与旧壳对齐：sidebar.qss 的 `#sidebar` 只有 background + border-radius，**没有描边**；
        // 只有 `#topBar` 有 `border: 1px solid {{border}}`。少画这一条，壁纸皮肤下才不会多出一圈线。
        bordered: false

        Sidebar {
            anchors.fill: parent
            onNotify: shell.notify(text)
        }
    }

    // ---- 右侧：顶栏卡片 + 会话卡片 ----
    Item {
        id: rightColumn
        anchors.left: sidebarCard.right
        anchors.leftMargin: Theme.spacing.panelGap
        anchors.right: parent.right
        // 右边距跟着右侧栏的展开量走 ⇒ 顶栏卡片与会话卡片一起被压窄（动画由 rightInset 的 Behavior 提供）
        anchors.rightMargin: Theme.spacing.windowMargin + shell.rightInset
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing.contentSideInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.contentSideInset

        Card {
            id: topBarCard
            // 与输入区对齐：Composer 是 columnClearance 内缩的（见下面 composerLoader），
            // 顶栏卡片不缩就会比输入框宽出一圈，看着和其它控件不协调。
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacing.columnClearance
            anchors.rightMargin: Theme.spacing.columnClearance
            anchors.top: parent.top
            implicitHeight: topBar.implicitHeight
            radius: Theme.radius.panel
            padding: 0
            shadowEnabled: true

            TopBar {
                id: topBar
                anchors.fill: parent
                onToolsFilterRequested: Hub.openToolsFilter()
                // ◫ 的三态状态机（需求 ④，照用户原话）：
                //   ① **副窗口存在且可见** ⇒ 这一次点击 = 关掉那个副窗口（右栏回"已附着但隐藏"：
                //      内容 reparent 回壳层、窗口关掉，Hub.rightSidebarOpen 保持 false）；
                //   ② 没有副窗口 ⇒ 走原来的开合（Hub.toggleRightSidebar()）；
                //   ③ 刚关完副窗口再点 ⇒ rightSidebarOpen 还是 false ⇒ toggle 走"打开"，
                //      而内容这时已经附着回槽位 ⇒ "关闭后再点开默认是附着状态"，正是需求要的。
                // ⚠️ 上一轮的 `Dock.anyDetachedVisible / closeDetachedWindows()` 那两个接口没删，
                //    但"按块脱离"现在没有任何调用方（抓手改拖整个右栏了，见 RightSidebar.qml），
                //    所以这条分支不再经过它们。
                onRightSidebarRequested: {
                    if (shell.sidebarDetached)
                        shell.attachSidebar(false)
                    else
                        Hub.toggleRightSidebar()
                }
            }
        }

        // 会话区：**不要圆角卡片背景**（圆角只给消息自己的气泡），所以这里用普通 Item 而非 Card
        Item {
            id: conversationCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBarCard.bottom
            // 与旧壳同值：顶栏卡片与会话卡片之间由阴影留白 + 这个间距共同决定
            anchors.topMargin: Theme.shadow.topBarGap
            anchors.bottom: parent.bottom

            // 选中会话时是消息列表 + 输入区，没选会话时给引导
            Loader {
                id: composerLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.spacing.columnClearance
                anchors.rightMargin: Theme.spacing.columnClearance
                anchors.bottomMargin: Theme.spacing.columnClearance
                height: item ? item.implicitHeight : 0
                visible: Hub.currentSessionId.length > 0
                source: Style.url("basic/chat/Composer.qml")
            }

            // 提问 / 审批面板（M4b）：排在输入区上方。
            // 旧壳是把它插进消息列表里（MessageHost::handleMuxFrame → InteractionHandler 建面板），
            // QML 侧放在输入区上方更稳：不用改消息模型的角色，也不会打乱列表的滚动与高度。
            Loader {
                id: interactionLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: composerLoader.top
                anchors.bottomMargin: Theme.spacing.columnClearance
                anchors.leftMargin: Theme.spacing.columnClearance
                anchors.rightMargin: Theme.spacing.columnClearance
                height: item ? item.implicitHeight : 0
                active: Hub.questionActive || Hub.approvalActive
                source: Style.url("basic/chat/InteractionPanel.qml")
            }

            Loader {
                id: messageLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: interactionLoader.top
                anchors.bottomMargin: Theme.spacing.labelGap
                visible: Hub.currentSessionId.length > 0
                // ⚠️ 必须等宽度定下来再创建列表：首帧宽度≈0 时委托里的 Text 会按假宽度排版
                //    （行数翻几倍），量出的 contentHeight 是最终的 ~3 倍 → 滑块长度先长后短，
                //    表现就是"一启动滚动条乱跳、然后稳定"（实测 contentH 2400 → 903）。
                active: width > 200
                source: Style.url("basic/chat/MessageListView.qml")
            }

            ConversationPlaceholder {
                anchors.fill: parent
                visible: Hub.currentSessionId.length === 0
            }
        }
    }

    // 右侧栏（面板坞）：顶栏 ◫ 开关。
    // 从窗口右缘外滑进来 + 淡入，位置与透明度都跟 shell.rightSidebarT 走（同一条动画曲线，
    // 所以"面板滑入"和"顶栏/会话区被压窄"是同一段动态，不会互相错拍）。
    // 只改 x/opacity、不改 width：滑动期间面板内部不重排。
    //
    // ⚠️ 位置/宽度/动画这段搬到了**附着槽位** sidebarSlot 上（语义逐条不变）：
    //    RightSidebar 自己只用 `anchors.fill: parent`，于是 reparent 到独立窗口的 contentItem 时
    //    自动填满那扇窗，不用为"两边各一套锚定"再写条件判断。槽位是空 Item，不画东西。
    Item {
        id: sidebarSlot

        anchors.top: sidebarCard.top
        anchors.bottom: sidebarCard.bottom
        width: shell.rightSidebarEffW
        x: shell.width - Theme.spacing.windowMargin - width
           + (1 - shell.rightSidebarT) * (width + Theme.spacing.windowMargin)

        RightSidebar {
            id: rightSidebar

            anchors.fill: parent
            // 附着态：可见性与淡入跟壳层的展开进度走（与搬家前逐字同一条）；
            // 拿起来/脱离态：它在独立窗口里，那扇窗显示它就显示。
            visible: shell.sidebarInWindow ? true : shell.rightSidebarT > 0.001
            opacity: shell.sidebarInWindow ? 1 : shell.rightSidebarT
            // 独立态（**确认脱离之后**）：三点改成"搬那扇窗 + 落回来就回附"；回附落点由壳层现算给它
            sideDetached: shell.sidebarDetached
            attachZoneProvider: shell.dockScreenRect
            // 几何出口注入：脱离后本 Item 的 parent 链上**没有**壳层（见 RightSidebar 的说明），
            // 所以"搬那扇窗"只能靠这个函数引用 —— 与 attachZoneProvider 同一个道理。
            // ⚠️ 必须包一层箭头函数：QML 的自定义方法**不能**当函数值跨组件直接调用（实测
            //    `TypeError: Property 'moveSidebarWindowTo' of object function … is not a function`）。
            moveWindowProvider: (x, y) => shell.moveSidebarWindowTo(x, y)
            // 面板自己标题行那颗 ✕：跟手/独立态下它也必须走"回附 + 关窗"（否就只收起主窗口那半边，
            // 内容留在副窗口里 = 状态自相矛盾）；附着态照旧只收起右栏。
            onCloseRequested: {
                if (shell.sidebarInWindow)
                    shell.attachSidebar(false)
                else
                    Hub.closeRightSidebar()
            }
            // 抓手的四条请求：拿起来（跟手开始）/ 松手（壳层按落点定结局）/ 回附 / 拖动中的落点预览
            onSidebarLiftRequested: shell.liftSidebarForDrag()
            onSidebarDropRequested: (screenX, screenY) => shell.finishSidebarDrag(screenX, screenY)
            onSidebarAttachRequested: shell.attachSidebar(true)
            // 头部那两颗窗口按钮（− / □❐，只在副窗口态可见）：转发给 SidebarWindow 自己那两个入口
            onMinimizeWindowRequested: shell.minimizeSidebarWindow()
            onToggleMaximizeRequested: shell.toggleMaximizeSidebarWindow()
            onSidebarPreviewChanged: (over) => {
                if (over)
                    shell.beginDetachPreview(true)
                else
                    shell.endDetachPreview()
            }

            // 右栏内容：面板坞（主界面 + 块/页签；网页块内部才用 WebEngine）
            PanelDock {
                id: dock
                anchors.fill: parent
            }
        }
    }

    // ---- 独立窗口：右栏整体"拿起来"之后住在这儿（无边框 + 自绘标题栏，需求 ③）----
    // 内容**不是副本**：RightSidebar 那个 Item 由 liftSidebarForDrag/attachSidebar reparent
    // 进/出 `panelSlot`。窗口本身只做：按 sidebarInWindow 显示、被 Shell 摆几何、自己的 move/resize 回写。
    SidebarWindow {
        id: sidebarWindow
        host: shell
    }

    // ---- 抓手的脱离接线：两条广播都落在壳层（见 PanelDockModel 的 requestDetach / setDetachPreview）----
    // 抓手在 RightSidebar 的标题行里，够不着 PanelDock 的内部信号 —— 所以它把请求发给单例 Dock，
    // 这里接住：提示由壳层画（DockDropHint）、独立窗口也由壳层建（下面的 Repeater）。
    // ⚠️ 两条都挂在 shell 上（不是挂在某个面板实例上）：抓手换了宿主，这段接线不该跟着换。
    Connections {
        target: Dock

        // 拖动中：拖出右栏（>48px）或水平位移 >60px ⇒ 亮起落点提示。
        // 开关就是抓手算出来的那个布尔量，这里不再由壳层叠一条"正在拖"的状态：
        // 抓手在右栏里按下时本来就在落点矩形内，overTarget=false ⇒ 提示不亮，
        // 与搬家前同一个观感（拖动前不亮、拖出去才亮）；叠一条反而会让提示一开始就亮着。
        function onDetachPreviewChanged(overTarget) {
            if (overTarget)
                shell.beginDetachPreview(overTarget)
            else
                shell.endDetachPreview()
        }

        // 松手且够阈值：在光标处把活动块脱离成独立窗口
        function onDetachRequested(blockId, atScreenX, atScreenY) {
            shell.detachActiveBlock(blockId, atScreenX, atScreenY)
        }
    }

    // ---- 独立窗口：一块一扇窗（需求 ②③④）----
    // 清单来自模型（Dock.detachedBlocks）：哪一项没了窗口就跟着销毁、visible=false 就只是隐藏
    // （"已附着但隐藏"，需求 ⑤ 的第二态）。系统装饰窗口自带四边缩放与最小化/最大化按钮。
    // geometry 由窗口自己回写模型，下一次脱离沿用它 ⇒ "拖回去再拖出来"不会跳回原位。
    Repeater {
        model: Dock.detachedBlocks

        delegate: DetachedBlockWindow {
            required property var modelData
            entry: modelData
            mainWindow: shell.Window.window
            host: shell
        }
    }

    // ---- 拖回时的落点提示：画在原右栏位置（需求 ③：建议给落点提示）----
    // z 抬到面板之上：拖动期间右栏还在自己的位置（附着态）或在独立窗口里（脱离态），
    // 主窗口这边只有这块轮廓要露出来给人看。
    // 文案两个方向不同：附着态往外拖（"松手：面板变成独立窗口"）vs 独立窗口往里拖（"松手：重新附着回右栏"）；
    // 两个方向都由正在拖的那个抓手（或跟手的 Timer）推 `shell.dropHintActive`
    // （拖动中主窗口收不到 hover，只能这样推）；`floating` 就是"此刻正在跟手"那一档。
    DockDropHint {
        id: dropHint
        z: 30
        zone: shell.dropZone
        active: shell.dropHintActive
        fromDock: !shell.sidebarDetached
        floating: shell.sidebarFloating
    }

    // 右侧栏宽度拖柄：贴在面板左缘的一条 10px 热区（平时完全透明，悬停才露一条灰边）。
    // 为什么放在 Shell 而不是面板里：宽度是壳层布局的一部分（压窄量 = 宽度 + 间距），
    // 面板自己只管画卡片；拖柄改的是 shell.rightSidebarW。
    //
    // ⚠️ 两条踩过的坑（都在真机拖拽日志里实测到，症状都是"只能往右拖一点、往左没反应"）：
    //   ① 可见性/可用性**绝对不能挂在 rightSidebarT 上**：宽度一变，rightSidebarFull 先更新、
    //      rightInset 差一拍，T 会瞬时掉到 302/311≈0.97 ⇒ visible 抖成 false ⇒ MouseArea 抓取
    //      被取消，后续 move 全丢（日志：[drag-ev] move … → [drag-ev] canceled）。
    //      所以这里只认 Hub.rightSidebarOpen 这个稳定布尔量。
    //   ② 拖动期间位置要**冻结**：拖柄跟着面板走 = 在光标底下位移，同样会打断抓取。
    //      空闲时才贴面板左缘（frozenX 在按下那一刻算好）。
    Item {
        id: resizeHandle

        anchors.top: sidebarSlot.top
        anchors.bottom: sidebarSlot.bottom
        width: 10
        visible: Hub.rightSidebarOpen
        enabled: visible
        // 按下时冻结的那个 x（见 ②）
        property real frozenX: 0
        // ⚠️ 贴的是**附着槽位**的左缘：右栏自己现在是 `anchors.fill: parent`，
        //    脱离时它的 x 是独立窗口里的 0，读它会在主窗口里画到左上角去。
        x: shell.resizing ? frozenX : sidebarSlot.x - width / 2

        // 灰边：⚠️ 画在**面板内侧**、上下各让开一个圆角半径 ——
        // 贴左缘画满全高的话，两端会越过圆角弧线（被指出过）；颜色也只用灰色系（原来用蓝色被否了）。
        // 拖动期间不画：拖柄是冻住的，画出来会停在旧位置误导人；此时"面板边自己在动"就是反馈。
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: parent.width / 2 + 1
            width: 2
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.radius.panel + 4
            anchors.bottomMargin: Theme.radius.panel + 4
            radius: 1
            color: Theme.color.textTertiary
            opacity: (shell.resizing || !resizeMouse.containsMouse) ? 0 : 0.55
            Behavior on opacity {
                NumberAnimation { duration: Theme.motion.fast }
            }
        }

        MouseArea {
            id: resizeMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor

            // 用**窗口坐标**记录按下点：拖柄自己会随宽度移动，用相对位移会自我参照
            property real pressWindowX: 0
            property real pressWidth: 0

            onPressed: (mouse) => {
                pressWindowX = resizeHandle.mapToItem(null, mouse.x, 0).x
                pressWidth = shell.rightSidebarEffW
                // 先冻结位置再置 resizing：顺序反了的话第一拍就会按"贴边"算一次 x
                resizeHandle.frozenX = resizeHandle.x
                shell.resizing = true
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                // 换算全在 widthFromDrag 里（往左拖 = 变宽），这里只负责把光标位置喂进去
                shell.setRightSidebarWidth(shell.widthFromDrag(
                    pressWindowX, pressWidth, resizeHandle.mapToItem(null, mouse.x, 0).x))
            }
            onReleased: shell.resizing = false
            onCanceled: shell.resizing = false
        }
    }

    // 服务端还没连上时的遮罩：不挡住操作，只提示状态
    Rectangle {
        anchors.fill: parent
        visible: Hub.initializing
        color: Theme.color.overlayBg
        z: 10

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacing.sectionGap

            BusySpinner {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32
                height: 32
                color: Theme.color.textOnAccent
                // ⚠️ 必须门控：BusySpinner.running 默认 true，而 RotationAnimator 是无限循环。
                // 只靠父级 visible 是不够的 —— 不可见的 item 上的动画照样每帧改属性、
                // 把场景标脏，于是应用**空闲时也在满速重绘**（实测：空闲 5.4% 单核 + GPU 3D 25~29%，
                // 换成壁纸皮肤后每帧还要多跑两个全窗离屏层，移动窗口就卡）。
                // Sidebar / PluginMarketPage 里的同类转圈都传了 running，这里漏了。
                running: Hub.initializing
                visible: Hub.initializing
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Hub.statusText
                color: Theme.color.textOnAccent
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.body
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3 * Theme.spacing.cardPadding
        z: 20
    }

    // 插件市场（M6）：覆盖整壳，侧栏 ◧ 打开。
    // 三块整壳覆盖页统一走 PageOverlay —— 遮罩 + 从**点击处**长出来的卡片 + 收回播完才卸载，
    // 于是"点侧栏那颗图标"与"卡片从哪儿长出来"天然对得上（页面自己不用接线）。
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.plugins.open
        source: Style.url("basic/plugins/PluginMarketPage.qml")
    }

    // 扩展管理：覆盖整壳，侧栏扩展按钮打开（工具 / 客户端 / Qt Quick 运行时三种来源）
    PageOverlay {
        anchors.fill: parent
        z: 35
        shown: Hub.extensions.open
        source: Style.url("basic/extensions/ExtensionManagerPage.qml")
    }

    // 主题切换过渡卡片（原版 #themeSwitchBody）
    Loader {
        anchors.fill: parent
        active: Hub.themeSwitching
        visible: Hub.themeSwitching
        source: Style.url("skin/shell/ThemeSwitchOverlay.qml")
        z: 40
    }

    // 设置页（模型 / API key）：覆盖整壳，侧栏 ⚙ 打开
    PageOverlay {
        anchors.fill: parent
        z: 30
        shown: Hub.settings.open
        source: Style.url("basic/settings/SettingsPage.qml")
    }

    // 工具过滤面板：顶栏 ☰ 打开，覆盖整壳 —— 与另外三个覆盖页**完全同一套** PageOverlay：
    // 遮罩只淡入（不缩放）、卡片从**点击处**长出来、收回播完才卸载。
    // （面板自身已改成"固定尺寸卡片 + 不自带遮罩"，见 ToolsFilterPanel.qml 里的说明。）
    PageOverlay {
        anchors.fill: parent
        z: 36
        shown: Hub.toolsFilterOpen
        source: Style.url("basic/chat/ToolsFilterPanel.qml")

        // 面板自己读 Hub.toolsFilter（import ULinHub 1.0），这里只转它的关闭请求
        onPageLoaded: (item) => {
            if (item && item.closeRequested)
                item.closeRequested.connect(Hub.closeToolsFilter)
        }
    }
}
