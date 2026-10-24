// 图标按钮（侧栏底部那排图标、标题栏/卡片上的小按钮都走它）。
// 外观（形状/底色/悬停/按下/字形色）全部取 token；点击只发 clicked()，动作归调用方。

import QtQuick
import QtQuick.Effects
import ULinHub 1.0

Item {
    id: root

    property string glyph: ""          // 字形（符号字体）；与 label 二选一或并用
    property string label: ""          // 可选文字

    // 图标资源（qrc:/ULinHub/*.png 旧 PNG，或 assets/resources/icons/ui/*.svg 单色图标）：
    // 给了就画图片，不再画字形/文字 —— 用 QIcon 的地方都走这里（发送/中止、侧栏入口…）。
    property string iconSource: ""

    // 可选染色：alpha > 0 时把 iconSource 当"白色单色图标"用 MultiEffect 按亮度刷成该颜色。
    // 同做法的独立控件是 widgets/LineIcon.qml，但两边**不互相委托**：LineIcon 按 name 拼 qrc 路径、
    // 只服务图标集；这里 iconSource 可以是任意资源，且必须保留 PNG 直绘路径（委托过去会让每个按钮
    // 多一层 Item，非染色时还多一次解码），所以两处各保留这十几行染色配置。
    property color iconColor: "transparent"

    // 可选悬停/按下底色。默认仍走官方 iconButtonHoverBg / iconButtonPressedBg；
    // 侧栏折叠这类"只想让图标变亮、不要出现深色方块"的按钮可以把两者都设成 transparent。
    property color hoverBackground: Theme.color.iconButtonHoverBg
    property color pressedBackground: Theme.color.iconButtonPressedBg
    property int iconSize: Math.round(boxSize * 0.72)

    // 圆形悬停蒙版（原版输入区发送键的做法：蒙版叠在**图标之上**、鼠标穿透、
    // hover 白 35% / pressed 白 55%、圆角 = 边长一半）。给了它就不再画方形悬停底。
    property bool circleHoverOverlay: false

    // 蒙版基色。默认白 —— 底色深（或透明）时"提亮"才是对的悬停反馈。
    // 但底色本身是浅色（如暗色主题下的白圆发送键）时，白蒙版叠上去等于没叠，此时要把 circleHoverTint 换成深色。
    property color circleHoverTint: "#FFFFFF"
    property string tooltip: ""
    property bool selected: false
    property int glyphSize: Theme.font.lead
    property int boxSize: 28           // 纯图标模式下的方形尺寸
    property int horizontalPadding: Theme.spacing.fieldPadX
    property color glyphColor: Theme.color.iconButtonText
    property color glyphColorHover: Theme.color.iconButtonTextHover

    // 内容的视觉垂直微调（px，正数往下）。默认 0 = 不干预。
    // 为什么需要它：`anchors.centerIn` 居中用的是 Text 的**行盒**（高度 = ascent + descent），
    // 而那是**字体级**的盒子 —— 要容纳该字体里最高的字形（带升降部的字母、括号…），所以单个
    // 字形未必落在盒子中线上。U+21C4「⇄」就是这种：Segoe UI Symbol 24px 下行盒 33px，而墨迹
    // 落在 [11, 27)，中线比盒中线低 2.9px，夹在一排**精确居中**的 PNG 图标里肉眼可见地偏下。
    property real contentYOffset: 0

    signal clicked()

    readonly property bool hovered: mouseArea.containsMouse
    readonly property bool pressed: mouseArea.pressed
    readonly property bool iconTinted: root.iconColor.a > 0.001   // 走不走染色：iconColor 透明（默认）就不走，浮点比较留一点余量

    implicitWidth: label.length > 0 ? (contentText.implicitWidth + 2 * horizontalPadding) : boxSize
    implicitHeight: boxSize

    Rectangle {
        anchors.fill: parent
        visible: !root.circleHoverOverlay
        radius: Theme.radius.small
        color: root.pressed
            ? root.pressedBackground
            : ((root.hovered || root.selected) ? root.hoverBackground : "transparent")
    }

    Text {
        id: contentText
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.contentYOffset
        visible: root.iconSource.length === 0
        text: root.label.length > 0 ? root.label : root.glyph
        color: root.hovered ? root.glyphColorHover : root.glyphColor
        font.family: root.label.length > 0 ? "Microsoft YaHei UI" : "Segoe UI Symbol"
        font.pixelSize: root.label.length > 0 ? Theme.font.body : root.glyphSize
        font.weight: root.selected ? Theme.font.weightMedium : Font.Normal
    }

    // 原版图标（同一套 PNG，深浅色主题各有一版的地方由调用方按 Theme.isDark 选）
    // 染色模式下本 Image 退居为**纹理源**（不上屏），由下面那颗 MultiEffect 画出来。
    Image {
        id: iconImage
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.contentYOffset
        visible: root.iconSource.length > 0 && !root.iconTinted
        width: root.iconSize
        height: root.iconSize
        source: root.iconSource
        sourceSize.width: root.iconSize * 2   // 源图多为 @2x，给足采样尺寸再缩放，避免糊（SVG 同样按 sourceSize*2 栅格化后缩下来）
        sourceSize.height: root.iconSize * 2
        fillMode: Image.PreserveAspectFit
        smooth: true

        // 染色模式下本层不上屏（只是纹素来源），禁用态由下面 MultiEffect 的 opacity 负责；
        // 这行只服务 PNG 直绘路径，不会与染色层叠加变暗（MultiEffect 不重复应用源项透明度）。
        opacity: root.enabled ? 1.0 : 0.5
    }

    // 染色层：只照 iconImage 的亮度取色（白描边 × iconColor = iconColor），保留抗锯齿 alpha。
    // 仅 iconColor 不透明时可见；默认 invisible ⇒ 老 PNG 调用方连这条分支的纹理都不会创建。
    MultiEffect {
        anchors.fill: iconImage
        visible: root.iconTinted && iconImage.status === Image.Ready
        source: iconImage
        opacity: root.enabled ? 1.0 : 0.5   // 禁用态与 Image 那层同一口径（0.5），否则染色图标在禁用时不跟着变淡
        colorization: 1.0
        colorizationColor: root.iconColor
        autoPaddingEnabled: false   // 只用染色、不用模糊/阴影：关掉 autoPadding，纹理不向外长，尺寸/对齐与 PNG 路径一致
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Rectangle {   // 圆形蒙版：叠在图标**之上**（原版 m_sendOverlay 是同层级做法），鼠标穿透交给上面的 MouseArea
        anchors.fill: parent
        visible: root.circleHoverOverlay
        radius: width / 2
        color: root.pressed   // 白 35% / 55% 原样保留，只是基色可换（默认白 ⇒ 与改动前逐像素相同）
            ? Qt.rgba(root.circleHoverTint.r, root.circleHoverTint.g, root.circleHoverTint.b, 0.55)
            : (root.hovered
               ? Qt.rgba(root.circleHoverTint.r, root.circleHoverTint.g, root.circleHoverTint.b, 0.35)
               : "transparent")
    }

    AppToolTip {
        text: root.tooltip
        visible: root.tooltip.length > 0 && mouseArea.containsMouse && !mouseArea.pressed
    }
}
