// Fluent · 右栏面（覆盖内置 skin/panel/PanelSurface.qml）
// 与侧栏/顶栏保持一致的"齐平"语言：不浮起、不画卡片，透明底 + 左侧一条发丝线。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    property bool shadow: false   // 兼容壳层绑定；本件不画阴影

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.edgeToEdge ? 0 : Theme.radius.window
        anchors.bottomMargin: root.edgeToEdge ? 0 : Theme.radius.window
        width: 1
        color: Theme.color.border
    }
}
