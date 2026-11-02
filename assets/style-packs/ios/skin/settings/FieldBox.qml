// iOS 玻璃 · 表单输入框
// ---------------------------------------------------------------------------
// 契约与内置 skin/settings/FieldBox.qml 一致（text / placeholder / numeric / password），
// 根仍是 Rectangle，所以调用方写 radius/color 也照旧生效。
//
// 造型是 iOS 的表单框：systemFill 灰底 + 10px 圆角，静息无描边；聚焦时浮出一圈
// systemBlue 的淡环（和搜索框同一套规则，整个包里"聚焦 = 环"是一致的）。
import QtQuick
import ULinHub 1.0

Rectangle {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property bool numeric: false
    property bool password: false

    implicitHeight: 34
    radius: Theme.radius.control
    color: Theme.color.inputBg
    border.width: 1
    border.color: input.activeFocus
        ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.60)
        : "transparent"

    Behavior on border.color { ColorAnimation { duration: Theme.motion.fast } }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.fieldPadX + 2
        anchors.rightMargin: Theme.spacing.fieldPadX + 2
        verticalAlignment: TextInput.AlignVCenter
        echoMode: root.password ? TextInput.Password : TextInput.Normal
        inputMethodHints: root.numeric ? Qt.ImhDigitsOnly : Qt.ImhNone
        selectByMouse: true
        clip: true
        color: Theme.color.textPrimary
        selectionColor: Theme.color.selectionBg
        selectedTextColor: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        x: Theme.spacing.fieldPadX + 2
        width: parent.width - 2 * (Theme.spacing.fieldPadX + 2)
        visible: input.text.length === 0
        text: root.placeholder
        elide: Text.ElideRight
        color: Theme.color.textTertiary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
    }
}
