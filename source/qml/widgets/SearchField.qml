// 搜索/输入框：token 化的 TextField，带可选的清除按钮。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

TextField {
    id: root

    property string clearTooltip: qsTrId("common_clear")

    // 左侧前置图标（= icons/ui/<name>.svg）：给了才多出一颗 LineIcon，并把文字左缩进让开它
    // （搜索框里没有放大镜，光看边框认不出是什么）。
    property string leadingIcon: ""

    color: Theme.color.textPrimary
    placeholderTextColor: Theme.color.textCaption
    selectionColor: Theme.color.selectionBg
    selectedTextColor: Theme.color.textPrimary
    font.family: Theme.font.uiFamily
    font.pixelSize: Theme.font.body
    selectByMouse: true

    leftPadding: root.leadingIcon.length > 0
        ? Theme.spacing.fieldPadX + 18 + 6
        : Theme.spacing.fieldPadX
    rightPadding: clearButton.visible ? clearButton.width + Theme.spacing.fieldPadX : Theme.spacing.fieldPadX
    topPadding: Theme.spacing.fieldPadY
    bottomPadding: Theme.spacing.fieldPadY

    background: Rectangle {
        implicitHeight: 30
        radius: Theme.radius.input
        color: Theme.color.inputBg
        border.width: 1

        // 静息用 inputBorder（10% 黑）= 官方 --dsw-alias-border-l2；聚焦给 1px 中性灰，
        // 官方靠 2px focus ring，这里是同等克制的替代，保住键盘走查的可见性。
        border.color: root.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder
    }

    LineIcon {
        visible: root.leadingIcon.length > 0
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.fieldPadX
        anchors.verticalCenter: parent.verticalCenter
        name: root.leadingIcon
        size: 14
        color: root.activeFocus ? Theme.color.textSecondary : Theme.color.textCaption
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
