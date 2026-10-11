// 自绘标题栏：左侧图标 + 产品名，右侧三个窗口按钮。
//
// 整块由 WindowBridge.setCaptionItem 登记为窗口拖动区（原生 WM_NCHITTEST 会把它判成 HTCAPTION），
// 所以这里**不要**自己写拖动逻辑：双击最大化、贴边吸附、右键系统菜单都由系统给。
//
// 三个按钮由本文件自己处理点击（WindowBridge.nativeWindowButtons 默认 false）：交给系统走非客户区
// 按钮那条路时 Windows 要在"按下→移动→抬起"的模态循环里判定，而 Qt 会插手鼠标捕获，实测关闭要按
// 很多次才生效。按钮因此登记为窗口控件（不算拖动区），点击/悬停/按下都在这里画。
//
// 颜色与尺寸全部取 Theme token；窗口按钮的几何来自 metrics.json 的 size.windowButton*。

import QtQuick
import ULinHub 1.0

Item {
    id: bar

    // 扩展注入点：扩展用 HostUi.item("titleBar") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("titleBar", this)

    implicitHeight: Theme.size.titleBarHeight

    Image {
        id: logo
        anchors.left: parent.left
        anchors.leftMargin: 14
        // 与右侧按钮行垂直居中对齐（旧壳是"标题栏内容带里居中"，按钮行高度就是那条带）
        anchors.verticalCenter: buttons.verticalCenter
        width: Theme.size.logoHeight
        height: Theme.size.logoHeight
        // 深色底用反色版（同目录 PNG 逐像素取反），与旧壳 TitleBar.cpp 一致
        source: Theme.isDark
            ? "qrc:/ULinHub/ulin-hub-icon-dark.png"
            : "qrc:/ULinHub/ulin-hub-icon.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Text {
        id: titleText
        anchors.left: logo.right
        anchors.leftMargin: Theme.size.logoTextGap
        anchors.verticalCenter: logo.verticalCenter
        // 产品名不翻译（各语言写法一致），与旧壳一致
        text: "ULin Hub"
        color: Theme.color.textPrimary
        font.family: "Microsoft YaHei UI"
        // 标题栏产品名在 main-window.qss 里是 13px（= body 档），不是面板标题的 14px
        font.pixelSize: Theme.font.body
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

                // 字形与旧壳一致（Segoe UI Symbol：− / □ / ❐ / ✕）
                readonly property string glyph: kind === "min"
                    ? "−"
                    : (kind === "close" ? "✕" : (WindowBridge.maximized ? "❐" : "□"))

                // 命中测试约定：登记后这些 item 不算标题栏拖动区（否则鼠标落在按钮上会被当成拖动）
                Component.onCompleted: WindowBridge.addWindowControl(button, kind)
                Component.onDestruction: WindowBridge.removeWindowControl(button)

                Rectangle {
                    anchors.fill: parent
                    // 三个窗口按钮在 main-window.qss 里是 6px 圆角（= small 档，不是通用控件的 control 8px）
                    radius: Theme.radius.small
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
