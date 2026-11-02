// 侧栏底部工具条（原型 styles.css 的 .sb-footer）：44px 高、顶部一条 hairline。
// 左边账户头像（原型是字母占位；宿主当前没有账户资料 API，字母由调用方传入），
// 右边设置 / 插件 / 主题 / 扩展 四个 SVG 图标，再接扩展停靠位（sessionBar.extDock 仍由宿主登记）
// 与初始化转圈，最后是"更多"（打开 SessionOverflowMenu，危险动作收在这里）。
// 纯视觉组件：不直接调 Hub，所有动作都以信号抛给 SessionBar，连接表/HostUi 契约保持不变。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property bool busy: false   // 初始化转圈（Hub.initializing）：必须门控 running，否则不可见时也在每帧改 opacity 把场景标脏
    property string accountInitial: ""
    property alias accountAnchor: avatar
    signal accountRequested()

    signal settingsRequested()
    signal pluginsRequested()
    signal themeRequested()
    signal extensionsRequested()
    signal moreRequested()

    // 底栏动作按钮总数上限（不含「更多」按钮）：内置 4 个 + 扩展入口。
    // 超过 6 个时，第 7 个起收进「更多」上拉框；扩展入口的可用内联数量 = 6 - 内置数量。
    property int totalInlineLimit: 6
    property int fixedButtonCount: 4
    readonly property int inlineExtLimit: Math.max(0, totalInlineLimit - fixedButtonCount)
    property var overflowEntries: []

    function extMetaSource(item) {
        // 宿主 mountInto 会在扩展根外面套一层 Item；元数据要从最内层根上取。
        var current = item
        for (var depth = 0; depth < 4 && current; ++depth) {
            if (current.iconSource !== undefined || current.tooltip !== undefined
                    || current.label !== undefined || current.description !== undefined)
                return current
            var kids = current.children
            if (!kids || kids.length === 0)
                break
            current = kids[0]
        }
        return item
    }

    function extString(item, keys) {
        var source = extMetaSource(item)
        for (var i = 0; i < keys.length; ++i) {
            var value = source ? source[keys[i]] : undefined
            if (typeof value === "string" && value.length > 0)
                return value
        }
        return ""
    }

    function extDescription(item) {
        var source = extMetaSource(item)
        if (!source)
            return ""
        var value = source.description !== undefined ? source.description : source.subtitle
        return typeof value === "string" ? value : ""
    }

    function syncExtensionOverflow() {
        if (!extDockRow)
            return

        var overflow = []
        var children = extDockRow.children
        for (var i = 0; i < children.length; ++i) {
            var child = children[i]
            if (!child)
                continue
            if (i < inlineExtLimit) {
                child.visible = true
                continue
            }
            child.visible = false
            overflow.push({
                item: child,
                iconSource: extString(child, ["iconSource", "icon", "source"]),
                label: extString(child, ["tooltip", "label", "text", "title"]),
                description: extDescription(child)
            })
        }

        // 被隐藏的入口也必须纳入可见计数：前六项始终显示。
        overflowEntries = overflow
    }

    // 扩展停靠位：宿主（SessionBar）在 Component.onCompleted 里登记为 "sessionBar.extDock"。
    // 仍是一个 Row（位置器）⇒ 扩展注入的按钮自动排在末尾，扩展不用写 x/y。
    property alias extDock: extDockRow

    implicitHeight: 44

    Rectangle {   // 顶部 hairline：原版侧栏底排与列表之间的分隔线
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.color.border
    }

    Rectangle {   // 账户头像：26px 圆；深色主题里原型是深字、浅色是白字 ⇒ 正好是 textOnAccent 的两套值
        id: avatar
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 26
        radius: width / 2

        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.color.accent }
            GradientStop {   // 原型 135° 蓝→紫；紫端只做色相偏移，不新增语义色 token
                position: 1.0
                color: Qt.hsla((Theme.color.accent.hsvHue + 0.17) % 1, 0.62, 0.68, 1.0)
            }
        }

        Text {
            anchors.centerIn: parent
            text: root.accountInitial
            color: Theme.color.textOnAccent
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.small
            font.weight: Theme.font.weightBold
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.accountRequested()
        }
    }

    component FooterIconButton: IconButton {   // 底排图标按钮：尺寸与常态色一致，只有图标/提示/动作不同 —— 用内联组件收口，避免 5 份重复
        boxSize: 27
        iconSize: 15
        iconColor: Theme.color.textTertiary
    }

    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        FooterIconButton {
            iconSource: "qrc:/ULinHub/icons/ui/settings.svg"
            tooltip: qsTrId("settings_title")
            onClicked: root.settingsRequested()
        }
        FooterIconButton {
            iconSource: "qrc:/ULinHub/icons/ui/plugin.svg"
            tooltip: qsTrId("sessionBar_plugins")
            onClicked: root.pluginsRequested()
        }
        FooterIconButton {
            iconSource: "qrc:/ULinHub/icons/ui/theme.svg"
            tooltip: qsTrId("sessionBar_switch_theme")
            onClicked: root.themeRequested()
        }
        FooterIconButton {
            iconSource: "qrc:/ULinHub/icons/ui/extensions.svg"
            tooltip: qsTrId("ext_manager_title")
            onClicked: root.extensionsRequested()
        }

        Row {   // 扩展入口：没有扩展注入时宽度为 0（见 EXTENSION_DEV.zh-CN.md §11.3）
            id: extDockRow
            spacing: 0
            onChildrenChanged: root.syncExtensionOverflow()
        }

        Item {   // 初始化转圈：塞进同尺寸的盒子，Row 里才不会把后面的按钮顶偏（不转时宽度收成 0）
            width: root.busy ? 20 : 0
            height: 27
            visible: root.busy
            BusySpinner {
                anchors.centerIn: parent
                width: 16
                height: 16
                lineWidth: 2
                running: root.busy
            }
        }

        FooterIconButton {
            iconSource: "qrc:/ULinHub/icons/ui/more-horizontal.svg"
            tooltip: qsTrId("sessionBar_more")
            onClicked: root.moreRequested()
        }
    }
}
