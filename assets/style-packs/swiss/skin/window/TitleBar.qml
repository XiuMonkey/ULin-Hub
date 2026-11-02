// 苏黎世 · 自绘标题栏
// 契约与内置 skin/window/TitleBar.qml 一致：整块是拖动区，三个按钮各自 addWindowControl。
// 造型：左对齐的粗黑产品名 + 一枚红方块；右侧方形窗口键，底沿 2px 黑线（版面基线）。
import QtQuick
import ULinHub 1.0

Item {
    id: bar

    Component.onCompleted: HostUi.registerItem("titleBar", this)

    implicitHeight: Theme.size.titleBarHeight

    Rectangle {
        id: mark
        anchors.left: parent.left
        anchors.leftMargin: Theme.size.titleBarPadLeft
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        color: Theme.color.accent
    }

    Text {
        id: titleText
        anchors.left: mark.right
        anchors.leftMargin: 10
        anchors.verticalCenter: mark.verticalCenter
        text: "ULin Hub"
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.title
        font.weight: Theme.font.weightBold
        font.letterSpacing: -0.2
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
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

                implicitWidth: 32
                implicitHeight: 26

                readonly property string glyph: kind === "min"
                    ? "−"
                    : (kind === "close" ? "✕" : (WindowBridge.maximized ? "❐" : "□"))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: 0
                    color: {
                        if (button.kind === "close" && (button.hovered || button.pressed))
                            return Theme.color.danger
                        if (button.pressed)
                            return Theme.color.primaryButtonBg
                        if (button.hovered)
                            return Theme.color.hoverBg
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
                        : (button.pressed ? Theme.color.primaryButtonText : Theme.color.textPrimary)
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
