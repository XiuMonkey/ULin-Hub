// 荧光屏 · 会话行
// 契约与内置 skin/session/SessionRow.qml 一致（title / running / selected / timeText +
// selectRequested / deleteRequested / menuRequested）。
// 造型：直角终端行；选中 = 深底 + 左沿贯通荧光条 + 等宽白字；运行中是一个会呼吸的方块光标。
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

    implicitHeight: 30

    readonly property bool hovered: rowMouse.containsMouse || moreButton.hovered
    readonly property bool showMeta: hovered || selected

    QtObject {
        id: metaFade
        property real value: root.showMeta ? 1 : 0
        Behavior on value {
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: root.selected ? Theme.color.activeBg : Theme.color.hoverBg
        opacity: metaFade.value
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
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
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 12
        visible: root.running

        Rectangle {
            anchors.fill: parent
            color: Theme.color.accent

            SequentialAnimation on opacity {
                running: root.running
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.0; duration: 620; easing.type: Easing.Stepped }
                NumberAnimation { from: 0.0; to: 1.0; duration: 620; easing.type: Easing.Stepped }
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
        color: root.selected ? Theme.color.accent
            : (root.showMeta ? Theme.color.textPrimary : Theme.color.textSecondary)
        font.family: "Consolas"
        font.pixelSize: Theme.font.body - 1
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
        font.family: "Consolas"
        font.pixelSize: 10
        opacity: metaFade.value
    }

    IconButton {
        id: moreButton
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        boxSize: 18
        iconSize: 13
        iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
        iconColor: moreButton.hovered ? Theme.color.accent : Theme.color.textCaption
        opacity: metaFade.value
        onClicked: root.menuRequested()
    }
}
