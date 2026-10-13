// Agent 消息气泡：整条消息套一层圆角气泡（用户气泡也是圆角气泡，两侧对称），
// 内部把模型给的有序段按顺序铺开 ——
//   thinking           -> 可折叠卡（就地展开）
//   toolCall/toolResult-> 工具卡（名称 + 参数/结果）
//   reply              -> 正文段，内部再按代码围栏切成"正文 / 代码块"单元
// 段顺序即"思考、工具调用、回复交错出现"的真实顺序（旧壳 StreamSegment 的语义）。
//
// 注：旧壳的 Agent 消息是**没有气泡**的裸文本；这里按你的要求统一成圆角气泡（有意偏离原版）。

import QtQuick
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property var segments: []
    property bool streaming: false

    implicitHeight: bubble.implicitHeight

    Rectangle {
        id: bubble

        anchors.left: parent.left
        // 限到 82%：左侧留出气泡感，右侧留白；用户气泡是从右侧 72% 收进来的
        width: Math.round(root.width * 0.82)
        implicitHeight: inner.implicitHeight + 2 * Theme.spacing.buttonPadY
        radius: Theme.radius.card
        color: Theme.color.panelBg
        border.width: 1
        border.color: Theme.color.border

        Column {
            id: inner

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.spacing.cardPadding
            anchors.rightMargin: Theme.spacing.cardPadding
            anchors.topMargin: Theme.spacing.buttonPadY
            spacing: Theme.spacing.labelGap

            Repeater {
                model: root.segments

                delegate: Column {
                    id: segment

                    required property var modelData

                    width: inner.width
                    spacing: Theme.spacing.labelGap

                    // ---- 思考 / 工具：折叠卡 ----
                    Collapsible {
                        id: card
                        width: parent.width
                        visible: segment.modelData.type !== "reply"

                        readonly property string segmentType: segment.modelData.type

                        // 工具两处用既有 id；"思考过程"没有合适的既有 id，暂用字面量（TODO(M6)）
                        title: segmentType === "thinking"
                            ? "思考过程"
                            : (segmentType === "toolCall"
                                ? qsTrId("chat_tool_call_fmt").arg(segment.modelData.toolName)
                                : qsTrId("chat_tool_result_label"))
                        meta: segmentType === "thinking" ? "" : (root.streaming ? "进行中…" : "")
                        // 默认**全部收起**（2026-10-03 改）：原来这里写的是
                        //     expanded: segmentType !== "thinking"   // 思考收起、工具卡展开
                        // 展开工具卡的代价实测很实在：25 条消息里有三张长结果卡（1916 / 1301 / 3566px），
                        // 整段内容 10287px 而视口只有 436px —— 滚动条几乎没法用。
                        // 这里**刻意不写 expanded**：Collapsible 自己的默认值就是 false（收起），
                        // 而它内部点击是 `root.expanded = !root.expanded`；若在父层写死绑定，
                        // 第一次点击会被那个赋值打断绑定（能work但很脆），不如根本不绑。

                        // 只读但可选中/复制；高度必须显式给 contentHeight（TextEdit 没有可用的 implicit*）
                        TextEdit {
                            objectName: "bubbleText"
                            width: parent.width
                            height: contentHeight
                            text: segment.modelData.text
                            textFormat: TextEdit.MarkdownText
                            wrapMode: TextEdit.Wrap
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: true
                            activeFocusOnPress: false
                            color: Theme.color.textSecondary
                            selectionColor: Theme.color.selectionBg
                            selectedTextColor: Theme.color.textPrimary
                            font.family: segment.segmentType === "thinking" ? "Microsoft YaHei UI" : "Consolas"
                            font.pixelSize: Theme.font.caption
                        }
                    }

                    // ---- 正文段：正文 / 代码块按出现顺序 ----
                    Repeater {
                        model: (segment.modelData.type === "reply" && segment.modelData.parts)
                            ? segment.modelData.parts
                            : []

                        delegate: Item {
                            id: part

                            required property var modelData

                            width: segment.width
                            implicitHeight: (modelData.kind === "code")
                                ? codeUnit.implicitHeight
                                : proseText.contentHeight

                            // 只读但可选中/复制
                            TextEdit {
                                id: proseText
                                objectName: "bubbleText"
                                visible: part.modelData.kind !== "code"
                                width: parent.width
                                height: contentHeight
                                text: visible ? part.modelData.text : ""
                                textFormat: TextEdit.MarkdownText
                                wrapMode: TextEdit.Wrap
                                readOnly: true
                                selectByMouse: true
                                persistentSelection: true
                                activeFocusOnPress: false
                                color: Theme.color.textPrimary
                                selectionColor: Theme.color.selectionBg
                                selectedTextColor: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                // ⚠️ 这里不能写 lineHeight —— 那是 Text 专有属性，TextEdit 没有；
                                //    写上会让整个 AgentBubble 变成 "Type AgentBubble unavailable"，
                                //    外面表现就是**消息列表整片空白**（踩过一次，qmllint 不报）。
                            }

                            CodeBlock {
                                id: codeUnit
                                visible: part.modelData.kind === "code"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                language: visible ? part.modelData.language : ""
                                code: visible ? part.modelData.text : ""
                                html: visible ? part.modelData.html : ""
                            }
                        }
                    }
                }
            }
        }
    }
}
