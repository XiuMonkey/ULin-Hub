// Fluent · 自绘标题栏（Win11 caption）
// 契约与内置 skin/window/TitleBar.qml 一致：整块是拖动区（WindowBridge.setCaptionItem），
// 三个按钮各自 addWindowControl 登记；不自己写拖动。
// 造型：46×32 的方形窗口键（贴右缘、无间距，悬停才出整块底色；关闭悬停 Win11 红 #C42B1C），
// 标题 12px 次要色；字形用 Segoe Fluent Icons（Win11 自带）。
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
        color: Theme.color.textSecondary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        font.weight: Font.Normal
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.windowButtonRightMargin
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Repeater {
            model: ["min", "max", "close"]

            delegate: Item {
                id: button

                required property string modelData
                readonly property string kind: modelData

                readonly property bool hovered: mouseArea.containsMouse
                readonly property bool pressed: mouseArea.pressed
                readonly property bool isClose: kind === "close"

                implicitWidth: Theme.size.windowButtonWidth
                implicitHeight: Theme.size.windowButtonHeight

                // Segoe Fluent Icons：E921 最小化 / E922 最大化 / E923 还原 / E8BB 关闭
                readonly property int glyphCode: kind === "min"
                    ? 0xE921
                    : (isClose ? 0xE8BB : (WindowBridge.maximized ? 0xE923 : 0xE922))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: 0          // Win11 窗口键是整块方形高亮，不画圆角
                    color: {
                        if (button.isClose && (button.hovered || button.pressed))
                            return Theme.color.danger
                        if (button.pressed)
                            return Theme.color.iconButtonPressedBg
                        if (button.hovered)
                            return Theme.color.captionHoverBg
                        return "transparent"
                    }
                    Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                }

                Text {
                    anchors.centerIn: parent
                    text: String.fromCharCode(button.glyphCode)
                    font.family: "Segoe Fluent Icons"
                    font.pixelSize: 10
                    color: (button.isClose && (button.hovered || button.pressed))
                        ? "#FFFFFF"
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
