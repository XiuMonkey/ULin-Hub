import QtQuick
import ULinHub 1.0

// Agent 回合的交付文件（DSH `deliverables/presented`）。
// 对齐 Web 版：不加标题、不做大卡片，只用贴合正文宽度的轻量文件行。
Item {
    id: root

    property var items: []
    readonly property int columns: root.width >= 460 ? 2 : 1

    visible: items.length > 0
    implicitHeight: visible ? grid.implicitHeight : 0
    height: implicitHeight

    Flow {
        id: grid
        width: root.width
        spacing: 10

        Repeater {
            model: root.items

            Rectangle {
                id: fileCard
                required property var modelData
                readonly property real cardWidth: root.columns === 1
                    ? grid.width
                    : (grid.width - grid.spacing) / 2

                width: cardWidth
                height: 60
                radius: Theme.radius.panel
                color: fileMouse.containsMouse ? Theme.color.hoverBg : Theme.color.codeBlockBg
                border.width: 0.5
                border.color: Theme.color.border
                clip: true

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 10
                    spacing: 10

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        radius: 10
                        color: Theme.color.panelBg
                        border.width: 0.5
                        border.color: Theme.color.border

                        Text {
                            anchors.centerIn: parent
                            text: modelData.suffix && modelData.suffix.length > 0
                                ? String(modelData.suffix).toUpperCase()
                                : "FILE"
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 8
                            font.weight: Theme.font.weightMedium
                            elide: Text.ElideRight
                            width: parent.width - 6
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - 62)
                        spacing: 2

                        Text {
                            width: parent.width
                            text: modelData.name || modelData.path
                            color: Theme.color.textPrimary
                            elide: Text.ElideMiddle
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            font.weight: Theme.font.weightMedium
                        }
                        Text {
                            width: parent.width
                            text: modelData.description && modelData.description.length > 0
                                ? modelData.description
                                : modelData.path
                            color: Theme.color.textTertiary
                            elide: Text.ElideMiddle
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }
                }

                MouseArea {
                    id: fileMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Hub.openSkillPath(modelData.path)
                }
            }
        }
    }
}
