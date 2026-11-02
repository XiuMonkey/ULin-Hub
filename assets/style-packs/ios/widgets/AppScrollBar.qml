// iOS 玻璃 · 滚动条
// ---------------------------------------------------------------------------
// 契约与内置 widgets/AppScrollBar.qml 完全一致（几何由框架摆放的内容项推出来，
// 自己不算 position/size，见内置件文件头那四条）。
//
// 造型是 iOS 的滚动指示器：没有轨道、只有一根 6px 宽的圆头棒，静止时半透明，
// 悬停或滚动时变实并微微变宽 —— iOS 的滚动条是"提示"，不是"控件"。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded

    readonly property bool isHorizontal: orientation === Qt.Horizontal

    readonly property int thickness: Math.max(Theme.size.scrollbarThickness || 0, 12)
    readonly property int handleWidth: Math.max(Theme.size.scrollbarHandleWidth || 0, 6)

    implicitWidth: isHorizontal ? 0 : root.thickness
    implicitHeight: isHorizontal ? root.thickness : 0

    readonly property int handleInset: Math.max(0, Math.round((root.thickness - root.handleWidth) / 2))

    contentItem: Item { }

    readonly property bool lit: root.pressed || root.hovered || root.active
    readonly property real grow: root.lit ? 2 : 0

    Rectangle {
        id: handle

        visible: root.policy !== ScrollBar.AlwaysOff && root.size < 1
        color: root.pressed ? Theme.color.scrollbarHandleHover
                            : (root.lit ? Theme.color.scrollbarHandle : Theme.color.scrollbar)
        opacity: root.lit ? 1.0 : 0.75
        radius: (root.isHorizontal ? height : width) / 2

        // 滚动/悬停时棒子变宽 2px，并且仍以原中心为轴（左右各外扩 1px）
        x: root.contentItem.x + (root.isHorizontal ? 0 : root.handleInset - root.grow / 2)
        y: root.contentItem.y + (root.isHorizontal ? root.handleInset - root.grow / 2 : 0)
        width: root.isHorizontal
            ? root.contentItem.width
            : Math.max(2, root.contentItem.width - 2 * root.handleInset + root.grow)
        height: root.isHorizontal
            ? Math.max(2, root.contentItem.height - 2 * root.handleInset + root.grow)
            : root.contentItem.height

        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 160 } }
    }

    background: Rectangle {
        color: "transparent"
    }
}
