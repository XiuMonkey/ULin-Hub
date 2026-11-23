// ULin Core 设置页统一下拉框：与 AppearancePage 的语言下拉同一套外观。
// 用法同 ComboBox：model / textRole / currentIndex / onActivated 都直接可用。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

ComboBox {
    id: control

    width: 200
    height: 32
    leftPadding: 10
    rightPadding: 30
    font.family: Theme.font.uiFamily
    font.pixelSize: Theme.font.body

    palette.windowText: Theme.color.textPrimary
    palette.text: Theme.color.textPrimary
    palette.buttonText: Theme.color.textPrimary

    contentItem: Text {
        leftPadding: 10
        rightPadding: 30
        text: control.displayText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        color: Theme.color.textPrimary
        font.family: Theme.font.uiFamily
        font.pixelSize: Theme.font.body
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    indicator: Text {   // 自绘箭头：贴右 26、宽 18，contentItem 的 rightPadding 30 就是给它留的沟
        x: control.width - 26
        y: Math.round((control.height - height) / 2)
        width: 18
        horizontalAlignment: Text.AlignHCenter
        text: "\u25BE"
        color: Theme.color.textSecondary
        font.pixelSize: Theme.font.small
    }

    background: Rectangle {
        color: Theme.color.inputBg
        border.width: 1
        border.color: hover.hovered ? Theme.color.scrollbarHover : Theme.color.inputBorder
        radius: Theme.radius.control
    }
}
