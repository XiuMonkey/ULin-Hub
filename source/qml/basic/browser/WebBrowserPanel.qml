// 右侧栏里的浏览器面板：地址栏 + 前进/后退/刷新/主页 + 加载进度 + WebEngineView。
//
// 为什么放在右侧栏：官方那套右栏也是"面板坞"，浏览网页属于"临时看一眼"的辅助内容，
// 不占主界面；宽度可拖（壳层已支持），窄了地址栏自己会挤，WebEngineView 跟着变。
//
// ⚠️ 依赖 Qt WebEngine（`QT += webenginequick`）：main.cpp 里必须**在 QApplication 之前**调
//    QtWebEngineQuick::initialize()，否则运行期报 "WebEngineContext used before initialize"。

import QtQuick
import QtQuick.Controls.Basic
import QtWebEngine
import ULinHub 1.0
import "../../widgets"

Item {
    id: root

    // 打开面板时的首页（空 = 显示提示，等用户输入）
    property string startUrl: ""

    // 应用视图 = 扩展的 harness 页面（打开时 URL 带 chrome=0，见 SupporterPlugin::openHarnessView）。
    // 为什么判标记而不是判 127.0.0.1/端口/index.html：宿主不该知道扩展的回环端口与文件布局，
    // 一个语义参数就够了；扩展换服务/换路径，宿主这行不用跟着改。
    property bool appView: false

    // 地址也可以**由外面给**（扩展面板）：PanelBarDock 把 PanelBarDock.activeViewUrl 活绑到 startUrl，
    // 切块/换面板时这条 handler 负责真正导航。用户自己在地址栏敲的仍走 navigate()。
    onStartUrlChanged: openExternal(startUrl)

    // 从真正的页面 URL 判定"要不要画地址栏"。放在函数里而不是属性绑定的表达式里：
    // 属性绑定可能在 view 还没创建时就被求值（bar 声明在 view 之前），那时读 view.url 会拿到空值。
    function syncChrome() {
        appView = view.url.toString().indexOf("chrome=0") >= 0
    }

    function navigate(text) {
        // 规范化（判 scheme / 补 https:// / 拼搜索词 / 百分号编码）全在 C++：
        // source/src/common/util/UrlInput.cpp，QML 面是单例 `UrlInput`（见 main.cpp 的注册）。
        // 返回空串 = "这行不该导航"（空输入/纯空白），直接不动，别去加载空 URL。
        const target = UrlInput.normalize(text)
        if (target.length === 0)
            return
        view.url = target
    }

    // 程序化打开（扩展面板 / 宿主给的地址）：**不经过 UrlInput 的输入规整** —— 那是给"人敲进来的
    // 半截地址"准备的（补 https://、把空格拼成搜索词）。扩展给的是完整 URL，规整反而会改坏它
    // （例如无 scheme 的 127.0.0.1:port/... 会被补成 https）。空串 = 不导航（用户自己开的块）。
    function openExternal(address) {
        const target = String(address === undefined || address === null ? "" : address)
        if (target.length === 0 || target === view.url.toString())
            return
        view.url = target
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- 地址栏（应用视图不显示：height 归零 + visible=false，Column 会整行跳过，不留空白）----
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
                    font.family: "Microsoft YaHei UI"
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
                    // 地址栏跟着浏览位置走，但用户正在输入时不要抢
                    Binding {
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

        // ---- 加载进度（2px，不占位；应用视图连这条宿主装饰也不画，让网页顶到最上面）----
        Rectangle {
            id: loadingBar
            width: parent.width * (view.loading ? view.loadProgress / 100 : 0)
            height: 2
            color: Theme.color.accent
            visible: view.loading && !root.appView
        }

        // ---- 视图 ----
        Item {
            id: viewHost
            width: parent.width
            // 减掉"当前真正占位的宿主装饰"（地址栏隐藏时 bar 被 Column 跳过，不能按它的旧高度算，
            // 否则底部会空出一整条）；进度条只在加载时占 2px。
            height: parent.height - bar.height - (loadingBar.visible ? loadingBar.height : 0)

            WebEngineView {
                id: view
                anchors.fill: parent
                backgroundColor: Theme.color.panelBg
                settings.javascriptEnabled: true

                // 页面地址一变就重判"这是不是应用视图"（扩展打开时带 chrome=0）
                onUrlChanged: root.syncChrome()

                onLoadingChanged: (request) => {
                    root.syncChrome()   // 重定向可能改 URL：每次加载状态变化都按真实 URL 重判一次
                    if (request.status !== WebEngineView.LoadSucceededStatus)
                        return
                    if (root.appView) {
                        // 为什么必须在这里抢焦点：应用视图没有地址栏可点，WebEngine 的页面又只有
                        // 在 WebEngineView 拿到 active focus 后才收键盘事件 —— 否则输入框点得进去、
                        // 打不了字（"编辑不好用"）。加载完成即把焦点交给网页。
                        view.forceActiveFocus()
                    } else {
                        urlField.text = url.toString()
                    }
                }
                // 页内 target=_blank：就在本视图打开，别丢给系统浏览器（右栏是"就地看一眼"）
                onNewWindowRequested: (request) => {
                    url = request.requestedUrl
                }
            }

            // 空态：还没输过地址
            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.columnClearance
                visible: !view.loading && view.url.toString() === "" && view.title.length === 0
                text: "输入网址或搜索词后回车\n（本面板用 Qt WebEngine 渲染，与主界面互不影响）"
                color: Theme.color.textTertiary
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Theme.font.caption
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.4
            }
        }
    }

    // 创建时就带地址（扩展面板绑上来的那份）；再同步一次地址栏可见性，
    // 避免"打开瞬间先闪一下地址栏"（startUrl 可能在 onLoaded 里才绑上来，urlChanged 稍后才到）
    Component.onCompleted: {
        openExternal(startUrl)
        syncChrome()
    }
}
