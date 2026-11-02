// 素纸 · 自绘标题栏
// 契约与内置 skin/window/TitleBar.qml 一致：整块被 WindowBridge.setCaptionItem 登记为拖动区，
// 三个按钮各自 addWindowControl 登记（否则鼠标落在按钮上会被当成拖窗口）；不要自己写拖动逻辑。
// 造型：居中版式标题（字距拉开）+ 左侧方标 + 右侧方角窗口键，底沿一条墨线。
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
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        text: "U L I N   H U B"
        color: Theme.color.textSecondary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        font.weight: Theme.font.weightMedium
        font.letterSpacing: 1.2
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.color.border
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

                implicitWidth: Theme.size.windowButtonWidth - 8
                implicitHeight: Theme.size.windowButtonHeight - 6

                readonly property string glyph: kind === "min"
                    ? "−"
                    : (kind === "close" ? "✕" : (WindowBridge.maximized ? "❐" : "□"))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: {
                        if (button.kind === "close") {
                            if (button.pressed)
                                return Theme.color.danger
                            if (button.hovered)
                                return Theme.color.dangerButtonBg
                            return "transparent"
                        }
                        if (button.pressed)
                            return Theme.color.primaryButtonPressed
                        if (button.hovered)
                            return Theme.color.captionHoverBg
                        return "transparent"
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: button.glyph
                    font.family: "Segoe UI Symbol"
                    font.pixelSize: Theme.font.glyph
                    color: (button.kind === "close" && (button.hovered || button.pressed))
                        ? Theme.color.textOnAccent
                        : (button.hovered ? Theme.color.textPrimary : Theme.color.textSecondary)
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
