// QML 壳的根：无边框 Window + 自绘标题栏 + 客户区。
//
// 这里只做"外壳 + 装配"，颜色/尺寸一律取 Theme 的 token（A 层，来自 <exe>/styles/theme-*.json
// 与 metrics.json），子界面一律经 Style.url() 装载（B 层，磁盘风格包同名文件可遮蔽内置）。
//
// ⚠️ 新增子界面时，要用 Style.url()+Loader 而不是相对 import —— 相对 import 的那条路绕不过风格包。
//
// M1/M2 阶段：客户区是骨架占位（shell/ShellPlaceholder.qml）；侧栏/会话列/输入区在 M3–M5 迁入。
// 旧控件壳仍默认可用（`--widgets` 或 ULINHUB_UI=widgets），见 src/core/main.cpp 的开关。

import QtQuick
import QtQuick.Window
import ULinHub 1.0

Window {
    id: root

    // QML 的 Window 默认不可见，必须显式打开（ApplicationWindow 也是）
    visible: true

    // 无边框：WS_THICKFRAME/WS_CAPTION 由 WindowBridge.attachWindow 的原生逻辑补回，
    // 这样系统投影、贴边吸附、边缘缩放与系统窗口菜单都还在（与旧控件壳同一套做法）
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    title: "ULin Hub"
    width: Theme.size.preferredWindowWidth
    height: Theme.size.preferredWindowHeight + Theme.size.titleBarHeight
    minimumWidth: Theme.size.minWindowWidth
    minimumHeight: Theme.size.minWindowHeight

    Component.onCompleted: {
        // 高 DPI 小屏（2560x1600 @200% => 逻辑仅 1280x800）上任何写死的宽/高都会让窗口超出屏幕，
        // 一律以可用桌面为上限（与旧壳 src/ui/Main.cpp 的算法同源）
        const margin = Theme.spacing.windowMargin
        root.width = Math.min(Theme.size.preferredWindowWidth, Screen.desktopAvailableWidth - 2 * margin)
        root.height = Math.min(Theme.size.preferredWindowHeight + Theme.size.titleBarHeight,
                               Screen.desktopAvailableHeight - 2 * margin)

        WindowBridge.attachWindow(root)
    }

    // 窗口可见面：圆角与 1px 描边就是窗口的真实外轮廓（圆角之外是透明，交给系统投影）
    Rectangle {
        id: surface
        anchors.fill: parent
        // 最大化时系统多给一圈不可见缩放边框，内容按它内缩，可见内容才落进工作区
        anchors.margins: WindowBridge.contentInset

        readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen

        radius: edgeToEdge ? 0 : Theme.radius.window
        color: Theme.color.windowBg
        clip: true

        // 背景层（B 层可替换单元，第 37 个）：磁盘风格包优先、qrc 兜底。
        // 与下面的 TitleBar 用同一套装载方式；它是兄弟节点且在 Column 之前，
        // 所以天然画在卡片之下，不需要 z。
        Loader {
            anchors.fill: parent
            source: Style.url("skin/window/Backdrop.qml")
        }

        Column {
            anchors.fill: parent

            // 标题栏：可覆盖单元。整块在原生侧被登记为窗口拖动区（WM_NCHITTEST -> HTCAPTION）
            Loader {
                id: titleBar
                width: parent.width
                source: Style.url("skin/window/TitleBar.qml")
                onLoaded: {
                    if (item)
                        WindowBridge.setCaptionItem(item)
                }
            }

            Loader {
                width: parent.width
                height: parent.height - titleBar.height
                // M3 起客户区是壳层（侧栏 + 顶栏 + 会话区占位）；同样走 Style 以便整块被风格包覆盖
                source: Style.url("basic/shell/Shell.qml")
            }
        }
    }
}
