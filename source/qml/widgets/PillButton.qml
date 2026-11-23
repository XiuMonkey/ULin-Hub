// 胶囊按钮：浅色胶囊底 + 半高圆角，选中时只把底色深一档（字色、字号、字重一律不变）。
// 只管画和发 clicked()：互斥归调用方（每颗只认自己的 selected，"一排里只能选一颗"由用它的地方保证）。
// 形状/颜色/字号/缓动全取 token，没有本地魔法值。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    property bool selected: false

    // 胶囊高度：圆角恒为半高（height / 2），所以形状永远是胶囊，改高度不用另改圆角。
    // 默认 20 = 比顶栏标题行（44）里那行字小一号：一排小胶囊是"副信息"，不能把顶栏撑高。
    property int pillHeight: 20
    property int horizontalPadding: Theme.spacing.fieldPadX

    signal clicked()

    // 底色只有两档，统一在这里挑，别在 Rectangle 里现算：
    // 未选中 = 浅色（light 是 rgba(0,0,0,0.08) 的浅灰，dark 是比卡片亮一档的灰）；
    // 选中 = 提亮 6% 的 buttonPressed：调色板在"太淡"与"太深"之间没有第三档，提亮后正好落在中间且仍挂 token。
    // 只动底色、不动文字：近黑实心 + 白字太抢，字色跟着变也不协调，两版都被否掉。
    // dark 主题里"更深"看不见，所以那边是亮一档，语义仍是"选中的那颗离底色更远"。
    readonly property color baseColor: Theme.color.iconButtonHoverBg
    readonly property color selectedColor: Qt.lighter(Theme.color.buttonPressed, 1.06)

    implicitWidth: label.implicitWidth + 2 * horizontalPadding
    implicitHeight: pillHeight

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.selected ? root.selectedColor : root.baseColor
        Behavior on color {   // 换选中是状态切换，补一下颜色不会显得"跳"，快档就够
            ColorAnimation { duration: Theme.motion.fast }
        }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text

        // 字色恒定：选中/未选中/悬停/按下全一样（两次否掉过"字色跟着变"——换色就不协调）。
        // 所以这里刻意不绑 mouseArea 的任何状态；状态反馈只由底色那一档承担。
        color: Theme.color.textSecondary
        font.family: Theme.font.uiFamily
        font.pixelSize: Theme.font.caption
        font.weight: Font.Normal
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent

        // hoverEnabled 刻意不开：本控件不打算做悬停反馈（文字不许变色，底色也只在选中时变），
        // 指针形状就是"能点"的唯一提示。开它只会让人以为漏了什么效果。
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
