// 顶栏的「父 ↔ 子」页签切换器（**不是**子代理列表）。
//
// 语义（唯一标准）：
//   · 当前是父会话（普通会话）⇒ **只有一颗**：父会话标题；子代理只能从「子智能体」下拉框进入。
//   · 当前是子代理           ⇒ **两颗**：[父标题][当前子代理标题]，点谁切谁，当前项高亮。
//   · 切回父会话             ⇒ 立刻收敛成一颗。
//
// 数据全部来自 Hub（引擎真实数据，不自己造）：
//   · 父标题/父 id = Hub.peerRootTitle / Hub.peerRootId（C++ 的 refreshPeerRoot 按**当前会话**
//     重算，父目录晚到也会把组根纠正回来，不会把标签显示错）；
//   · 第二颗 = Hub.peerTabs 里 id 等于当前会话的那一行；目录还没到就用当前标题兜底。
// 点任意一颗 ⇒ selected(id) ⇒ Shell 里接 Hub.selectSession(id)：输入框、消息列表、跟随流一起换，
// 复用既有的那套 selectSession，不新造轮子。
//
// 形状照官方 DSH 的 session-header switcher：12px 字、未选中 label-secondary、选中 label-primary，
// 选中项加一层 chip 底色（不做实心强调色）。最多两颗，放不下时 elide + 顶层 tooltip 给全名。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string rootId: ""
    property string rootTitle: ""
    // 当前页签组的直接子代理：[{ id, label, mode, running }]（这里只用来找"当前子代理"那一行）
    property var peers: []
    property string currentId: ""
    // 当前会话标题：当前子代理不在目录里时，第二颗页签用它兜底（否则只能显示 id）
    property string currentTitle: ""

    signal selected(string sessionId)

    // 页签是"父 ↔ 子"的切换器，不是子代理列表：
    //   · 当前在组根（父会话）上 → 只有父标题一颗，子代理从「子智能体」下拉框进入；
    //   · 当前在某个子代理上     → 父标题 + 该子代理两颗，点谁切谁。
    readonly property bool onSubagent: root.currentId.length > 0
                                       && root.rootId.length > 0
                                       && root.currentId !== root.rootId

    // ⚠️ 每颗要用的"形态/是否当前"都由这里算好塞进数据里，delegate 只读 modelData。
    //    早先 delegate 里写 `asTab: root.onSubagent`：绑定在委托创建的那一刻求值，而那一刻外层 id
    //    还没挂上（实测 warn.log 里一串 `Cannot read property 'onSubagent' of undefined`），
    //    求值失败就**不会登记依赖** ⇒ 之后 onSubagent 变 true 它也永远停在 false（页签永远不长按钮）。
    //    数据在 root 作用域里算就完全没这个问题，而且 tabs 一变 Repeater 会重建委托，形态自然跟上。
    readonly property var tabs: {
        const asTab = root.onSubagent
        const rootTab = {
            id: root.rootId,
            label: root.rootTitle,
            running: false,
            asTab: asTab,
            current: root.currentId === root.rootId
        }
        if (!asTab)
            return [rootTab]

        // 第二颗只放"当前子代理"这一行（不是父会话下的全部子代理）
        const peers = root.peers || []
        for (let i = 0; i < peers.length; ++i) {
            const p = peers[i]
            if (p && p.id === root.currentId) {
                return [rootTab, {
                    id: p.id,
                    label: p.label && p.label.length > 0 ? p.label : p.id,
                    running: p.running === true,
                    asTab: true,
                    current: true
                }]
            }
        }
        // 父目录还没到（冷父会话）：用当前标题兜底，别让用户看不出"现在在哪"
        return [rootTab, {
            id: root.currentId,
            label: root.currentTitle.length > 0 ? root.currentTitle : root.currentId,
            running: false,
            asTab: true,
            current: true
        }]
    }
    readonly property real gap: Theme.spacing.buttonGap
    // 最多两颗：单颗占满整行，两颗各分一半（放不下时靠 elide 收，不换行、不藏起来）
    readonly property real maxTabWidth: Math.max(72, (width - gap * (tabs.length - 1)) / tabs.length)
    readonly property int tabHeight: Theme.size.selectorChipHeight + 4

    implicitWidth: contentRow.implicitWidth
    implicitHeight: tabHeight
    clip: true

    Row {
        id: contentRow

        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.gap

        Repeater {
            model: root.tabs

            delegate: Item {
                id: tab

                required property var modelData
                required property int index

                // 形态与"是不是当前项"都来自数据（见 root.tabs 的说明）：**不要**在这里读 root.xxx
                // 做绑定 —— 委托创建时外层 id 可能还没挂上，绑定会永久停在初始值。
                readonly property bool asTab: modelData.asTab === true
                readonly property bool isCurrent: modelData.current === true
                readonly property bool isTruncated: tabLabel.truncated
                // running 点占掉的宽度（只有页签形态才留）
                readonly property real dotExtra: modelData.running === true ? 10 : 0

                width: tab.asTab
                    ? Math.min(tabLabel.implicitWidth + 2 * Theme.spacing.labelGap + tab.dotExtra,
                               root.maxTabWidth)
                    : Math.min(tabLabel.implicitWidth, root.maxTabWidth)
                height: root.tabHeight

                Rectangle {
                    anchors.fill: parent
                    visible: tab.asTab
                    radius: Theme.radius.chip
                    color: tab.isCurrent ? Theme.color.activeBg
                        : (tabMouse.containsMouse ? Theme.color.chipHoverBg : "transparent")

                    Behavior on color {
                        ColorAnimation { duration: Theme.motion.fast }
                    }
                }

                Row {
                    id: labelRow

                    anchors.verticalCenter: parent.verticalCenter
                    // 页签居中（chip 底要均匀）；标题形态左对齐 —— 它的左缘要与下面那排
                    // 「对话 / 轨迹」、输入框保持同一条竖线，多出 8px 内缩会看着错位。
                    x: tab.asTab ? (tab.width - width) / 2 : 0
                    spacing: 4

                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        visible: tab.modelData.running === true
                        color: Theme.color.accent
                    }

                    Text {
                        id: tabLabel

                        anchors.verticalCenter: parent.verticalCenter
                        width: tab.asTab
                            ? Math.min(implicitWidth, tab.width - 2 * Theme.spacing.labelGap - tab.dotExtra)
                            : Math.min(implicitWidth, tab.width)
                        text: tab.modelData.label
                        elide: Text.ElideRight
                        color: (!tab.asTab || tab.isCurrent) ? Theme.color.textPrimary
                                                             : Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        // 标题形态与 TopBar 里那颗普通标题（titleText）逐项对齐：14px / medium
                        font.pixelSize: tab.asTab ? Theme.font.caption : Theme.font.title
                        font.weight: (!tab.asTab || tab.isCurrent) ? Theme.font.weightMedium : Font.Normal

                        Behavior on color {
                            ColorAnimation { duration: Theme.motion.fast }
                        }
                    }
                }

                MouseArea {
                    id: tabMouse

                    anchors.fill: parent
                    // 标题形态不接收点击（否则手型 + 悬停底色会让人以为它是按钮）
                    enabled: tab.asTab
                    hoverEnabled: tab.asTab
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(tab.modelData.id)
                }

                // 顶层 tooltip（AppToolTip 自己把气泡挂到窗口 contentItem，不会被卡片裁掉）
                AppToolTip {
                    text: tab.modelData.label
                    visible: tab.asTab && tabMouse.containsMouse && tab.isTruncated
                }
            }
        }
    }
}
