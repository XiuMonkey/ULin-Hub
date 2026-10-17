// 轻提示（旧壳的 toastLabel：「没有更多了」这类）。挂在页面底部居中，show() 后自动淡出。
// 用法：Toast { id: toast; anchors.bottom: parent.bottom }  →  toast.show("没有更多了")

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    property int duration: 1600
    property int horizontalPadding: Theme.spacing.cardPadding
    property int verticalPadding: Theme.spacing.buttonPadY

    readonly property bool showing: opacity > 0

    implicitWidth: bubble.width
    implicitHeight: bubble.height
    opacity: 0
    visible: opacity > 0

    function show(message) {
        text = message
        opacity = 1
        hideTimer.restart()
    }

    function hide() {
        hideTimer.stop()
        opacity = 0
    }

    Behavior on opacity {
        NumberAnimation { duration: Theme.motion.fast }
    }

    Rectangle {
        id: bubble
        anchors.centerIn: parent
        implicitWidth: label.implicitWidth + 2 * root.horizontalPadding
        implicitHeight: label.implicitHeight + 2 * root.verticalPadding
        radius: Theme.radius.pill
        color: Theme.color.toastBg

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: Theme.color.textOnAccent
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }

    Timer {
        id: hideTimer
        interval: root.duration
        onTriggered: root.opacity = 0
    }
}
