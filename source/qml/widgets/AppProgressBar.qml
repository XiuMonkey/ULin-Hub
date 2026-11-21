// 通用进度条：value ∈ [0,1] 走定态；value < 0 走不定态（一条 30% 宽的色块来回滑）。
// 插件市场用它显示 pnpm / 服务端操作进度；服务端没给百分比时自动退回不定态。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property real value: -1        // 0..1；< 0 = 不定态
    property real barHeight: 5

    implicitHeight: barHeight
    height: barHeight
    clip: true

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: Theme.color.inputBg
    }

    Rectangle {
        id: fill
        visible: root.value >= 0
        width: track.width * Math.max(0, Math.min(1, root.value))
        height: parent.height
        radius: height / 2
        color: Theme.color.accent

        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: wave
        visible: root.value < 0
        width: Math.max(24, track.width * 0.3)
        height: parent.height
        radius: height / 2
        color: Theme.color.accent

        SequentialAnimation on x {
            running: wave.visible && root.visible
            loops: Animation.Infinite
            NumberAnimation {
                from: 0
                to: Math.max(0, track.width - wave.width)
                duration: 900
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: Math.max(0, track.width - wave.width)
                to: 0
                duration: 900
                easing.type: Easing.InOutSine
            }
        }
    }
}
