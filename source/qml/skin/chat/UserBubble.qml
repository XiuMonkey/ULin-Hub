// 用户消息气泡：右对齐、圆角，颜色取自 userBubble* token。

import QtQuick
import QtQuick.Controls.Basic
import ULinHub 1.0

Item {
    id: root

    property string text: ""
    readonly property string copyText: text
    property var images: []   // [{ id, mediaType }]，源由 Hub.attachmentSource 取

    // Non-image files are appended to the prompt as "附件：\n- name (path)".
    // The bubble shows the user text only; files get dedicated cards.
    readonly property var attachmentParse: splitAttachmentText(text)
    readonly property string displayText: attachmentParse.text
    readonly property var files: attachmentParse.files
    readonly property var browsers: attachmentParse.browsers

    function splitBrowserContexts(raw) {
        var source = String(raw || "")
        var marker = "[浏览器上下文] "
        var cards = []
        var searchFrom = 0
        while (searchFrom < source.length) {
            var start = source.indexOf(marker, searchFrom)
            if (start < 0)
                break
            // 只认行首标记，避免普通正文里偶然出现同样字符串。
            if (start > 0 && source.charAt(start - 1) !== "\n") {
                searchFrom = start + marker.length
                continue
            }
            var next = source.indexOf("\n" + marker, start + marker.length)
            var end = next >= 0 ? next : source.length
            var block = source.slice(start, end)
            var lines = block.split("\n")
            var title = String(lines[0] || "").slice(marker.length).trim()
            var url = ""
            var selector = ""
            var ref = ""
            for (var i = 1; i < lines.length; ++i) {
                var line = lines[i]
                if (line.indexOf("URL: ") === 0)
                    url = line.slice(5).trim()
                else if (line.indexOf("位置: ") === 0)
                    selector = line.slice(4).trim()
                var refMatch = line.match(/ref:\s*([^）。\n]+)/)
                if (refMatch)
                    ref = refMatch[1].trim()
            }
            if (title.length > 0 || url.length > 0)
                cards.push({ title: title, url: url, selector: selector, ref: ref })
            source = source.slice(0, start) + source.slice(end)
            searchFrom = start
        }
        return { text: source.replace(/\s+$/, ""), browsers: cards }
    }

    function splitAttachmentText(raw) {
        var browserParse = splitBrowserContexts(raw)
        var source = browserParse.text
        var browsers = browserParse.browsers
        var marker = source.lastIndexOf("附件：\n")
        if (marker < 0)
            return { text: source, files: [], browsers: browsers }
        if (marker > 0 && source.slice(Math.max(0, marker - 2), marker) !== "\n\n")
            return { text: source, files: [], browsers: browsers }
        var body = source.slice(marker + 4)
        var lines = body.split("\n")
        var parsed = []
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i]
            if (line.length === 0)
                continue
            var match = line.match(/^- (.+?) \((.*)\)$/)
            if (!match)
                return { text: source, files: [], browsers: browsers }
            var path = match[2]
            var dot = path.lastIndexOf(".")
            var suffix = dot >= 0 ? path.slice(dot + 1).toLowerCase() : ""
            parsed.push({ name: match[1], path: path, suffix: suffix })
        }
        if (parsed.length === 0)
            return { text: source, files: [], browsers: browsers }
        return { text: source.slice(0, marker).replace(/\s+$/, ""), files: parsed, browsers: browsers }
    }

    function fileSuffixLabel(suffix) {
        return String(suffix || "").length > 0 ? String(suffix).toUpperCase() : "FILE"
    }

    function copyAll() {
        if (root.copyText.length === 0)
            return
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.deselect()
    }

    // 右键菜单「复制」：把拖选中的文字写进剪贴板（与 Ctrl+C 同一份内容）。
    // 选区在右键按下 / 弹窗抢焦点后可能被清掉，所以 press 阶段先抓成字符串；
    // 一条消息可能拆成多个 TextEdit，这里取第一个有选区的，跨控件拖选的代价照旧接受。
    property string capturedSelection: ""

    function findSelectedText(item) {
        if (!item)
            return ""
        if (item.selectedText !== undefined && item.selectedText.length > 0)
            return item.selectedText
        var kids = item.children || []
        for (var i = 0; i < kids.length; ++i) {
            var t = root.findSelectedText(kids[i])
            if (t.length > 0)
                return t
        }
        // Flickable 等容器把子项挂在 contentItem 上，不在 children 里
        var ci = item.contentItem
        if (ci && ci !== item) {
            var ciKids = ci.children || []
            for (var j = 0; j < ciKids.length; ++j) {
                var t2 = root.findSelectedText(ciKids[j])
                if (t2.length > 0)
                    return t2
            }
        }
        return ""
    }

    function copySelection() {
        if (root.capturedSelection.length === 0)
            return
        selectionHelper.text = root.capturedSelection
        selectionHelper.selectAll()
        selectionHelper.copy()
        selectionHelper.deselect()
    }

    implicitHeight: (bubble.visible ? bubble.implicitHeight : 0)
        + (filesRow.visible ? filesRow.height + 8 : 0)
        + (browsersRow.visible ? browsersRow.height + 8 : 0)
        + (imagesRow.visible ? imagesRow.height + (bubble.visible ? 6 : 0) : 0)

    // 图片贴在气泡上方（右对齐）；单图按原比例限制 240px 长边，多图用 120px 方图裁切。
    // 浏览器上下文卡片：和输入区发送前那张卡同构。历史消息里的标记文本也还原成卡片。
    Column {
        id: browsersRow
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 0
        spacing: 6
        visible: root.browsers.length > 0
        height: visible ? implicitHeight : 0

        Repeater {
            model: root.browsers
            Rectangle {
                id: browserCard
                required property var modelData
                width: 300
                height: 64
                radius: Theme.radius.panel
                color: Theme.color.cardBg
                border.width: 0.5
                border.color: browserCardMouse.containsMouse ? Theme.color.inputBorder : Theme.color.border
                clip: true

                MouseArea {
                    id: browserCardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: modelData.url.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    enabled: modelData.url.length > 0
                    onClicked: {
                        if (modelData.url.length === 0)
                            return
                        PanelBarDock.openUrl(modelData.url,
                            modelData.title.length > 0 ? modelData.title : modelData.url)
                        Hub.openPanelBar()
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 30
                    radius: 8
                    color: Theme.color.tagBg
                    Text {
                        anchors.centerIn: parent
                        text: "◈"
                        color: Theme.color.accent
                        font.pixelSize: 14
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 50
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    Text {
                        width: parent.width
                        text: modelData.title.length > 0 ? modelData.title : modelData.url
                        color: Theme.color.textPrimary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.caption
                        font.weight: Theme.font.weightMedium
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: modelData.url
                        color: Theme.color.textTertiary
                        font.family: "Microsoft YaHei UI"
                        font.pixelSize: Theme.font.small
                        elide: Text.ElideMiddle
                    }
                    Text {
                        width: parent.width
                        visible: modelData.selector.length > 0
                        text: modelData.selector
                        color: Theme.color.textCaption
                        font.family: "Consolas"
                        font.pixelSize: Theme.font.small
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Column {
        id: filesRow
        anchors.right: parent.right
        anchors.bottom: imagesRow.top
        anchors.bottomMargin: 8
        spacing: 6
        visible: root.files.length > 0
        height: visible ? implicitHeight : 0

        Repeater {
            model: root.files
            Rectangle {
                required property var modelData
                width: 286
                height: 58
                radius: Theme.radius.panel
                color: Theme.color.cardBg
                border.width: 0.5
                border.color: Theme.color.border
                clip: true

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 10

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 38
                        height: 38
                        radius: 10
                        color: Theme.color.tagBg
                        Text {
                            anchors.centerIn: parent
                            text: root.fileSuffixLabel(modelData.suffix)
                            color: Theme.color.accent
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: 8
                            font.weight: Theme.font.weightBold
                            elide: Text.ElideRight
                            width: parent.width - 6
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - 50)
                        spacing: 3

                        Text {
                            width: parent.width
                            text: modelData.name
                            color: Theme.color.textPrimary
                            elide: Text.ElideMiddle
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.caption
                            font.weight: Theme.font.weightMedium
                        }
                        Text {
                            width: parent.width
                            text: modelData.path
                            color: Theme.color.textTertiary
                            elide: Text.ElideMiddle
                            font.family: "Microsoft YaHei UI"
                            font.pixelSize: Theme.font.small
                        }
                    }
                }
            }
        }
    }

    Row {
        id: imagesRow
        anchors.right: parent.right
        anchors.bottom: bubble.top
        anchors.bottomMargin: bubble.visible ? 6 : 0
        spacing: 6
        visible: root.images.length > 0
        // 不再用 Math.max(120, …) 强行抬高行高：宽图会在 240px 宽的 tile 里
        // 上下留出大片 letterbox，看起来就像图片和下面那条消息之间隔了一大段。
        height: visible ? implicitHeight : 0

        Repeater {
            model: root.images
            Rectangle {
                id: imageTile
                required property var modelData
                readonly property bool single: root.images.length === 1
                readonly property real ratio: (img.implicitWidth > 0 && img.implicitHeight > 0)
                    ? Math.min(4, Math.max(0.25, img.implicitWidth / img.implicitHeight))
                    : 16 / 9   // 图未加载完时按横向比例占位，别先用 1:1 把行高撑到 240
                width: single ? (ratio >= 1 ? 240 : 240 * ratio) : 120
                height: single ? (ratio >= 1 ? 240 / ratio : 240) : 120
                radius: Theme.radius.small
                color: Theme.color.inputBg
                clip: true
                Image {
                    id: img
                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    fillMode: imageTile.single ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                    // 本地预览优先，历史图片由 attachmentSource 异步取
                    source: {
                        var rev = Hub.attachmentRevision
                        var preview = modelData.preview ? String(modelData.preview) : ""
                        if (preview.length > 0)
                            return preview
                        return rev >= 0 && modelData.id ? Hub.attachmentSource(modelData.id) : ""
                    }
                }
            }
        }
    }

    Rectangle {
        id: bubble
        anchors.bottom: browsersRow.visible ? browsersRow.top : parent.bottom
        anchors.bottomMargin: browsersRow.visible ? 6 : 0
        anchors.right: parent.right
        visible: root.displayText.length > 0
        width: visible ? content.width + 2 * Theme.spacing.cardPadding : 0
        implicitHeight: visible ? content.contentHeight + 2 * Theme.spacing.buttonPadY : 0
        radius: Theme.radius.panel
        color: Theme.color.userBubbleBg

        // 量宽度专用：Text 的 implicitWidth 在设宽前不可用，不能直接用 TextEdit 量。
        Text {
            id: measure
            visible: false
            text: root.displayText
            textFormat: Text.MarkdownText
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body   // 与 Agent 正文同档
        }
        TextEdit {
            id: content
            objectName: "bubbleText"
            anchors.centerIn: parent
            width: Math.min(measure.implicitWidth, Math.max(40, root.width * 0.72 - 2 * Theme.spacing.cardPadding))   // 长消息不铺满整行
            height: contentHeight
            text: root.displayText
            textFormat: TextEdit.MarkdownText
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            activeFocusOnPress: true
            color: Theme.color.userBubbleText
            selectionColor: Theme.color.selectionBg
            selectedTextColor: content.color
            font.family: "Microsoft YaHei UI"
            font.pixelSize: Theme.font.body
        }
    }

    TextEdit {
        id: clipboardHelper
        visible: false
        readOnly: true
        textFormat: TextEdit.PlainText
        text: root.copyText
        width: 1
        height: 1
    }

    // 复制选区用：TextEdit.copy() 只认自己的选区，借一个无绑定的隐藏 TextEdit 写字符串。
    TextEdit {
        id: selectionHelper
        visible: false
        readOnly: true
        textFormat: TextEdit.PlainText
        width: 1
        height: 1
    }

    Popup {
        id: copyMenu
        parent: root
        width: 156
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 8
            color: Theme.color.menuBg
            border.width: 1
            border.color: Theme.color.menuBorder
        }

        contentItem: Column {
            width: copyMenu.width - 12
            spacing: 2

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                visible: root.capturedSelection.length > 0
                color: selCopyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("common_copy")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: selCopyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.copySelection()
                        copyMenu.close()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 30
                radius: 5
                color: copyMouse.containsMouse ? Theme.color.hoverBg : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrId("context_copy_all")
                    color: Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.copyAll()
                        copyMenu.close()
                    }
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        // 右键按下时选区还在，先抓一份；Popup 打开会抢焦点，之后不再依赖原选区。
        onPressedChanged: {
            if (pressed)
                root.capturedSelection = root.findSelectedText(root)
        }
        onTapped: (eventPoint) => {
            var p = eventPoint.position
            if (root.capturedSelection.length === 0)
                root.capturedSelection = root.findSelectedText(root)
            copyMenu.x = Math.max(8, Math.min(root.width - copyMenu.width - 8, p.x))
            copyMenu.y = Math.max(8, Math.min(root.height - copyMenu.height - 8, p.y))
            copyMenu.open()
        }
    }
}
