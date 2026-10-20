// 悬浮提示。
//
// ⚠️ 刻意**不用** QtQuick.Controls 的 ToolTip/Popup：Popup 弹层会接管鼠标 —— 默认
//   closePolicy 是 CloseOnPressOutside，于是"悬停出提示 → 紧接着点击"的第一下会被用来关弹层，
//   按钮收不到点击（实测：侧栏 ＋ 第一次点没反应，第二下才生效）。
//
// ⚠️ 气泡**必须画在窗口顶层**：早先它是控件自己的子项 + `z: 1000`，而 z 只在同一父项的兄弟之间
//   排序（跨层不生效），父级的 clip 也照样裁得到它。于是顶栏卡片后面声明的兄弟 —— 会话区、
//   顶栏那两枚入口 chip —— 一律盖在气泡上面。
//   现在气泡挂到**窗口 contentItem** 上（新追加的子项画在所有既有内容之上，contentItem 不裁剪、
//   尺寸就是窗口），鼠标行为不变（仍是个普通 Item，不抓鼠标）。
//
//   为什么不用 `Overlay.overlay`：那一层是给 Popup 用的，**空的时候会被 Qt 自己销毁**
//   （实测：创建后 120ms 就变 null），拿它当 tooltip 的长期父项会被一起带走。
//   contentItem 是场景根，永不为 null。
//
// ⚠️ 位置必须**现算 + 跟随**（2026-10-19 修）：
//   把 x/y 写成 QML 绑定调用 mapToItem() 是**错的** —— mapToItem 是 C++ 实现，它内部读
//   控件的 x/y/父链**不会**进 QML 的绑定依赖图。于是绑定只在创建时算一次：按钮换行、布局重排、
//   窗口缩放、滚动之后气泡全都不动（用户实测："位置不对、不跟随按钮"）。
//   现在改成：
//     · 显示瞬间 place() 立即定位（不会先闪一下错位置）；
//     · 显示期间一个 16ms Timer 反复 place()，因此布局动画/滚动/窗口缩放都跟得上；
//     · 再挂 Connections 监听锚点与窗口的 x/y/width/height，几何一变立刻重算（不用等下一帧）；
//     · 隐藏时 Timer 停 ⇒ 空闲零开销。
//
// 用法（作为子项放在要提示的控件里，visible 绑到悬停）：
//   AppToolTip { text: "设置"; visible: mouseArea.containsMouse }
// 也可以显式给锚点（例如 AppToolTip 不是控件的直接子项时）：
//   AppToolTip { text: "..."; anchorItem: someButton; visible: ... }

import QtQuick
import QtQuick.Window          // Window.window / contentItem
import ULinHub 1.0

Item {
    id: root

    property string text: ""

    // 被提示的控件 = 声明处的父项（默认）。与旧 anchors 语义一致：贴它下方居中。
    property Item anchorItem: parent

    // 顶层落点：窗口的 contentItem。窗口还没就绪时先留在宿主里（不可见时段无所谓）。
    readonly property Item topLayer: Window.window ? Window.window.contentItem : null

    // 气泡搬到顶层后**不再**跟着宿主一起隐藏（宿主的 visible 不会传导到 contentItem），
    // 这里自己沿 parent 链查一遍。在绑定里逐个读 .visible 会自动建立依赖。
    readonly property bool anchorShown: {
        let it = root.anchorItem
        while (it) {
            if (!it.visible)
                return false
            it = it.parent
        }
        return true
    }

    // 外层仍保留原来的占位尺寸（有些调用点把它放在 Row/Column 里），视觉上什么都不画
    implicitWidth: bubble.implicitWidth
    implicitHeight: bubble.implicitHeight
    width: implicitWidth
    height: implicitHeight

    // 现算气泡位置（每次调用都重新取锚点与窗口几何，不缓存任何坐标）：
    //   · 水平：锚点中心对齐，夹进窗口 [4, 窗口宽-气泡宽-4]；
    //   · 垂直：优先贴锚点下方 4px；下方放不下就翻到上方；两边都放不下时夹到窗口顶 4px。
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
        visible: root.visible && root.anchorItem !== null && root.anchorShown
            && root.text.length > 0

        implicitWidth: label.implicitWidth + 2 * Theme.spacing.fieldPadX
        implicitHeight: label.implicitHeight + Theme.spacing.labelGap
        width: implicitWidth
        height: implicitHeight
        radius: Theme.radius.small
        color: Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder

        // 出现/换父层/自身尺寸变化时立即定位（不等 Timer 下一拍）
        onVisibleChanged: if (visible) root.place()
        onParentChanged: if (visible) root.place()
        onWidthChanged: if (visible) root.place()
        onHeightChanged: if (visible) root.place()

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
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

    // 锚点几何变化：立刻重算（顶栏 chip 换行、按钮被布局挪动等）
    Connections {
        target: root.anchorItem
        enabled: bubble.visible
        function onXChanged() { root.place() }
        function onYChanged() { root.place() }
        function onWidthChanged() { root.place() }
        function onHeightChanged() { root.place() }
    }

    // 窗口尺寸变化：夹边界与上下翻转都要重算
    Connections {
        target: Window.window
        enabled: bubble.visible
        function onWidthChanged() { root.place() }
        function onHeightChanged() { root.place() }
    }

    // 锚点被显式改掉时也要重算
    onAnchorItemChanged: root.place()

    // 换 parent 会连同 QObject 父子关系一起改（气泡从此归 contentItem 所有），
    // 所以宿主被销毁时必须自己收尸 —— 否则 ListView 委托里的提示会一层层堆在窗口根上。
    Component.onDestruction: {
        followTimer.running = false
        if (bubble)
            bubble.destroy()
    }
}
