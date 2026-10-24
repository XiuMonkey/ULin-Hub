// 侧栏会话搜索框（Quiet Rail 版，原型 styles.css 的 .sb-search）。
// 只负责"长什么样"：32px 高、inputBg 面 + inputBorder 环、左侧放大镜、右侧 Ctrl K 键帽。
// 过滤动作归调用方（SessionBar 负责调 Hub.sessions.setFilterText），本组件只发 textEdited(text)。
// 输入本身由 TextInput 承担（不用 TextField：这里不要它自带的背景/内边距体系）。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property alias text: input.text
    property string placeholder: qsTrId("sessionBar_search_placeholder")

    signal textEdited(string text)

    implicitHeight: 32

    Rectangle {
        id: background
        anchors.fill: parent
        radius: 9
        color: Theme.color.inputBg
        border.width: 1

        // 聚焦反馈照原型图：inputBorder → accent（55% 太重，直接 accent；1px 在 32px 框上已经够清楚）
        border.color: input.activeFocus ? Theme.color.accent : Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Theme.motion.fast } }
    }

    LineIcon {   // 放大镜：SVG 单色图标，颜色跟 textCaption 走（深浅主题同一份资源）
        id: searchIcon
        name: "search"
        size: 14
        color: Theme.color.textCaption
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
    }

    Rectangle {
        id: keycap
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: keycapLabel.implicitWidth + 8
        height: 16
        radius: 4
        color: "transparent"
        border.width: 1
        border.color: Theme.color.border

        Text {
            id: keycapLabel
            anchors.centerIn: parent
            text: qsTrId("sessionBar_shortcut_search")
            color: Theme.color.textCaption
            font.family: "Cascadia Code"
            font.pixelSize: Theme.font.small
        }
    }

    Text {   // 占位提示：TextInput 没有 TextField 的 placeholder 属性，这里用一层灰色 Text 代替
        anchors.left: input.left
        anchors.right: input.right
        anchors.verticalCenter: parent.verticalCenter
        visible: input.text.length === 0
        text: root.placeholder
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        elide: Text.ElideRight
    }

    TextInput {
        id: input
        anchors.left: searchIcon.right
        anchors.leftMargin: 7
        anchors.right: keycap.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.color.textPrimary
        selectionColor: Theme.color.selectionBg
        selectedTextColor: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        selectByMouse: true
        clip: true

        onTextChanged: root.textEdited(input.text)

        Keys.onEscapePressed: (event) => {
            input.text = ""
            input.focus = false
            event.accepted = true
        }
    }

    Shortcut {   // Ctrl K 聚焦（原型"键盘优先"）：窗口级快捷键，输入法打开时也生效
        sequence: "Ctrl+K"
        onActivated: {
            input.forceActiveFocus()
            input.selectAll()
        }
    }
}
