// ULin Core · 数据：本地设置/会话/工作目录的路径与恢复默认。
import QtQuick
import ULinHub 1.0
import "../../../widgets"

Item {
    id: page

    readonly property var cfg: Hub.settings.ulincoreBridge   // 自研内核配置桥：路径、状态与打开/恢复操作都从它来

    Column {
        anchors.fill: parent
        spacing: 8

        Text {
            text: "数据"
            color: Theme.color.textPrimary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.title
            font.weight: Theme.font.weightBold
        }

        Text {
            width: parent.width
            text: "会话记录、模型配置和工作目录都在 data/ulincore 下。"
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        Text {
            text: "设置文件"
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }
        Text {
            width: parent.width
            text: page.cfg.settingsPath
            wrapMode: Text.WrapAnywhere
            color: Theme.color.textPrimary
            font.family: Theme.font.monoFamily
            font.pixelSize: Theme.font.small
        }

        Text {
            text: "数据目录"
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }
        Text {
            width: parent.width
            text: page.cfg.dataDir
            wrapMode: Text.WrapAnywhere
            color: Theme.color.textPrimary
            font.family: Theme.font.monoFamily
            font.pixelSize: Theme.font.small
        }

        Text {
            width: parent.width
            text: "会话账本：data/ulincore/sessions/<sessionId>.json\n默认工作区：data/ulincore/launch-root"
            wrapMode: Text.WordWrap
            color: Theme.color.textTertiary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }

        // 三个操作都直接调桥接，不经过本地草稿；反馈写回 cfg.status。
        Row {
            spacing: 8

            IconButton {
                label: "打开数据目录"
                onClicked: page.cfg.openDataDir()
            }

            IconButton {
                label: "重新读取"
                onClicked: page.cfg.reload()
            }

            IconButton {
                label: "恢复默认"
                onClicked: page.cfg.resetToDefaults()
            }
        }

        Text {
            text: page.cfg.status
            color: Theme.color.textSecondary
            font.family: Theme.font.uiFamily
            font.pixelSize: Theme.font.caption
        }
    }
}
