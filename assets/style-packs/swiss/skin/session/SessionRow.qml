// 苏黎世 · 会话行
// 契约与内置 skin/session/SessionRow.qml 一致（title / running / selected / timeText +
// selectRequested / deleteRequested / menuRequested）。
// 造型：直角色块行 —— 选中是整行反白（黑底白字），左侧 3px 红条；悬停只铺一层极浅灰。
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

    implicitHeight: 32

    readonly property bool hovered: rowMouse.containsMouse || moreButton.hovered
    readonly property bool showMeta: hovered || selected
    readonly property color fgColor: root.selected ? Theme.color.primaryButtonText
        : (root.showMeta ? Theme.color.textPrimary : Theme.color.textSecondary)

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: root.selected ? Theme.color.primaryButtonBg : Theme.color.hoverBg
        opacity: root.selected ? 1.0 : (root.hovered ? 1.0 : 0.0)
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
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
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        visible: root.running

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: Theme.color.accent

            SequentialAnimation on opacity {
                running: root.running
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.4; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.4; to: 1.0; duration: 900; easing.type: Easing.InOutSine }
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
        color: root.fgColor
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
        font.weight: root.selected ? Theme.font.weightBold : Font.Normal
        elide: Text.ElideRight
    }

    Text {
        id: timeText
        anchors.right: moreButton.left
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        visible: root.timeText.length > 0
        text: root.timeText
        color: root.selected ? Theme.color.primaryButtonText : Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 10
        opacity: root.showMeta ? 1.0 : 0.0
    }

    IconButton {
        id: moreButton
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        boxSize: 18
        iconSize: 13
        iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
        iconColor: root.selected ? Theme.color.primaryButtonText
            : (moreButton.hovered ? Theme.color.textPrimary : Theme.color.textCaption)
        opacity: root.showMeta ? 1.0 : 0.0
        onClicked: root.menuRequested()
    }
}
