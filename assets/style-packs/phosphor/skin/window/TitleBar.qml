// 荧光屏 · 自绘标题栏
// 契约与内置 skin/window/TitleBar.qml 一致：整块是拖动区，三个按钮各自 addWindowControl。
// 造型：终端提示行 —— `▚ ULin Hub`，等宽字、字距拉开，右侧直角窗口键；底沿一条磷光细线。
import QtQuick
import ULinHub 1.0

Item {
    id: bar

    Component.onCompleted: HostUi.registerItem("titleBar", this)

    implicitHeight: Theme.size.titleBarHeight

    Image {
        id: logo
        anchors.left: parent.left
        anchors.leftMargin: Theme.size.titleBarPadLeft
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.size.logoHeight
        height: Theme.size.logoHeight
        source: Theme.isDark
            ? "qrc:/ULinHub/icons/ulin-hub-icon-dark.png"
            : "qrc:/ULinHub/icons/ulin-hub-icon.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Text {
        id: titleText
        anchors.left: logo.right
        anchors.leftMargin: Theme.size.logoTextGap
        anchors.verticalCenter: logo.verticalCenter
        text: "▚ ULin Hub"
        color: Theme.color.accent
        font.family: "Consolas"
        font.pixelSize: Theme.font.body
        font.letterSpacing: 1.4
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.28)
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.windowButtonRightMargin
        anchors.top: parent.top
        anchors.topMargin: Theme.size.windowButtonTopMargin
        spacing: 2

        Repeater {
            model: ["min", "max", "close"]

            delegate: Item {
                id: button

                required property string modelData
                readonly property string kind: modelData

                readonly property bool hovered: mouseArea.containsMouse
                readonly property bool pressed: mouseArea.pressed

                implicitWidth: 26
                implicitHeight: 24

                readonly property string glyph: kind === "min"
                    ? "–"
                    : (kind === "close" ? "✕" : (WindowBridge.maximized ? "▣" : "□"))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: 0
                    color: {
                        if (button.kind === "close") {
                            if (button.pressed)
                                return Theme.color.danger
                            if (button.hovered)
                                return Theme.color.dangerButtonBg
                            return "transparent"
                        }
                        if (button.pressed)
                            return Theme.color.activeBg
                        if (button.hovered)
                            return Theme.color.hoverBg
                        return "transparent"
                    }
                    border.width: button.hovered ? 1 : 0
                    border.color: button.kind === "close" ? Theme.color.danger : Theme.color.border
                }

                Text {
                    anchors.centerIn: parent
                    text: button.glyph
                    font.family: "Consolas"
                    font.pixelSize: Theme.font.glyph
                    color: (button.kind === "close" && (button.hovered || button.pressed))
                        ? Theme.color.textOnAccent
                        : (button.hovered ? Theme.color.accent : Theme.color.textSecondary)
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.ArrowCursor
                    onClicked: {
                        if (button.kind === "min")
                            WindowBridge.minimize()
                        else if (button.kind === "max")
                            WindowBridge.toggleMaximize()
                        else
                            WindowBridge.closeWindow()
                    }
                }
            }
        }
    }
}
