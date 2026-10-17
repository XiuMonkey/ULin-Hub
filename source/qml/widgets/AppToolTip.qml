// 悬浮提示。
//
// ⚠️ 刻意**不用** QtQuick.Controls 的 ToolTip/Popup：Popup 弹层会接管鼠标 —— 默认
//   closePolicy 是 CloseOnPressOutside，于是"悬停出提示 → 紧接着点击"的第一下会被用来关弹层，
//   按钮收不到点击（实测：侧栏 ＋ 第一次点没反应，第二下才生效）。这里用普通 Item + 高 z 值实现，
//   不产生独立窗口、不参与鼠标抓取，点击照常落到下面的控件上。
//
// 用法（作为子项放在要提示的控件里，visible 绑到悬停）：
//   AppToolTip { text: "设置"; visible: mouseArea.containsMouse }

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string text: ""

    // 贴在控件正下方居中：既能显示完整，也不会浮在指针下面
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.top: parent ? parent.bottom : undefined
    anchors.topMargin: 4

    // 别被父级的 clip 裁掉（卡片会 clip）
    z: 1000

    implicitWidth: bubble.implicitWidth
    implicitHeight: bubble.implicitHeight
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        id: bubble
        anchors.fill: parent
        implicitWidth: label.implicitWidth + 2 * Theme.spacing.fieldPadX
        implicitHeight: label.implicitHeight + Theme.spacing.labelGap
        radius: Theme.radius.small
        color: Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            wrapMode: Text.NoWrap
        }
    }
}
