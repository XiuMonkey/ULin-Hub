// 示例皮肤：把窗口背景换成壁纸（等价复刻原版 QSS 壁纸包 Bg-Genshin-Keqing）。
//
// 与原版一一对应（原版见 misc/ReleaseBuild/StyleExample/Bg-Genshin-Keqing.zip）：
//   · 原版 #dshhubCentral{ background-color:{{wallpaperMask}};
//                          border-image:url({{wallpaperPath}}) 0 0 0 0 stretch stretch; }
//     → 地板色由皮肤的 windowBg 覆盖成蒙版色；这里的 Image 用 Stretch 铺满（原版是拉伸，不是裁剪）
//   · 原版那条 `border: 1px solid {{windowBorder}}` 会被 border-image 盖掉（视觉上无描边）
//     → 这里也不画描边
//   · 原版 border-radius:12px → 这里用 MultiEffect 遮罩把壁纸裁进窗口圆角
//     （Qt 6.11 的 QtQuick.Effects 只提供 MultiEffect 与 RectangularShadow，没有 OpacityMask）
import QtQuick
import QtQuick.Effects
import ULinHub 1.0

Item {
    id: root

    readonly property bool edgeToEdge: WindowBridge.maximized || WindowBridge.fullScreen
    readonly property real cornerRadius: edgeToEdge ? 0 : Theme.radius.window

    // 圆角遮罩源：不参与显示，只被当遮罩纹理
    Item {
        id: maskItem
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle {
            anchors.fill: parent
            radius: root.cornerRadius
            color: "white"
        }
    }

    Image {
        anchors.fill: parent
        source: Style.url("wallpaper.png")
        fillMode: Image.Stretch
        smooth: true
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: maskItem
        }
    }
}
