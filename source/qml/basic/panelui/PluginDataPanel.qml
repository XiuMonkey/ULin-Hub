// 右栏「插件数据」块的宿主外壳（PanelBarDockModel 的 kind = pluginData）。
//
// 宿主只提供：标题 + 空态提示 + 一个名为 `pluginData.host` 的容器。
// 内容由扩展 MountInto 进来（原生 QML，数据由扩展 C++ 取）——**这里没有 WebEngine**。

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

        Text {
            width: parent.width
            text: PanelBarDock.activeTitle.length > 0 ? PanelBarDock.activeTitle : "插件数据"
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightMedium
            elide: Text.ElideRight
        }

        // 内容容器：扩展把"这个插件的数据视图"挂到这里
        Column {
            id: host
            width: parent.width
            spacing: Theme.spacing.labelGap
            Component.onCompleted: HostUi.registerItem("pluginData.host", host)
        }

        Text {
            width: parent.width
            visible: host.children.length === 0
            text: "这块由扩展填内容：如果一直空着，说明扩展没有为这个插件提供数据视图。"
            color: Theme.color.textTertiary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            wrapMode: Text.WordWrap
        }
    }
}
