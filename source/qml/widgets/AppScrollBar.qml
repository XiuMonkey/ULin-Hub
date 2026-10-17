// 滚动条：包住 QtQuick.Controls 的 ScrollBar，换成 token 尺寸/颜色。
// 用法（挂在 Flickable/ListView 上）：ScrollBar.vertical: AppScrollBar { }
//                                  ScrollBar.horizontal: AppScrollBar { }
//
// 设计要点（都是踩坑换来的）：
//   1) 几何**仍由框架摆放**：contentItem 交给框架（它按 position/size 算好并夹取），
//      可见把手只是跟随这份几何 —— 不要自己按 position/size 算，试过，position 为负时把手会被钉住，更糟。
//   2) 把手几何**一律直跟框架**，不要加 Behavior 缓动：框架在平滑滚动时本来就是每帧给新几何，
//      再补一层 120ms 缓动只会让拖动滞后于鼠标、让内容高度变化时把手"滑"过去（更像是乱跳）。
//      内容高度会来回修正的根因在 ListView 侧的估值，已在 chat/MessageListView.qml 用 cacheBuffer
//      从源头解决 —— 不要再走"用动画掩盖"的老路。
//   3) 内容没超屏时不显示把手：由 policy: AsNeeded 负责。
//   4) 观感：细窄（scrollbarHandleWidth 6 < 轨道 8）、两端圆角、低对比 —— 静止最淡、
//      悬停/滚动中略深、按下最深；轨道透明，把手四周内收 1px 与内容对齐。
//      ⚠️ 内收只作用在"可见把手"这层：拖拽/点击轨道用的仍是框架摆放的 contentItem 几何，
//      所以滚动行为（拖拽、点轨道、滚轮、淡出时机）一字未改。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded

    readonly property bool isHorizontal: orientation === Qt.Horizontal

    // 粗细（框架只定长度，厚度靠 implicit）
    implicitWidth: isHorizontal ? 0 : Theme.size.scrollbarThickness
    implicitHeight: isHorizontal ? Theme.size.scrollbarThickness : 0

    // 可见把手相对轨道每侧内收多少（厚度差的一半）：让把手比轨道窄一圈，留出气口
    readonly property int handleInset: Math.max(0, Math.round(
        (Theme.size.scrollbarThickness - Theme.size.scrollbarHandleWidth) / 2))

    // 框架摆放的"几何来源"，本身不画（拖拽/点轨道的命中区仍是它，即整条 thickness）
    contentItem: Item { }

    Rectangle {
        id: handle

        visible: root.policy !== ScrollBar.AlwaysOff && root.size < 1
        // 三态克制（都是中性灰，无强调色）：静止 scrollbar（最淡）→ 悬停/滚动 scrollbarHandle → 按下 scrollbarHandleHover
        color: root.pressed
            ? Theme.color.scrollbarHandleHover
            : ((root.hovered || root.active) ? Theme.color.scrollbarHandle : Theme.color.scrollbar)
        radius: (root.isHorizontal ? height : width) / 2

        x: root.contentItem.x + (root.isHorizontal ? 0 : root.handleInset)
        y: root.contentItem.y + (root.isHorizontal ? root.handleInset : 0)
        width: root.isHorizontal
            ? root.contentItem.width
            : Math.max(2, root.contentItem.width - 2 * root.handleInset)
        height: root.isHorizontal
            ? Math.max(2, root.contentItem.height - 2 * root.handleInset)
            : root.contentItem.height
    }

    background: Rectangle {
        color: "transparent"
    }
}
