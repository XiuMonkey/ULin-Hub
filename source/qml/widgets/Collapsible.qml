// 折叠行：**照 DSH 官方 DisclosureRow 的规格**做（2026-10-20 第二次重写，形态以用户给的截图为最终口径）。
//
// 官方出处（本项目 resources/server/node_modules 里的 dsh 0.2.0-rc.2）：
//   dsh-client-ui-primitives/lib/DisclosureRow.module.css + index.js::DisclosureRow()   ← 通用折叠行
//   dsh-client-ui-chat/lib/client.js::ReasoningRow()                                    ← 思考行
//   ...::ChatGroupSeat / ProcessGroupHeader                                             ← 过程分组行
//   dsh-client-ui-tool/lib/client.js::ToolRow                                           ← 工具行
//
// 规格（截图逐条核对过）：
//   · 行高恒 24px；**没有**底块 / 描边 / 圆角 / 悬停底色 —— 只有一行 tertiary 文字
//   · 左侧 16px 图标位（内部图形 14px）+ 右边距 6；**收起时显示图标，鼠标悬停时图标淡出换成 ⌄，展开换 ⌃**（100ms）
//   · 标题 13px，颜色 textTertiary → 悬停 textSecondary，不加粗
//   · 折叠摘要紧跟在标题**之后内联**，前面一颗 2×2 圆点（两侧 8px），单行省略（官方 .sep / .summary）
//   · 展开/收起是瞬时切换（官方是 display 切换，没有高度动画）
//   · 工具行（ToolRow）传 keepContentWhenOpen ⇒ 展开后摘要仍留在标题行；思考行不传 ⇒ 展开后摘要消失
//   · 工具行正文限高 260px 内滚；思考正文限高 400px 内滚（过程组只做排版容器，自己不再限高 ——
//     组一旦内滚，同组后面的成员行会被卷进滚动内容，见 AgentBubble 里 group 的注）
//   · 最外层 turn 行另有一套排布：**没有 leading 图标**、箭头贴在标题右侧（chevronTrailing）
//
// 图标与箭头都用 Canvas 画：项目没有线性图标资源，而 U+2303/U+2304（⌃⌄）在部分字体里缺字、
// 实测会渲染成豆腐块。Canvas 画出来的笔画 1.1px，与官方 chevron 同档。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Column {
    id: root

    property string title: ""
    property string meta: ""                 // 表头右侧小灰字（保留给其他调用方；官方行没有这一档）
    property string summary: ""              // 折叠态的正文摘要（官方：单行、省略）
    property string glyph: ""                // leading 图标位的字符（没 iconKind 时的兜底）
    property string iconKind: ""             // 官方 leading 的线性图标："" | "think"（四角星）| "tool"（终端 >_）
    property bool expanded: false
    property bool controlled: false          // true ⇒ 自己不改 expanded，只发 toggled（展开态外提给调用方）
    property bool showChevron: true
    property bool chevronTrailing: false      // true ⇒ 箭头贴标题右侧、leading 不占位（最外层 turn 行）
    property bool running: false             // 流式尾巴标记（只透传，不驱动视觉；官方 TextShimmer 的呼吸动画已移除，见 summaryText 的注）
    property bool keepSummaryWhenExpanded: false   // 官方 keepContentWhenOpen（工具行 = true）
    property int maxBodyHeight: 0             // 正文限高（思考 400 / 工具 260）；0 = 不限高

    default property alias body: bodyHolder.data

    signal toggled()   // 受控模式（controlled=true）下由调用方翻转 expanded

    // 点行 / 空格 / 回车都走这里：受控模式只发信号，自持模式自己翻转
    function requestToggle() {
        if (!root.controlled)
            root.expanded = !root.expanded
        root.toggled()
    }

    // 箭头：两条线画 ∧ / ∨（不用字形，理由见文件头）
    function paintChevron(canvas) {
        var ctx = canvas.getContext("2d")
        ctx.reset()
        ctx.strokeStyle = canvas.stroke
        ctx.lineWidth = 1.1
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        if (canvas.up) {
            ctx.moveTo(3.4, 9.2)
            ctx.lineTo(7.0, 5.6)
            ctx.lineTo(10.6, 9.2)
        } else {
            ctx.moveTo(3.4, 5.6)
            ctx.lineTo(7.0, 9.2)
            ctx.lineTo(10.6, 5.6)
        }
        ctx.stroke()
    }

    readonly property bool hovered: headerArea.containsMouse
    // 官方：.row 颜色 label-tertiary，:hover → label-secondary；展开态再亮一档
    readonly property color labelColor: (hovered || expanded) ? Theme.color.textSecondary : Theme.color.textTertiary

    // 摘要只给"一行"：先把换行/制表/连续空格压平，再限长（免得对超长正文跑正则）
    readonly property string summaryLine: summary.substr(0, 600).trim().replace(/\s+/g, " ")

    readonly property int rowHeight: 24      // 官方 .row { height: 24px }
    readonly property int leadingBox: 16     // 官方 .leading { width/height: 16px }
    readonly property int leadingGap: 6      // 官方 .leading { margin-right: 6px }
    readonly property int dotGap: 8          // 官方 .sep { margin: 0 8px }

    // ---- 标题行 ----
    Item {
        id: header
        width: root.width
        implicitHeight: root.rowHeight

        // 键盘可达：官方 rowExpands 时 role=button + Enter/Space
        activeFocusOnTab: true
        Keys.onSpacePressed: function(event) {
            root.requestToggle()
            event.accepted = true
        }
        Keys.onReturnPressed: function(event) {
            root.requestToggle()
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            root.requestToggle()
            event.accepted = true
        }

        // ---- 左侧图标位（16px 盒）----
        Item {
            id: leading
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: root.leadingBox
            height: root.leadingBox
            visible: !root.chevronTrailing

            // 收起态的活动图标（官方 14px SVG；这里 Canvas 画）
            Canvas {
                id: kindIcon
                anchors.centerIn: parent
                width: 14
                height: 14
                visible: opacity > 0
                opacity: (root.iconKind !== "" && !root.expanded && !root.hovered) ? 1 : 0
                property color stroke: root.labelColor
                onStrokeChanged: requestPaint()
                property string kind: root.iconKind
                onKindChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = stroke
                    ctx.lineWidth = 1.1
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"

                    if (kind === "think") {
                        // 思考：四角星（sparkle）。2026-10-20 由"圆角方框 + 交叉"换掉 ——
                        // 用户反馈那个形状像"复选框打了个叉/取消"，而这一行的语义是"模型在想"。
                        // 四角星是 AI 生成语境里的通用符号，1.1px 线条 + 内凹曲线在 14px 下仍认得出。
                        // 画法：外尖 + 内凹各一条直线组成的八点星（尖端贴 1.6 / 12.4，腰部收到 5.6）。
                        // 用直线而不是曲线：14px 下贝塞尔的内凹会被抗锯齿糊成一团，直线星反而认得出。
                        ctx.beginPath()
                        ctx.moveTo(7.0, 1.6)     // 上尖
                        ctx.lineTo(8.4, 5.6)     // 右上内凹
                        ctx.lineTo(12.4, 7.0)    // 右尖
                        ctx.lineTo(8.4, 8.4)     // 右下内凹
                        ctx.lineTo(7.0, 12.4)    // 下尖
                        ctx.lineTo(5.6, 8.4)     // 左下内凹
                        ctx.lineTo(1.6, 7.0)     // 左尖
                        ctx.lineTo(5.6, 5.6)     // 左上内凹
                        ctx.closePath()
                        ctx.stroke()
                    } else if (kind === "tool") {
                        // 工具/命令：终端提示符 `>_`
                        ctx.beginPath()
                        ctx.moveTo(3.0, 3.4)
                        ctx.lineTo(6.8, 7.0)
                        ctx.lineTo(3.0, 10.6)
                        ctx.stroke()

                        ctx.beginPath()
                        ctx.moveTo(8.2, 10.6)
                        ctx.lineTo(11.8, 10.6)
                        ctx.stroke()
                    }
                }
            }

            // 兜底：没有 iconKind 时可以塞一个字符当图标
            Text {
                id: glyphText
                anchors.centerIn: parent
                text: root.glyph
                visible: opacity > 0
                opacity: (root.iconKind === "" && root.glyph.length > 0 && !root.expanded && !root.hovered) ? 1 : 0
                color: root.labelColor
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                Behavior on opacity {
                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                }
            }

            // 箭头：没有图标时常显；有图标时只在悬停/展开出现
            Canvas {
                id: chevronIcon
                anchors.centerIn: parent
                width: 14
                height: 14
                visible: opacity > 0 && root.showChevron && !root.chevronTrailing
                opacity: {
                    if (!root.showChevron || root.chevronTrailing)
                        return 0
                    var hasIcon = (root.iconKind !== "" || root.glyph.length > 0)
                    if (root.expanded || root.hovered)
                        return 1
                    return hasIcon ? 0 : 1
                }
                property color stroke: root.labelColor
                onStrokeChanged: requestPaint()
                property bool up: root.expanded
                onUpChanged: requestPaint()
                onPaint: root.paintChevron(chevronIcon)

                Behavior on opacity {
                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                }
            }
        }

        // ---- 标题（官方 .title：13px、不加粗、flex:none）----
        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: root.chevronTrailing ? 0 : (root.leadingBox + root.leadingGap)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Math.max(0, parent.width - x - metaText.width - root.dotGap
                - (root.chevronTrailing ? trailingChevron.width + 4 : 0)))
            text: root.title
            color: root.labelColor
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
            elide: Text.ElideRight
            Behavior on color {
                ColorAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
        }

        // ---- 尾随箭头（chevronTrailing）：最外层 turn 行「已完成，用时 1 分 21 秒 ⌃」----
        Canvas {
            id: trailingChevron
            anchors.left: titleText.right
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            visible: root.chevronTrailing && root.showChevron
            property color stroke: root.labelColor
            onStrokeChanged: requestPaint()
            property bool up: root.expanded
            onUpChanged: requestPaint()
            onPaint: root.paintChevron(trailingChevron)
        }

        // ---- 摘要前的圆点（官方 .sep：2×2、圆角 1、margin 0 8px、颜色 caption）----
        Rectangle {
            id: sepDot
            objectName: "summaryDot"
            anchors.left: root.chevronTrailing ? trailingChevron.right : titleText.right
            anchors.leftMargin: root.dotGap
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 2
            radius: 1
            color: Theme.color.textCaption
            visible: summaryText.visible
        }

        // ---- 折叠摘要（官方 .summary：与标题同档、单行省略、flex:auto）----
        Text {
            id: summaryText
            objectName: "summaryText"
            anchors.left: sepDot.right
            anchors.leftMargin: root.dotGap
            anchors.right: metaText.visible ? metaText.left : parent.right
            anchors.rightMargin: metaText.visible ? root.dotGap : 0
            anchors.verticalCenter: parent.verticalCenter
            visible: root.summaryLine.length > 0 && width > 24
                   && (root.keepSummaryWhenExpanded || !root.expanded)
            text: root.summaryLine
            // 官方 .summary 不设 color ⇒ 继承行的 label-tertiary（悬停变 secondary），与标题同步
            color: root.labelColor
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
            elide: Text.ElideRight
            maximumLineCount: 1

            // 官方在流式尾巴上给摘要套 TextShimmer（高亮扫光）。**本版不做任何动画**：
            // 之前用"整段透明度 1→0.55→1 呼吸"近似，流式时每 700ms 一次、看起来就是"这一行在闪"，
            // 而且还拖累 MessageListView 的贴底（每次 opacity 变化都触发一次重绘）。
            // running 只保留成属性（调用方还在算"这一段是不是流式尾巴"），不再驱动视觉。
        }

        Text {
            id: metaText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.meta.length > 0
            text: root.meta
            color: Theme.color.textCaption
            font.pixelSize: Theme.font.caption
        }

        MouseArea {
            id: headerArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                header.forceActiveFocus()
                root.requestToggle()
            }
        }
    }

    // ---- 正文 ----
    // 瞬时显隐（官方 display 切换）：不加高度动画 —— 动画中间值会让 MessageListView 的贴底/锚点抖。
    Item {
        id: bodyWrapper
        width: root.width
        // 只裁自己的内容；高度**必须**等于滚动区的实际占高：wrapper 是 root Column 的兄弟排布依据，
        // 下一条内容（过程组里的工具调用行等）从 bodyFlick.height 之后开始排。滚动区绝不冒出去。
        clip: true
        implicitHeight: root.expanded ? bodyFlick.height : 0

        Flickable {
            id: bodyFlick
            objectName: "collapseBody"   // 供离屏试装台断言"限高内滚"的真实高度
            width: parent.width
            // 高度 = min(实际内容高, 限高)；不限高时就是内容高。
            // 刻意用 bodyHolder.implicitHeight 而不是 contentHeight：后者含尾部呼吸区（见下），
            // 呼吸区只该出现在可滚内容里，不该把短内容也撑高 4px。
            height: (root.maxBodyHeight > 0)
                ? Math.min(bodyHolder.implicitHeight, root.maxBodyHeight)
                : bodyHolder.implicitHeight
            contentWidth: width
            // 尾部呼吸区（4px，与滚动条上下 padding 同档）：只在真的可滚时计进 contentHeight。
            // 滚到底时 contentY 的边界就是精确的 contentHeight - height，最后一行下面因此还留 4px，
            // 不会被 Flickable 的裁切边削掉一截（改前最后一行正好压在 400 的边上，实测 bottomMargin=0）。
            contentHeight: bodyHolder.implicitHeight + (needsScroll ? scrollTail : 0)
            boundsBehavior: Flickable.StopAtBounds
            // 只在真的可滚时才让 Flickable 抢拖拽（否则会吃掉正文里的文字选择）
            interactive: needsScroll
            visible: root.expanded

            // ---- 是否真的需要滚动 ----
            // 判据只看「实际内容高 > 限高」这个布局事实，不再拿 contentHeight / height 互算：
            //   needsScroll 本身参与 scrollGutter → bodyHolder.width → 换行数 → 内容高；
            //   用 contentHeight > height 判，会把“尾部 4px / 滚动沟 12px”这些本判据的**产物**再吃回来，
            //   在限高边界上来回翻。内容高 vs 常数限高没有这个回路（与 Composer.editorView.needsScroll 同义）。
            // 2026-10-20 修“滚到底最后一行被裁”：可滚时 contentHeight 额外加 scrollTail，滚到底留 4px。
            readonly property int scrollTail: 4
            readonly property bool needsScroll: root.maxBodyHeight > 0
                && bodyHolder.implicitHeight > root.maxBodyHeight + 0.5

            // 右侧给滚动条留的沟：滚动条厚度（AppScrollBar 的 implicitWidth=10）+ 2px 呼吸缝。
            // 取 implicitWidth 而不是写死 10：厚度令牌以后若改（或皮肤包换了实现），这里跟着走。
            readonly property int scrollGutter: needsScroll
                ? scrollBar.implicitWidth + 2
                : 0

            // 滚轮：**推理区自己能滚时，由它独占滚轮**（到顶/到底也不把事件放给外层消息列表）。
            // 为什么必须自己接管：Qt 的 Flickable 在内容边缘**不消费**滚轮 —— 实测内层到底后再滚一格，
            // 内层停在 maxContentY 不动、外层 ListView 却跟着滚了 120px（正是用户说的"滚一下跳两处"）。
            // 反过来，内容没超高时下面的 WheelHandler 直接 enabled=false，滚轮照常落到外层列表 ——
            // 鼠标停在"展开但其实很短"的推理区上时列表还能滚。
            // 步长与 Composer 一致（一格 60px）；pixelDelta 是触控板真实像素，不乘系数。
            function handleWheel(angleY, pixelY) {
                if (!needsScroll)
                    return false
                var dy = (pixelY !== 0) ? pixelY : angleY / 2
                if (dy === 0)
                    return false
                var maxY = Math.max(0, contentHeight - height)
                contentY = Math.min(maxY, Math.max(0, contentY - dy))
                return true
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                // 不可滚时整只关掉：WheelHandler 只要参与投递就会把事件截住（实测里面对 accepted 写 false
                // 也不会再交给祖先的 WheelHandler）—— 展开但很短的正文会因此吞掉外层列表的滚轮。
                enabled: bodyFlick.needsScroll
                onWheel: function(event) {
                    event.accepted = bodyFlick.handleWheel(event.angleDelta.y, event.pixelDelta.y)
                }
            }

            // 滚动条：挂在 Flickable 上（框架摆到右缘、按 visibleArea 定长度），这里只负责
            //   · 不需要滚动时整条关掉（policy 用 AlwaysOff，把手也不会画）；
            //   · 可见把手的几何：Basic 样式默认 padding:2 会把 contentItem 四边各缩 2px ——
            //     10px 厚的滚动条可见把手只剩 4px，还上下顶满区块。左右归零找回 AppScrollBar 设计的
            //     8px 把手（与它头注里的 10/8 一致），上下各留 4px，不贴推理区的上下边缘。
            //     注：其它地方的 AppScrollBar 仍是 4px 把手（同一个 padding 坑）；这里先只修推理区，
            //     全局改会把 Composer 只按 8px 预留的宽度顶到字上，需要一起调，暂不动。
            ScrollBar.vertical: AppScrollBar {
                id: scrollBar
                objectName: "collapseScrollBar"   // 供离屏试装台定位
                policy: bodyFlick.needsScroll ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                leftPadding: 0
                rightPadding: 0
                topPadding: 4
                bottomPadding: 4
            }

            Column {
                id: bodyHolder
                // 右侧让出 scrollGutter（滚动条只在需要时出现，沟宽随之在 0 / thickness+2 之间切）。
                width: bodyFlick.width - bodyFlick.scrollGutter
                spacing: Theme.spacing.buttonGap
            }
        }
    }
}
