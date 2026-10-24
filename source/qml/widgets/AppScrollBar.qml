// 用法（挂在 Flickable/ListView 上）：ScrollBar.vertical: AppScrollBar { } / 横向同理。
// 1) 几何**仍由框架摆放**：contentItem 交给框架（按 position/size 算好并夹取），可见把手只跟随 ——
// 自己按 position/size 算会把负 position 的把手钉住，更糟。
// 2) 把手几何**一律直跟框架**，不要加 Behavior：框架在平滑滚动时本来每帧给新几何，再补缓动只会
// 让拖动滞后于鼠标、让内容高度变化时把手"滑"过去。
// 3) 内容没超屏时不显示把手：由 policy: AsNeeded 负责。
// 4) 观感：细窄、两端圆角、低对比；轨道透明，把手四周内收 1px 与内容对齐。
// ⚠️ 内收只作用在"可见把手"这层：拖拽/点击轨道仍用框架摆放的 contentItem 几何，滚动行为一字未改。
// ⚠️ 令牌硬编码在 C++（ThemeBridge::defaultSize），改它必须重编 EXE；这里用 Math.max 兜一个下限，
// 令牌取到旧值时也会被抬到 10/8，立即生效；重编后两边一致。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded

    readonly property bool isHorizontal: orientation === Qt.Horizontal

    readonly property int thickness: Math.max(Theme.size.scrollbarThickness || 0, 10)   // 粗细（框架只定长度，厚度靠 implicit）。见文件头第 5 条：下限保证"免编也变粗"
    readonly property int handleWidth: Math.max(Theme.size.scrollbarHandleWidth || 0, 8)

    implicitWidth: isHorizontal ? 0 : root.thickness
    implicitHeight: isHorizontal ? root.thickness : 0

    readonly property int handleInset: Math.max(0, Math.round(   // 可见把手相对轨道每侧内收多少（厚度差的一半）：让把手比轨道窄一圈，留出气口
        (root.thickness - root.handleWidth) / 2))

    contentItem: Item { }   // 框架摆放的"几何来源"，本身不画（拖拽/点轨道的命中区仍是它，即整条 thickness）

    Rectangle {
        id: handle

        visible: root.policy !== ScrollBar.AlwaysOff && root.size < 1
        color: root.pressed   // 三态克制（都是中性灰，无强调色）：静止 scrollbar（最淡）→ 悬停/滚动 scrollbarHandle → 按下 scrollbarHandleHover
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
