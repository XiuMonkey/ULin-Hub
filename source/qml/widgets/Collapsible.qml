// 可折叠块：表头（可点、有悬停底、带折角字形）+ 可动画展开的正文。
// 用于思考/工具卡（旧壳 agentThinkCard/agentToolCard）与目录分组表头；展开态由调用方持有
// （流式重建时不丢状态 —— 这是旧壳 AgentMessageUnit 明确要求的行为）。

import QtQuick
import ULinHub 1.0

Column {
    id: root

    property string title: ""
    property string meta: ""            // 表头右侧小灰字（计数/状态）
    property string summary: ""         // 折叠时显示的正文摘要（自动压成一行、右侧省略）
    property bool expanded: false
    property bool showChevron: true
    property color headerColor: "transparent"

    default property alias body: bodyHolder.data

    readonly property bool hovered: headerArea.containsMouse

    // 先截断再压平：换行/制表/连续空格一律并成单空格，否则一行放不下（也免得对长正文跑正则）
    readonly property string summaryLine: summary.substr(0, 600).trim().replace(/\s+/g, " ")

    // 宽度由调用方给（part 文件里一般是 anchors.fill 或 width: parent.width）

    // ---- 表头 ----
    Item {
        id: header
        width: root.width
        implicitHeight: Math.max(Theme.size.directoryHeaderMinHeight - 8, 28)

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius.small
            color: headerArea.containsMouse ? Theme.color.hoverBg : root.headerColor
        }

        Text {
            id: chevron
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.labelGap
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showChevron
            text: "▾"
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
            rotation: root.expanded ? 0 : -90
            Behavior on rotation {
                NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
            }
        }

        Text {
            id: titleText
            anchors.left: chevron.visible ? chevron.right : parent.left
            anchors.leftMargin: chevron.visible ? Theme.spacing.labelGap : Theme.spacing.labelGap
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width - x - metaText.width - 3 * Theme.spacing.labelGap)
            text: root.title
            color: Theme.color.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
            font.weight: Theme.font.weightMedium
            elide: Text.ElideRight
        }

        // 折叠态的正文摘要：跟在标题后被推着 elide；展开后正文自身可见，不再重复一遍
        Text {
            id: summaryText
            objectName: "summaryText"
            anchors.left: titleText.right
            anchors.right: metaText.left
            anchors.leftMargin: Theme.spacing.labelGap
            anchors.rightMargin: Theme.spacing.labelGap
            anchors.verticalCenter: parent.verticalCenter
            visible: root.summaryLine.length > 0 && !root.expanded && width > 24
            text: root.summaryLine
            color: Theme.color.textCaption
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            id: metaText
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing.labelGap
            anchors.verticalCenter: parent.verticalCenter
            text: root.meta
            color: Theme.color.textCaption
            font.pixelSize: Theme.font.caption
        }

        MouseArea {
            id: headerArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expanded = !root.expanded
        }
    }

    // ---- 正文（展开/收起用高度动画；clip 免得动画期间溢出）----
    Item {
        id: bodyWrapper
        width: root.width
        clip: true
        implicitHeight: root.expanded ? bodyHolder.implicitHeight : 0
        Behavior on implicitHeight {
            NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutCubic }
        }

        Column {
            id: bodyHolder
            width: parent.width
            spacing: Theme.spacing.labelGap
        }
    }
}
