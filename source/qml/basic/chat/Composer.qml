// 输入区（原版 ui/ChatInputWidget.cpp + chat.qss 复刻，视觉上做了一次"成套化"收敛）
//
// 结构（自上而下，与原版一一对应）：
//   ① 细阴影卡片（圆角 Theme.radius.composer=16；1px composerBorder；MultiEffect 阴影取
//      Theme.shadow.inputBlur/inputDy = 8/2，与 Card 的 cardBlur/cardDy 同一档，克制不发光）
//   ② 卡片内容列（左右统一 cardPadH=16、上 cardPadTop=12、下 cardPadBottom=12、段间距 cardGap=8）：
//        ├ 输入框（13px；左右内边距=cardPadH、上=editorPadTop 4、下 0；高 48–140 =
//        │  editorMinHeight–editorMaxHeight，单行最小、多行跟内容长；到上限后由外层 Flickable
//        │  滚动，右侧出现 AppScrollBar，**未超高时不画**）—— 文字左上角内缩 16/16，与卡片左边距同列
//        └ #inputControlRow（高 = 发送键 32）：左 chip（24px 配角）、右发送键（32×32 圆钮），
//          两者各贴一边、行内垂直居中；chip 文字与编辑器文字同列，圆钮右缘与编辑器文字右缘同列
//   ③ sessionStatsLine：固定 14px、**始终显示**（空串不画字）；上沿离卡片下沿 statsGap(4px)、
//      字号 caption(12) —— 它是卡片的附属说明，不跟编辑区抢视线
//
// 视觉约定（**改一处即全局成套**）：内边距/行距全部由 token 派生（cardPadding / sectionGap /
// labelGap / editorPadTop），颜色圆角一律取 token；字号三档相邻（13 / 12 / 11），
// 聚焦态只把 1px 描边从 10% 黑加深到 textSecondary 灰（**不用 accent 蓝**），不加发光、不加第二层阴影。
//
// 与原版一致的键位：Enter/Return 无 Shift 则发送（输出中则中止）——**Ctrl+Enter 也发送**；
// Shift+Enter 换行。输入区**从不禁用**；空文本静默丢弃（拦截在宿主）。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 扩展注入点：扩展用 HostUi.item("composer") 拿到本对象后 setParentItem 进来（见 HostExports.h 的名字契约）
    Component.onCompleted: HostUi.registerItem("composer", this)

    // 斜杠命令菜单句柄：菜单是 Popup，可视项挂在 Overlay 下、不在本项的 children 里，
    // 外部（离屏试装台、扩展）要读它的状态只能走这个转发。
    readonly property var commandMenuRef: commandMenu

    property bool streaming: Hub.streaming

    // ---- 视觉节奏（只在这里定数，下面全部引用它，保证内边距/行距/对齐三者成套）----
    // 左右统一内边距 16（cardPadding）：编辑器文字左缘、编辑器文字右缘、发送键右缘都落在这一列上
    readonly property real cardPadH: Theme.spacing.cardPadding
    // 上内边距 12（sectionGap）+ 编辑器自身的 editorPadTop(4) = 16 = cardPadH：左上角内缩成正方形
    readonly property real cardPadTop: Theme.spacing.sectionGap
    // 下内边距：原 12（sectionGap）—— 用户要求"调用量小字的底部对齐会话栏底部"，
    // 于是宿主那条 composerLoader.bottomMargin 归零，这里也收到 4（editorPadTop）
    // ⇒ 小字底距会话栏底约 4px，视觉上就是对齐。
    readonly property real cardPadBottom: Theme.spacing.editorPadTop
    // 编辑区与控制行/用量小字之间的行距：原 8（labelGap）⇒ 用户要求"再近一些"，收到 2（panelGap）。
    // 这一列内部的行距统一走它，所以编辑区↔chip↔小字会一起收紧（观感更成套）。
    readonly property real cardGap: Theme.spacing.panelGap

    // 发送键的"可用"观感：空文本且未在输出中 ⇒ 压暗。
    // ⚠️ **纯视觉**：按钮依旧可点（原版语义 = 空文本静默丢弃，拦截在宿主），键盘 Enter 规则也不变。
    readonly property bool canSend: streaming || editor.text.trim().length > 0

    implicitHeight: shadowHost.height + Theme.size.statsLineHeight

    function submit() {
        const text = editor.text
        if (text.trim().length === 0)
            return      // 空文本静默丢弃（原版由宿主拦，这里同一语义）
        Hub.sendPrompt(text)
        editor.clear()
        editor.forceActiveFocus()
    }

    function stop() {
        Hub.cancelPrompt()
    }

    // 按键路由（单独一个函数：规则集中一处，也便于离屏测）。
    // 返回 true = 已消费（调用方把 event.accepted 置真）。
    function handleKey(event) {
        // 斜杠命令菜单优先吃键：↑↓ 选、Tab 补全、Esc 收起
        if (commandMenu.visible) {
            if (event.key === Qt.Key_Down) {
                commandMenu.move(1)
                return true
            }
            if (event.key === Qt.Key_Up) {
                commandMenu.move(-1)
                return true
            }
            if (event.key === Qt.Key_Escape) {
                commandMenu.dismissed = true
                commandMenu.close()
                return true
            }
            if (event.key === Qt.Key_Tab) {
                commandMenu.complete()
                return true
            }
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // 只有 Shift 例外（Ctrl+Enter 也发送，与原版 eventFilter 一致）
            if (event.modifiers & Qt.ShiftModifier)
                return false
            // 候选还没敲全 ⇒ 这一下是"补全"，不是提交
            if (commandMenu.visible && !commandMenu.isExact()) {
                commandMenu.complete()
                return true
            }
            if (root.streaming)
                root.stop()
            else
                root.submit()
            return true
        }
        return false
    }

    // ---- ① 阴影卡片 ----
    Item {
        id: shadowHost
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: capsule.implicitHeight

        // 阴影：blur 8 / dy 2 / radius 16（token inputBlur/inputDy/inputRadius）——
        // 与 Card 的 cardBlur/cardDy 同一档，把原来"更重的一档"收平，避免输入卡比顶栏卡片更抢眼
        MultiEffect {
            anchors.fill: capsule
            anchors.topMargin: Theme.shadow.inputDy
            source: capsule
            shadowEnabled: true
            shadowBlur: Theme.shadow.inputBlur
            shadowColor: Theme.color.shadowSubtle
            shadowVerticalOffset: Theme.shadow.inputDy
            shadowHorizontalOffset: 0
            autoPaddingEnabled: true
        }

        // ---- ② 输入卡片 ----
        Rectangle {
            id: capsule
            visible: false                 // 只作为上面 MultiEffect 的 source 绘制
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            // 高度 = 内容 + 上下内边距（不再是写死的 +10，改内边距时高度自动跟着成套）
            implicitHeight: content.implicitHeight + root.cardPadTop + root.cardPadBottom
            radius: Theme.radius.composer
            // composerBg：可选 token。壁纸类皮肤需要单独把输入卡做成半透明，
            // 但输入卡与 Agent 气泡共用 panelBg（AgentBubble.qml），靠 panelBg 区分不开，
            // 所以给输入卡留一个专属键；皮肤不定义它时回退 panelBg（行为与原来完全一致）。
            color: Theme.color.composerBg || Theme.color.panelBg
            border.width: 1
            // 聚焦态：只把描边换成**更深的灰**（Theme.color.textSecondary），不发光、不加阴影、不改高度。
            // 为什么不用 accent 蓝：输入区是常驻控件，蓝描边在整个界面里过于抢眼，也把"主色"用在了
            // 一个只需要"我在这儿"提示的地方；灰阶描边与静息态（composerBorder，10% 黑）同一族，只深一档。
            border.color: editor.activeFocus ? Theme.color.textSecondary : Theme.color.composerBorder
        }

        // 真实内容（与 capsule 同尺寸；卡片本体不参与绘制）
        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: root.cardPadTop
            spacing: root.cardGap

            // ---- 编辑区：高度内容驱动（单行 = editorMinHeight、多行跟内容长、封顶 editorMaxHeight），
            //      封顶之后靠 Flickable 滚动 + **只在超高时**出现 AppScrollBar ----
            // 为什么要一层 Flickable：TextEdit 自己那套"光标可见就内部挪一下"没有对外暴露 contentY，
            // 挂不了滚动条；把它放进 Flickable、让 TextEdit 的高度等于全文高度，滚动就归 Flickable 管。
            Flickable {
                id: editorView
                width: parent.width
                // 高度：内容 + 上下内边距，夹在 [editorMinHeight, editorMaxHeight] 之间
                // ⇒ 单行 = min（用户说的"再高 10px"落在这个 token 上：38 → 48）；
                //    多行跟着内容长；到 max 就不再长（此时进入滚动）。
                // 行高实测：13px "Microsoft YaHei UI" 每行 17 逻辑 px（editorMaxHeight=140 ⇒ 8 行 + 上内边距 4）。
                height: Math.min(Math.max(editor.contentHeight + editor.topPadding + editor.bottomPadding,
                                          Theme.size.editorMinHeight),
                                 Theme.size.editorMaxHeight)
                contentWidth: width
                contentHeight: editor.height
                clip: true
                // 关掉拖拽 flick：编辑区里按住鼠标是**选文字**，不能让 Flickable 抢走鼠标；
                // 滚轮不受 interactive 影响（Flickable 自己处理滚轮），滚动条照常可拖。
                interactive: false
                boundsBehavior: Flickable.StopAtBounds

                // 只有"内容超高"时才需要给滚动条让出宽度（未超高时不减，单行的换行宽度与改动前一致）。
                // 判据直接用几何：contentHeight 比可视高度大 ⇒ 一定滚得动，与 ScrollBar 的 AsNeeded 同义。
                readonly property bool needsScroll: contentHeight > height + 0.5

                // 滚动条：挂在 Flickable 上（框架负责摆到右缘、按 position/size 定长度）。
                // policy 用 AsNeeded：内容没超高就不画，不会常驻一条占位。
                ScrollBar.vertical: AppScrollBar {
                    id: editorScrollBar
                    objectName: "composerScrollBar"   // 供离屏试装台定位（同 composerEditor 的约定）
                    policy: editorView.needsScroll ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                // 光标跑出可视区时把内容滚过去（TextEdit 现在高度等于全文高度 ⇒ 它自己不滚了，
                // 光标可见性归这里管）。上沿/下沿各留 4px（editorPadTop）免得光标贴着边。
                function ensureCursorVisible() {
                    if (!needsScroll)
                        return
                    const r = editor.cursorRectangle
                    const maxY = Math.max(0, contentHeight - height)
                    if (r.y < contentY)
                        contentY = Math.max(0, r.y - Theme.spacing.editorPadTop)
                    else if (r.y + r.height > contentY + height)
                        contentY = Math.min(maxY, r.y + r.height - height + Theme.spacing.editorPadTop)
                }

                // 高度变化（换行/删行）后光标可能落到可视区外：跟着滚一把
                onHeightChanged: ensureCursorVisible()

                // 滚轮：编辑区自己滚内容（**不吞事件**：真的滚了才改 contentY，滚不动时什么也不做）。
                // 显式写一个 WheelHandler 而不是只靠 Flickable 内建行为：interactive 关掉后靠它更稳，
                // 也不会和"按住拖选文字"抢鼠标（拖拽仍归 TextEdit）。
                WheelHandler {
                    onWheel: (event) => {
                        const maxY = Math.max(0, editorView.contentHeight - editorView.height)
                        if (maxY <= 0)
                            return
                        editorView.contentY = Math.min(maxY, Math.max(0,
                            editorView.contentY - event.angleDelta.y / 2))
                    }
                }

                TextEdit {
                    id: editor
                    objectName: "composerEditor"   // 供离屏试装台定位（见 misc/tools/qml-harness.py）
                    // 宽度：超高时右侧让出滚动条厚度（文字换行宽度因此窄一点，但不会被滚动条压住）；
                    // 未超高时全额宽度（滚动条此时根本不画）。
                    width: editorView.width - (editorView.needsScroll
                                               ? Theme.size.scrollbarThickness : 0)
                    // 内边距：左右 = cardPadH（与卡片左右内边距同列）/ 上 4 / 下 0
                    leftPadding: root.cardPadH
                    rightPadding: root.cardPadH
                    topPadding: Theme.spacing.editorPadTop
                    bottomPadding: 0
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    selectByMouse: true
                    persistentSelection: true
                    color: Theme.color.textPrimary
                    selectionColor: Theme.color.selectionBg
                    selectedTextColor: Theme.color.textPrimary
                    font.family: "Microsoft YaHei UI"
                    // 13px（Theme.font.body）：与 widgets/SearchField.qml 的输入文字同档。
                    // 原来取 lead(16) 比全应用正文大 3px，输入区显得"字很大"；现在整条输入区的字号是
                    // 13（正文/chip 之上）→ 12（chip、菜单说明、统计行）→ 11（▾、分组标签）三档相邻，
                    // 主次靠**颜色**（textPrimary / textSecondary / textTertiary）拉开，不靠字号断层。
                    font.pixelSize: Theme.font.body
                    // 高度 = 全文高度（不再自己夹 min/max）：夹取与滚动都在外层 editorView 上，
                    // 这样超高时滚动条才有东西可滚。
                    // ⚠️ TextEdit 的 implicitHeight 是只读的（原版也是 setFixedHeight 而非设 implicit），
                    // 所以这里给 height 赋值。
                    height: contentHeight + topPadding + bottomPadding
                    // 光标移动（打字/方向键/点选）后跟着滚
                    onCursorRectangleChanged: editorView.ensureCursorVisible()

                    // 占位符（空文本时画）：与正文同列同基线
                    Text {
                        anchors.left: parent.left
                        // 占位符与正文同列同基线：直接引用编辑器自己的内边距/字体，改一处不会跑偏
                        anchors.leftMargin: editor.leftPadding
                        anchors.top: parent.top
                        anchors.topMargin: editor.topPadding
                        visible: editor.text.length === 0
                        text: qsTrId("chat_input_placeholder")
                        // 占位符再弱一档（textCaption），与 widgets/SearchField.qml 的 placeholder 同色
                        color: Theme.color.textCaption
                        font.family: editor.font.family
                        font.pixelSize: editor.font.pixelSize
                    }

                    onTextChanged: {
                        commandMenu.dismissed = false     // 一敲字就重新参与候选（Esc/点外面收起后不要赖着不走）
                        commandMenu.syncVisibility()      // Popup 只能显式开关（见 commandMenu 上的说明）
                        // 刚打出 `/` 时懒拉一次命令表：会话切换时宿主已拉过，这里兜"开着会话时新注册了命令"
                        if (text === "/")
                            Hub.commands.refresh(Hub.currentSessionId)
                    }

                    Keys.onPressed: (event) => {
                        if (root.handleKey(event))
                            event.accepted = true
                    }
                }
            }

            // ---- #inputControlRow：左 chip / 右发送键（配角区）----
            // 用锚点而不是 Row + 手算 stretch：两件配角各贴一边、在同一行里垂直居中，
            // 行高就取发送键高（chip 更矮，居中后与圆钮同心）。这样既没有"手算宽度在
            // chip 隐藏（width 0）时算错"的隐患，也不再需要那个 height:1 的占位块。
            Item {
                id: controlRow
                width: parent.width
                implicitHeight: sendButton.implicitHeight

                // 模型 / 思考档位 chip（配角：24px、12px、次级色；无模型时自身隐藏 ⇒ 宽 0）
                ModelSelectorChip {
                    id: chip
                    anchors.left: parent.left
                    // chip 内部左右各留 labelGap(8)，这里再退 8 ⇒ chip **文字**正好落在
                    // cardPadH(16) 这一列上，与上面的输入文字左对齐（悬停底色会往左溢出 8px，
                    // 那是 chip 的点击热区，属于有意为之）。
                    anchors.leftMargin: root.cardPadH - Theme.spacing.labelGap
                    anchors.verticalCenter: parent.verticalCenter
                    onSelectModelRequested: (provider, model) => Hub.selectModel(provider, model)
                    onSelectLevelRequested: (levelId) => Hub.selectLevel(levelId)
                }

                IconButton {
                    id: sendButton
                    anchors.right: parent.right
                    // 圆钮右缘 = 编辑器文字右缘（同在 cardPadH 这一列），不再比文字多探出 4px
                    anchors.rightMargin: root.cardPadH
                    anchors.verticalCenter: parent.verticalCenter
                    boxSize: Theme.size.sendButton   // 32×32
                    // 原版：enter-btn.png / stop-btn.png（ChatInputWidget.cpp:334/418）。
                    // 现在是**成对**资源，按主题选一套（IconButton.qml:68 写明的约定，与 SessionBar 的 logo 同例）：
                    //   浅色 = 深灰圆 #343A42 + 白图形 ｜ 暗色 = 白圆 #FFFFFF + 深灰图形（两套几何逐像素相同，只换色）。
                    // 四条路径都写字面量、不拼后缀：改名/挪位时 grep 得到，也免得白圆悄悄贴到白面板上整个消失
                    // （见 ulinhub.qrc 里那段注记）。
                    readonly property string sendIcon: Theme.isDark
                        ? "qrc:/ULinHub/buttons/enter-btn-dark.png"
                        : "qrc:/ULinHub/buttons/enter-btn.png"
                    readonly property string stopIcon: Theme.isDark
                        ? "qrc:/ULinHub/buttons/stop-btn-dark.png"
                        : "qrc:/ULinHub/buttons/stop-btn.png"
                    iconSource: root.streaming ? stopIcon : sendIcon
                    iconSize: Theme.size.sendButton
                    // 四态：可用（满不透明=1.0）/ 空文本（压暗=0.45，视觉上的"不可用"）/
                    //      悬停（圆形蒙版 35%）/ 按下（55%）—— 由 circleHoverOverlay 统一给
                    opacity: root.canSend ? 1.0 : 0.45
                    circleHoverOverlay: root.canSend
                    // 蒙版基色跟着圆底走：暗色主题下圆底是**白的**，白蒙版叠上去等于没叠
                    // （悬停/按下会完全没有反馈）⇒ 白底改成压暗；浅色主题维持原样的提亮。
                    circleHoverTint: Theme.isDark ? "#000000" : "#FFFFFF"
                    tooltip: root.streaming ? qsTrId("chat_stop_output") : qsTrId("common_send")
                    // 原版：按钮**永远可用**（没有 disabled 判定）；上面只改观感，行为一字不动
                    onClicked: root.streaming ? root.stop() : root.submit()
                }
            }
        }
    }

    // ---- ③ 小灰字统计行：固定 14px、始终显示、空串不画字 ----
    Text {
        id: statsLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // 用户要的是"小字**底边**对齐会话栏底边"⇒ 取 0：本行底边正好落在 root.bottom
        // （= Shell composerLoader 的底 = 会话栏底边，因为那条 bottomMargin 已经是 0）。
        // 不取 -statsGap（那会让小字落到列底**下方** 4px，轻微溢出）；也不动 root.implicitHeight
        // （band 里本来就含 statsLineHeight）⇒ 卡片位置与会话区那条缝都不受影响。
        anchors.bottomMargin: 0
        height: Theme.size.statsLineHeight
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        text: Hub.statsLine
        elide: Text.ElideRight
        color: Theme.color.textTertiary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption

        // 仅在被省略号截断时给 tooltip（原版 syncToolTip 同规则）
        AppToolTip {
            visible: statsLine.truncated && statsLineMouse.containsMouse
            text: Hub.statsLine
        }

        MouseArea {
            id: statsLineMouse
            anchors.fill: parent
            hoverEnabled: true
        }
    }

    // ---- ④ 斜杠命令菜单 ----
    // 用 Popup（= 画在窗口 Overlay 层）而不是内联 Rectangle，原因是实测踩到过一次：
    //   · Composer 的兄弟节点 messageLoader 是**后声明**的（见 basic/shell/Shell.qml），
    //     后声明的兄弟画在上面 ⇒ 菜单被消息列表压住，列表里那颗「加载更多」直接盖在菜单上；
    //   · 命中测试跟绘制同序 ⇒ 菜单行根本点不到，点了是列表/按钮收到事件。
    //   `z` 只在同一个父节点内排序，加多大都没用；Popup 走 Overlay，天然在所有普通项之上。
    // `focus: false` 沿用 ModelSelectorChip 的做法：不抢输入框焦点，用户可以一边看候选一边继续敲。
    // 候选来自引擎（Hub.commands.items），这里只做前缀过滤 —— 本地不维护第二份命令表，"认不认识"由引擎裁定。
    Popup {
        id: commandMenu
        objectName: "composerCommandMenu"   // 供离屏试装台定位（Popup 不在 children 里，试装台走 Overlay）

        // 只在"还在敲命令名"时有候选：`/com` 会过滤，`/compact ` 之后（名字已敲定）自动收起
        readonly property var candidates: Hub.commands.matching(editor.text)
        property int index: 0
        property bool dismissed: false
        // 补全时会把文本改掉，那次改动不该被当成"用户又在敲"而把菜单重新弹出来
        property bool suppressReopen: false

        function current() {
            if (candidates.length === 0)
                return null
            return candidates[Math.max(0, Math.min(index, candidates.length - 1))]
        }
        function move(step) {
            if (candidates.length === 0)
                return
            index = Math.max(0, Math.min(index + step, candidates.length - 1))
        }
        // "选中的正是我敲完的那个名字" ⇒ 这一下 Enter 不再是补全，而是提交（= 执行命令）
        function isExact() {
            const item = current()
            return item !== null && editor.text === "/" + item.name
        }
        function complete() {
            const item = current()
            if (item === null)
                return
            suppressReopen = true
            // 有 input hint 的命令补一个空格（用户接着敲参数）；没有的就把名字放上，再按 Enter 即执行
            editor.text = "/" + item.name + (item.hint ? " " : "")
            editor.cursorPosition = editor.text.length
            suppressReopen = false
            dismissed = true          // 补完就收，别继续挡着内容
            close()
            editor.forceActiveFocus()
        }
        // Popup 的 visible 不能直接绑（closePolicy 从内部关掉它会**拆掉绑定**，之后再也弹不出来），
        // 所以只在这里显式开关；所有会影响"该不该显示"的地方都调它。
        function syncVisibility() {
            if (suppressReopen || dismissed || candidates.length === 0)
                close()
            else
                open()
        }

        onCandidatesChanged: syncVisibility()
        // 被 Esc / 点到外面关掉时记下来：等用户再敲字（onTextChanged 会清掉 dismissed）才重新弹
        onClosed: dismissed = true

        parent: root
        x: 16
        y: shadowHost.y - height - 8            // 贴输入卡上沿 8px
        width: Math.min(560, root.width - 32)
        padding: 6
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 12                    // 与 ModelSelectorChip 的菜单同款
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Flickable {
            id: menuScroll
            // 命令多时封顶（与 ModelSelectorChip 的菜单同一手法），否则会把整个菜单顶出屏幕
            implicitHeight: Math.min(contentHeight, 320)
            contentHeight: menuColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: menuColumn
                width: menuScroll.width
                spacing: 2

                Repeater {
                    model: commandMenu.candidates

                    delegate: Rectangle {
                        id: commandRow
                        required property var modelData
                        required property int index

                        width: menuColumn.width
                        height: 34
                        radius: 8
                        color: (commandRow.index === commandMenu.index || rowMouse.containsMouse)
                               ? Theme.color.hoverBg : "transparent"

                        Row {
                            id: rowHost
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Text {
                                id: nameText
                                text: "/" + commandRow.modelData.name
                                // 命令名走 textPrimary 而不是 accent 蓝：整个输入区（含弹层）不出现蓝色，
                                // 层级靠"白底黑字 > 灰字"拉开，和输入框的灰阶体系保持一致
                                color: Theme.color.textPrimary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.body
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: hintText
                                // input hint = 这个命令吃什么参数（引擎给的 `input.hint`）
                                text: commandRow.modelData.hint
                                visible: text.length > 0
                                color: Theme.color.textTertiary
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                // 宽度手工算：Row 里若用 `parent.width - x` 会和 Row 的定位互相触发
                                text: commandRow.modelData.description
                                width: Math.max(0, rowHost.width - nameText.width - hintText.width
                                                   - (hintText.visible ? rowHost.spacing * 2 : rowHost.spacing))
                                elide: Text.ElideRight
                                color: Theme.color.textCaption
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: Theme.font.caption
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: commandMenu.index = commandRow.index
                            onClicked: commandMenu.complete()
                        }
                    }
                }
            }
        }
    }
}
