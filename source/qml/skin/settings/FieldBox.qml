// 表单输入框皮肤：panelBg 底 + 1px 极浅描边（透明度对齐官方 DSH 的 --dsw-alias-border-l2），
// 聚焦时描边转 textSecondary：设置页保持中性灰，不出蓝。

import QtQuick
import ULinHub 1.0

Rectangle {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property bool numeric: false
    property bool password: false

    implicitHeight: 30
    radius: Theme.radius.control
    color: Theme.color.panelBg
    border.width: 1
    border.color: input.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.fieldPadX
        anchors.rightMargin: Theme.spacing.fieldPadX
        verticalAlignment: TextInput.AlignVCenter
        echoMode: root.password ? TextInput.Password : TextInput.Normal
        inputMethodHints: root.numeric ? Qt.ImhDigitsOnly : Qt.ImhNone
        selectByMouse: true
        clip: true
        color: Theme.color.textPrimary
        selectionColor: Theme.color.selectionBg
        selectedTextColor: Theme.color.textPrimary
        font.family: Theme.font.uiFamily
        font.pixelSize: Theme.font.body
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        x: Theme.spacing.fieldPadX
        width: parent.width - 2 * Theme.spacing.fieldPadX
        visible: input.text.length === 0
        text: root.placeholder
        elide: Text.ElideRight
        color: Theme.color.textTertiary
        font.family: Theme.font.uiFamily
        font.pixelSize: Theme.font.body
    }
}
