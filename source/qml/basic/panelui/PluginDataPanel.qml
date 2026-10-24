// 右栏「插件数据」块的宿主外壳（PanelBarDockModel kind = pluginData）。
// 只提供容器与空态；内容由扩展挂到 pluginData.host，这里不含 WebEngine。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    Column {
        width: parent.width
        anchors.top: parent.top
        anchors.margins: Theme.spacing.columnClearance
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacing.labelGap

        Column {   // 标题由右栏页签提供
            id: host   // 扩展把数据视图挂到这里
            width: parent.width
            spacing: Theme.spacing.labelGap
            Component.onCompleted: HostUi.registerItem("pluginData.host", host)
        }

        Text {
            width: parent.width
            visible: host.children.length === 0
            text: "选择插件后，这里会显示它的内容。"
            color: Theme.color.textTertiary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            wrapMode: Text.WordWrap
        }
    }
}
