// 输入区停放面（B 层可替换单元）
// 只是"输入卡后面那层可画的背景"，不参与布局：宽度/居中/底部对齐由壳层按输入卡几何绑过来。
// 默认透明 ⇒ 不改变任何外观；皮肤想给输入区铺底色/描边时才用得上。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    property color color: "transparent"
    property color borderColor: Theme.color.border
    property bool bordered: false
    property real radius: Theme.radius.composer

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.width: root.bordered ? 1 : 0
        border.color: root.borderColor
    }
}
