// Agent 消息气泡：整条消息套一层圆角气泡（用户气泡也是圆角气泡，两侧对称），
// 内部把模型给的有序段按顺序铺开 ——
//   thinking           -> 可折叠卡（就地展开）
//   toolCall/toolResult-> 工具卡（名称 + 参数/结果）
//   reply              -> 正文段，内部再按代码围栏切成"正文 / 代码块"单元
// 段顺序即"思考、工具调用、回复交错出现"的真实顺序（旧壳 StreamSegment 的语义）。
//
// 注：旧壳的 Agent 消息是**没有气泡**的裸文本；这里按你的要求统一成圆角气泡（有意偏离原版）。
// 但"气泡怎么立住"仍按原版：**不画描边**，用原版那层极淡的外阴影（shadowSubtle）给层次 —— 见下面 bubble 的注。

import QtQuick
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property var segments: []
    property bool streaming: false

    implicitHeight: bubble.implicitHeight

    // 柔和外阴影：原版 Agent 消息单元画的不是描边，而是 `shadowSubtle`（AgentMessageUnit.cpp:1011）
    // 那层几乎看不见的晕 —— 真机像素实测原版气泡左缘是 #FAFBFD→#F8F9FB→#F4F5F7→#ECEDEF 的四段渐隐，
    // 本版原来是一条 #E5E7EB 硬线（2 物理px），这就是"边框太深/太实"的主因。
    // 做法：拿一块同形状的实心矩形当 MultiEffect 的源，气泡本体（下面那层、不透明）正好盖住它，
    // 于是只露出晕开的一圈，不会多画任何颜色；参数与 Card 同一档（= 原版 CardShadow{8,2,*12}）。
    Rectangle {
        id: shadowShape

        anchors.left: parent.left
        width: bubble.width
        height: bubble.implicitHeight
        radius: bubble.radius
        color: Theme.color.panelBg
        visible: false   // 只做 MultiEffect 的源（与 widgets/Card.qml 的 surface 同一套写法）
    }

    MultiEffect {
        anchors.fill: shadowShape
        source: shadowShape
        shadowEnabled: true
        shadowColor: Theme.color.shadowSubtle
        shadowBlur: Math.min(1.0, Theme.shadow.cardBlur / 32)   // shadowBlur 是 0..1，按 32px 满值线性映射
        shadowVerticalOffset: Theme.shadow.cardDy
        autoPaddingEnabled: true
    }

    Rectangle {
        id: bubble

        anchors.left: parent.left
        // 限到 82%：左侧留出气泡感，右侧留白；用户气泡是从右侧 72% 收进来的
        width: Math.round(root.width * 0.82)
        implicitHeight: inner.implicitHeight + 2 * Theme.spacing.buttonPadY
        radius: Theme.radius.card
        color: Theme.color.panelBg
        // 无描边：原版 `#agentBubble` / `#agentUnit` 只有 `background: {{panelBg}} + border-radius: 12px`
        // （chat.qss），一条 border 都没有 —— 层次由上面那层柔和阴影给，不靠线。

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

                        // 三处标题都走既有 id（思考箱没有别的候选词，另起了 chat_thinking_title）
                        title: segmentType === "thinking"
                            ? qsTrId("chat_thinking_title")
                            : (segmentType === "toolCall"
                                ? qsTrId("chat_tool_call_fmt").arg(segment.modelData.toolName)
                                : qsTrId("chat_tool_result_label"))
                        meta: segmentType === "thinking" ? "" : (root.streaming ? qsTrId("chat_streaming_meta") : "")
                        // 只有思考段给折叠摘要：工具卡的表头业已写着工具名与流式状态
                        summary: segmentType === "thinking" ? segment.modelData.text : ""
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
