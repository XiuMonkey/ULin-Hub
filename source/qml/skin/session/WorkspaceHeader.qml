// 工作区分组表头（Quiet Rail 设计）：一行 30px = 折叠箭头 + 彩色徽标 + 组名 + 会话数 + 悬停出现的「＋ / ⋯」。
// 箭头收起时旋转 -90°、160ms；徽标 = 工作区色 16% 底 + 22% 描边 + 组名首字母。
// ⚠️ 动作按钮只改 opacity、不改 visible，且必须排在整行 MouseArea **之后**：否则指针移到按钮上时
// 整行 hover 被抢走，按钮消失、点击落到折叠（旧实现踩过）。
// 向后兼容：toggleRequested / addSessionRequested / deleteRequested 保持原名；新增 menuRequested。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string title: ""
    property bool expanded: true
    property int sessionCount: 0
    property color workspaceColor: Theme.color.accent

    signal toggleRequested()
    signal addSessionRequested()
    signal menuRequested()
    signal deleteRequested()

    implicitHeight: 30

    // 指针移进右侧 ＋/⋯ 子按钮时 rowMouse 会丢 hover；把子按钮的 hovered 聚合进来，
    // 否则动作行会在指针正悬停时淡出（按钮仍可点，但看不见）。
    readonly property bool hovered: rowMouse.containsMouse || plusButton.hovered || moreButton.hovered

    // 徽标底色/描边：工作区色 16% / 22%（设计稿 color-mix(in srgb, var(--ws-color) 16%, transparent)）
    readonly property color badgeBg: Qt.rgba(workspaceColor.r, workspaceColor.g, workspaceColor.b, 0.16)
    readonly property color badgeBorder: Qt.rgba(workspaceColor.r, workspaceColor.g, workspaceColor.b, 0.22)

    // 底板与右侧动作的显隐条件相同（整行 hover），共用一份动画值：只维护一条 160ms 曲线、
    // 少一个动画对象。160ms 取设计稿 --motion（Theme.motion 档位是 100/200/300，对不上，故不引 token）。
    QtObject {
        id: hoverFade
        property real value: root.hovered ? 1 : 0
        Behavior on value {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    // 底板：悬停 hoverBg（设计稿 .ws-head:hover）。
    // 用 opacity 淡入而不是 ColorAnimation("transparent" → hoverBg)：透明是 rgba(0,0,0,0)，
    // 颜色插值中途会经过偏黑的值，深色主题下看起来就是"先变深再变浅"。这里底色恒定，只动透明度。
    Rectangle {
        anchors.fill: parent
        radius: 7
        color: Theme.color.hoverBg
        opacity: hoverFade.value
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                root.deleteRequested()
            else
                root.toggleRequested()
        }
    }

    LineIcon {   // 折叠箭头：展开 0°，收起 -90°，160ms ease-out
        id: chevron
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 12
        color: Theme.color.textSecondary
        opacity: 0.5                     // 设计稿 .chev { opacity: .5 }
        rotation: root.expanded ? 0 : -90
        Behavior on rotation {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {   // 18px 彩色徽标：组名首字母 + 工作区识别色
        id: badge
        anchors.left: chevron.right
        anchors.leftMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: 5
        color: root.badgeBg
        border.width: 1
        border.color: root.badgeBorder

        Text {
            anchors.centerIn: parent
            text: root.title.length > 0 ? root.title.charAt(0).toUpperCase() : ""
            color: root.workspaceColor
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 10   // 设计稿 9.5px；QML font.pixelSize 只有整数，取 10
            font.weight: Font.Bold
        }
    }

    Text {   // 会话数：10px caption，固定贴在右侧动作区左边，组名负责 elide
        id: count
        anchors.right: actions.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: root.sessionCount
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 10
    }

    Text {   // 组名：12.5px / 550，textSecondary（设计稿 .ws-name 继承 .ws-toggle 的 --secondary）
        id: name
        anchors.left: badge.right
        anchors.leftMargin: 7
        anchors.right: count.left
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.color.textSecondary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 13   // 设计稿 12.5px；QML font.pixelSize 只有整数，取 13（Theme.font.body 同值）
        font.weight: 550
        elide: Text.ElideRight
    }

    // 悬停动作行：＋（在此工作区新建会话）/ ⋯（工作区菜单）。
    // opacity 0 时仍占位；悬停判定来自整行 MouseArea，所以指针移到按钮上也不会消失。
    Row {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: 2       // 设计稿 .ws-head { padding-right: 2px }
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        opacity: hoverFade.value

        IconButton {
            id: plusButton
            boxSize: 22
            iconSize: 13
            iconSource: "qrc:/ULinHub/icons/ui/plus.svg"
            iconColor: plusButton.hovered ? Theme.color.textPrimary : Theme.color.textTertiary
            tooltip: qsTrId("workdir_new_session_tip")
            onClicked: root.addSessionRequested()
        }

        IconButton {
            id: moreButton
            boxSize: 22
            iconSize: 13
            iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
            iconColor: moreButton.hovered ? Theme.color.textPrimary : Theme.color.textTertiary
            onClicked: root.menuRequested()
        }
    }
}
