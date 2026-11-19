// ULin Core 设置页共用输入框：标签 + 圆角输入框 + 占位符。
import QtQuick
import QtQml
import ULinHub 1.0

Column {
    id: field

    property string label
    property string value
    property string placeholder
    property bool password: false

    signal edited(string text)

    width: parent.width
    spacing: 4

    Text {
        text: field.label
        color: Theme.color.textSecondary
        font.family: Theme.font.uiFamily
        font.pixelSize: Theme.font.caption
    }

    Rectangle {
        width: parent.width
        height: 30
        radius: Theme.radius.control
        color: Theme.color.controlBg || Theme.color.panelBg
        border.width: 1
        border.color: input.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacing.fieldPadX
            visible: input.text.length === 0
            text: field.placeholder
            color: Theme.color.textTertiary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.body
        }

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing.fieldPadX
            anchors.rightMargin: Theme.spacing.fieldPadX
            verticalAlignment: TextInput.AlignVCenter
            echoMode: field.password ? TextInput.Password : TextInput.Normal
            selectByMouse: true
            clip: true
            color: Theme.color.textPrimary
            selectionColor: Theme.color.selectionBg
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.body
            onTextEdited: field.edited(text)
        }

        Binding {
            target: input
            property: "text"
            value: field.value
        }
    }
}
