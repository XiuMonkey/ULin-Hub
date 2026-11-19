// ULin Core · 提供商：provider 增删改/选择。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    readonly property var cfg: Hub.settings.ulincore

    property string editId: ""
    property string draftName: ""
    property string draftBaseUrl: ""
    property string draftApiKey: ""

    function resetForm() {
        editId = ""
        draftName = ""
        draftBaseUrl = ""
        draftApiKey = ""
    }

    function editProvider(provider) {
        editId = provider.id
        draftName = provider.name
        draftBaseUrl = provider.baseUrl
        draftApiKey = provider.apiKey
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
                text: "提供商"
                color: Theme.color.textPrimary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }

            Text {
                width: parent.width
                text: "为 ULin Core 配置 OpenAI 兼容的提供商；这里选中的 provider/model 会用于下一轮对话。"
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
            }

            Repeater {
                model: page.cfg.providers

                Rectangle {
                    id: providerCard
                    required property var modelData

                    width: parent.width
                    implicitHeight: providerRow.implicitHeight + 12
                    radius: Theme.radius.small
                    color: providerMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.color.border
                    }

                    Rectangle {
                        visible: modelData.selected
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: Math.max(0, parent.height - 12)
                        radius: 1
                        color: Theme.color.accent
                    }

                    MouseArea {
                        id: providerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.cfg.selectProvider(modelData.id)
                    }

                    Row {
                        id: providerRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 6

                        Column {
                            width: Math.max(0, parent.width - 128)
                            spacing: 2

                            Text {
                                width: parent.width
                                text: modelData.name
                                elide: Text.ElideRight
                                color: Theme.color.textPrimary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.body
                                font.weight: Theme.font.weightMedium
                            }
                            Text {
                                width: parent.width
                                text: modelData.baseUrl
                                elide: Text.ElideMiddle
                                color: Theme.color.textTertiary
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.small
                            }
                            Text {
                                text: modelData.apiKey.length > 0 ? "Key 已配置" : "Key 未配置"
                                color: modelData.apiKey.length > 0
                                    ? Theme.color.textTertiary : Theme.color.accent
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }
                            Text {
                                text: modelData.models.length + " 个模型" + (modelData.selected ? " · 当前" : "")
                                color: modelData.selected ? Theme.color.accent : Theme.color.textCaption
                                font.family: Theme.font.uiFamily
                                font.pixelSize: Theme.font.caption
                            }
                        }

                        IconButton {
                            label: "编辑"
                            onClicked: page.editProvider(modelData)
                        }
                        IconButton {
                            label: "删除"
                            onClicked: {
                                if (page.editId === modelData.id)
                                    page.resetForm()
                                page.cfg.removeProvider(modelData.id)
                            }
                        }
                    }
                }
            }

            Text {
                text: page.editId === "" ? "添加提供商" : "编辑提供商"
                color: Theme.color.textPrimary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.body
                font.weight: Theme.font.weightMedium
            }

            UlinCoreField {
                label: "名称"
                value: page.draftName
                placeholder: "DeepSeek"
                onEdited: (text) => page.draftName = text
            }

            UlinCoreField {
                label: "接口地址"
                value: page.draftBaseUrl
                placeholder: "https://api.deepseek.com/v1"
                onEdited: (text) => page.draftBaseUrl = text
            }

            UlinCoreField {
                label: "API Key"
                value: page.draftApiKey
                password: true
                placeholder: "sk-..."
                onEdited: (text) => page.draftApiKey = text
            }

            Row {
                spacing: 8

                IconButton {
                    label: page.editId === "" ? "添加" : "保存"
                    onClicked: {
                        if (page.editId === "") {
                            page.cfg.addProvider(page.draftName, page.draftBaseUrl, page.draftApiKey)
                        }
                        else {
                            page.cfg.updateProvider(page.editId, page.draftName,
                                page.draftBaseUrl, page.draftApiKey)
                        }
                        page.resetForm()
                    }
                }

                IconButton {
                    label: "清空"
                    onClicked: page.resetForm()
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.cfg.status
                    color: Theme.color.textSecondary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                }
            }
        }
    }
}
