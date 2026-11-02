// 模型 / 思考档位 chip + 弹层（按原版 ui/ModelSelector.cpp + chat.qss 复刻，视觉上"降一档"）：
// chip 24px 高、radius chip、透明底；悬停 chipHoverBg、按下 activeBg；文字 = 当前模型名（secondary 12/500）
// + 当前档位名（tertiary 12）+ ▾/▴（caption 11）。整块比正文弱一档，是输入区的配角。
// 提供方多于一个时插分组标签；档位用兜底时插一行 model_think_generic_note 说明。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"
import "../../skin/chat"

Item {
    id: root

    // 弹层内文字的列线：MenuRow 自身的左内边距就是 8，这里让分组标签、区块标题
    // 也都按 8 缩进（相对 Popup 内容区，绝对位置再叠加 Popup padding 6）。
    // 改之前三者分别是 6 / 8 / 14，差几像素看着就是"三行字没对齐"。
    readonly property int textInset: 8

    signal selectModelRequested(string provider, string model)
    signal selectLevelRequested(string levelId)

    visible: Hub.hasModels
    width: visible ? chipRow.implicitWidth + 2 * Theme.spacing.labelGap : 0   // 宽 = 内容宽 + 左右各 labelGap(8)：与上面 "chip 文字落在编辑器文字列线上" 的推导配套
    height: Theme.size.selectorChipHeight

    Rectangle {
        id: chip
        anchors.fill: parent
        radius: Theme.radius.chip

        // 展开时 chip 自身也铺一层淡灰底：弹层是从它身上"长"出来的，
        // 两者需要有呼应，否则像一块无关的板子浮在旁边。
        // 浓度(8%)与 MenuRow 的选中底同档 —— 弹层一开，chip 与选中的那一行深浅一致。
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
                id: modelLabel
                anchors.verticalCenter: parent.verticalCenter
                text: Hub.currentModelName.length > 0
                    ? Hub.currentModelName
                    : qsTrId("model_none_published")
                // 展开时文字加深到 textPrimary：在淡灰底上如果还保持 textSecondary，
                // 会和"未展开的另一处 chip"没区别；加深而不上色，仍留在灰阶里。
                color: menu.visible ? Theme.color.textPrimary : Theme.color.textSecondary
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

            // ▾ / ▴ 原来用字体符号：不同字体下宽高、粗细、基线位置都不一样，
            // 而且 ▴ 的字面高度和 ▾ 不对等，展开收起时箭头会跳一下。
            // 改成等边三角（Canvas），展开时整体旋转 180°，两态严格镜像。
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

        x: 0   // 贴 chip 上方 8px（原版 kMenuGap）；越屏时翻到下方
        y: -height - 8
        width: 300                       // 最长的副标题（12px，约 200）仍有余量；再窄就开始吃省略号
        padding: 4
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // 背景原来只是一块 radius 12 的圆角面 + 1px 描边。单层阴影在这里是隐形的：
        // 浅灰底与页面底(#F2F4F7)/输入卡(#FFF) 只差几个色阶，实测能压出的
        // 最大明暗差只有 26 个灰阶 —— 弹层于是"糊"在背景上，看不出悬浮层级。
        // 改成两层：近层收紧边缘给"压感"，远层弥散交代"浮多高"。
        // （与 Card.qml 同一手法：源面隐藏，由 MultiEffect 连同阴影一起绘制。）
        //
        // 底色取 menuBg = 纯白（试过 #F8F9FA、#F7F9FD 两档微灰，用户最终选纯白）：
        // 弹层与背后白输入卡同色，靠**下面这两层阴影 + 1px 描边**把边界交代出来，
        // 不靠底色明度差 —— 明度差这条路本来就走不通：比白暗就成了"凹进去"。
        background: Item {
            Rectangle {
                id: menuFace
                anchors.fill: parent
                radius: 12                   // kMenuRadius
                color: Theme.color.menuBg
                border.width: 1
                border.color: Theme.color.menuBorder
                visible: false
            }

            MultiEffect {   // 近层：小模糊、小偏移，压住边缘
                anchors.fill: menuFace
                source: menuFace
                shadowEnabled: true
                shadowColor: Theme.color.shadowFloat
                shadowBlur: Math.min(1.0, Theme.shadow.floatBlur / 64)   // 归一化：32px 为满值
                shadowVerticalOffset: 2
                shadowHorizontalOffset: 0
                autoPaddingEnabled: true
            }

            MultiEffect {   // 远层：大模糊、大偏移，做弥散
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
            implicitHeight: Math.min(Math.max(contentHeight, 120), 480)
            contentHeight: menuColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: menuColumn
                width: menuScroll.width
                spacing: 2

                Text {
                    text: qsTrId("model_label")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    leftPadding: root.textInset
                    topPadding: 1
                    bottomPadding: 3
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

                        Text {   // 分组标签：仅多于一个提供方时插（原版 groups.size()>1）
                            width: parent.width
                            visible: Hub.modelGroups.length > 1
                            text: groupHost.modelData.name
                            color: Theme.color.textTertiary
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                            font.weight: Theme.font.weightBold
                            font.letterSpacing: 0.5      // 全大写英文靠字距看起来才"是一组"，而不是一个词
                            leftPadding: root.textInset
                            // 第二组起上方留白：否则 "DEEPSEEK 账户" 会紧贴上一组最后一行，
                            // 被读成那行的第二行副标题。首组也要留 4，才和上方的区块标题分开
                            // （两个都是小号灰字，不隔开就是两行一样的标签）。
                            topPadding: groupHost.index === 0 ? 4 : 8
                            bottomPadding: 0
                            elide: Text.ElideRight
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

                Item {   // 模型区 / 档位区之间的分隔线：两类选项性质不同，只靠字号区分不够
                    width: menuColumn.width
                    height: 12

                    Rectangle {
                        x: root.textInset
                        y: 5
                        width: parent.width - 2 * root.textInset
                        height: 1
                        color: Theme.color.menuBorder
                    }
                }

                Text {
                    text: qsTrId("model_think_depth")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium
                    leftPadding: root.textInset
                    bottomPadding: 3
                }

                Text {   // 档位来自客户端兜底时先说明一句（原版 modelSelectorOptionHint）
                    width: parent.width
                    visible: Hub.useGenericLevels
                    text: qsTrId("model_think_generic_note")
                    wrapMode: Text.WordWrap
                    color: Theme.color.textCaption
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    leftPadding: root.textInset
                }

                Repeater {
                    model: Hub.reasoningLevels

                    delegate: MenuRow {
                        required property var modelData
                        width: menuColumn.width
                        title: modelData.name
                        muted: true
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
