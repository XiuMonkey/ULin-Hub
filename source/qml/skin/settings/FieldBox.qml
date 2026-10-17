// 表单输入框皮肤（对应旧壳 model-list.qss 的 `#modelListForm QLineEdit#modelListField`）：
// panelBg + 1px 极浅描边 + radius 8 + 内边距 6/10 + 13px textPrimary，
// 聚焦时描边转 textSecondary（中性灰，按"设置页不出蓝"）。
// 静息描边 2026-10-17 改：`scrollbar`(#D1D5DB，约 17% 灰) → `inputBorder`(10% 黑) ——
// 对齐官方 DSH 的 `--dsw-alias-border-l2`(10%)，也是官方输入框的取法（官方静息根本没有 CSS border，
// 层次靠 `--dsw-elevation-soft` 的 0.5px ring + 柔和阴影；见 build/temp/ui-texture/official/）。

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
        font.family: "Microsoft YaHei UI"
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
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
    }
}
