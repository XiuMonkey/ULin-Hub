// iOS 玻璃 · 用户消息气泡（iMessage 造型）
// ---------------------------------------------------------------------------
// 契约与内置 skin/chat/UserBubble.qml 一致：text / copyText / images / copyAll()。
//
// 造型：iMessage 的那颗 systemBlue 圆角气泡 —— 18px 连续圆角、无尾巴、白字，
// 面上加一层自上而下的同色微渐变（#007AFF 顶部略亮、底部略深）和一条极淡的顶部高光，
// 这是 iMessage 在 iOS 上看起来"鼓起来"的原因。图片角比气泡小一档（14），跟随同一色系。
import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    readonly property string copyText: text
    property var images: []

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
                radius: Theme.radius.inner
                color: Theme.color.inputBg
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.12 : 0.65)
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
        width: content.width + 2 * Theme.spacing.cardPadding + 8
        implicitHeight: content.contentHeight + 2 * Theme.spacing.buttonPadY
        radius: Math.min(18, height / 2)
        color: Theme.color.userBubbleBg

        // 同色微渐变：顶部提亮、底部压深，气泡才有体积
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.20) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.10) }
            }
        }

        // 顶沿高光：iMessage 气泡边界那道细白线
        Rectangle {
            x: parent.radius * 0.6
            y: 0.5
            width: Math.max(0, parent.width - 2 * x)
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, 0.42) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
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
            selectionColor: Qt.rgba(1, 1, 1, 0.30)
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
        width: 160
        padding: 5
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: Theme.radius.menu
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.10 : 0.5) }
                    GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0.0) }
                }
            }
        }

        contentItem: Column {
            width: copyMenu.width - 10
            spacing: 1

            Rectangle {
                width: parent.width
                height: 32
                radius: 9
                color: copyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 9
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
