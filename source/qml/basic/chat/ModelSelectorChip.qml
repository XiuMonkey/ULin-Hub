// 模型 / 思考档位 chip + 弹层（按原版 ui/ModelSelector.cpp + chat.qss 复刻，视觉上做了"降一档"处理）
//
// chip：24px 高、radius chip(12)、透明底；悬停 chipHoverBg、按下 activeBg；
//       文字 = 当前模型名（textSecondary 12/500）+ 当前档位名（textTertiary 12）+ ▾/▴（textCaption 11）；
//       整块比正文弱一档（次级色 + 小一号字），它是输入区的配角，不跟编辑区（13px textPrimary）抢主视觉；
//       左右各留 labelGap(8) —— Composer 靠这个值把 chip 文字对齐到编辑器文字的列线上；
//       目录已加载但为空 → chip 隐藏（原版 setVisible(hasModels())）。
// 弹层：宽 300、menuBg + 1px menuBorder + radius 12、贴 chip 上方 8px；
//       两节：模型（model_label）/ 思考深度（model_think_depth）；行最小高 38、右侧 ✓；
//       提供方多于一个时插分组标签；档位用兜底时插一行 model_think_generic_note；
//       目录为空时插一行 model_none_published。
// 写入：选模型 → session/selectModel（并清空档位）；选档位 → 同端点只改 reasoningEffort。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0
import "../../widgets"
import "../../skin/chat"

Item {
    id: root

    signal selectModelRequested(string provider, string model)
    signal selectLevelRequested(string levelId)

    visible: Hub.hasModels
    // 宽 = 内容宽 + 左右各 labelGap(8)：与上面 "chip 文字落在编辑器文字列线上" 的推导配套
    width: visible ? chipRow.implicitWidth + 2 * Theme.spacing.labelGap : 0
    height: Theme.size.selectorChipHeight

    Rectangle {
        id: chip
        anchors.fill: parent
        radius: Theme.radius.chip
        color: chipMouse.pressed
            ? Theme.color.activeBg
            : (chipMouse.containsMouse ? Theme.color.chipHoverBg : "transparent")

        Row {
            id: chipRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.labelGap
            spacing: 4

            Text {
                id: modelLabel
                anchors.verticalCenter: parent.verticalCenter
                text: Hub.currentModelName.length > 0
                    ? Hub.currentModelName
                    : qsTrId("model_none_published")
                color: Theme.color.textSecondary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                font.weight: Theme.font.weightMedium
            }

            Text {
                id: levelLabel
                anchors.verticalCenter: parent.verticalCenter
                visible: Hub.currentLevelName.length > 0
                text: Hub.currentLevelName
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
            }

            Text {
                id: chevron
                anchors.verticalCenter: parent.verticalCenter
                text: menu.visible ? "▴" : "▾"
                color: Theme.color.textCaption
                font.pixelSize: Theme.font.small
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

        // 贴 chip 上方 8px（原版 kMenuGap）；越屏时翻到下方
        x: 0
        y: -height - 8
        width: 300                       // kMenuWidth
        padding: 6
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 12                   // kMenuRadius
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Flickable {
            id: menuScroll
            implicitHeight: Math.min(Math.max(contentHeight, 120), 420)
            contentHeight: menuColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: menuColumn
                width: menuScroll.width
                spacing: 2

                // ---- 模型一节 ----
                Text {
                    text: qsTrId("model_label")
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    topPadding: 4
                    bottomPadding: 4
                }

                Text {
                    visible: Hub.modelGroups.length === 0
                    text: qsTrId("model_none_published")
                    color: Theme.color.textCaption
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                Repeater {
                    model: Hub.modelGroups

                    delegate: Column {
                        id: groupHost

                        required property var modelData
                        required property int index

                        width: menuColumn.width
                        spacing: 2

                        // 分组标签：仅多于一个提供方时插（原版 groups.size()>1）
                        Text {
                            width: parent.width
                            visible: Hub.modelGroups.length > 1
                            text: groupHost.modelData.name
                            color: Theme.color.textCaption
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                            font.weight: Theme.font.weightBold
                            topPadding: 2
                            leftPadding: 2
                        }

                        Repeater {
                            model: groupHost.modelData.models

                            delegate: MenuRow {
                                required property var modelData
                                width: groupHost.width
                                title: modelData.name
                                subtitle: modelData.description
                                checked: modelData.selected
                                onChosen: {
                                    menu.close()
                                    root.selectModelRequested(groupHost.modelData.id, modelData.id)
                                }
                            }
                        }
                    }
                }

                // ---- 思考深度一节 ----
                Text {
                    text: qsTrId("model_think_depth")
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    topPadding: 8
                    bottomPadding: 4
                }

                // 档位来自客户端兜底时先说明一句（原版 modelSelectorOptionHint）
                Text {
                    width: parent.width
                    visible: Hub.useGenericLevels
                    text: qsTrId("model_think_generic_note")
                    wrapMode: Text.WordWrap
                    color: Theme.color.textCaption
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    leftPadding: 2
                }

                Repeater {
                    model: Hub.reasoningLevels

                    delegate: MenuRow {
                        required property var modelData
                        width: menuColumn.width
                        title: modelData.name
                        checked: modelData.selected
                        onChosen: {
                            menu.close()
                            root.selectLevelRequested(modelData.id)
                        }
                    }
                }
            }
        }
    }
}
