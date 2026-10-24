// 会话行（Quiet Rail 设计）：一行 30px = 运行点 + 标题 + 时间（悬停/选中显形）+ ⋯ 菜单。
// 常态透明底、悬停 hoverBg、选中 activeBg + 左侧 2px accent 短条；标题悬停提亮、选中加 500 字重；
// running 时行首 6px accent 圆点（2s 呼吸），不 running 仍占 6px，标题不横向跳动。
// 旧 API（title/running/selected + selectRequested/deleteRequested）全部保留；新增 timeText/menuRequested。
// 整行 MouseArea 负责左键选中 / 右键删除；⋯ 按钮排在它之后（上层），点它不会触发选中。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string title: ""
    property bool running: false
    property bool selected: false
    property string timeText: ""   // 预格式化的相对时间，可能为空（空 = 不渲染也不占位）

    signal selectRequested()
    signal deleteRequested()
    signal menuRequested()

    implicitHeight: 30

    // 指针移进 ⋯ 子按钮时 rowMouse 会丢 hover；聚合子按钮 hovered，避免时间/⋯ 在指针悬停时淡出。
    readonly property bool hovered: rowMouse.containsMouse || moreButton.hovered
    readonly property bool showMeta: hovered || selected   // 时间与 ⋯ 只在悬停/选中时显形（只动 opacity，不动布局）

    // 底板、时间、⋯ 的淡入淡出**条件完全相同**（悬停/选中），共用一份动画值：
    // 观感与各挂一个 Behavior 一致，但只维护一条 160ms 曲线、少两个动画对象。
    // 160ms 取设计稿 --motion（Theme.motion 档位是 100/200/300，对不上，故不引 token）。
    QtObject {
        id: metaFade
        property real value: root.showMeta ? 1 : 0
        Behavior on value {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    // 底板：悬停 hoverBg、选中 activeBg（设计稿 .session-row:hover / .active）。
    // 底色恒定、只淡入透明度（曲线复用上面 metaFade）：从 "transparent" 做 ColorAnimation 会
    // 插值经过偏黑值，深色主题下看起来"先忽然变深再变浅"，opacity 淡入没有这个问题。
    Rectangle {
        anchors.fill: parent
        radius: 7
        color: root.selected ? Theme.color.activeBg : Theme.color.hoverBg
        opacity: metaFade.value
    }

    Rectangle {   // 选中左强调条：2px、上下各 7、圆角 2（设计稿 .session-row.active::before）
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 7
        anchors.bottomMargin: 7
        width: 2
        radius: 2
        color: Theme.color.accent
        visible: root.selected
    }

    MouseArea {   // 整行点击：左键选中，右键删除会话（M3b 原路径）
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                root.deleteRequested()
            else
                root.selectRequested()
        }
    }

    // 运行中：6px accent 圆点 + 16% 光晕，2s 呼吸（设计稿 @keyframes pulse）。
    // 不运行时 visible=false —— 宽度仍是 6px，起“隐形占位”作用，标题起点不变。
    Item {
        id: runningDot
        anchors.left: parent.left
        anchors.leftMargin: 8       // 设计稿 .session-row { padding: 0 8px }
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        visible: root.running

        Rectangle {   // 光晕：accent 16%（设计稿 box-shadow 0 0 0 3px color-mix(...16%...)）
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.16)
        }

        Rectangle {
            id: dot
            anchors.fill: parent
            radius: 3
            color: Theme.color.accent

            SequentialAnimation on opacity {
                running: root.running
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.45; duration: 1000; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.45; to: 1.0; duration: 1000; easing.type: Easing.InOutSine }
            }
        }
    }

    Text {   // 标题：常态 secondary，悬停/选中提亮 primary；选中再加 500（设计稿 .session-row.active）
        id: titleText
        anchors.left: runningDot.right
        anchors.leftMargin: 7
        anchors.right: root.timeText.length > 0 ? timeText.left : moreButton.left
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: root.showMeta ? Theme.color.textPrimary : Theme.color.textSecondary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 13   // 设计稿 12.5px；QML font.pixelSize 只有整数，取 13（Theme.font.body 同值）
        font.weight: root.selected ? Theme.font.weightMedium : Font.Normal
        elide: Text.ElideRight
    }

    Text {   // 相对时间：10px caption，只动 opacity；空串时 visible=false 且不占位
        id: timeText
        anchors.right: moreButton.left
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        visible: root.timeText.length > 0
        text: root.timeText
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 10
        opacity: metaFade.value
    }

    IconButton {   // 尾部「更多」：18px 热区、13px 图标，悬停/选中淡入；点击只发 menuRequested
        id: moreButton
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        boxSize: 18
        iconSize: 13
        iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
        iconColor: moreButton.hovered ? Theme.color.textPrimary : Theme.color.textCaption
        opacity: metaFade.value
        onClicked: root.menuRequested()
    }
}
