// 搜索/输入框：token 化的 TextField，带可选的清除按钮。
// 旧壳对应 #pluginSearchEdit / #modelListField 这类输入。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

TextField {
    id: root

    property string clearTooltip: qsTrId("common_clear")

    color: Theme.color.textPrimary
    placeholderTextColor: Theme.color.textCaption
    selectionColor: Theme.color.selectionBg
    selectedTextColor: Theme.color.textPrimary
    font.family: "Microsoft YaHei UI"
    font.pixelSize: Theme.font.body
    selectByMouse: true

    leftPadding: Theme.spacing.fieldPadX
    rightPadding: clearButton.visible ? clearButton.width + Theme.spacing.fieldPadX : Theme.spacing.fieldPadX
    topPadding: Theme.spacing.fieldPadY
    bottomPadding: Theme.spacing.fieldPadY

    background: Rectangle {
        implicitHeight: 30
        radius: Theme.radius.input
        color: Theme.color.inputBg
        border.width: 1
        border.color: root.activeFocus ? Theme.color.accent : Theme.color.inputBorder
    }

    IconButton {
        id: clearButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.buttonGap
        anchors.verticalCenter: parent.verticalCenter
        visible: root.text.length > 0 && root.enabled
        glyph: "✕"
        tooltip: root.clearTooltip
        boxSize: 20
        glyphSize: Theme.font.small
        onClicked: root.clear()
    }
}
