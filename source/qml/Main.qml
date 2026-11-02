// QML 壳的根：无边框 Window + 自绘标题栏 + 客户区。
// 这里只做"外壳 + 装配"，颜色/尺寸一律取 Theme 的 token（A 层，来自 <exe>/styles/theme-*.json
// 与 metrics.json），子界面一律经 Style.url() 装载（B 层，磁盘风格包同名文件可遮蔽内置）。
// ⚠️ 新增子界面时，要用 Style.url()+Loader 而不是相对 import —— 相对 import 的那条路绕不过风格包。

import QtQuick
import QtQuick.Window
import QtQuick.Effects
import ULinHub 1.0

Window {
    id: root

    visible: true   // QML 的 Window 默认不可见，必须显式打开（ApplicationWindow 也是）

    // 无边框：WS_THICKFRAME/WS_CAPTION 由 WindowBridge.attachWindow 的原生逻辑补回，
    // 这样系统投影、贴边吸附、边缘缩放与系统窗口菜单都还在
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    title: "ULin Hub"
    width: Theme.size.preferredWindowWidth
    height: Theme.size.preferredWindowHeight + Theme.size.titleBarHeight
    minimumWidth: Theme.size.minWindowWidth
    minimumHeight: Theme.size.minWindowHeight

    // 拖入文件时 0→1；内容层虚化和落点遮罩都跟这个值走，进入/退出都是短促渐变。
    property real dropT: fileDropArea.containsDrag ? 1 : 0
    Behavior on dropT { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }


    Component.onCompleted: {
        // 高 DPI 小屏（2560x1600 @200% => 逻辑仅 1280x800）上任何写死的宽/高都会让窗口超出屏幕，
        // 一律以可用桌面为上限
        const margin = Theme.spacing.windowMargin
        root.width = Math.min(Theme.size.preferredWindowWidth, Screen.desktopAvailableWidth - 2 * margin)
        root.height = Math.min(Theme.size.preferredWindowHeight + Theme.size.titleBarHeight,
                               Screen.desktopAvailableHeight - 2 * margin)

        WindowBridge.attachWindow(root)
    }

    Rectangle {   // 窗口可见面：圆角与 1px 描边就是窗口的真实外轮廓（圆角之外是透明，交给系统投影）
        id: surface
        anchors.fill: parent
        anchors.margins: WindowBridge.contentInset   // 最大化时系统多给一圈不可见缩放边框，内容按它内缩，可见内容才落进工作区

        readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen

        radius: edgeToEdge ? 0 : Theme.radius.window
        color: Theme.color.windowBg
        clip: true

        // 背景层（B 层可替换单元，第 37 个）：磁盘风格包优先、qrc 兜底。
        // 所以天然画在卡片之下，不需要 z。
        Loader {
            anchors.fill: parent
            source: Style.url("skin/window/Backdrop.qml")
        }


        Column {
            id: mainColumn
            anchors.fill: parent
            layer.enabled: root.dropT > 0.001
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 0.85 * root.dropT
                autoPaddingEnabled: false
            }

            // 标题栏：可覆盖单元。整块在原生侧被登记为窗口拖动区（WM_NCHITTEST -> HTCAPTION）。
            // 覆盖页的抓手（三个点）会把自己也登记进原生命中测试（WindowBridge.addWindowControl），
            // 原生在 caption 矩形内会先问"这里有没有已登记控件" ⇒ 三个点优先，拖标题栏空白处仍是拖窗口。
            // 所以这里**始终登记**，不需要因为"有覆盖页在前台"而取消。
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
                source: Style.url("basic/shell/Shell.qml")   // M3 起客户区是壳层（侧栏 + 顶栏 + 会话区占位）；同样走 Style 以便整块被风格包覆盖
            }
        }

        // 拖入文件的落点层：背景内容已由 mainColumn 的 layer 做模糊，
        // 这里只叠遮罩和提示，自身不参加交互（enabled:false），避免挡住 DropArea。
        Item {
            id: fileDropOverlay
            anchors.fill: parent
            z: 500
            visible: root.dropT > 0.001
            opacity: root.dropT
            enabled: false

            Rectangle {
                anchors.fill: parent
                color: "#FFFFFF"
                opacity: 0.74
            }

            Column {
                anchors.centerIn: parent
                spacing: 12
                transform: Translate { y: (1 - root.dropT) * 10 }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64
                    height: 64
                    radius: 18
                    color: "#F7F8FA"
                    border.width: 1
                    border.color: "#D8DBE0"

                    Image {
                        anchors.centerIn: parent
                        width: 40
                        height: 40
                        source: "qrc:/ULinHub/icons/ui/file-generic.svg"
                        sourceSize.width: 80
                        sourceSize.height: 80
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "松开以添加文件"
                    color: "#0F1115"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.lead
                    font.weight: Theme.font.weightBold
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "图片会内联显示，其他文件以附件卡片加入"
                    color: "#61666B"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                }
            }
        }

        // 整个窗口都能接文件；放最后保证它是拖放命中目标。
        DropArea {
            id: fileDropArea
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: (drop) => {
                if (drop.hasUrls)
                    Hub.addAttachments(drop.urls)
            }
        }
    }
}
