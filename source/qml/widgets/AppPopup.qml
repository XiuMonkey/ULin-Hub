// 模态弹窗：遮罩 + 居中 + 标题栏 + 关闭按钮 + 进出场淡入淡出。
// 取代旧壳的 PopupWindow/StatusPopupWindow 与 common/WindowFrame 的"铺遮罩 + 居中"那套（QML 里
// 遮罩由 Overlay.modal 负责，不需要宿主再算几何）。
//
// 用法：
//   AppPopup { id: p; title: "设置"; bodyWidth: 560; bodyHeight: 420; contentComponent: ... }
//   p.open()

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Popup {
    id: root

    property string title
    // 内容：给 contentComponent 或直接写子项（都会被塞进 body 里）
    property Component contentComponent
    default property alias content: contentHolder.data

    property int bodyWidth: 560
    property int bodyHeight: 420

    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay

    Overlay.modal: Rectangle {
        color: Theme.color.overlayBg
    }

    background: Rectangle {
        radius: Theme.radius.popup
        color: Theme.color.menuBg
        border.width: 1
        border.color: Theme.color.menuBorder
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.fast }
    }

    contentItem: Column {
        spacing: 0

        // 标题栏
        Item {
            implicitWidth: root.bodyWidth
            implicitHeight: 44

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.cardPadding
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                color: Theme.color.textPrimary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.title
                font.weight: Theme.font.weightBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth, parent.width - 2 * Theme.spacing.cardPadding - closeButton.width)
            }

            IconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing.buttonGap
                anchors.verticalCenter: parent.verticalCenter
                glyph: "✕"
                tooltip: qsTrId("common_close")
                onClicked: root.close()
            }
        }

        // 内容区
        Item {
            implicitWidth: root.bodyWidth
            implicitHeight: root.bodyHeight

            Column {
                id: contentHolder
                anchors.fill: parent
                anchors.margins: Theme.spacing.cardPadding
                spacing: Theme.spacing.sectionGap
            }

            Loader {
                anchors.fill: parent
                anchors.margins: Theme.spacing.cardPadding
                active: root.contentComponent !== null
                sourceComponent: root.contentComponent
            }
        }
    }
}
