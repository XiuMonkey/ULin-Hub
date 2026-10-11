// 主题切换过渡卡片（按原版 ThemeManager::switchTheme 里的 #themeSwitchBody 复刻）
//   360×200 卡片 + radius 20 + shadowFloat；内边距 24/20/24/20、间距 12；
//   40×40 spinner 居中 + themeSwitchLabel = theme_switching；无关闭按钮、无交互。
// 旧壳是"过渡卡片 + 重建主窗口"；QML 壳不重建窗口，切模式后 token 热重载，卡片只闪一下。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: overlay

    Rectangle {
        anchors.fill: parent
        color: "transparent"       // 原版这张卡片不压遮罩，只浮一张卡
    }

    Card {
        id: body
        anchors.centerIn: parent
        width: 360
        height: 200
        radius: Theme.radius.float      // 20（原版 setRadius(20)）
        padding: 0
        shadowEnabled: true

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            anchors.topMargin: 20
            spacing: 12

            BusySpinner {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 40
                height: 40
                lineWidth: 3
                running: overlay.visible
            }

            Text {
                width: parent.width
                text: qsTrId("theme_switching")
                horizontalAlignment: Text.AlignHCenter
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.lead
            }
        }
    }
}
