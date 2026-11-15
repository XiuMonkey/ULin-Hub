// ULin Core 设置页统一勾选框：与 ModelListPage 的 CheckBox 同一套指示器。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

CheckBox {
    id: control

    font.family: "Microsoft YaHei UI"
    font.pixelSize: Theme.font.body
    spacing: Theme.spacing.labelGap

    palette.windowText: Theme.color.textPrimary
    palette.text: Theme.color.textPrimary
    palette.highlight: Theme.color.textSecondary
    palette.highlightedText: Theme.color.panelBg

    indicator: Rectangle {
        implicitWidth: Theme.size.checkbox
        implicitHeight: Theme.size.checkbox
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: Theme.radius.check
        color: {
            if (!control.enabled)
                return "transparent"
            if (control.checked)
                return Theme.color.textPrimary
            return control.hovered ? Theme.color.hoverBg : "transparent"
        }
        border.width: 1
        border.color: {
            if (!control.enabled)
                return Theme.color.border
            if (control.checked)
                return Theme.color.textPrimary
            return (control.hovered || control.visualFocus)
                ? Theme.color.textSecondary
                : Theme.color.inputBorder
        }

        Text {
            anchors.centerIn: parent
            visible: control.checkState === Qt.Checked
            text: "✓"
            color: Theme.color.panelBg
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.small
            font.weight: Theme.font.weightBold
        }
    }
}
