// MarkdownBody：正文展示层。
// 解析/排版在 C++ 侧（common/util/MarkdownSegmenter）：Qt 6 内置 md4c，GitHub 方言，
// 按 DSH 规格调整标题/列表/引用/代码块/表格，然后直接维护这个 TextEdit 的 QTextDocument。
// 增量流式：
// · 只有末尾未闭合段落在每个 tick 替换；
// · 不再每个 token 重新生成整篇 HTML / 重排。
// 代码块仍由 AgentBubble 里的 CodeBlock 控件承担；本组件只接收 prose 部分。
// 整条 prose 内容仍是**一个 QTextDocument**，所以正文/列表/表格之间可以连续拖选。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    objectName: "mdBody"

    property string text: ""
    property var tableLines: []
    property var inlineCodeRects: []
    property bool decorationsRefreshPending: false

    // 用组件内的 Timer 而不是 Qt.callLater：后者排在事件循环里，Repeater 重建委托时
    // 组件可能已销毁，回调再访问 root 就会报 "refreshDecorations is not a function"。
    // Timer 是子对象，随组件一起销毁，不会在销毁后触发。
    // interval 不是 0：取表格线/行内代码矩形要 documentSize() 强制整篇布局，而流式正文
    // 每 33ms 变一次 —— 放到 ~100ms 一拍（10Hz），肉眼无感，`documentSize` 少掉三分之二。
    // decorationsRefreshPending 保证上层每 tick 调 start() 也不会把这一拍一直往后推。
    Timer {
        id: decorationsTimer
        interval: 100
        repeat: false
        onTriggered: {
            root.decorationsRefreshPending = false
            root.refreshDecorations()
        }
    }

    function scheduleRefreshDecorations() {
        if (decorationsRefreshPending)
            return
        decorationsRefreshPending = true
        decorationsTimer.start()
    }

    readonly property var colorTokens: ({   // 主题 token 一次性交给 C++；主题切换时重挂文档并重渲染
        text: Theme.color.textPrimary,
        secondary: Theme.color.textSecondary,
        link: Theme.color.textSecondary
    })

    width: parent ? parent.width : 0
    implicitHeight: body.contentHeight
    height: implicitHeight

    function renderNow() {
        if (body.textDocument === null)
            return
        Hub.markdownUpdate(body.textDocument, root.text)
        root.scheduleRefreshDecorations()
    }

    function refreshDecorations() {
        if (body.textDocument === null)
            return
        // 纯正文流式占多数：没有 | 就不要为表格扫整篇 block，没有 ` 就不要扫 inline-code。
        // 并且空数组不重复赋值，避免 Repeater/绑定在无装饰时也被标脏。
        const table = root.text.indexOf("|") >= 0
            ? Hub.markdownTableLines(body.textDocument) : []
        if (table.length > 0 || root.tableLines.length > 0)
            root.tableLines = table
        const inline = root.text.indexOf("`") >= 0
            ? Hub.markdownInlineCodeRects(body.textDocument) : []
        if (inline.length > 0 || root.inlineCodeRects.length > 0)
            root.inlineCodeRects = inline
    }

    function reattach() {
        if (body.textDocument === null)
            return
        Hub.markdownAttach(body.textDocument, root.colorTokens)
        renderNow()
    }

    // 点正文里的 http(s) 链接：开/切到右侧栏的浏览器块。
    // PanelBarDock 已经是现成的单例（openUrl 会追加一块带 startUrl 的 browser 块），
    // 这里只负责把地址递过去，并确保右栏展开。
    function openLink(link) {
        const raw = String(link === undefined || link === null ? "" : link).trim()
        if (!/^https?:\/\//i.test(raw))
            return
        const title = raw.replace(/^https?:\/\//i, "").split(/[/?#]/)[0] || "网页"
        PanelBarDock.openUrl(raw, title)
        Hub.openPanelBar()
    }

    Component.onCompleted: reattach()
    Component.onDestruction: {
        if (body.textDocument !== null)
            Hub.markdownDetach(body.textDocument)
    }

    // 流式节流：文本持续变化时不要 restart（那会一直等不到空档），
    // 而是在固定 tick 上直接渲染最新文本。
    onTextChanged: if (!renderTimer.running) renderTimer.start()

    // 气泡宽度变化会重排表格；C++ 的表格行线坐标是布局结果，必须重新取一次。
    // ⚠️ 宽度动画期间不要每次 width/contentHeight 变化都取：那会强制每个文档全篇布局，
    // 而 TextEdit 本来就已经在按新宽度重排了。动画落定后 contentHeight 的最后一次变化会补上。
    onWidthChanged: scheduleRefreshDecorations()

    // colorTokens 已经依赖 Theme.color；这里不再额外 onColorTokensChanged + Theme.Connection
    // 双份 reattach（旧写法主题切换会整篇重建两次）。
    Connections {
        target: Theme
        function onChanged() { root.reattach() }
    }

    Timer {
        id: renderTimer
        interval: 33
        repeat: false
        onTriggered: root.renderNow()
    }

    // QTextDocument 只能给 inline-code 画直角纯色底；圆角/内边距用这层矩形补。
    // 声明在 TextEdit 之前，确保底块在文字下方，不盖住选中高亮。
    Repeater {
        // 按实际 inline-code 数量创建：固定 256 会让每个 MarkdownBody 常驻几百个 Rectangle，流式时明显变重。
        model: root.inlineCodeRects.length
        delegate: Rectangle {
            required property int index
            readonly property var rect: root.inlineCodeRects[index]
            x: rect.x
            y: rect.y
            width: rect.w
            height: rect.h
            radius: 4
            color: Theme.color.inlineCodeBg || Theme.color.inputBg
            border.width: 1
            border.color: Theme.color.border
        }
    }

    TextEdit {
        id: body

        objectName: "bubbleText"
        width: parent.width
        height: contentHeight
        textFormat: TextEdit.RichText
        wrapMode: TextEdit.Wrap
        readOnly: true
        selectByMouse: true
        persistentSelection: true
        activeFocusOnPress: true

        color: Theme.color.textPrimary
        selectionColor: Qt.rgba(Theme.color.accent.r, Theme.color.accent.g,   // 选区是一整块半透明底色 + selectedTextColor；默认 #BFDBFE 太实
                                Theme.color.accent.b, Theme.isDark ? 0.30 : 0.16)
        selectedTextColor: Theme.color.textPrimary
        font.family: Theme.font.uiFamily
        font.pixelSize: Theme.font.markdown || 13

        onLinkActivated: root.openLink(link)
        onContentHeightChanged: root.scheduleRefreshDecorations()
    }

    // QML 的 QTextDocument 不画 table cell 边框；这里只覆盖旧 MarkdownTable 需要的那一条
    // 横向发丝线（行底），没有外框/竖线。按实际行数创建，数组换了只更新已有 Rectangle 的几何。
    Repeater {
        model: root.tableLines.length
        delegate: Rectangle {
            required property int index
            readonly property var line: root.tableLines[index]
            x: line.x
            y: line.y
            width: line.w
            height: 1
            z: 2
            color: Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g,
                           Theme.color.textPrimary.b, Theme.isDark ? 0.30 : 0.22)
        }
    }
}
