import QtQuick
import QtQuick.Controls.Basic
import QtWebEngine
import ULinHub 1.0
import "../../widgets"

// 右栏文件浏览：JetBrains 风格。
// 大文件连续滚动：已加载块首尾相接，滚到边缘时按块前后补载。
Item {
    id: page

    readonly property bool viewing: FileBrowser.textMode
    readonly property int rowHeight: 28
    readonly property string uiFont: "Microsoft YaHei UI"
    readonly property string monoFont: "JetBrains Mono"
    readonly property int monoSize: 12

    property var chunks: []
    property int firstChunk: 0
    property int lastChunk: -1
    property bool windowBusy: false
    property bool pdfRendered: false

    function languageOf(name) {
        var i = name.lastIndexOf(".")
        if (i < 0)
            return ""
        var ext = name.substring(i + 1).toLowerCase()
        if (ext === "js" || ext === "jsx" || ext === "mjs" || ext === "cjs") return "javascript"
        if (ext === "ts" || ext === "tsx") return "typescript"
        if (ext === "py") return "python"
        if (ext === "c" || ext === "h") return "c"
        if (ext === "cpp" || ext === "cc" || ext === "cxx" || ext === "hpp" || ext === "hh") return "cpp"
        if (ext === "cs") return "csharp"
        if (ext === "rs") return "rust"
        if (ext === "go") return "go"
        if (ext === "java") return "java"
        if (ext === "kt" || ext === "kts") return "kotlin"
        if (ext === "swift") return "swift"
        if (ext === "json" || ext === "json5") return "json"
        if (ext === "yaml" || ext === "yml") return "yaml"
        if (ext === "toml") return "toml"
        if (ext === "xml" || ext === "qrc" || ext === "ui") return "xml"
        if (ext === "html" || ext === "htm") return "html"
        if (ext === "css" || ext === "scss" || ext === "less") return "css"
        if (ext === "qml") return "qml"
        if (ext === "sh" || ext === "bash" || ext === "zsh" || ext === "ps1") return "bash"
        if (ext === "md" || ext === "markdown") return "markdown"
        if (ext === "sql") return "sql"
        return ext
    }

    readonly property string language: page.languageOf(FileBrowser.fileName)
    readonly property bool officeMode: FileBrowser.officeKind.length > 0
        && FileBrowser.officeText.length > 0
    readonly property string officeKind: FileBrowser.officeKind
    readonly property bool officePdfReady: officeMode && FileBrowser.officePdfBase64.length > 0

    function renderPdfIntoView() {
        if (!page.officePdfReady || !pdfView)
            return
        if (pdfView.loading) {
            Qt.callLater(page.renderPdfIntoView)
            return
        }
        console.log("[OfficeViewer] render pdf bytes(base64)=", FileBrowser.officePdfBase64.length)
        pdfView.runJavaScript("renderPdf(" + JSON.stringify(FileBrowser.officePdfBase64) + ")")
        page.pdfRendered = true
    }

    function htmlFor(text) {
        var dark = Theme.isDark
        if (!text || text.length === 0)
            return ""
        var html = Hub.highlightCode(page.language, text)
        return html.length > 0 ? html : text
    }

    function makeChunk(index) {
        var text = FileBrowser.readPage(index)
        var html = page.htmlFor(text)
        return {
            "index": index,
            "startLine": FileBrowser.pageStartLineAt(index),
            "lineCount": FileBrowser.pageLineCountAt(index),
            "text": text,
            "html": html,
            "rich": html.indexOf("<pre") >= 0
        }
    }

    function rebuild() {
        if (!FileBrowser.textMode || FileBrowser.pageCount <= 0) {
            chunks = []
            firstChunk = 0
            lastChunk = -1
            return
        }
        var out = []
        var end = Math.min(2, FileBrowser.pageCount)
        for (var i = 0; i < end; ++i)
            out.push(makeChunk(i))
        chunks = out
        firstChunk = 0
        lastChunk = Math.max(0, end - 1)
        Qt.callLater(function () { editorList.contentY = 0 })
    }

    function appendMore() {
        if (windowBusy || !FileBrowser.textMode)
            return
        if (lastChunk + 1 >= FileBrowser.pageCount)
            return
        windowBusy = true
        var out = chunks.slice()
        var end = Math.min(lastChunk + 3, FileBrowser.pageCount - 1)
        for (var i = lastChunk + 1; i <= end; ++i)
            out.push(makeChunk(i))
        chunks = out
        lastChunk = end
        windowBusy = false
    }

    function prependMore() {
        if (windowBusy || !FileBrowser.textMode || firstChunk <= 0)
            return
        windowBusy = true
        var oldHeight = editorList.contentHeight
        var out = []
        var start = Math.max(0, firstChunk - 2)
        for (var i = start; i < firstChunk; ++i)
            out.push(makeChunk(i))
        out = out.concat(chunks)
        chunks = out
        firstChunk = start
        windowBusy = false
        Qt.callLater(function () {
            editorList.contentY = Math.max(0, editorList.contentY + (editorList.contentHeight - oldHeight))
        })
    }

    function ensureWindow() {
        if (editorList.contentHeight <= editorList.height + 100)
            return
        if (editorList.contentY + editorList.height > editorList.contentHeight - 1200)
            appendMore()
        if (editorList.contentY < 600)
            prependMore()
    }

    function syncRoot() {
        if (Hub.currentWorkspacePath.length > 0)
            FileBrowser.openRoot(Hub.currentWorkspacePath)
    }

    onViewingChanged: {
        if (viewing) {
            rebuild()
        } else {
            chunks = []
            firstChunk = 0
            lastChunk = -1
        }
    }

    Component.onCompleted: syncRoot()

    Connections {
        target: Hub
        function onCurrentSessionChanged() { page.syncRoot() }
    }

    Connections {
        target: FileBrowser
        function onFileChanged() {
            if (FileBrowser.textMode && page.chunks.length === 0)
                page.rebuild()
        }
        function onCurrentPathChanged() {
            pathInput.text = FileBrowser.currentPath
        }
    }

    Timer {
        interval: 300
        repeat: true
        running: FileBrowser.rootPath.length === 0
        onTriggered: page.syncRoot()
    }

    // 目录树
    Item {
        anchors.fill: parent
        anchors.margins: 8
        visible: !page.viewing

        Rectangle {
            id: treeToolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 34
            radius: 5
            color: Theme.color.panelBg
            border.width: 1
            border.color: Theme.color.border

            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 2

                ActionButton {
                    width: 26
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↑"
                    variant: "ghost"
                    enabled: FileBrowser.currentPath !== FileBrowser.rootPath
                    onClicked: FileBrowser.goUp()
                }
                Rectangle {
                    width: Math.max(120, parent.width - 96)
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 4
                    color: Theme.color.inputBg
                    border.width: 1
                    border.color: pathInput.activeFocus ? Theme.color.accent : Theme.color.border

                    clip: true

                    // 非编辑态：单行省略，保证再长的路径也不会撑出地址栏。
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        verticalAlignment: Text.AlignVCenter
                        visible: !pathInput.activeFocus
                        text: FileBrowser.currentPath
                        elide: Text.ElideMiddle
                        color: Theme.color.textSecondary
                        font.family: page.monoFont
                        font.pixelSize: Theme.font.caption
                    }

                    MouseArea {
                        anchors.fill: parent
                        visible: !pathInput.activeFocus
                        cursorShape: Qt.IBeamCursor
                        onClicked: {
                            pathInput.forceActiveFocus()
                            pathInput.selectAll()
                        }
                    }

                    TextInput {
                        id: pathInput
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        verticalAlignment: TextInput.AlignVCenter
                        visible: activeFocus
                        clip: true
                        text: FileBrowser.currentPath
                        color: Theme.color.textPrimary
                        selectionColor: Theme.color.selectionBg
                        selectedTextColor: Theme.color.textPrimary
                        selectByMouse: true
                        font.family: page.monoFont
                        font.pixelSize: Theme.font.caption
                        onActiveFocusChanged: {
                            if (activeFocus)
                                selectAll()
                            else
                                text = FileBrowser.currentPath
                        }
                        Keys.onReturnPressed: {
                            FileBrowser.navigate(text)
                            focus = false
                        }
                        Keys.onEscapePressed: {
                            text = FileBrowser.currentPath
                            focus = false
                        }
                    }
                }
                ActionButton {
                    width: 26
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↵"
                    variant: "ghost"
                    onClicked: {
                        FileBrowser.navigate(pathInput.text)
                        pathInput.focus = false
                    }
                }
                ActionButton {
                    width: 26
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⟳"
                    variant: "ghost"
                    onClicked: FileBrowser.refresh()
                }
            }
        }

        ListView {
            id: tree
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: treeToolbar.bottom
            anchors.bottom: treeStatus.top
            anchors.topMargin: 6
            clip: true
            model: FileBrowser.entries
            spacing: 1
            ScrollBar.vertical: AppScrollBar { }

            delegate: Rectangle {
                id: treeRow
                required property var modelData
                width: tree.width
                height: page.rowHeight
                radius: 4
                color: treeRowMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 8
                    spacing: 7

                    LineIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: treeRow.modelData.isDir ? "folder" : "file"
                        size: 14
                        color: treeRow.modelData.isDir ? Theme.color.accent : Theme.color.textTertiary
                    }
                    Text {
                        width: parent.width - 70
                        anchors.verticalCenter: parent.verticalCenter
                        text: treeRow.modelData.name
                        color: Theme.color.textPrimary
                        font.family: page.uiFont
                        font.pixelSize: Theme.font.caption
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: !treeRow.modelData.isDir && treeRow.modelData.sizeText.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: treeRow.modelData.sizeText
                        color: Theme.color.textTertiary
                        font.family: page.monoFont
                        font.pixelSize: Theme.font.small
                    }
                }

                MouseArea {
                    id: treeRowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (treeRow.modelData.isDir)
                            FileBrowser.navigate(treeRow.modelData.path)
                        else
                            FileBrowser.openFile(treeRow.modelData.path)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: tree.count === 0 && FileBrowser.error.length === 0
                text: FileBrowser.currentPath.length > 0 ? "空目录" : "选择会话后显示工作区文件"
                color: Theme.color.textTertiary
                font.family: page.uiFont
                font.pixelSize: Theme.font.caption
            }
        }

        Rectangle {
            id: treeStatus
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 22
            color: "transparent"

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: FileBrowser.error.length > 0 ? FileBrowser.error : FileBrowser.entries.length + " 项"
                color: FileBrowser.error.length > 0 ? Theme.color.danger : Theme.color.textTertiary
                font.family: page.uiFont
                font.pixelSize: Theme.font.small
                elide: Text.ElideRight
                width: parent.width
            }
        }
    }

    // 办公文档：本机 Office/WPS 导出 PDF，PDF.js 渲染；失败则走下面的原始文本降级。
    Item {
        anchors.fill: parent
        anchors.margins: 8
        visible: page.viewing && page.officeMode && page.officePdfReady

        Rectangle {
            id: pdfTabBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 32
            radius: 5
            color: Theme.color.panelBg
            border.width: 1
            border.color: Theme.color.border

            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6

                ActionButton {
                    width: 24
                    height: 24
                    anchors.verticalCenter: parent.verticalCenter
                    text: "‹"
                    variant: "ghost"
                    onClicked: FileBrowser.closeFile()
                }
                Text {
                    width: parent.width - 44
                    anchors.verticalCenter: parent.verticalCenter
                    text: FileBrowser.fileName + "  ·  PDF 预览"
                    color: Theme.color.textPrimary
                    font.family: page.uiFont
                    font.pixelSize: Theme.font.caption
                    elide: Text.ElideRight
                }
            }
        }

        WebEngineView {
            id: pdfView
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: pdfTabBar.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 6
            backgroundColor: Theme.color.panelBg
            url: FileBrowser.officeViewerUrl + "viewer.html"

            onLoadingChanged: (request) => {
                console.log("[OfficeViewer] load status=", request.status,
                    "url=", request.url, "error=", request.errorString)
                if (request.status === WebEngineView.LoadSucceededStatus)
                    Qt.callLater(page.renderPdfIntoView)
            }
        }

        Connections {
            target: FileBrowser
            function onFileChanged() {
                page.pdfRendered = false
                Qt.callLater(page.renderPdfIntoView)
            }
        }
    }

    // 代码预览
    Item {
        anchors.fill: parent
        anchors.margins: 8
        visible: page.viewing && (!page.officeMode || !page.officePdfReady)

        Rectangle {
            id: editorTabBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 32
            radius: 5
            color: Theme.color.panelBg
            border.width: 1
            border.color: Theme.color.border

            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 4
                spacing: 6

                ActionButton {
                    width: 24
                    height: 24
                    anchors.verticalCenter: parent.verticalCenter
                    text: "‹"
                    variant: "ghost"
                    onClicked: FileBrowser.closeFile()
                }
                LineIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "file"
                    size: 14
                    color: Theme.color.accent
                }
                Text {
                    width: parent.width - 80
                    anchors.verticalCenter: parent.verticalCenter
                    text: FileBrowser.fileName
                    color: Theme.color.textPrimary
                    font.family: page.monoFont
                    font.pixelSize: Theme.font.caption
                    elide: Text.ElideRight
                }
                ActionButton {
                    width: 38
                    height: 24
                    anchors.verticalCenter: parent.verticalCenter
                    text: "复制"
                    variant: "ghost"
                    onClicked: {
                        var all = ""
                        for (var i = 0; i < page.chunks.length; ++i)
                            all += page.chunks[i].text
                        codeClip.text = all
                        codeClip.selectAll()
                        codeClip.copy()
                    }
                }
            }
        }

        Rectangle {
            id: fallbackBanner
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: editorTabBar.bottom
            anchors.topMargin: 4
            height: visible ? Math.max(38, fallbackText.implicitHeight + 14) : 0
            visible: page.officeMode && !page.officePdfReady
            radius: 4
            color: Theme.color.inputBg
            border.width: 1
            border.color: Theme.color.border

            Text {
                id: fallbackText
                width: parent.width - 16
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: FileBrowser.officeConverting
                    ? "正在准备 Office 原版预览…\n转换完成前先显示提取的文本。"
                    : ("原版预览不可用\n"
                        + (FileBrowser.officeError.length > 0
                            ? FileBrowser.officeError : "未检测到可用的 Office/WPS")
                        + "。以下显示提取的纯文本。")
                color: Theme.color.textSecondary
                font.family: page.uiFont
                font.pixelSize: Theme.font.small
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            id: editorBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: fallbackBanner.bottom
            anchors.bottom: editorStatus.top
            anchors.topMargin: 6
            radius: 5
            color: Theme.color.codeBlockBg || Theme.color.inputBg
            border.width: 1
            border.color: Theme.color.border
            clip: true

            Flickable {
                id: hflick
                anchors.fill: parent
                contentWidth: editorList.width
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.horizontal: AppScrollBar {
                    policy: hflick.contentWidth > hflick.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
                ScrollBar.vertical: AppScrollBar { }

                ListView {
                    id: editorList
                    width: Math.max(hflick.width, page.codeWidth)
                    height: hflick.height
                    clip: true
                    model: page.chunks
                    spacing: 0
                    cacheBuffer: 2400

                    onContentYChanged: page.ensureWindow()
                    onMovementEnded: page.ensureWindow()

                    delegate: Item {
                        id: chunkItem
                        required property var modelData
                        width: editorList.width
                        height: Math.max(gutterText.implicitHeight, codeText.implicitHeight) + 8

                        Row {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: 4

                            Text {
                                id: gutterText
                                width: 50
                                rightPadding: 8
                                horizontalAlignment: Text.AlignRight
                                text: {
                                    var s = ""
                                    for (var i = 0; i < chunkItem.modelData.lineCount; ++i)
                                        s += (i === 0 ? "" : "\n") + (chunkItem.modelData.startLine + i + 1)
                                    return s
                                }
                                color: Theme.color.textTertiary
                                font.family: page.monoFont
                                font.pixelSize: page.monoSize
                            }

                            TextEdit {
                                id: codeText
                                width: Math.max(10, chunkItem.width - gutterText.width - 12)
                                text: page.htmlFor(chunkItem.modelData.text)
                                textFormat: TextEdit.RichText
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.NoWrap
                                color: Theme.color.textPrimary
                                selectionColor: Theme.color.selectionBg
                                selectedTextColor: Theme.color.textPrimary
                                font.family: page.monoFont
                                font.pixelSize: page.monoSize
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: editorStatus
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 22
            color: "transparent"

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    var lang = page.language.length > 0 ? page.language + "  ·  " : ""
                    return lang + FileBrowser.totalLines + " 行  ·  UTF-8"
                }
                color: Theme.color.textTertiary
                font.family: page.monoFont
                font.pixelSize: Theme.font.small
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: FileBrowser.fileName
                color: Theme.color.textTertiary
                font.family: page.monoFont
                font.pixelSize: Theme.font.small
                elide: Text.ElideRight
            }
        }
    }

    TextEdit {
        id: codeClip
        visible: false
        readOnly: true
    }
}
