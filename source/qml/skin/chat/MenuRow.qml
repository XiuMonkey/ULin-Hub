// 菜单行（模型 / 思考档位共用；原版 ModelSelectorRow，objectName modelSelectorOption）
// 最小高 38、内边距 8/6/8/6、间距 8；选中时右侧 ✓（accent）；悬停/聚焦 chipHoverBg；选中行背景仍透明。

import QtQuick
import ULinHub 1.0

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property bool checked: false

    signal chosen()

    implicitHeight: Math.max(38, textColumn.implicitHeight + 12)
    radius: 10
    color: (rowMouse.containsMouse || rowMouse.pressed) ? Theme.color.chipHoverBg : "transparent"

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Column {
            id: textColumn
            width: parent.width - check.width - parent.spacing
            spacing: 0

            Text {
                width: parent.width
                text: root.title
                elide: Text.ElideRight
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightMedium
            }

            Text {
                width: parent.width
                visible: root.subtitle.length > 0
                text: root.subtitle
                elide: Text.ElideRight
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }
        }

        Text {
            id: check
            width: 18
            anchors.verticalCenter: parent.verticalCenter
            text: root.checked ? "✓" : ""
            horizontalAlignment: Text.AlignHCenter
            color: Theme.color.accent
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightBold
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.chosen()
    }
}
