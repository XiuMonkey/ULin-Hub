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

    function navigate(text) {
        // 规范化（判 scheme / 补 https:// / 拼搜索词 / 百分号编码）全在 C++：
        // source/src/common/util/UrlInput.cpp，QML 面是单例 `UrlInput`（见 main.cpp 的注册）。
        // 返回空串 = "这行不该导航"（空输入/纯空白），直接不动，别去加载空 URL。
        const target = UrlInput.normalize(text)
        if (target.length === 0)
            return
        view.url = target
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- 地址栏 ----
        Item {
            id: bar
            width: parent.width
            height: 40

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

        // ---- 加载进度（2px，不占位）----
        Rectangle {
            width: parent.width * (view.loading ? view.loadProgress / 100 : 0)
            height: 2
            color: Theme.color.accent
            visible: view.loading
        }

        // ---- 视图 ----
        Item {
            id: viewHost
            width: parent.width
            height: parent.height - bar.height - 2

            WebEngineView {
                id: view
                anchors.fill: parent
                backgroundColor: Theme.color.panelBg
                settings.javascriptEnabled: true

                onLoadingChanged: (request) => {
                    if (request.status === WebEngineView.LoadSucceededStatus)
                        urlField.text = url.toString()
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

    Component.onCompleted: {
        if (startUrl.length > 0)
            view.url = startUrl
    }
}
