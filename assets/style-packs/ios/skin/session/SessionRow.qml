// iOS 玻璃 · 会话行
// ---------------------------------------------------------------------------
// 契约与内置 skin/session/SessionRow.qml 一致（title / running / selected / timeText +
// selectRequested / deleteRequested / menuRequested）。
//
// 造型：iOS 侧边栏的胶囊选中 —— 悬停铺一层 systemFill 灰，选中换成 systemBlue 的淡色
// 胶囊并且字变蓝（Apple 自家的侧边栏就是靠"整颗胶囊染色"表达选中，不是加左边条）。
// 没有分割线：iOS 的列表靠留白分组，分割线只在真表格里出现。
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

    implicitHeight: 36

    readonly property bool hovered: rowMouse.containsMouse || moreButton.hovered
    readonly property bool showMeta: hovered || selected

    // 选中/悬停的胶囊：比行本身内收 2px，行与行之间才有"呼吸"
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: height / 2
        color: root.selected
            ? Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.16)
            : Theme.color.hoverBg
        opacity: root.selected ? 1.0 : (root.hovered ? 0.9 : 0.0)
        border.width: 0

        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    QtObject {
        id: metaFade
        property real value: root.showMeta ? 1 : 0
        Behavior on value {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
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
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        visible: root.running

        Rectangle {
            anchors.centerIn: parent
            width: 14
            height: 14
            radius: 7
            color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.16)
        }

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: Theme.color.accent

            SequentialAnimation on opacity {
                running: root.running
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.40; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.40; to: 1.0; duration: 900; easing.type: Easing.InOutSine }
            }
        }
    }

    Text {
        id: titleText
        anchors.left: runningDot.right
        anchors.leftMargin: 9
        anchors.right: root.timeText.length > 0 ? timeText.left : moreButton.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: root.selected ? Theme.color.accent
            : (root.hovered ? Theme.color.textPrimary : Theme.color.textSecondary)
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
        font.weight: root.selected ? 600 : Font.Normal
        elide: Text.ElideRight

        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    Text {
        id: timeText
        anchors.right: moreButton.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: root.timeText.length > 0
        text: root.timeText
        color: Theme.color.textCaption
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 11
        opacity: metaFade.value
    }

    IconButton {
        id: moreButton
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        boxSize: 20
        iconSize: 13
        iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
        iconColor: moreButton.hovered ? Theme.color.textPrimary : Theme.color.textCaption
        opacity: metaFade.value
        onClicked: root.menuRequested()
    }
}
