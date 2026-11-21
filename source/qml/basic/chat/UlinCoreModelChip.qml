// Composer 的 ULin Core 模型 chip：显示当前 provider/model，点开选模型。
// 和右侧 ModelSelectorChip 同一套 chip / 弹层视觉（24px、透明底、悬停 chipHoverBg）。
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQml
import ULinHub 1.0
import "../../widgets"
import "../../skin/chat"

Item {
    id: root

    readonly property var cfg: Hub.settings.ulincoreBridge
    readonly property int textInset: 8

    // 设置页写入 API Key 后，模型列表可能从“无可用模型”变成“有模型”。
    // providers 的 changed 通知之外再加一个本地 revision，强制 Repeater 重建；
    // 否则部分会话里的 chip 要等切会话才刷新。
    property int modelRefreshToken: 0
    Connections {
        target: root.cfg
        function onChanged() { root.modelRefreshToken++ }
    }

    visible: Hub.settings.ulincoreMode
    width: visible ? chipRow.implicitWidth + 2 * Theme.spacing.labelGap : 0
    height: Theme.size.selectorChipHeight

    Rectangle {
        id: chip
        anchors.fill: parent
        radius: Theme.radius.chip
        color: chipMouse.pressed
            ? Theme.color.activeBg
            : (menu.visible
               ? Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g,
                         Theme.color.textPrimary.b, 0.08)
               : (chipMouse.containsMouse ? Theme.color.chipHoverBg : "transparent"))

        Row {
            id: chipRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.labelGap
            spacing: 4

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.cfg.selectedModel.length > 0 ? root.cfg.selectedModel : "选择模型"
                color: menu.visible ? Theme.color.textPrimary : Theme.color.textSecondary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
                font.weight: Theme.font.weightMedium
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.cfg.selectedProviderHasKey
                text: "未配置 Key"
                color: Theme.color.accent
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
            }

            Canvas {
                id: chevron
                width: 9
                height: 6
                anchors.verticalCenter: parent.verticalCenter
                rotation: menu.visible ? 180 : 0

                Behavior on rotation {
                    NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic }
                }

                property color ink: menu.visible ? Theme.color.textPrimary : Theme.color.textCaption
                onInkChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = ink
                    ctx.beginPath()
                    ctx.moveTo(0.5, 0.8)
                    ctx.lineTo(width - 0.5, 0.8)
                    ctx.lineTo(width / 2, height - 0.4)
                    ctx.closePath()
                    ctx.fill()
                }
            }
        }

        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: menu.visible ? menu.close() : menu.open()
        }
    }

    Popup {
        id: menu

        x: 0
        y: -height - 8
        width: 300
        padding: 4
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Item {
            Rectangle {
                id: menuFace
                anchors.fill: parent
                radius: 12
                color: Theme.color.menuBg
                border.width: 1
                border.color: Theme.color.menuBorder
                visible: false
            }

            MultiEffect {
                anchors.fill: menuFace
                source: menuFace
                shadowEnabled: true
                shadowColor: Theme.color.shadowFloat
                shadowBlur: Math.min(1.0, Theme.shadow.floatBlur / 64)
                shadowVerticalOffset: 2
                shadowHorizontalOffset: 0
                autoPaddingEnabled: true
            }

            MultiEffect {
                anchors.fill: menuFace
                source: menuFace
                shadowEnabled: true
                shadowColor: Theme.color.shadow
                shadowBlur: Math.min(1.0, Theme.shadow.floatBlur / 32)
                shadowVerticalOffset: Theme.shadow.floatDy
                shadowHorizontalOffset: 0
                autoPaddingEnabled: true
            }
        }

        contentItem: Flickable {
            id: menuScroll
            implicitHeight: Math.min(Math.max(contentHeight, 100), 420)
            contentHeight: menuColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: menuColumn
                width: menuScroll.width
                spacing: 2

                Text {
                    text: "ULin Core 模型"
                    color: Theme.color.textSecondary
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    leftPadding: root.textInset
                    topPadding: 1
                    bottomPadding: 3
                }

                Text {
                    width: parent.width
                    visible: !root.cfg.selectedProviderHasKey
                    text: "未配置 API Key：当前会退回 Echo"
                    wrapMode: Text.WordWrap
                    color: Theme.color.accent
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                    leftPadding: root.textInset
                    rightPadding: root.textInset
                    bottomPadding: 3
                }

                Repeater {
                    model: {
                        root.modelRefreshToken
                        return root.cfg.providers
                    }

                    delegate: Column {
                        id: providerGroup
                        required property var modelData
                        required property int index

                        width: menuColumn.width
                        spacing: 2

                        Text {
                            width: parent.width
                            visible: root.cfg.providers.length > 1
                            text: providerGroup.modelData.name
                            color: Theme.color.textTertiary
                            font.family: Theme.font.uiFamily
                            font.pixelSize: Theme.font.small
                            font.weight: Theme.font.weightBold
                            font.letterSpacing: 0.5
                            leftPadding: root.textInset
                            topPadding: providerGroup.index === 0 ? 4 : 8
                            elide: Text.ElideRight
                        }

                        Repeater {
                            model: providerGroup.modelData.models

                            delegate: MenuRow {
                                required property var modelData
                                width: providerGroup.width
                                title: modelData.name
                                subtitle: modelData.id
                                checked: root.cfg.selectedProvider === providerGroup.modelData.id
                                    && root.cfg.selectedModel === modelData.id
                                onChosen: {
                                    menu.close()
                                    root.cfg.selectModel(providerGroup.modelData.id, modelData.id)
                                }
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: root.cfg.providers.length === 0
                    text: "还没有提供商，请到设置里添加。"
                    wrapMode: Text.WordWrap
                    color: Theme.color.textCaption
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.caption
                    leftPadding: root.textInset
                    rightPadding: root.textInset
                }
            }
        }
    }
}
