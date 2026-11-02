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

    function scheduleRefreshDecorations() {
        if (decorationsRefreshPending)
            return
        decorationsRefreshPending = true
        Qt.callLater(function () {
            root.decorationsRefreshPending = false
            root.refreshDecorations()
        })
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
        root.tableLines = Hub.markdownTableLines(body.textDocument)
        root.inlineCodeRects = Hub.markdownInlineCodeRects(body.textDocument)
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
        model: 256
        delegate: Rectangle {
            required property int index
            readonly property var rect: index < root.inlineCodeRects.length ? root.inlineCodeRects[index] : null
            visible: rect !== null
            x: rect ? rect.x : 0
            y: rect ? rect.y : 0
            width: rect ? rect.w : 0
            height: rect ? rect.h : 0
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
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.markdown || 13

        onLinkActivated: root.openLink(link)
        onContentHeightChanged: root.scheduleRefreshDecorations()
    }

    // QML 的 QTextDocument 不画 table cell 边框；这里只覆盖旧 MarkdownTable 需要的那一条
    // 横向发丝线（行底），没有外框/竖线。
    // 固定池：Repeater(model: tableLines) 在数组一换时会销毁重建全部 delegate，
    // 表格线会整批闪；固定池只更新已有 Rectangle 的 x/y/w/visible。
    Repeater {
        model: 128
        delegate: Rectangle {
            required property int index
            readonly property var line: index < root.tableLines.length ? root.tableLines[index] : null
            visible: line !== null
            x: line ? line.x : 0
            y: line ? line.y : 0
            width: line ? line.w : 0
            height: 1
            z: 2
            color: Qt.rgba(Theme.color.textPrimary.r, Theme.color.textPrimary.g,
                           Theme.color.textPrimary.b, Theme.isDark ? 0.30 : 0.22)
        }
    }
}
