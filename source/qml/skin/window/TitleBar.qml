// 自绘标题栏：左侧图标 + 产品名，右侧三个窗口按钮。
// 整块由 WindowBridge.setCaptionItem 登记为窗口拖动区（原生 WM_NCHITTEST 会把它判成 HTCAPTION），
// 所以这里**不要**自己写拖动逻辑：双击最大化、贴边吸附、右键系统菜单都由系统给。
// 三个按钮由本文件自己处理点击（WindowBridge.nativeWindowButtons 默认 false）：交给系统走非客户区
// 很多次才生效。按钮因此登记为窗口控件（不算拖动区），点击/悬停/按下都在这里画。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: bar

    // 扩展注入点：扩展用 HostUi.item("titleBar") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("titleBar", this)

    implicitHeight: Theme.size.titleBarHeight

    Image {
        id: logo
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: buttons.verticalCenter   // 与右侧按钮行垂直居中对齐
        width: Theme.size.logoHeight
        height: Theme.size.logoHeight
        source: Theme.isDark   // 深色底用反色版（同目录 PNG 逐像素取反），与现有实现 TitleBar.cpp 一致
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
        text: "ULin Hub"   // 产品名不翻译（各语言写法一致），保持一致
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.body   // 标题栏产品名在 main-window.qss 里是 13px（= body 档），不是面板标题的 14px
        font.weight: Theme.font.weightBold
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: Theme.size.windowButtonRightMargin
        anchors.top: parent.top
        anchors.topMargin: Theme.size.windowButtonTopMargin
        spacing: 0

        Repeater {
            model: ["min", "max", "close"]

            delegate: Item {
                id: button

                required property string modelData
                readonly property string kind: modelData

                readonly property bool hovered: mouseArea.containsMouse
                readonly property bool pressed: mouseArea.pressed

                implicitWidth: Theme.size.windowButtonWidth
                implicitHeight: Theme.size.windowButtonHeight

                readonly property string iconName: kind === "min" ? "window-minimize"
                    : (kind === "close" ? "window-close"
                        : (WindowBridge.maximized ? "window-restore" : "window-maximize"))

                Component.onCompleted: WindowBridge.addWindowControl(button, kind)   // 命中测试约定：登记后这些 item 不算标题栏拖动区（否则鼠标落在按钮上会被当成拖动）
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.small   // 三个窗口按钮在 main-window.qss 里是 6px 圆角（= small 档，不是通用控件的 control 8px）
                    color: {
                        if (button.kind === "close") {
                            const danger = Theme.color.danger
                            if (button.pressed)
                                return Qt.rgba(danger.r, danger.g, danger.b, 0.16)
                            if (button.hovered)
                                return Qt.rgba(danger.r, danger.g, danger.b, 0.09)
                            return "transparent"
                        }
                        if (button.pressed)
                            return Theme.color.iconButtonPressedBg
                        if (button.hovered)
                            return Theme.color.iconButtonHoverBg
                        return "transparent"
                    }
                }

                LineIcon {
                    anchors.centerIn: parent
                    name: button.iconName
                    size: 13
                    color: button.kind === "close"
                        ? ((button.hovered || button.pressed) ? Theme.color.danger : Theme.color.textSecondary)
                        : ((button.hovered || button.pressed) ? Theme.color.textPrimary : Theme.color.textSecondary)
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
