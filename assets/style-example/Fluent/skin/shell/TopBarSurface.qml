// Fluent · 顶栏面（覆盖内置 skin/shell/TopBarSurface.qml）
// 样张没有"顶栏卡片"：顶栏直接坐在背景上，只用一条底部发丝线收口。
// shadow 由壳层传入（宽度动画期间关阴影），这里不再需要，保留入参以兼容绑定。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    default property alias content: holder.data
    property bool shadow: true   // 兼容壳层绑定；本件不画阴影

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.color.border
    }

    Item {
        id: holder
        anchors.fill: parent
        anchors.bottomMargin: 1
    }
}
