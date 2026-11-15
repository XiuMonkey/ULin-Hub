// ULin Core · 模型：按提供商列出模型，增删改 + 选择默认模型。
import QtQuick
import QtQuick.Controls.Basic
import QtQml
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    readonly property var cfg: Hub.settings.ulincore
    readonly property var currentProvider: {
        const rows = page.cfg.providers
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === page.cfg.selectedProvider)
                return rows[i]
        }
        return null
    }

    property string editId: ""
    property string draftId: ""
    property string draftName: ""
    property string draftMaxTokens: "4096"
    property string draftTemperature: "0.2"

    function providerIndex() {
        const rows = page.cfg.providers
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === page.cfg.selectedProvider)
                return i
        }
        return 0
    }

    function resetForm() {
        editId = ""
        draftId = ""
        draftName = ""
        draftMaxTokens = "4096"
        draftTemperature = "0.2"
    }

    function editModel(model) {
        editId = model.id
        draftId = model.id
        draftName = model.name
        draftMaxTokens = String(model.maxTokens)
        draftTemperature = String(model.temperature)
    }

    function submit() {
        const providerId = page.cfg.selectedProvider
        if (providerId === "")
            return
        const maxTokens = parseInt(page.draftMaxTokens)
        const temperature = parseFloat(page.draftTemperature)
        const tokens = Number.isNaN(maxTokens) ? 4096 : maxTokens
        const temp = Number.isNaN(temperature) ? 0.2 : temperature
        if (page.editId === "")
            page.cfg.addModel(providerId, page.draftId, page.draftName, tokens, temp)
        else
            page.cfg.updateModel(providerId, page.editId, page.draftName, tokens, temp)
        page.resetForm()
    }

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: form.height + 8

        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: form
            width: parent.width
            spacing: 8

            Text {
                text: "模型"
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }

            Row {
                width: parent.width
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "提供商"
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                UlinCoreComboBox {
                    id: providerCombo
                    width: 200
                    model: page.cfg.providers
                    textRole: "name"
                    onActivated: (index) => {
                        const rows = page.cfg.providers
                        if (index >= 0 && index < rows.length)
                            page.cfg.selectProvider(rows[index].id)
                    }

                    Binding {
                        target: providerCombo
                        property: "currentIndex"
                        value: page.providerIndex()
                    }
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    label: "获取模型"
                    enabled: page.cfg.selectedProvider !== ""
                    onClicked: page.cfg.fetchModels(page.cfg.selectedProvider)
                }
            }

            Text {
                width: parent.width
                text: page.currentProvider
                    ? "当前提供商的模型；点行选中，作为下一轮使用的模型。"
                    : "还没有提供商，请先到“提供商”页添加。"
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Repeater {
                model: page.currentProvider ? page.currentProvider.models : []

                Rectangle {
                    id: modelCard
                    required property var modelData

                    width: parent.width
                    implicitHeight: modelRow.implicitHeight + 10
                    radius: Theme.radius.small
                    color: modelMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.color.border
                    }

                    Rectangle {
                        visible: page.cfg.selectedModel === modelData.id
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: Math.max(0, parent.height - 14)
                        radius: 1
                        color: Theme.color.accent
                    }

                    MouseArea {
                        id: modelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.cfg.selectModel(page.cfg.selectedProvider, modelData.id)
                    }

                    Row {
                        id: modelRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 6

                        Column {
                            width: Math.max(0, parent.width - 150)
                            spacing: 2

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
                                text: modelData.id + " · " + modelData.maxTokens + " tokens · T=" + modelData.temperature
                                elide: Text.ElideRight
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.small
                            }
                            Text {
                                text: page.cfg.selectedModel === modelData.id ? "当前模型" : ""
                                color: Theme.color.accent
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                            }
                        }

                        IconButton {
                            label: "编辑"
                            onClicked: page.editModel(modelData)
                        }
                        IconButton {
                            label: "删除"
                            onClicked: {
                                if (page.editId === modelData.id)
                                    page.resetForm()
                                page.cfg.removeModel(page.cfg.selectedProvider, modelData.id)
                            }
                        }
                    }
                }
            }

            Text {
                text: page.editId === "" ? "添加模型" : "编辑模型"
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
                spacing: 8

                IconButton {
                    label: page.editId === "" ? "添加" : "保存"
                    enabled: page.cfg.selectedProvider !== ""
                    onClicked: page.submit()
                }

                IconButton {
                    label: "清空"
                    onClicked: page.resetForm()
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.cfg.status
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }
            }
        }
    }
}
