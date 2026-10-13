// 右侧栏（面板坞）：顶栏那颗 ◫ 开关；展开时把「顶栏卡片 + 会话卡片」一起压窄。
//
// 为什么"压窄"由 Shell 驱动、面板自己只做位移 + 淡入（见 Shell.qml 的 rightInset）：
//   · 压窄是**布局**（顶栏与会话区必须一起变窄），只有父层能推动；
//   · 面板是固定宽的卡片，滑动期间若改自己的 width，里面的文字每帧都要重排（白花 CPU 且会闪），
//     只改 x/opacity 的话每帧就一次平移。
//
// 内容用默认属性注入：`RightSidebar { 你的面板 {} }`；没注入时显示占位文案。
// 后续要挂的东西（见 build/temp/dsh-official/GAP-REPORT.md）：终端 / 工作区文件 / 文档预览 / 轨迹。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 内容注入点：外部子项落到 body 里（不是落到卡片根上）
    default property alias contentData: body.data
    // 头部标题
    property string title: "面板"
    // 没有注入内容时的占位（TODO(i18n)：新文案 id 还没进 .ts，先留字面量）
    property string emptyHint: "还没有接入内容\n终端 / 文件 / 预览将挂在这里"
    // 关闭请求：Shell 接到 Hub.closeRightSidebar()
    signal closeRequested()

    Card {
        anchors.fill: parent
        radius: Theme.radius.panel
        padding: 0
        shadowEnabled: true

        Column {
            anchors.fill: parent
            spacing: 0

            // ---- 头部：标题 + 关闭 ----
            Item {
                id: header
                width: parent.width
                height: 44          // 与 TopBar 同高，两条卡片看起来是一套

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing.columnClearance
                    anchors.right: closeButton.left
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    color: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.title
                    font.weight: Theme.font.weightMedium
                    elide: Text.ElideRight
                }

                IconButton {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing.columnClearance
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "✕"
                    tooltip: qsTrId("common_close")
                    onClicked: root.closeRequested()
                }
            }

            // ---- 内容区：注入点 + 空态 ----
            Item {
                id: bodyArea
                width: parent.width
                height: parent.height - header.height

                // 注入点自己是空的：占位文案靠 body.children.length 判断（所以占位不能放进 body）
                Item {
                    id: body
                    anchors.fill: parent
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacing.columnClearance
                    visible: body.children.length === 0
                    text: root.emptyHint
                    color: Theme.color.textTertiary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    lineHeight: 1.4
                }
            }
        }
    }
}
