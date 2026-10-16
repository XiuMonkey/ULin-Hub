// 图标按钮（侧栏底部那排图标、标题栏/卡片上的小按钮都走它）。
// 外观（形状/底色/悬停/按下/字形色）全部取 token；点击只发 clicked()，动作归调用方。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property string glyph: ""          // 字形（符号字体）；与 label 二选一或并用
    property string label: ""          // 可选文字
    // 原版图标资源（qrc:/ULinHub/*.png）：给了就画图片，不再画字形/文字 —— 旧壳用 QIcon 的地方
    // 都走这里（发送/中止、侧栏四个入口…），保证和原版同一套图
    property string iconSource: ""
    property int iconSize: Math.round(boxSize * 0.72)
    // 圆形悬停蒙版（原版输入区发送键的做法：蒙版叠在**图标之上**、鼠标穿透、
    // hover 白 35% / pressed 白 55%、圆角 = 边长一半）。给了它就不再画方形悬停底。
    property bool circleHoverOverlay: false
    // 蒙版基色。默认白 —— 底色深（或透明）时"提亮"才是对的悬停反馈。
    // 但底色本身是浅色（如暗色主题下的白圆发送键）时，白蒙版叠上去等于没叠，
    // 悬停/按下会**完全没有反馈** ⇒ 那种情况由调用方改成黑（见 Composer.qml）。
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
    // 这不是字体没加载对，是字体度量与字形墨迹的固有差异，只能按字形逐个校。给调用方一个口子，
    // 值由离屏渲染数像素得出（见 SessionBar.qml 里那颗转译器按钮的注释）。
    property real contentYOffset: 0

    signal clicked()

    readonly property bool hovered: mouseArea.containsMouse
    readonly property bool pressed: mouseArea.pressed

    implicitWidth: label.length > 0 ? (contentText.implicitWidth + 2 * horizontalPadding) : boxSize
    implicitHeight: boxSize

    Rectangle {
        anchors.fill: parent
        visible: !root.circleHoverOverlay
        radius: Theme.radius.small
        color: root.pressed
            ? Theme.color.iconButtonPressedBg
            : ((root.hovered || root.selected) ? Theme.color.iconButtonHoverBg : "transparent")
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
    Image {
        id: iconImage
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.contentYOffset
        visible: root.iconSource.length > 0
        width: root.iconSize
        height: root.iconSize
        source: root.iconSource
        // 源图多为 @2x，给足采样尺寸再缩放，避免糊
        sourceSize.width: root.iconSize * 2
        sourceSize.height: root.iconSize * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: root.enabled ? 1.0 : 0.5
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // 圆形蒙版：叠在图标**之上**（原版 m_sendOverlay 是同层级做法），鼠标穿透交给上面的 MouseArea
    Rectangle {
        anchors.fill: parent
        visible: root.circleHoverOverlay
        radius: width / 2
        // 白 35% / 55% 原样保留，只是基色可换（默认白 ⇒ 与改动前逐像素相同）
        color: root.pressed
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
