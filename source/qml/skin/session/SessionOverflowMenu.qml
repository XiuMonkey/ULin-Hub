// 侧栏"更多"溢出菜单（原型右下角 ⋯ 的落点）：危险动作与低频动作不再常驻首屏。
// 三项：
// 新建工作区  —— 原顶部全宽按钮降级到这里（Hub.createWorkspace()）
// 已归档会话  —— 原型里的占位项，功能未实现，置灰不可点
// 清空会话    —— danger；不直连 Hub，发 clearSessionsRequested() 走连接表（扩展可接管）

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Popup {
    id: root

    width: extensionEntries.length > 0 ? 224 : 176
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 由 SessionFooter 传入：第 7 个及之后的扩展入口。每项形如
    // { item, iconSource, label, description }，点击时调用原入口的 clicked 信号。
    property var extensionEntries: []

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

    function invokeExtensionItem(item, depth) {
        if (!item || depth > 4)
            return false
        if (typeof item.clicked === "function") {
            item.clicked()
            return true
        }
        if (typeof item.trigger === "function") {
            item.trigger()
            return true
        }
        if (typeof item.invoke === "function") {
            item.invoke()
            return true
        }
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i) {
            if (invokeExtensionItem(kids[i], depth + 1))
                return true
        }
        return false
    }

    function invokeExtension(entry) {
        if (!entry)
            return
        invokeExtensionItem(entry.item, 0)
        root.close()
    }

    contentItem: Column {
        id: menuColumn
        width: root.availableWidth
        spacing: 0

        // 扩展溢出入口：左边图标，右边标题 + 描述；点击复用原入口的 clicked 信号。
        Repeater {
            model: root.extensionEntries
            delegate: Item {
                id: extItem

                required property var modelData
                readonly property bool hasDescription: modelData.description !== undefined
                    && String(modelData.description).length > 0

                width: menuColumn.width
                height: 44

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.small
                    color: extMouse.containsMouse ? Theme.color.activeBg : "transparent"
                }

                IconButton {
                    width: 24
                    height: 24
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    boxSize: 24
                    iconSize: 17
                    iconSource: modelData.iconSource && modelData.iconSource.length > 0
                        ? modelData.iconSource
                        : "qrc:/ULinHub/icons/ui/extensions.svg"
                    iconColor: Theme.color.textSecondary
                    hoverBackground: "transparent"
                    pressedBackground: "transparent"
                    enabled: false
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 38
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1

                    Text {
                        width: parent.width
                        text: modelData.label && String(modelData.label).length > 0
                            ? String(modelData.label)
                            : qsTrId("sessionBar_extension_entry")
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: extItem.hasDescription
                        text: extItem.hasDescription ? String(modelData.description) : ""
                        color: Theme.color.textCaption
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: extMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.invokeExtension(extItem.modelData)
                }
            }
        }

        Rectangle {   // 扩展入口与低频动作之间
            visible: root.extensionEntries.length > 0
            height: 9
            width: parent.width
            color: "transparent"
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                height: 1
                color: Theme.color.border
            }
        }

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
