// 霜玻 · 会话行
// 契约与内置 skin/session/SessionRow.qml 一致（title / running / selected / timeText +
// selectRequested / deleteRequested / menuRequested）。
// 造型：胶囊行 —— 悬停半透明白、选中更亮一层玻璃，左沿一小段圆头条标出当前会话。
import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string title: ""
    property bool running: false
    property bool selected: false
    property string timeText: ""

    signal selectRequested()
    signal deleteRequested()
    signal menuRequested()

    implicitHeight: 34

    readonly property bool hovered: rowMouse.containsMouse || moreButton.hovered
    readonly property bool showMeta: hovered || selected

    QtObject {
        id: metaFade
        property real value: root.showMeta ? 1 : 0
        Behavior on value {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: height / 2
        color: root.selected ? Theme.color.activeBg : Theme.color.hoverBg
        opacity: metaFade.value
        border.width: root.selected ? 1 : 0
        border.color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.14 : 0.8)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: 16
        radius: 2
        color: Theme.color.accent
        visible: root.selected
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                root.deleteRequested()
            else
                root.selectRequested()
        }
    }

    Item {
        id: runningDot
        anchors.left: parent.left
        anchors.leftMargin: 13
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        visible: root.running

        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.18)
        }

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: Theme.color.accent

            SequentialAnimation on opacity {
                running: root.running
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.45; duration: 1000; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.45; to: 1.0; duration: 1000; easing.type: Easing.InOutSine }
            }
        }
    }

    Text {
        id: titleText
        anchors.left: runningDot.right
        anchors.leftMargin: 8
        anchors.right: root.timeText.length > 0 ? timeText.left : moreButton.left
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: root.showMeta ? Theme.color.textPrimary : Theme.color.textSecondary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
        font.weight: root.selected ? Theme.font.weightMedium : Font.Normal
        elide: Text.ElideRight
    }

    Text {
        id: timeText
        anchors.right: moreButton.left
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        visible: root.timeText.length > 0
        text: root.timeText
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 10
        opacity: metaFade.value
    }

    IconButton {
        id: moreButton
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        boxSize: 18
        iconSize: 13
        iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
        iconColor: moreButton.hovered ? Theme.color.textPrimary : Theme.color.textCaption
        opacity: metaFade.value
        onClicked: root.menuRequested()
    }
}
