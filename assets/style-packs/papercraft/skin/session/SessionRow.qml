// 素纸 · 会话行
// 契约与内置 skin/session/SessionRow.qml 一致：
//   title / running / selected / timeText + selectRequested / deleteRequested / menuRequested
// 造型：直角行、选中时左沿一条贯通墨条 + 标题加粗、悬停铺一层薄纸灰；运行中仍是呼吸圆点。
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

    QtObject {
        id: metaFade
        property real value: root.showMeta ? 1 : 0
        Behavior on value {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 0
        color: root.selected ? Theme.color.activeBg : Theme.color.hoverBg
        opacity: metaFade.value
    }

    // 选中墨条：贯通整行（印刷品的"侧标"）
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
        anchors.leftMargin: 10
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
            id: dot
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
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
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
        iconColor: moreButton.hovered ? Theme.color.textPrimary : Theme.color.textCaption
        opacity: metaFade.value
        onClicked: root.menuRequested()
    }
}
