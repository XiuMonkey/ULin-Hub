// 用户消息气泡：右对齐、圆角，颜色取自 userBubble* token。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    readonly property string copyText: text
    property var images: []   // [{ id, mediaType }]，源由 Hub.attachmentSource 取

    // Non-image files are appended to the prompt as "附件：\n- name (path)".
    // The bubble shows the user text only; files get dedicated cards.
    readonly property var attachmentParse: splitAttachmentText(text)
    readonly property string displayText: attachmentParse.text
    readonly property var files: attachmentParse.files

    function splitAttachmentText(raw) {
        var source = String(raw || "")
        var marker = source.lastIndexOf("附件：\n")
        if (marker < 0)
            return { text: source, files: [] }
        if (marker > 0 && source.slice(Math.max(0, marker - 2), marker) !== "\n\n")
            return { text: source, files: [] }
        var body = source.slice(marker + 4)
        var lines = body.split("\n")
        var parsed = []
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i]
            if (line.length === 0)
                continue
            var match = line.match(/^- (.+?) \((.*)\)$/)
            if (!match)
                return { text: source, files: [] }
            var path = match[2]
            var dot = path.lastIndexOf(".")
            var suffix = dot >= 0 ? path.slice(dot + 1).toLowerCase() : ""
            parsed.push({ name: match[1], path: path, suffix: suffix })
        }
        if (parsed.length === 0)
            return { text: source, files: [] }
        return { text: source.slice(0, marker).replace(/\s+$/, ""), files: parsed }
    }

    function fileSuffixLabel(suffix) {
        return String(suffix || "").length > 0 ? String(suffix).toUpperCase() : "FILE"
    }

    function copyAll() {
        if (root.copyText.length === 0)
            return
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.deselect()
    }

    implicitHeight: (bubble.visible ? bubble.implicitHeight : 0)
        + (filesRow.visible ? filesRow.height + 8 : 0)
        + (imagesRow.visible ? imagesRow.height + 8 : 0)

    // 图片贴在气泡上方（右对齐）；单图按原比例限制 240px 长边，多图用 120px 方图裁切。
    Column {
        id: filesRow
        anchors.right: parent.right
        anchors.bottom: imagesRow.top
        anchors.bottomMargin: 8
        spacing: 6
        visible: root.files.length > 0
        height: visible ? implicitHeight : 0

        Repeater {
            model: root.files
            Rectangle {
                required property var modelData
                width: 286
                height: 58
                radius: Theme.radius.panel
                color: Theme.color.cardBg
                border.width: 0.5
                border.color: Theme.color.border
                clip: true

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 10

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 38
                        height: 38
                        radius: 10
                        color: Theme.color.tagBg
                        Text {
                            anchors.centerIn: parent
                            text: root.fileSuffixLabel(modelData.suffix)
                            color: Theme.color.accent
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 8
                            font.weight: Theme.font.weightBold
                            elide: Text.ElideRight
                            width: parent.width - 6
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - 50)
                        spacing: 3

                        Text {
                            width: parent.width
                            text: modelData.name
                            color: Theme.color.textPrimary
                            elide: Text.ElideMiddle
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            font.weight: Theme.font.weightMedium
                        }
                        Text {
                            width: parent.width
                            text: modelData.path
                            color: Theme.color.textTertiary
                            elide: Text.ElideMiddle
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }
                }
            }
        }
    }

    Row {
        id: imagesRow
        anchors.right: parent.right
        anchors.bottom: bubble.top
        anchors.bottomMargin: 8
        spacing: 6
        visible: root.images.length > 0
        height: visible ? Math.max(120, implicitHeight) : 0

        Repeater {
            model: root.images
            Rectangle {
                id: imageTile
                required property var modelData
                readonly property bool single: root.images.length === 1
                readonly property real ratio: (img.implicitWidth > 0 && img.implicitHeight > 0)
                    ? Math.min(4, Math.max(0.25, img.implicitWidth / img.implicitHeight))
                    : 1
                width: single ? (ratio >= 1 ? 240 : 240 * ratio) : 120
                height: single ? (ratio >= 1 ? 240 / ratio : 240) : 120
                radius: Theme.radius.small
                color: Theme.color.inputBg
                clip: true
                Image {
                    id: img
                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    fillMode: imageTile.single ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                    // 本地预览优先，历史图片由 attachmentSource 异步取
                    source: {
                        var rev = Hub.attachmentRevision
                        var preview = modelData.preview ? String(modelData.preview) : ""
                        if (preview.length > 0)
                            return preview
                        return rev >= 0 && modelData.id ? Hub.attachmentSource(modelData.id) : ""
                    }
                }
            }
        }
    }

    Rectangle {
        id: bubble
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        visible: root.displayText.length > 0
        width: visible ? content.width + 2 * Theme.spacing.cardPadding : 0
        implicitHeight: visible ? content.contentHeight + 2 * Theme.spacing.buttonPadY : 0
        radius: Theme.radius.panel
        color: Theme.color.userBubbleBg

        // 量宽度专用：Text 的 implicitWidth 在设宽前不可用，不能直接用 TextEdit 量。
        Text {
            id: measure
            visible: false
            text: root.displayText
            textFormat: Text.MarkdownText
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body   // 与 Agent 正文同档
        }
        TextEdit {
            id: content
            objectName: "bubbleText"
            anchors.centerIn: parent
            width: Math.min(measure.implicitWidth, Math.max(40, root.width * 0.72 - 2 * Theme.spacing.cardPadding))   // 长消息不铺满整行
            height: contentHeight
            text: root.displayText
            textFormat: TextEdit.MarkdownText
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            activeFocusOnPress: true
            color: Theme.color.userBubbleText
            selectionColor: Theme.color.selectionBg
            selectedTextColor: content.color
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        readOnly: true
        textFormat: TextEdit.PlainText
        text: root.copyText
        width: 1
        height: 1
    }

    Popup {
        id: copyMenu
        parent: root
        width: 156
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 8
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: copyMenu.width - 12
            spacing: 2

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                color: copyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("context_copy_all")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.copyAll()
                        copyMenu.close()
                    }
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => {
            var p = eventPoint.position
            copyMenu.x = Math.max(8, Math.min(root.width - copyMenu.width - 8, p.x))
            copyMenu.y = Math.max(8, Math.min(root.height - copyMenu.height - 8, p.y))
            copyMenu.open()
        }
    }
}
