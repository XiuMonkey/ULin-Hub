// 悬浮提示。刻意不用 QtQuick.Controls 的 ToolTip/Popup：Popup 弹层会接管鼠标（CloseOnPressOutside），
// "悬停出提示 → 紧接着点击"的第一下会被用来关弹层。
// 气泡必须画在窗口 contentItem 顶层：作为控件子项时 z 排序跨层不生效、父级 clip 还会裁掉它；
// 挂到 contentItem 后不裁剪、尺寸=窗口，鼠标行为不变。不用 Overlay.overlay：那层给 Popup 用，
// 空时会被 Qt 销毁，而 contentItem 是场景根，永不为 null。
// 位置必须现算 + 跟随：x/y 写成绑定调 mapToItem() 是错的（mapToItem 是 C++ 实现，读 x/y/父链
// 不进 QML 绑定依赖图，绑定只在创建时算一次）；所以显示瞬间 place()、显示期间 16ms Timer 反复 place()、
// 再挂 Connections 监听锚点与窗口几何；隐藏时 Timer 停 ⇒ 空闲零开销。
// 用法：AppToolTip { text: "设置"; visible: mouseArea.containsMouse }（或显式给 anchorItem）。

import QtQuick
import QtQuick.Window          // Window.window / contentItem
import ULinHub 1.0

Item {
    id: root

    property string text: ""

    property Item anchorItem: parent   // 被提示的控件 = 声明处的父项（默认）。与旧 anchors 语义一致：贴它下方居中

    readonly property Item topLayer: Window.window ? Window.window.contentItem : null   // 顶层落点：窗口的 contentItem。窗口还没就绪时先留在宿主里（不可见时段无所谓）

    // 气泡搬到顶层后不再跟着宿主一起隐藏（宿主的 visible 不会传导到 contentItem），这里自己沿 parent 链查一遍。
    // visible 不能直接绑 parent 链：QML 会把"气泡 reparent 到 contentItem"与锚点可见性识别成循环，
    // 启动/开设置页时刷出大量 Binding loop；所以用显式 bool，提示可见期间用 200ms 轮询刷新。
    property bool tipVisible: false

    function refreshTipVisible() {
        let it = root.anchorItem
        let shown = true
        while (it && it !== root.topLayer) {
            if (!it.visible) {
                shown = false
                break
            }
            it = it.parent
        }
        const next = root.visible && root.anchorItem !== null && shown
            && root.text.length > 0
        if (root.tipVisible !== next)
            root.tipVisible = next
    }

    Component.onCompleted: refreshTipVisible()
    onVisibleChanged: refreshTipVisible()
    onTextChanged: refreshTipVisible()
    onAnchorItemChanged: {
        refreshTipVisible()
        root.place()
    }

    Timer {
        interval: 200
        repeat: true
        running: root.visible && root.text.length > 0
        onTriggered: root.refreshTipVisible()
    }

    implicitWidth: bubble.implicitWidth
    implicitHeight: bubble.implicitHeight
    width: implicitWidth
    height: implicitHeight

    // 现算气泡位置（每次调用都重新取锚点与窗口几何，不缓存任何坐标）：
    // · 水平：锚点中心对齐，夹进窗口 [4, 窗口宽-气泡宽-4]；
    // · 垂直：优先贴锚点下方 4px；下方放不下就翻到上方；两边都放不下时夹到窗口顶 4px。
    function place() {
        if (!bubble || !root.anchorItem)
            return
        const host = bubble.parent     // = 窗口 contentItem（窗口未就绪时是宿主自己）
        if (!host)
            return

        const center = root.anchorItem.mapToItem(host, root.anchorItem.width / 2, 0)
        const maxX = Math.max(4, host.width - bubble.width - 4)
        bubble.x = Math.round(Math.max(4, Math.min(center.x - bubble.width / 2, maxX)))

        const below = root.anchorItem.mapToItem(host, 0, root.anchorItem.height).y + 4
        if (below + bubble.height <= host.height - 4) {
            bubble.y = Math.round(below)
            return
        }
        const above = root.anchorItem.mapToItem(host, 0, 0).y - bubble.height - 4
        bubble.y = Math.round(Math.max(4, above))
    }

    Rectangle {
        id: bubble

        // 顶层：contentItem 的最后一批子项 ⇒ 盖住顶栏/会话区。Popup 的 Overlay 比它更晚创建，
        // 所以菜单仍然压在提示之上（这正是想要的顺序）。
        parent: root.topLayer ? root.topLayer : root.anchorItem
        visible: root.tipVisible

        implicitWidth: label.implicitWidth + 2 * Theme.spacing.fieldPadX
        implicitHeight: label.implicitHeight + Theme.spacing.labelGap
        width: implicitWidth
        height: implicitHeight
        radius: Theme.radius.small
        color: Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder

        onVisibleChanged: if (visible) root.place()   // 出现/换父层/自身尺寸变化时立即定位（不等 Timer 下一拍）
        onParentChanged: if (visible) root.place()
        onWidthChanged: if (visible) root.place()
        onHeightChanged: if (visible) root.place()

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: Theme.color.textPrimary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
            wrapMode: Text.NoWrap
        }
    }

    // 跟随：显示期间每帧现算一次。见文件头的说明 —— mapToItem 的读不进依赖图，
    // 所以"布局重排 / 换行 / 窗口缩放 / 列表滚动"这些只靠绑定是跟不上的。
    Timer {
        id: followTimer
        interval: 16
        repeat: true
        running: bubble !== null && bubble.visible
        onTriggered: root.place()
    }

    Connections {   // 锚点几何变化：立刻重算（顶栏 chip 换行、按钮被布局挪动等）
        target: root.anchorItem
        enabled: bubble.visible
        function onXChanged() { root.place() }
        function onYChanged() { root.place() }
        function onWidthChanged() { root.place() }
        function onHeightChanged() { root.place() }
    }

    Connections {   // 窗口尺寸变化：contentItem 跟着窗口变，夹边界与上下翻转都要重算。
        // 不能写 target: Window.window —— attached property 只能在 Item 上求值，
        // Connections 不是 Item，Qt 会逐次报 "Window.window only supports types derived from Item"。
        target: root.topLayer
        enabled: bubble.visible
        function onWidthChanged() { root.place() }
        function onHeightChanged() { root.place() }
    }

    // 换 parent 会连同 QObject 父子关系一起改（气泡从此归 contentItem 所有），
    // 所以宿主被销毁时必须自己收尸 —— 否则 ListView 委托里的提示会一层层堆在窗口根上。
    Component.onDestruction: {
        followTimer.running = false
        if (bubble)
            bubble.destroy()
    }
}
