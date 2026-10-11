// 工作区分组表头。
//
// 结构（按原版 QPushButton#workspaceButton 的观感，但把"＋"拆成一个真按钮，QML 里更可靠）：
//   * 一行 = 底板 + 标题文本 + 右侧"＋"按钮 + 铺满整行的 MouseArea；
//   * 常态底色 {{hoverBg}}、悬停 {{activeBg}}、radius 8、标题 13px/600/textPrimary、
//     左内边距 10、右侧给"＋"留 26px（原版 padding: 6px 26px 6px 10px）；
//   * 点"＋" = 在此工作区新建会话；点其他地方 = 折叠。
//
// ⚠️ 踩过的两个坑（都别再犯）：
//   1) `＋` 用 `visible: 行hover` ⇒ 指针移到 ＋ 上时上层 MouseArea 抢走 hover → ＋ 被隐藏，
//      点击落到整行上只能折叠（"点它没用"）。现在 ＋ **常态显示**，可见性与 hover 无关。
//   2) 用 HoverHandler 做悬停 + x 带命中判定时，实测悬停不亮、＋ 也不出现。
//      现在悬停交回 MouseArea.containsMouse（这个在委托里是work的），点击由 ＋ 自己的按钮处理。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string title: ""
    property bool expanded: true

    signal toggleRequested()
    signal addSessionRequested()
    // 右键删除这个工作区（M3b）
    signal deleteRequested()

    implicitHeight: 32

    // 右侧 26px = "＋"的热区（原版 padding-right: 26px）
    readonly property int plusZone: 26

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.control
        color: mouse.containsMouse ? Theme.color.activeBg : Theme.color.hoverBg
    }

    // 箭头 + 标题合成一行文本（原版 setText("▾ " + title) 的写法），左内边距 10
    Text {
        id: label
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: addButton.left
        anchors.rightMargin: Theme.spacing.buttonGap
        anchors.verticalCenter: parent.verticalCenter
        text: (root.expanded ? "▾ " : "▸ ") + root.title
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
        font.weight: Theme.font.weightBold
        elide: Text.ElideRight
    }

    // "＋"：常态可见、自带 hover/点击；z: 1 让它在整行 MouseArea 之上。
    IconButton {
        id: addButton
        z: 1
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        boxSize: 22
        glyphSize: Theme.font.title
        glyph: "＋"
        tooltip: "在此工作区新建会话"
        onClicked: root.addSessionRequested()
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor

        // 右键 = 删除这个工作区（M3b）
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.deleteRequested()
                return
            }
            // 左键：右侧 26px 带 = 新建会话（原版命中判定），其余 = 折叠/展开
            if (mouse.x >= root.width - root.plusZone)
                root.addSessionRequested()
            else
                root.toggleRequested()
        }
    }
}
