// 窗口背景层（B 层可替换单元，第 37 个）。
//
// 契约（与其它单元一致，只有三条）：
//   1. 根是 Item，锚满父级；父级已设好最大化 inset 与窗口圆角地板，本件只管往上画。
//   2. 不接受鼠标事件（不放 MouseArea；纯 Item/Rectangle/Image 本来就不抓事件）。
//   3. 描边与任何叠加绘制由本件负责；不透明绘制必须自己裁圆角；最大化/全屏时不要圆角与描边。
//
// 皮肤作者换背景 = 替换本文件；壁纸写法见
// assets/StyleExample/Qml-Wallpaper/ui/window/Backdrop.qml。
//
// 内置默认只画描边：底色由宿主 Main.qml 的地板色提供，所以默认状态与抽出之前像素级一致。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    // 直接读单例，与其它组件一致；宿主不传任何契约属性进来
    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: root.edgeToEdge ? 0 : 1
        border.color: Theme.color.windowBorder
    }
}
