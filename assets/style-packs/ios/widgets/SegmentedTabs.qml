// iOS 玻璃 · 分段控件（UISegmentedControl）
// ---------------------------------------------------------------------------
// 契约与内置 widgets/SegmentedTabs.qml 一致（model / currentKey / tabHeight /
// tabPaddingX / selected(string)），内部仍然自己保证互斥。
//
// 造型就是 iOS 的分段控件：systemFill 灰胶囊轨道 + 一颗浮起的白色（暗色下是 #636366）
// 胶囊指示器，指示器带一层柔影和一条顶部高光；切换时用带回弹的缓动滑过去
// （Easing.OutBack，iOS 的弹簧感就是这个过冲）。选中项字重 600，未选中 500。
import QtQuick
import ULinHub 1.0

Item {
    id: root

    property var model: []
    property string currentKey: ""
    property int tabHeight: 22
    property int tabPaddingX: 14

    signal selected(string key)

    readonly property int pad: 3
    readonly property int gap: 2
    readonly property int count: model ? model.length : 0

    readonly property color trackColor: Theme.color.trackBg || Theme.color.iconButtonHoverBg
    readonly property color indicatorColor: Theme.isDark ? Qt.rgba(0.42, 0.42, 0.45, 0.92)
                                                        : Qt.rgba(1, 1, 1, 0.94)

    readonly property real tabWidth: Math.max(48, Math.ceil(measure.implicitWidth) + 2 * tabPaddingX)

    implicitWidth: count * tabWidth + (count - 1) * gap + 2 * pad
    implicitHeight: tabHeight + 2 * pad

    Text {
        id: measure
        visible: false
        font.family: "Microsoft YaHei UI"
        font.pixelSize: Theme.font.caption
        font.weight: 600
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

    // 轨道：整块是胶囊，不是圆角矩形 —— iOS 26 的分段控件两端是半圆
    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.trackColor
    }

    // 指示器：三层叠出"浮起"
    Item {
        id: indicator
        visible: root.count > 0
        x: root.pad + root.index * (root.tabWidth + root.gap)
        y: root.pad
        width: root.tabWidth
        height: root.tabHeight

        Behavior on x {
            NumberAnimation {
                duration: 260
                easing.type: Easing.OutBack
                easing.overshoot: 1.06
            }
        }

        // 柔影（两层近似）
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 2
            radius: height / 2
            color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.40 : 0.10)
        }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 1
            radius: height / 2
            color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.28 : 0.07)
        }

        Rectangle {
            id: knob
            anchors.fill: parent
            radius: height / 2
            color: root.indicatorColor

            // 顶沿高光：白胶囊的受光边
            Rectangle {
                x: parent.radius * 0.6
                y: 0.5
                width: Math.max(0, parent.width - 2 * x)
                height: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.0) }
                    GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0.9) }
                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                }
                visible: !Theme.isDark
            }
        }
    }

    readonly property int index: {
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
                    font.weight: tabItem.isCurrent ? 600 : 500

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
