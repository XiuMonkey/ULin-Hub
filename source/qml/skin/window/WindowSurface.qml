// 窗口可见面（B 层可替换单元）
// 契约：透传子项；圆角/底色/裁剪就是窗口的真实外轮廓（圆角之外保持透明，交给系统投影）。
// 壳层只负责 anchors.margins（最大化时系统缩放边框的让位），这里决定"面"本身。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    default property alias content: holder.data

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    property real radius: edgeToEdge ? 0 : Theme.radius.window
    property color color: Theme.color.windowBg

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.color
        clip: true
    }

    Item {
        id: holder
        anchors.fill: parent
    }
}
