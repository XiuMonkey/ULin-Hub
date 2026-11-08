// 自动化任务（DSH Schedule）：整壳 PageOverlay 的页面内容。
// 服务端没有 create RPC，所以「新建任务」= 开一个新会话让 Agent 用 schedule_create 建；
// 这里只管目录、详情、打开原会话与删除（catalog / delete）。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    implicitWidth: 780
    implicitHeight: 520

    // PageOverlay 会把这个属性绑到开合动画状态；动画期间停掉转圈，别让整窗每帧重绘。
    property bool overlayAnimating: false

    property int selectedIndex: Hub.scheduleTasks.length > 0 ? 0 : -1
    property string pendingDeleteId: ""

    readonly property var selectedTask: (selectedIndex >= 0 && selectedIndex < Hub.scheduleTasks.length)
        ? Hub.scheduleTasks[selectedIndex] : null

    // 页面每次打开都刷新一次；页面存在期间每 60 秒补一次（到期状态/下次时间会变）。
    Component.onCompleted: Hub.refreshSchedules()
    Timer {
        interval: 60000
        repeat: true
        running: Hub.scheduleOpen
        onTriggered: Hub.refreshSchedules()
    }

    Item {
        id: popup
        anchors.fill: parent

        Item {
            id: titleRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 16
            implicitHeight: 30

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTrId("schedule_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 15
                font.weight: Theme.font.weightBold
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                IconButton {
                    boxSize: 28
                    glyph: "↻"
                    tooltip: qsTrId("schedule_refresh")
                    onClicked: Hub.refreshSchedules()
                }

                IconButton {
                    boxSize: 28
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: Hub.closeSchedulePage()
                }
            }
        }

        Text {
            id: subtitle
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 4
            text: qsTrId("schedule_subtitle")
            color: Theme.color.textTertiary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.caption
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2
        }

        Row {
            id: layout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: subtitle.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 12
            anchors.bottomMargin: 20
            spacing: 12

            // 左：任务目录
            Item {
                id: listColumn
                width: 316
                height: layout.height

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.card
                    color: Theme.color.windowBg
                    border.width: 1
                    border.color: Theme.color.border
                }

                ListView {
                    id: taskList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 2
                    model: Hub.scheduleTasks
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: Item {
                        id: row

                        required property int index
                        required property var modelData

                        readonly property bool selected: page.selectedIndex === index
                        readonly property bool hovered: hover.containsMouse

                        width: taskList.width
                        height: 64

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius.small
                            color: row.selected
                                ? Theme.color.panelBg
                                : (row.hovered ? Theme.color.activeBg : "transparent")
                            border.width: row.selected ? 1 : 0
                            border.color: Theme.color.border
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            spacing: 5

                            Text {
                                width: parent.width
                                text: row.modelData ? row.modelData.title : ""
                                color: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                font.weight: Theme.font.weightMedium
                                elide: Text.ElideRight
                            }

                            Row {
                                spacing: 8

                                Rectangle {
                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: (row.modelData && row.modelData.active)
                                        ? Theme.color.accent
                                        : Theme.color.textTertiary
                                }

                                Text {
                                    text: row.modelData ? row.modelData.ruleText : ""
                                    color: Theme.color.textTertiary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.caption
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: (row.modelData && row.modelData.active)
                                        ? (row.modelData.nextRelative || "") : qsTrId("schedule_status_inactive")
                                    color: Theme.color.textTertiary
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: Theme.font.caption
                                }
                            }
                        }

                        MouseArea {
                            id: hover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                page.selectedIndex = row.index
                                page.pendingDeleteId = ""
                            }
                        }
                    }
                }

                // 空 / 加载失败提示（列表为空且有状态文案时）
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    spacing: 8
                    visible: Hub.scheduleTasks.length === 0 && Hub.scheduleStatus.length > 0

                    Text {
                        width: parent.width
                        text: Hub.scheduleStatus
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        visible: Hub.scheduleStatus === qsTrId("schedule_empty")
                        text: qsTrId("schedule_empty_hint")
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                BusySpinner {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 10
                    width: 14
                    height: 14
                    lineWidth: 2
                    visible: Hub.scheduleBusy && !page.overlayAnimating
                    running: visible
                }
            }

            // 右：详情
            Item {
                id: detailColumn
                width: layout.width - listColumn.width - layout.spacing
                height: layout.height

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.card
                    color: Theme.color.panelBg
                    border.width: 1
                    border.color: Theme.color.border
                }

                // 空态：没有选中任务
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    spacing: 12
                    visible: page.selectedTask === null

                    Text {
                        width: parent.width
                        text: qsTrId("schedule_empty_title")
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.lead
                        font.weight: Theme.font.weightBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        width: parent.width
                        text: qsTrId("schedule_create_hint")
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    ActionButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 150
                        height: 34
                        text: qsTrId("schedule_create")
                        onClicked: Hub.createScheduleSession()
                    }
                }

                // 详情
                Column {
                    id: detail
                    visible: page.selectedTask !== null
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        width: parent.width
                        text: page.selectedTask ? page.selectedTask.title : ""
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.display
                        font.weight: Theme.font.weightBold
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        spacing: 8

                        Rectangle {
                            width: 7
                            height: 7
                            radius: 3.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: (page.selectedTask && page.selectedTask.active)
                                ? Theme.color.accent : Theme.color.textTertiary
                        }

                        Text {
                            text: page.selectedTask ? page.selectedTask.statusText : ""
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                        }

                        Text {
                            text: page.selectedTask ? ("· " + page.selectedTask.ruleText) : ""
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.color.border
                    }

                    Text {
                        width: parent.width
                        text: qsTrId("schedule_next")
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }

                    Text {
                        width: parent.width
                        text: page.selectedTask
                            ? (page.selectedTask.nextText + "（" + page.selectedTask.nextRelative + "）")
                            : ""
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.body
                    }

                    Text {
                        width: parent.width
                        text: qsTrId("schedule_prompt")
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                    }

                    Flickable {
                        width: parent.width
                        height: 84
                        contentHeight: promptText.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: AppScrollBar { }

                        Text {
                            id: promptText
                            width: parent.width
                            text: page.selectedTask ? page.selectedTask.prompt : ""
                            color: Theme.color.textSecondary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.body
                            wrapMode: Text.WordWrap
                            lineHeight: 1.35
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTrId("schedule_session") + "：" + (page.selectedTask ? page.selectedTask.sessionTitle : "")
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: page.selectedTask && page.selectedTask.lastDeliveryText.length > 0
                        text: qsTrId("schedule_last_delivery") + "：" + (page.selectedTask ? page.selectedTask.lastDeliveryText : "")
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                    }

                    Row {
                        spacing: 8

                        ActionButton {
                            width: 118
                            height: 34
                            text: qsTrId("schedule_open_session")
                            onClicked: {
                                if (page.selectedTask)
                                    Hub.openScheduleSession(page.selectedTask.sessionId)
                            }
                        }

                        ActionButton {
                            width: page.pendingDeleteId === (page.selectedTask ? page.selectedTask.id : "")
                                ? 118 : 88
                            height: 34
                            text: page.pendingDeleteId === (page.selectedTask ? page.selectedTask.id : "")
                                ? qsTrId("schedule_confirm_delete")
                                : qsTrId("schedule_delete")
                            variant: "danger"
                            onClicked: {
                                if (!page.selectedTask)
                                    return
                                if (page.pendingDeleteId === page.selectedTask.id) {
                                    Hub.deleteSchedule(page.selectedTask.id, page.selectedTask.sessionId)
                                    page.pendingDeleteId = ""
                                } else {
                                    page.pendingDeleteId = page.selectedTask.id
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
