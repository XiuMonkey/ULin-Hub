// iOS 玻璃 · 侧栏搜索框
// ---------------------------------------------------------------------------
// 只负责"长什么样"，契约与内置件一致：text / placeholder / textEdited(string) + Ctrl+K 聚焦。
//
// 造型是 iOS 的搜索栏：systemFill 灰底 + 10px 圆角，**静息没有描边**（iOS 的搜索框
// 不用边界线表达自己，靠底色差），放大镜在左、占位文字跟在放大镜后面。
// 聚焦时才浮出一圈 systemBlue 的淡环 —— iOS 的聚焦反馈是"环"，不是"变粗的边框"。
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
        radius: Theme.radius.control
        color: Theme.color.inputBg

        border.width: 1
        border.color: input.activeFocus
            ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.55)
            : "transparent"

        Behavior on border.color { ColorAnimation { duration: Theme.motion.fast } }

        // 聚焦时环外再加一圈更淡的晕，做出 iOS 焦点环的"两层"手感
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.16)
            visible: input.activeFocus
        }
    }

    LineIcon {
        id: searchIcon
        name: "search"
        size: 13
        color: input.activeFocus ? Theme.color.textSecondary : Theme.color.textTertiary
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
    }

    // iOS 上没有键帽，就是一行淡字。这里保留提示但不画框，免得又变回桌面软件
    Text {
        id: shortcutHint
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        visible: input.text.length === 0
        text: "Ctrl K"
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.small
    }

    Text {
        anchors.left: input.left
        anchors.right: shortcutHint.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        visible: input.text.length === 0
        text: root.placeholder
        color: Theme.color.textTertiary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        elide: Text.ElideRight
    }

    TextInput {
        id: input
        anchors.left: searchIcon.right
        anchors.leftMargin: 7
        anchors.right: shortcutHint.left
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

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: {
            input.forceActiveFocus()
            input.selectAll()
        }
    }
}
