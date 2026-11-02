// 荧光屏 · 滚动条
// 契约与内置 widgets/AppScrollBar.qml 一致：几何由框架摆放（contentItem 交给框架），
// 可见把手只跟随；拖拽/点轨道的命中区仍是整条 thickness。
// 造型：直角细条、磷光色，静止时更暗一档（像屏幕边的滑动电位器）。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded

    readonly property bool isHorizontal: orientation === Qt.Horizontal

    readonly property int thickness: Math.max(Theme.size.scrollbarThickness || 0, 10)
    readonly property int handleWidth: Math.max(Theme.size.scrollbarHandleWidth || 0, 8)

    implicitWidth: isHorizontal ? 0 : root.thickness
    implicitHeight: isHorizontal ? root.thickness : 0

    readonly property int handleInset: Math.max(0, Math.round((root.thickness - root.handleWidth) / 2))

    contentItem: Item { }

    Rectangle {
        id: handle

        visible: root.policy !== ScrollBar.AlwaysOff && root.size < 1
        color: root.pressed
            ? Theme.color.accent
            : ((root.hovered || root.active)
               ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.55)
               : Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.28))
        radius: 0

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
