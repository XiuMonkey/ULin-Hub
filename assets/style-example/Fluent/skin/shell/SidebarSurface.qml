// Fluent · 侧栏面（覆盖内置 skin/shell/SidebarSurface.qml）
// 样张里侧栏不是一个卡片，而是"贴在窗口左缘的导航列"：
// 透明底 + 右侧一条发丝分隔线，和会话区共用一个背景。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    default property alias content: holder.data

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen

    // 右侧发丝线（上下留一点，避免顶到窗口圆角）
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.edgeToEdge ? 0 : Theme.radius.window
        anchors.bottomMargin: root.edgeToEdge ? 0 : Theme.radius.window
        width: 1
        color: Theme.color.border
    }

    Item {
        id: holder
        anchors.fill: parent
        anchors.rightMargin: 1
    }
}
