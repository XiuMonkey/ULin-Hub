// 樱岚 · 会话行
// 契约与内置 skin/session/SessionRow.qml 一致（title / running / selected / timeText +
// selectRequested / deleteRequested / menuRequested）。
// 造型：胶囊行 + 选中时左侧一朵小花（三瓣圆点），保持柔软的边界。
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
            NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: height / 2
        color: root.selected ? Theme.color.activeBg : Theme.color.hoverBg
        opacity: metaFade.value
    }

    Item {
        id: flower
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 9
        height: 9
        visible: root.selected && !root.running

        Rectangle {
            anchors.centerIn: parent
            width: 9
            height: 9
            radius: 5
            color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.55)
        }
        Rectangle {
            anchors.centerIn: parent
            width: 4
            height: 4
            radius: 2
            color: Theme.color.cardBg
        }
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
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        visible: root.running

        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.16)
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
        color: root.selected ? Theme.color.textPrimary
            : (root.showMeta ? Theme.color.textPrimary : Theme.color.textSecondary)
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
