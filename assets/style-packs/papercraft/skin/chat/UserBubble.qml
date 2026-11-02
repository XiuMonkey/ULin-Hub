// 素纸 · 用户消息气泡
// 契约与内置 skin/chat/UserBubble.qml 一致：text / copyText / images / copyAll()。
// 造型：纸片方角 + 1px 墨线 + 右沿一道赭石批注条（像在稿纸上做的标记）。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    readonly property string copyText: text
    property var images: []   // [{ id, mediaType }]，源由 Hub.attachmentSource 取

    function copyAll() {
        if (root.copyText.length === 0)
            return
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.deselect()
    }

    implicitHeight: bubble.implicitHeight + (imagesRow.visible ? imagesRow.height + 8 : 0)

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
                radius: 1
                color: Theme.color.inputBg
                border.width: 1
                border.color: Theme.color.border
                clip: true
                Image {
                    id: img
                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    fillMode: imageTile.single ? Image.PreserveAspectFit : Image.PreserveAspectCrop
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
        width: content.width + 2 * Theme.spacing.cardPadding + 10
        implicitHeight: content.contentHeight + 2 * Theme.spacing.buttonPadY
        radius: 2
        color: Theme.color.userBubbleBg
        border.width: 1
        border.color: Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g,
                              Theme.color.textPrimary.b, 0.14)

        // 右沿批注条
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 5
            width: 3
            radius: 1
            color: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g, Theme.color.accent.b, 0.75)
        }

        Text {
            id: measure
            visible: false
            text: root.text
            textFormat: Text.MarkdownText
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }

        TextEdit {
            id: content
            objectName: "bubbleText"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: -2
            width: Math.min(measure.implicitWidth, Math.max(40, root.width * 0.72 - 2 * Theme.spacing.cardPadding))
            height: contentHeight
            text: root.text
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
            radius: 2
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
                radius: 1
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
