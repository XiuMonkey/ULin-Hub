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
        // 输入类控件：静息走 inputBorder(10% 黑) = 官方 DSH `--dsw-alias-border-l2`；
        // 聚焦仍给一条克制可见的中性灰（textSecondary），保住键盘走查的可见性 ——
        // 官方是靠 2px focus ring，这里 1px 中性灰是同等的克制替代。
        border.color: root.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder
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
