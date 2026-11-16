// 会话区承载面（B 层可替换单元）
// 默认是**裸容器**（消息直接坐在背景上）；皮肤可以把 surfaceEnabled 打开，
// 给会话区铺一层独立的面（圆角 + 面色 + 描边），壳层不用改。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    default property alias content: holder.data

    property bool surfaceEnabled: false
    property color surfaceColor: Theme.color.panelBg
    property color surfaceBorder: Theme.color.border
    property real radius: Theme.radius.panel

    Rectangle {
        anchors.fill: parent
        visible: root.surfaceEnabled
        radius: root.radius
        color: root.surfaceColor
        border.width: root.surfaceEnabled ? 1 : 0
        border.color: root.surfaceBorder
    }

    Item {
        id: holder
        anchors.fill: parent
    }
}
