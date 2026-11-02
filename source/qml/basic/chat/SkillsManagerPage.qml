// Agent 过滤器里的技能页：只读官方 skills/list 目录，点击打开 SKILL.md。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    readonly property var skills: Hub.skills
    readonly property bool busy: Hub.skillsBusy
    readonly property string status: Hub.skillsStatus

    property var selectedSkill: null
    property string detailText: ""
    property bool detailFailed: false

    function openSkill(skill) {
        if (!skill || skill.path.length === 0)
            return
        selectedSkill = skill
        detailFailed = false
        detailText = Hub.readSkillText(skill.path)
        if (detailText.length === 0)
            detailFailed = true
    }

    function closeSkill() {
        selectedSkill = null
        detailText = ""
        detailFailed = false
    }

    // 会话/技能目录一变，旧详情就失效，避免停在上一个 session 的 SKILL.md 上。
    Connections {
        target: Hub
        function onSkillsChanged() {
            if (root.selectedSkill)
                root.closeSkill()
        }
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: statusText.top
        anchors.bottomMargin: 8
        clip: true
        model: root.skills
        spacing: 2
        ScrollBar.vertical: AppScrollBar { }
        boundsBehavior: Flickable.StopAtBounds

        delegate: Rectangle {
            id: row
            required property var modelData
            width: list.width
            height: 62
            radius: Theme.radius.inner
            color: rowHover.containsMouse && row.modelData.path.length > 0
                ? Theme.color.hoverBg : "transparent"

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 30
                    radius: 9
                    color: Theme.color.buttonBg

                    LineIcon {
                        anchors.centerIn: parent
                        name: row.modelData.modelInvocable ? "tools" : "file-doc"
                        size: 16
                        color: Theme.color.textSecondary
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width - 62)
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.modelData.name
                        elide: Text.ElideRight
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                        font.weight: Theme.font.weightMedium
                    }

                    Text {
                        width: parent.width
                        text: row.modelData.description
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        color: Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                    }

                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: row.modelData.path
                        elide: Text.ElideMiddle
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }
                }
            }

            MouseArea {
                id: rowHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: row.modelData.path.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.openSkill(row.modelData)
            }
        }
    }

    Text {
        id: statusText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        text: root.busy ? qsTrId("skillfilter_loading")
            : (root.status.length > 0 ? root.status
                : qsTrId("skillfilter_summary_fmt").arg(root.skills.length))
        elide: Text.ElideRight
        maximumLineCount: 1
        color: Theme.color.textTertiary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        horizontalAlignment: Text.AlignRight
    }

    // 应用内技能详情：不再把 SKILL.md 交给系统 .md 关联（避免 WorkBuddy/DDE 卡住宿主）。
    Item {
        id: detailView
        anchors.fill: parent
        visible: root.selectedSkill !== null
        z: 20

        Rectangle {
            anchors.fill: parent
            color: Theme.color.panelBg
            radius: Theme.radius.inner
        }

        Row {
            id: detailHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            height: 30
            spacing: 8

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                boxSize: 28
                glyph: "‹"
                tooltip: qsTrId("skilldetail_back")
                onClicked: root.closeSkill()
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - 42)
                spacing: 1

                Text {
                    width: parent.width
                    text: root.selectedSkill ? root.selectedSkill.name : ""
                    elide: Text.ElideRight
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body
                    font.weight: Theme.font.weightMedium
                }

                Text {
                    width: parent.width
                    text: root.selectedSkill ? root.selectedSkill.path : ""
                    elide: Text.ElideMiddle
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.small
                }
            }
        }

        Flickable {
            id: detailFlick
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: detailHeader.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 4
            clip: true
            contentWidth: width
            contentHeight: detailBody.height + 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            TextEdit {
                id: detailBody
                width: detailFlick.width
                height: contentHeight
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                text: root.detailFailed
                    ? qsTrId("skilldetail_failed")
                    : root.detailText
                color: root.detailFailed ? Theme.color.danger : Theme.color.textSecondary
                selectionColor: Theme.color.selectionBg
                font.family: "Cascadia Mono"
                font.pixelSize: Theme.font.small
                leftPadding: 4
                rightPadding: 4
                topPadding: 2
                bottomPadding: 8
            }
        }
    }
}
