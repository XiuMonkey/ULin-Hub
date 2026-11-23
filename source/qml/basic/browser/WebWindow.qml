// 通用兜底落点：网页窗口（宿主自带 WebEngine 打开任意 URL），由 VirtualUiHost::OpenWebWindow 创建。
// 插件界面本是 web 的、原生表达不了（或代价不划算）时用它；宿主不认识插件，只知道"给一个 URL，还一个能前进后退刷新的窗口"。

import QtQuick
import QtQuick.Controls.Basic
import QtWebEngine
import ULinHub 1.0

Window {
    id: win

    required property url initialUrl   // 必需属性：创建时由宿主注入初始地址，本窗口没有别的入口

    width: Math.max(720, Math.min(1280, Math.round(Screen.width * 0.8)))
    height: Math.max(520, Math.min(900, Math.round(Screen.height * 0.82)))
    minimumWidth: 640
    minimumHeight: 420
    title: view.title.length > 0 ? view.title : "网页"
    color: Theme.color.dockBg || Theme.color.panelBg

    Item {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Repeater {
                model: [   // 按钮模型：text 显示、enabled 控制可用、go 交给 onClicked 分发
                    { "text": "←", "enabled": view.canGoBack, "go": "back" },
                    { "text": "→", "enabled": view.canGoForward, "go": "forward" },
                    { "text": "↻", "enabled": true, "go": "reload" }
                ]

                Rectangle {
                    required property var modelData
                    width: 28
                    height: 26
                    radius: Theme.radius.small
                    color: area.containsMouse && modelData.enabled ? Theme.color.iconButtonHoverBg : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: modelData.text
                        color: modelData.enabled ? Theme.color.textPrimary : Theme.color.textTertiary
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!modelData.enabled)
                                return
                            if (modelData.go === "back")
                                view.goBack()
                            else if (modelData.go === "forward")
                                view.goForward()
                            else
                                view.reload()
                        }
                    }
                }
            }
        }

        Rectangle {   // 地址（只读展示：这是宿主给的落点，不给手输）
            anchors.left: parent.left
            anchors.leftMargin: 112
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 26
            radius: Theme.radius.small
            color: Theme.color.inputBg
            border.width: 1
            border.color: Theme.color.inputBorder

            Text {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter
                text: view.url
                color: Theme.color.textSecondary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
                elide: Text.ElideMiddle
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.color.border
        }
    }

    Rectangle {   // 加载进度条：宽度按 loadProgress 拉
        id: progress
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        height: 2
        width: parent.width * (view.loadProgress / 100)
        color: Theme.color.primaryButtonBg
        visible: view.loading
    }

    WebEngineView {
        id: view
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        url: win.initialUrl
        backgroundColor: Theme.color.dockBg || Theme.color.panelBg
    }
}
