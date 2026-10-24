// 线性图标（单色 SVG 图标集 assets/resources/icons/ui/*.svg 的 QML 载体）。
// 对应设计原型 build/temp/sidebar-redesign/index.html 顶部的 <symbol>（search/plus/chevron/dots/
// settings/plugin/moon/grid/pin/panel/send/clock），尺寸参照同目录 styles.css（12~18px 为主）。
// 职责只有一件：**按名字画一枚可染色的矢量图标**。按钮壳（悬停底、按下、圆形蒙版、tooltip）

import QtQuick
import QtQuick.Effects
import ULinHub 1.0

Item {
    id: root

    property string name: ""   // 图标名（= assets/resources/icons/ui/ 下的文件名，不含 .svg）
    property int size: 16   // 边长（px）：原型里 12（分组折叠/置顶）、13（行菜单）、14（搜索/收起/新建）、15（底部入口）
    property color color: Theme.color.iconButtonText   // 墨迹色：默认与 IconButton 的 glyphColor 同源，调用方按状态给 hover/selected 色即可

    // 需要"禁用/次要"时由调用方直接给 Item.opacity：本组件只有一层墨迹，整体压暗与染色无关，
    // 没必要再单开一个 iconOpacity 属性（默认值必然也是 1，等于重复）。

    // qrc 路径与 assets/resources/ulinhub.qrc 的 prefix「ULinHub」+ icons/ui/ 一一对应（无 alias）
    readonly property string iconSource: root.name.length > 0
        ? "qrc:/ULinHub/icons/ui/" + root.name + ".svg"
        : ""

    implicitWidth: root.size
    implicitHeight: root.size

    Image {   // 纹理源：只参与离屏纹理，不上屏（MultiEffect 仍能取到它的 texture）
        id: texture
        anchors.fill: parent
        source: root.iconSource

        // SVG 按 sourceSize 栅格化：先出 2x 再缩到 size，14~20px 下描边（1.6 / 24 ≈ 1.1px）不发虚
        sourceSize.width: root.size > 0 ? Math.ceil(root.size * 2) : 0
        sourceSize.height: root.size > 0 ? Math.ceil(root.size * 2) : 0
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: texture
        visible: texture.status === Image.Ready   // 资源还没解出来时先不画：否则会闪一帧空/糊图（qsvg 解码很快，但首帧仍可能赶上）
        colorization: 1.0   // 1 = 完全按 colorizationColor 重上色（源图纯白 ⇒ 精确得到 color，并保留抗锯齿 alpha）
        colorizationColor: root.color
        autoPaddingEnabled: false   // 这里只用染色，不用模糊/阴影：关掉 autoPadding，免得纹理四周多出透明边改写布局
    }
}
