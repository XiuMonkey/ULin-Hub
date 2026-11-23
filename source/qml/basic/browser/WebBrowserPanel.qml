// 右侧栏里的浏览器面板：地址栏 + 后退/前进/刷新 + 加载进度 + WebEngineView。
// 放右栏的理由：网页属于"临时看一眼"的辅助内容，不占主界面；宽度可拖，地址栏随宽度挤压。
// ⚠️ 依赖 Qt WebEngine（`QT += webenginequick`）：main.cpp 里必须**在 QApplication 之前**调
// QtWebEngineQuick::initialize()，否则运行期报 "WebEngineContext used before initialize"。

import QtQuick
import QtQuick.Controls.Basic
import QtWebEngine
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    property string startUrl: ""   // 打开面板时的首页（空 = 显示提示，等用户输入）

    // 应用视图 = 扩展的 harness 页面（打开时 URL 带 chrome=0，见 SupporterPlugin::openHarnessView）。
    // 只判这个标记、不判回环端口/路径：宿主不该知道扩展的服务与文件布局，扩展换服务这里也不用改。
    property bool appView: false
    // 由 PanelBarDock 注入的稳定块 id：网页块离开/回附后据此把当前地址写回模型。
    property string blockId: ""

    // 地址也可由外部给：PanelBarDock 把 activeViewUrl 活绑到 startUrl，切块/换面板时由这条 handler 真正导航（手输仍走 navigate）。
    onStartUrlChanged: openExternal(startUrl)

    // 按真实页面 URL 判定"要不要画地址栏"；写成函数而非绑定表达式，便于在 onUrlChanged 里显式调用。
    function syncChrome() {
        appView = view.url.toString().indexOf("chrome=0") >= 0
    }

    function persistCurrentUrl() {
        if (blockId.length === 0)
            return
        const current = view.url.toString()
        if (current.length === 0)
            return
        PanelBarDock.setBlockUrl(blockId, current)
    }

    function navigate(text) {
        // 输入规整（判 scheme / 补 https:// / 拼搜索词 / 百分号编码）在 C++ 的 UrlInput 单例（source/src/common/util/UrlInput.cpp）；
        // 返回空串表示"这行不该导航"（空输入/纯空白），不要拿空 URL 去加载。
        const target = UrlInput.normalize(text)
        if (target.length === 0)
            return
        view.url = target
    }

    // 程序化打开（扩展面板 / 宿主给的地址）：不经过 UrlInput.normalize —— 那是给"人敲的半截地址"准备的，
    // 会把无 scheme 的 127.0.0.1:port/... 补成 https，反而改坏完整 URL。空串 = 不导航（用户自己开的块）。
    function openExternal(address) {
        const target = String(address === undefined || address === null ? "" : address)
        if (target.length === 0 || target === view.url.toString())
            return
        view.url = target
    }

    // 右键上下文：request 是临时对象（js GC），弹菜单前把要用的值抄出来。
    property point ctxPos: Qt.point(0, 0)
    property int ctxEditFlags: 0
    property string ctxLinkUrl: ""
    property string ctxSelectedText: ""

    // 右键「询问 Agent」：取光标下的元素 + 选区 + 页面文本，交给宿主推给 ulin-kit 并生成卡片。
    function askAgent(pos) {
        const script = `(function(){
            const cap = (value, limit) => (typeof value === "string" && value.length > limit)
                ? value.slice(0, limit) : (value || "");
            const el = document.elementFromPoint(${Math.round(pos.x)}, ${Math.round(pos.y)});
            const sel = window.getSelection() ? window.getSelection().toString() : "";
            function cssPath(node) {
                if (!node || node.nodeType !== 1) return "";
                const parts = [];
                let cur = node;
                while (cur && cur.nodeType === 1 && parts.length < 8) {
                    let part = cur.tagName.toLowerCase();
                    if (cur.id) { part += "#" + cur.id; parts.unshift(part); break; }
                    if (cur.classList && cur.classList.length > 0)
                        part += "." + Array.from(cur.classList).slice(0, 3).join(".");
                    const parent = cur.parentElement;
                    if (parent) part += ":nth-child(" + (Array.from(parent.children).indexOf(cur) + 1) + ")";
                    parts.unshift(part);
                    cur = parent;
                }
                return parts.join(" > ");
            }
            return {
                url: location.href,
                title: document.title,
                selector: el ? cssPath(el) : "",
                selectedText: sel,
                text: cap(el ? el.innerText : "", 60000),
                html: cap(el ? el.outerHTML : "", 60000),
                pageText: cap(document.body ? document.body.innerText : "", 60000),
                pageHtml: cap(document.documentElement ? document.documentElement.outerHTML : "", 60000)
            };
        })()`
        view.runJavaScript(script, (result) => {
            if (result && typeof result === "object")
                Hub.askBrowserContext(result)
        })
    }

    Connections {
        target: view
        function onUrlChanged() {
            root.persistCurrentUrl()
        }
        function onLoadingChanged(loadRequest) {
            if (loadRequest.status === WebEngineView.LoadSucceededStatus)
                root.persistCurrentUrl()
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        Item {
            id: bar
            width: parent.width
            height: root.appView ? 0 : 40
            visible: !root.appView

            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 2

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "‹"
                    tooltip: "后退"
                    enabled: view.canGoBack
                    opacity: enabled ? 1.0 : 0.35
                    onClicked: view.goBack()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "›"
                    tooltip: "前进"
                    enabled: view.canGoForward
                    opacity: enabled ? 1.0 : 0.35
                    onClicked: view.goForward()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: view.loading ? "✕" : "⟳"
                    tooltip: view.loading ? "停止" : "刷新"
                    onClicked: view.loading ? view.stop() : view.reload()
                }

                TextField {
                    id: urlField
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 3 * 30 - 2 * parent.spacing
                    height: 28
                    selectByMouse: true
                    font.family: Theme.font.uiFamily
                    font.pixelSize: Theme.font.body
                    color: Theme.color.textPrimary
                    placeholderText: "输入网址或搜索词，回车打开"
                    placeholderTextColor: Theme.color.textTertiary
                    background: Rectangle {
                        radius: Theme.radius.input
                        color: Theme.color.inputBg
                        border.width: 1
                        border.color: urlField.activeFocus ? Theme.color.accent : Theme.color.inputBorder
                    }
                    Binding {   // 地址栏跟着浏览位置走，但用户正在输入时不要抢
                        target: urlField
                        property: "text"
                        value: view.url.toString()
                        when: !urlField.activeFocus
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    onAccepted: root.navigate(text)
                }
            }
        }

        Rectangle {
            id: loadingBar
            width: parent.width * (view.loading ? view.loadProgress / 100 : 0)
            height: 2
            color: Theme.color.accent
            visible: view.loading && !root.appView
        }

        // 高度减掉"当前真正占位的宿主装饰"：地址栏隐藏时 bar 被 Column 跳过，不能按它的旧高度算（否则底部空一条）；
        // 进度条只在加载时占 2px。
        Item {
            id: viewHost
            width: parent.width
            height: parent.height - bar.height - (loadingBar.visible ? loadingBar.height : 0)

            WebEngineView {
                id: view
                anchors.fill: parent
                backgroundColor: Theme.color.panelBg
                settings.javascriptEnabled: true

                onUrlChanged: root.syncChrome()   // 页面地址一变就重判"这是不是应用视图"（扩展打开时带 chrome=0）

                onContextMenuRequested: (request) => {
                    request.accepted = true   // 关掉 Qt 默认菜单，换成宿主自己的（里面加「询问 Agent」）
                    root.ctxPos = request.position
                    root.ctxEditFlags = request.editFlags
                    root.ctxLinkUrl = request.linkUrl.toString()
                    root.ctxSelectedText = request.selectedText
                    const p = view.mapToItem(root, request.position)
                    contextMenu.showAt(p)
                }

                onLoadingChanged: (request) => {
                    root.syncChrome()   // 重定向可能改 URL：每次加载状态变化都按真实 URL 重判一次
                    if (request.status !== WebEngineView.LoadSucceededStatus)
                        return
                    if (root.appView) {
                        // 应用视图没有地址栏可点，WebEngine 页面又只在 WebEngineView 拿到 active focus
                        // 后才收键盘事件 —— 不抢焦点就会"输入框点得进去、打不了字"。加载完成即把焦点交给网页。
                        view.forceActiveFocus()
                    } else {
                        urlField.text = url.toString()
                    }
                }
                onNewWindowRequested: (request) => {   // 页内 target=_blank：就在本视图打开，别丢给系统浏览器（右栏是"就地看一眼"）
                    url = request.requestedUrl
                }
            }

            Text {   // 空态：还没输过地址
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.columnClearance
                visible: !view.loading && view.url.toString() === "" && view.title.length === 0
                text: "输入网址或搜索词后回车"
                color: Theme.color.textTertiary
                font.family: Theme.font.uiFamily
                font.pixelSize: Theme.font.caption
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.4
            }
        }
    }

    // 创建时带上外部地址，并立即同步一次地址栏可见性：startUrl 可能晚于 urlChanged 才绑上来，不补这一下会闪。
    Component.onCompleted: {
        openExternal(startUrl)
        syncChrome()
    }

    // 右键菜单：宿主自绘（默认菜单已被 request.accepted 关掉），保留常用动作并加「询问 Agent」。
    // 用 Popup 而非原生 Menu，才能做圆角、行悬停与淡入淡出；动作仍走 WebEngineView 的 action。
    Popup {
        id: contextMenu
        parent: root
        width: 228
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // 关闭方向也做同样的淡出；标准 Menu 没有这层过渡，右键弹出会显得硬。
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fast }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.fast }
        }

        background: Rectangle {
            radius: 10
            color: Theme.color.menuBg
            border.width: 0.5
            border.color: Theme.color.menuBorder
        }

        function showAt(pos) {
            const h = implicitHeight > 0 ? implicitHeight : 280
            x = Math.max(8, Math.min(Math.max(8, root.width - width - 8), pos.x))
            y = Math.max(8, Math.min(Math.max(8, root.height - h - 8), pos.y))
            open()
        }

        function activate(kind) {
            switch (kind) {
            case "back": view.goBack(); break
            case "forward": view.goForward(); break
            case "reload": view.reload(); break
            case "copy": view.triggerWebAction(WebEngineView.Copy); break
            case "paste": view.triggerWebAction(WebEngineView.Paste); break
            case "selectAll": view.triggerWebAction(WebEngineView.SelectAll); break
            case "copyLink": view.triggerWebAction(WebEngineView.CopyLinkToClipboard); break
            case "viewSource": view.triggerWebAction(WebEngineView.ViewSource); break
            case "ask": root.askAgent(root.ctxPos); break
            default: return
            }
            close()
        }

        // 菜单项：kind 交给 activate() 分发，enabled 由 WebEngine 的 editFlags 与导航状态决定。
        property var menuModel: [
            { kind: "back", label: "后退", enabled: view.canGoBack },
            { kind: "forward", label: "前进", enabled: view.canGoForward },
            { kind: "reload", label: "刷新", enabled: true },
            { kind: "sep", separator: true },
            { kind: "copy", label: "复制", enabled: (root.ctxEditFlags & ContextMenuRequest.CanCopy) !== 0 },
            { kind: "paste", label: "粘贴", enabled: (root.ctxEditFlags & ContextMenuRequest.CanPaste) !== 0 },
            { kind: "selectAll", label: "全选", enabled: (root.ctxEditFlags & ContextMenuRequest.CanSelectAll) !== 0 },
            { kind: "copyLink", label: "复制链接", enabled: true, visible: root.ctxLinkUrl.length > 0 },
            { kind: "viewSource", label: "查看源代码", enabled: true },
            { kind: "sep", separator: true },
            { kind: "ask", label: "询问 Agent", enabled: true, accent: true }
        ]

        contentItem: Column {
            width: contextMenu.availableWidth
            spacing: 2

            Repeater {
                model: contextMenu.menuModel

                delegate: Item {
                    id: menuRow
                    required property var modelData
                    readonly property bool separator: modelData.separator === true
                    readonly property bool rowEnabled: modelData.enabled !== false
                    readonly property bool accent: modelData.accent === true
                    width: parent.width
                    height: separator ? 9 : 32
                    visible: modelData.visible !== false
                    opacity: rowEnabled ? 1.0 : 0.38

                    Rectangle {
                        visible: !menuRow.separator
                        anchors.fill: parent
                        radius: 6
                        color: menuRow.accent
                            ? (menuMouse.containsMouse ? Theme.color.activeBg : "transparent")
                            : (menuMouse.containsMouse ? Theme.color.hoverBg : "transparent")

                        Behavior on color {
                            ColorAnimation { duration: Theme.motion.fast }
                        }
                    }

                    Rectangle {
                        visible: menuRow.separator
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        height: 1
                        color: Theme.color.border
                    }

                    // “询问 Agent”放到菜单底部并保留一个轻量图标，形成明确的主动作。
                    LineIcon {
                        visible: menuRow.accent && !menuRow.separator
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        name: "bot"
                        size: 14
                        color: menuMouse.containsMouse ? Theme.color.textPrimary : Theme.color.textSecondary
                    }

                    Text {
                        visible: !menuRow.separator
                        anchors.left: parent.left
                        anchors.leftMargin: menuRow.accent ? 34 : 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: (menuRow.modelData && menuRow.modelData.label !== undefined)
                            ? String(menuRow.modelData.label) : ""
                        color: menuRow.accent
                            ? Theme.color.textPrimary
                            : (menuRow.rowEnabled ? Theme.color.textPrimary : Theme.color.textCaption)
                        font.family: Theme.font.uiFamily
                        font.pixelSize: Theme.font.body
                        font.weight: menuRow.accent ? Theme.font.weightBold : Font.Normal
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: menuMouse
                        anchors.fill: parent
                        enabled: menuRow.rowEnabled && !menuRow.separator
                        hoverEnabled: true
                        cursorShape: rowEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: contextMenu.activate(menuRow.modelData.kind)
                    }
                }
            }
        }
    }
}
