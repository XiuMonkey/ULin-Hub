// ULin Core · 运行时：系统提示词、工作目录、工具线程、最大步数、写文件开关。
import QtQuick
import QtQuick.Controls.Basic
import QtQml
import ULinHub 1.0
import "../../widgets"

Item {
    id: page

    readonly property var cfg: Hub.settings.ulincore

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
                text: "运行时"
                color: Theme.color.textPrimary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
            }

            Text {
                width: parent.width
                text: "这些设置影响内核本身；修改后点保存，下一轮生效。"
                wrapMode: Text.WordWrap
                color: Theme.color.textSecondary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
            }

            Text {
                text: "系统提示词"
                color: Theme.color.textSecondary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
            }

            Rectangle {
                width: parent.width
                height: 86
                radius: Theme.radius.control
                color: Theme.color.panelBg
                border.width: 1
                border.color: promptArea.activeFocus ? Theme.color.textSecondary : Theme.color.inputBorder

                TextArea {
                    id: promptArea
                    anchors.fill: parent
                    anchors.margins: 6
                    padding: 0
                    background: null
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.color.textPrimary
                    selectionColor: Theme.color.selectionBg
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.body
                    onTextChanged: if (activeFocus) page.cfg.systemPrompt = text
                }

                Binding {
                    target: promptArea
                    property: "text"
                    value: page.cfg.systemPrompt
                }
            }

            UlinCoreField {
                label: "工作目录（相对 data/ulincore 或绝对路径）"
                value: page.cfg.workspaceRoot
                placeholder: "launch-root"
                onEdited: (text) => page.cfg.workspaceRoot = text
            }

            Row {
                width: parent.width
                spacing: 8

                UlinCoreField {
                    width: (parent.width - 8) / 2
                    label: "工具线程数（1..16）"
                    value: String(page.cfg.toolThreads)
                    placeholder: "4"
                    onEdited: (text) => {
                        const value = parseInt(text)
                        if (!Number.isNaN(value))
                            page.cfg.toolThreads = value
                    }
                }

                UlinCoreField {
                    width: (parent.width - 8) / 2
                    label: "最大步数（1..64）"
                    value: String(page.cfg.maxSteps)
                    placeholder: "24"
                    onEdited: (text) => {
                        const value = parseInt(text)
                        if (!Number.isNaN(value))
                            page.cfg.maxSteps = value
                    }
                }
            }

            UlinCoreCheckBox {
                id: allowWritesBox
                text: "允许 write_file 写工作区内的文件"
                onToggled: page.cfg.allowWrites = checked

                Binding {
                    target: allowWritesBox
                    property: "checked"
                    value: page.cfg.allowWrites
                }
            }

            Row {
                spacing: 8

                IconButton {
                    label: "保存"
                    onClicked: page.cfg.save()
                }

                IconButton {
                    label: "恢复默认"
                    onClicked: page.cfg.resetToDefaults()
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
