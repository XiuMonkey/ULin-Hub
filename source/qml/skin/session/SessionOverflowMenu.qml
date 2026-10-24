// 侧栏"更多"溢出菜单（原型右下角 ⋯ 的落点）：危险动作与低频动作不再常驻首屏。
// 三项：
// 新建工作区  —— 原顶部全宽按钮降级到这里（Hub.createWorkspace()）
// 已归档会话  —— 原型里的占位项，功能未实现，置灰不可点
// 清空会话    —— danger；不直连 Hub，发 clearSessionsRequested() 走连接表（扩展可接管）

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Popup {
    id: root

    width: 176
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    signal clearSessionsRequested()
    signal newWorkspaceRequested()

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.fast }
    }

    function activate(kind) {   // "已归档会话"是置灰占位项（enabled=false，点击不触发）⇒ 这里只有 workspace / clear 两条活路
        if (kind === "workspace")
            root.newWorkspaceRequested()
        else if (kind === "clear")
            root.clearSessionsRequested()
        else
            return
        root.close()
    }

    background: Rectangle {
        radius: Theme.radius.control
        color: Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder
    }

    contentItem: Column {
        id: menuColumn
        width: root.availableWidth
        spacing: 0

        Repeater {
            model: [
                { kind: "workspace", label: qsTrId("sessionBar_new_workspace"), danger: false, enabled: true },
                { kind: "archived", label: qsTrId("sessionBar_archived"), danger: false, enabled: false },
                { kind: "separator", label: "", danger: false, enabled: false },
                { kind: "clear", label: qsTrId("sessionBar_clear_sessions"), danger: true, enabled: true }
            ]

            delegate: Item {
                id: menuItem

                required property var modelData
                readonly property bool isSeparator: modelData.kind === "separator"

                width: menuColumn.width
                height: menuItem.isSeparator ? 9 : 30

                Rectangle {
                    anchors.fill: parent
                    visible: !menuItem.isSeparator
                    radius: Theme.radius.small
                    color: (mouse.containsMouse && menuItem.modelData.enabled)
                        ? Theme.color.activeBg
                        : "transparent"
                }

                Rectangle {   // 危险项与普通项之间的分隔线
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    height: 1
                    visible: menuItem.isSeparator
                    color: Theme.color.border
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !menuItem.isSeparator
                    text: menuItem.modelData.label
                    color: menuItem.modelData.danger
                        ? (Theme.color.dangerText || Theme.color.danger)
                        : (menuItem.modelData.enabled ? Theme.color.textPrimary : Theme.color.textCaption)
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: menuItem.modelData.enabled === true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.activate(menuItem.modelData.kind)
                }
            }
        }
    }
}
