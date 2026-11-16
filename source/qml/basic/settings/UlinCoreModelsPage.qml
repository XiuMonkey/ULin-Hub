// ULin Core · 模型：按宿主 ModelListPage 的卡片结构展示 provider/model。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    readonly property var cfg: Hub.settings.ulincore
    property var expandedRows: ({})
    property bool formOpen: false
    property string draftId: ""
    property string draftName: ""
    property string draftMaxTokens: "4096"
    property string draftTemperature: "0.2"

    readonly property int totalModels: {
        var n = 0
        for (var i = 0; i < page.cfg.providers.length; ++i)
            n += page.cfg.providers[i].models.length
        return n
    }

    function rowKey(provider, model) { return provider + "/" + model }
    function isExpanded(provider, model) { return page.expandedRows[page.rowKey(provider, model)] === true }
    function toggle(provider, model) {
        const key = page.rowKey(provider, model)
        const next = Object.assign({}, page.expandedRows)
        next[key] = !next[key]
        page.expandedRows = next
    }

    function resetForm() {
        page.draftId = ""
        page.draftName = ""
        page.draftMaxTokens = "4096"
        page.draftTemperature = "0.2"
    }

    function submitForm() {
        if (page.cfg.selectedProvider === "")
            return
        const tokens = parseInt(page.draftMaxTokens)
        const temp = parseFloat(page.draftTemperature)
        page.cfg.addModel(page.cfg.selectedProvider, page.draftId, page.draftName,
            Number.isNaN(tokens) ? 4096 : tokens,
            Number.isNaN(temp) ? 0.2 : temp)
        page.resetForm()
        page.formOpen = false
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: content
            width: parent.width
            spacing: 3

            Text {
                text: qsTrId("model_list_title")
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }

            Text {
                width: parent.width
                text: qsTrId("model_list_desc")
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Text {
                width: parent.width
                text: "共 " + page.totalModels + " 个模型，" + page.cfg.providers.length + " 个提供方。"
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Text {
                width: parent.width
                visible: page.cfg.status.length > 0
                text: page.cfg.status
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Repeater {
                model: page.cfg.providers

                delegate: Column {
                    id: providerGroup
                    required property var modelData

                    width: parent.width
                    spacing: 2

                    Text {
                        width: parent.width
                        text: providerGroup.modelData.name
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        font.weight: Theme.font.weightBold
                        font.letterSpacing: 0.4
                        topPadding: 4
                        leftPadding: 2
                        elide: Text.ElideRight
                    }

                    Repeater {
                        model: providerGroup.modelData.models

                        delegate: Rectangle {
                            id: entry
                            required property var modelData

                            readonly property bool selected: providerGroup.modelData.selected === true
                                && page.cfg.selectedModel === modelData.id
                            readonly property bool expanded: page.isExpanded(
                                providerGroup.modelData.id, modelData.id)

                            width: parent.width
                            implicitHeight: entryColumn.implicitHeight
                            radius: Theme.radius.card
                            color: Theme.color.pageBg || Theme.color.panelBg
                            border.width: 1
                            border.color: entry.expanded
                                ? Theme.color.inputBorder : Theme.color.menuBorder

                            Column {
                                id: entryColumn
                                width: parent.width

                                Item {
                                    width: parent.width
                                    implicitHeight: 48

                                    Row {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8

                                        Column {
                                            width: parent.width - badge.width - chevron.width - 2 * parent.spacing
                                            spacing: 0

                                            Text {
                                                width: parent.width
                                                text: modelData.name
                                                elide: Text.ElideRight
                                                color: Theme.color.textPrimary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.body
                                                font.weight: Theme.font.weightMedium
                                            }
                                            Text {
                                                width: parent.width
                                                text: providerGroup.modelData.name + " · " + modelData.id
                                                elide: Text.ElideRight
                                                color: Theme.color.textSecondary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.caption
                                            }
                                        }

                                        Rectangle {
                                            id: badge
                                            implicitWidth: badgeText.implicitWidth + 16
                                            implicitHeight: badgeText.implicitHeight + 2
                                            radius: Theme.radius.control
                                            color: Theme.color.buttonBg
                                            anchors.verticalCenter: parent.verticalCenter

                                            Text {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: entry.selected
                                                    ? "当前" : qsTrId("model_source_adapter_builtin")
                                                color: Theme.color.textSecondary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.small
                                            }
                                        }

                                        Text {
                                            id: chevron
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: entry.expanded ? "▾" : "▸"
                                            color: Theme.color.textCaption
                                            font.pixelSize: Theme.font.caption
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page.toggle(providerGroup.modelData.id, modelData.id)
                                    }
                                }

                                Column {
                                    width: parent.width
                                    visible: entry.expanded
                                    topPadding: 0
                                    bottomPadding: 10
                                    leftPadding: 10
                                    rightPadding: 10
                                    spacing: 2

                                    Repeater {
                                        model: [
                                            ["提供商", providerGroup.modelData.name],
                                            ["模型 ID", modelData.id],
                                            ["最大 Token", String(modelData.maxTokens)],
                                            ["温度", String(modelData.temperature)]
                                        ]

                                        delegate: Row {
                                            required property var modelData
                                            width: parent.width
                                            spacing: 8

                                            Text {
                                                width: 96
                                                text: modelData[0]
                                                color: Theme.color.textTertiary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.caption
                                            }
                                            Text {
                                                width: parent.width - 96 - parent.spacing
                                                text: modelData[1]
                                                wrapMode: Text.Wrap
                                                color: Theme.color.textSecondary
                                                font.family: "Microsoft YaHei UI"
                                                font.pixelSize: Theme.font.caption
                                            }
                                        }
                                    }

                                    Row {
                                        width: parent.width
                                        layoutDirection: Qt.RightToLeft
                                        topPadding: 4
                                        spacing: 6

                                        IconButton {
                                            height: 30
                                            label: qsTrId("common_delete")
                                            onClicked: page.cfg.removeModel(
                                                providerGroup.modelData.id, modelData.id)
                                        }
                                        ActionButton {
                                            width: 116
                                            height: 30
                                            text: "设为当前"
                                            enabled: !entry.selected
                                            onClicked: page.cfg.selectModel(
                                                providerGroup.modelData.id, modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: providerGroup.modelData.models.length === 0
                        text: "该提供商还没有模型。"
                        color: Theme.color.textSecondary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                    }
                }
            }

            Text {
                width: parent.width
                visible: page.cfg.providers.length === 0
                text: "还没有提供商，请先到“提供商”页添加。"
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            // ---- 新增模型表单（照宿主底部动作区）----
            Column {
                width: parent.width
                visible: page.formOpen
                spacing: 6

                Text {
                    text: "添加模型"
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.body
                    font.weight: Theme.font.weightMedium
                }

                UlinCoreField {
                    label: "模型 ID"
                    value: page.draftId
                    placeholder: "deepseek-chat"
                    onEdited: (text) => page.draftId = text
                }
                UlinCoreField {
                    label: "显示名"
                    value: page.draftName
                    placeholder: "DeepSeek Chat"
                    onEdited: (text) => page.draftName = text
                }
                Row {
                    width: parent.width
                    spacing: 8

                    UlinCoreField {
                        width: (parent.width - 8) / 2
                        label: "最大 Token"
                        value: page.draftMaxTokens
                        placeholder: "4096"
                        onEdited: (text) => page.draftMaxTokens = text
                    }
                    UlinCoreField {
                        width: (parent.width - 8) / 2
                        label: "温度"
                        value: page.draftTemperature
                        placeholder: "0.2"
                        onEdited: (text) => page.draftTemperature = text
                    }
                }
                Row {
                    width: parent.width
                    spacing: 6
                    layoutDirection: Qt.RightToLeft

                    ActionButton {
                        width: 116
                        height: 30
                        text: qsTrId("common_add")
                        enabled: page.cfg.selectedProvider !== ""
                        onClicked: page.submitForm()
                    }
                    ActionButton {
                        width: 116
                        height: 30
                        text: qsTrId("common_cancel")
                        variant: "ghost"
                        onClicked: {
                            page.formOpen = false
                            page.resetForm()
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: 34

                ActionButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 116
                    height: 30
                    text: "获取模型"
                    enabled: page.cfg.selectedProvider !== ""
                    onClicked: page.cfg.fetchModels(page.cfg.selectedProvider)
                }

                ActionButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 116
                    height: 30
                    text: page.formOpen ? qsTrId("common_cancel") : qsTrId("model_add")
                    variant: page.formOpen ? "ghost" : "elevated"
                    enabled: page.formOpen || page.cfg.selectedProvider !== ""
                    onClicked: {
                        if (page.formOpen) {
                            page.formOpen = false
                            page.resetForm()
                        } else {
                            page.formOpen = true
                        }
                    }
                }
            }
        }
    }
}
