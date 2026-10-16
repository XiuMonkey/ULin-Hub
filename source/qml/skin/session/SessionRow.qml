// 会话行（按原版 sidebar.qss 的 QPushButton#sessionButton 对齐）：
//   * padding 0 10px、radius 8、常态透明底；
//   * **悬停与选中都是 activeBg**（原版 :hover / :checked 同色），文字常态就是 textPrimary；
//   * 左内边距 10 与工作区标题行对齐（原版会话不做额外缩进）。
// 右键菜单（删除会话）留到 M3b：那条路要确认 session 删除的 RPC 与副作用，先不做哑菜单。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string title: ""
    property bool running: false
    property bool selected: false

    signal selectRequested()
    // 右键删除这一条会话（M3b）
    signal deleteRequested()

    implicitHeight: 32

    readonly property bool hovered: area.containsMouse

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.control      // 8（原版 sessionButton border-radius: 8px）
        color: (root.selected || root.hovered) ? Theme.color.activeBg : "transparent"
    }

    Text {
        id: label
        anchors.left: parent.left
        anchors.leftMargin: 10            // padding: 0 10px
        anchors.right: runningDot.visible ? runningDot.left : parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.color.textPrimary     // 原版未选中会话也是 textPrimary（不压暗）
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
        font.weight: root.selected ? Theme.font.weightMedium : Font.Normal
        elide: Text.ElideRight
    }

    // 运行中：一颗呼吸的小点（旧壳是按钮上的状态点，这里同理只做提示）
    Rectangle {
        id: runningDot
        visible: root.running
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        color: Theme.color.accent

        SequentialAnimation on opacity {
            running: root.running
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.3; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.3; to: 1.0; duration: 700; easing.type: Easing.InOutSine }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            // 右键 = 删除会话（M3b；旧壳是 SessionButton 的右键菜单 → ULinHub::onDeleteSessionRequested）
            if (mouse.button === Qt.RightButton)
                root.deleteRequested()
            else
                root.selectRequested()
        }
    }
}
