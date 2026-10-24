// 分段控件（segmented control）：按 DSH 官方版 `SegmentedControl.module.css` 的形状做 ——
// * 轨道 = 半透明交互底色（--dsw-alias-interactive-bg-hover）+ 4px 内边距 + md 圆角；
// * 选中指示器 = 一块"浮起来"的面（bg-layer-1 + 柔和高度阴影），在轨道里滑动
// （官方是 transform 过渡 160ms，这里用 x 的 Behavior 同速）；
// * 标签 = 未选中 label-secondary、选中 label-primary（官方就是靠字色 + 指示器双重表达，
// 用法：model 给 [{ key: "conversation", label: "对话" }, ...]，currentKey 是选中项的 key。
// 只负责画和发 selected(string)：互斥由内部按 currentKey 保证，容器不需要再管。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property var model: []
    property string currentKey: ""
    property int tabHeight: 22   // 单颗标签的高度（轨道上下各留 3px 内边距，所以整块高 = tabHeight + 6）
    property int tabPaddingX: 14

    signal selected(string key)

    readonly property int pad: 3
    readonly property int gap: 2
    readonly property int count: model ? model.length : 0

    readonly property color trackColor: Theme.color.trackBg || Theme.color.iconButtonHoverBg   // 轨道底色 / 指示器面：新 token 带兜底，老风格包里没有也能用
    readonly property color indicatorColor: Theme.color.indicatorBg || Theme.color.panelBg

    // 每颗等宽：取最宽的那颗（官方也是 grid-auto-columns: 1fr —— 等宽才不会让指示器算错位置）
    readonly property real tabWidth: Math.max(48, Math.ceil(measure.implicitWidth) + 2 * tabPaddingX)

    implicitWidth: count * tabWidth + (count - 1) * gap + 2 * pad
    implicitHeight: tabHeight + 2 * pad

    Text {   // 量最宽标签用的隐形文本（与实际标签同字体）
        id: measure
        visible: false
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        font.weight: Theme.font.weightMedium
        text: {
            let longest = ""
            for (let i = 0; i < count; ++i) {
                const l = model[i].label || ""
                if (l.length > longest.length)
                    longest = l
            }
            return longest
        }
    }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: Theme.radius.small
        color: root.trackColor
    }

    Rectangle {   // 选中指示器：滑动 + 柔和面
        id: indicator

        visible: root.count > 0
        x: root.pad + root.index * (root.tabWidth + root.gap)
        y: root.pad
        width: root.tabWidth
        height: root.tabHeight
        radius: Theme.radius.small - 1
        color: root.indicatorColor
        border.width: 1
        border.color: Theme.color.border

        Behavior on x {
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    readonly property int index: {   // 当前选中项在 model 里的下标
        for (let i = 0; i < count; ++i) {
            if (model[i].key === currentKey)
                return i
        }
        return 0
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        anchors.topMargin: root.pad
        anchors.bottomMargin: root.pad
        spacing: root.gap

        Repeater {
            model: root.model

            delegate: Item {
                id: tabItem

                required property var modelData
                required property int index

                width: root.tabWidth
                height: root.tabHeight

                readonly property bool isCurrent: modelData.key === root.currentKey

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: tabItem.isCurrent ? Theme.color.textPrimary : Theme.color.textSecondary
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: Theme.font.caption
                    font.weight: Theme.font.weightMedium

                    Behavior on color {
                        ColorAnimation { duration: Theme.motion.fast }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(tabItem.modelData.key)
                }
            }
        }
    }
}
