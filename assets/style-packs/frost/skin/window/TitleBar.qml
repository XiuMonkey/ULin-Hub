// 霜玻 · 自绘标题栏
// 契约与内置 skin/window/TitleBar.qml 一致：整块是拖动区（WindowBridge.setCaptionItem），
// 三个按钮各自 addWindowControl 登记；不自己写拖动。
// 造型：完全透明的浮层标题 —— 左侧方标 + 产品名，右侧圆形窗口键（悬停亮一层玻璃）。
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
        text: "ULin Hub"
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body
        font.weight: Theme.font.weightBold
        font.letterSpacing: 0.6
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.windowButtonRightMargin
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Repeater {
            model: ["min", "max", "close"]

            delegate: Item {
                id: button

                required property string modelData
                readonly property string kind: modelData

                readonly property bool hovered: mouseArea.containsMouse
                readonly property bool pressed: mouseArea.pressed

                implicitWidth: 28
                implicitHeight: 28

                readonly property string glyph: kind === "min"
                    ? "−"
                    : (kind === "close" ? "✕" : (WindowBridge.maximized ? "❐" : "□"))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: {
                        if (button.kind === "close" && (button.hovered || button.pressed))
                            return Theme.color.danger
                        if (button.pressed)
                            return Theme.color.iconButtonPressedBg
                        if (button.hovered)
                            return Theme.color.iconButtonHoverBg
                        return "transparent"
                    }
                    border.width: button.hovered ? 1 : 0
                    border.color: Qt.rgba(255, 255, 255, Theme.isDark ? 0.16 : 0.6)
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
